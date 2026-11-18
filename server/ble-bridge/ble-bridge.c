/*
 * ble-bridge - a pseudo terminal for every paired Control Deck on Bluetooth LE (Linux).
 *
 * The Deck is a GATT server carrying the bytes of its USB serial line (docs/04-PROTOCOL.md).
 * This program is the central: it asks BlueZ (D-Bus, through sd-bus) for paired devices that
 * offer the Control Deck service, connects, acquires the two characteristics as sockets and
 * copies bytes between them and a pty. The Java host opens the pty like a USB cable; it finds
 * it through a file in $XDG_RUNTIME_DIR/control-deck-ble/.
 *
 * One thread, one poll() loop. BlueZ calls are asynchronous so an absent Deck never stalls
 * the others. The bridge never pairs: the bond made from the system's Bluetooth settings is
 * the access.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#define SERVICE_UUID "c6d2fba6-17ec-4591-a399-4982dacbaf6d"
#define DECK_IN_UUID "200a64c8-1a2b-4927-9c15-3783198616b4"
#define DECK_OUT_UUID "d444ff04-43f4-4f30-8fc8-4b38a8466870"

#define BLUEZ "org.bluez"
#define DEVICE_IFACE "org.bluez.Device1"
#define CHAR_IFACE "org.bluez.GattCharacteristic1"

#define MSEC 1000ULL
#define SEC 1000000ULL
#define CONNECT_TIMEOUT (15 * SEC)
#define CALL_TIMEOUT (10 * SEC)
#define RETRY_DELAY (5 * SEC)
#define RELINK_DELAY (2 * SEC)
#define SCAN_INTERVAL (5 * SEC)
#define BUS_RETRY (10 * SEC)
#define PTY_CHECK (250 * MSEC)

#define MAX_PROBLEMS 8
#define MAX_CHARS 64
#define ATT_DEFAULT_MTU 23
#define TX_MAX 512 /* the ATT value limit */

enum state { IDLE, CONNECTING, ACQUIRING, LINKED };

struct deck {
    struct deck *next;
    char *path;
    char address[18];
    char alias[128];
    bool seen, connected, resolved, we_connected;
    char in_path[160], out_path[160];
    uint16_t in_mtu; /* the characteristic's MTU property, for the WriteValue fallback */

    enum state state;
    uint64_t retry_at;
    sd_bus_slot *call; /* the pending Connect, Acquire* or StartNotify */
    unsigned problems[MAX_PROBLEMS];
    unsigned nproblems;

    int write_fd, notify_fd;
    bool write_fallback, notify_fallback, notifying;
    size_t chunk;
    sd_bus_slot *write_call;
    unsigned char tx[TX_MAX];
    size_t tx_len;

    int master;
    char slave[64];
    char file[PATH_MAX];
    bool host_open;
    uint64_t pty_check_at;
};

static sd_bus *bus;
static struct deck *decks;
static uint64_t bus_retry_at, scan_at;
static bool scan_pending, rescan_wanted;
static bool bus_missing, bluez_missing, none_logged;
static char dir[PATH_MAX - 512]; /* room for "/<name>" and the temporary file's suffix */
static bool dir_made;

static uint64_t now_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * SEC + (uint64_t)ts.tv_nsec / 1000;
}

static void say(const struct deck *d, const char *fmt, ...) {
    va_list ap;
    printf("%s (%s): ", d->alias, d->address);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

/* A Deck that is off BLE fails the same way every 5 s: say each distinct problem once. */
static void problem(struct deck *d, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    unsigned h = 5381;
    for (const char *p = msg; *p; p++) h = h * 33 + (unsigned char)*p;
    unsigned n = d->nproblems < MAX_PROBLEMS ? d->nproblems : MAX_PROBLEMS;
    for (unsigned i = 0; i < n; i++)
        if (d->problems[i] == h) return;
    d->problems[d->nproblems++ % MAX_PROBLEMS] = h;
    say(d, "%s", msg);
}

static const char *error_text(const sd_bus_error *e) {
    if (e == NULL) return "unknown error";
    return e->message != NULL ? e->message : e->name != NULL ? e->name : "unknown error";
}

static void request_scan(void) {
    if (scan_pending)
        rescan_wanted = true;
    else
        scan_at = 0;
}

static struct deck *find_deck(const char *path) {
    for (struct deck *d = decks; d != NULL; d = d->next)
        if (strcmp(d->path, path) == 0) return d;
    return NULL;
}

/* ---- the link file ---- */

static bool ensure_dir(void) {
    if (dir[0] == '\0') {
        const char *runtime = getenv("XDG_RUNTIME_DIR");
        snprintf(dir, sizeof dir, "%s/control-deck-ble",
                 runtime != NULL && *runtime != '\0' ? runtime : "/tmp");
    }
    if (mkdir(dir, 0700) == 0) {
        dir_made = true;
        return true;
    }
    return errno == EEXIST;
}

/* A file left by a bridge that died, for this same Deck: safe to replace. */
static bool stale_file(const char *path, const char *address) {
    char line[512] = "";
    FILE *f = fopen(path, "r");
    if (f == NULL) return false;
    bool got = fgets(line, sizeof line, f) != NULL;
    fclose(f);
    if (!got) return false;
    char tag[40];
    snprintf(tag, sizeof tag, "(BLE %s)", address);
    long pid = strtol(line, NULL, 10);
    return strstr(line, tag) != NULL && pid > 0 && kill((pid_t)pid, 0) != 0 && errno == ESRCH;
}

static void clean_text(char *out, size_t size, const char *in, bool for_file) {
    size_t i = 0;
    for (; in[i] != '\0' && i + 1 < size; i++) {
        unsigned char c = (unsigned char)in[i];
        bool bad = c < 0x20 || c == 0x7f || (for_file && (c == ' ' || c == '/'));
        out[i] = bad ? '_' : (char)c;
    }
    out[i] = '\0';
    if (for_file && out[0] == '.') out[0] = '_';
}

/*
 * Written beside the folder then hard-linked in, so the host never reads half a line, and
 * link() refuses a name that is taken (a simulated Deck, or a second Deck with the same name).
 */
static bool write_link_file(struct deck *d) {
    if (!ensure_dir()) {
        problem(d, "cannot create %s: %s", dir, strerror(errno));
        return false;
    }
    char base[100], desc[128], tmp[PATH_MAX], names[2][120];
    clean_text(base, sizeof base, d->alias[0] ? d->alias : d->address, true);
    clean_text(desc, sizeof desc, d->alias[0] ? d->alias : "Control Deck", false);
    snprintf(names[0], sizeof names[0], "%s", base);
    snprintf(names[1], sizeof names[1], "%s_%c%c%c%c%c%c%c%c%c%c%c%c", base, d->address[0], d->address[1],
             d->address[3], d->address[4], d->address[6], d->address[7], d->address[9], d->address[10],
             d->address[12], d->address[13], d->address[15], d->address[16]);
    snprintf(tmp, sizeof tmp, "%s.%ld.tmp", dir, (long)getpid());

    FILE *f = fopen(tmp, "w");
    if (f == NULL) {
        problem(d, "cannot write %s: %s", tmp, strerror(errno));
        return false;
    }
    fprintf(f, "%ld %s %s (BLE %s)\n", (long)getpid(), d->slave, desc, d->address);
    if (fclose(f) != 0) {
        unlink(tmp);
        problem(d, "cannot write %s: %s", tmp, strerror(errno));
        return false;
    }
    for (int i = 0; i < 2; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if (link(tmp, path) == 0 ||
            (errno == EEXIST && stale_file(path, d->address) && unlink(path) == 0 && link(tmp, path) == 0)) {
            unlink(tmp);
            snprintf(d->file, sizeof d->file, "%s", path);
            return true;
        }
    }
    unlink(tmp);
    problem(d, "%s/%s and %s are taken", dir, names[0], names[1]);
    return false;
}

/* ---- the pty ---- */

/*
 * Opening the slave once and closing it leaves it in raw mode and puts the master in its
 * "other side closed" state (POLLHUP), which is how we see the host open and close it.
 * TCIFLUSH drops what the last host left unread, so the next one starts clean.
 */
static bool reset_slave(struct deck *d) {
    int s = open(d->slave, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (s < 0) return false;
    struct termios t;
    if (tcgetattr(s, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(s, TCSANOW, &t);
    }
    tcflush(s, TCIFLUSH);
    close(s);
    d->host_open = false;
    d->pty_check_at = now_usec() + PTY_CHECK;
    return true;
}

static bool open_pty(struct deck *d) {
    d->master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (d->master < 0 || grantpt(d->master) != 0 || unlockpt(d->master) != 0 ||
        ptsname_r(d->master, d->slave, sizeof d->slave) != 0 || !reset_slave(d)) {
        problem(d, "cannot open a pty: %s", strerror(errno));
        return false;
    }
    return true;
}

static void host_closed(struct deck *d) {
    say(d, "the host closed %s", d->slave);
    reset_slave(d);
}

/* ---- tearing down ---- */

static void unlink_deck(struct deck *d, const char *reason) {
    bool was_linked = d->state == LINKED;
    d->call = sd_bus_slot_unref(d->call);
    d->write_call = sd_bus_slot_unref(d->write_call);
    if (d->write_fd >= 0) close(d->write_fd);
    if (d->notify_fd >= 0) close(d->notify_fd);
    d->write_fd = d->notify_fd = -1;
    if (d->notifying && bus != NULL && sd_bus_is_open(bus) > 0)
        sd_bus_call_method_async(bus, NULL, BLUEZ, d->out_path, CHAR_IFACE, "StopNotify", NULL, NULL, "");
    d->notifying = d->write_fallback = d->notify_fallback = false;
    if (d->master >= 0) close(d->master);
    d->master = -1;
    d->host_open = false;
    if (d->file[0] != '\0') unlink(d->file);
    d->file[0] = '\0';
    d->tx_len = 0;
    d->state = IDLE;
    d->retry_at = now_usec() + RELINK_DELAY;
    if (was_linked && reason != NULL) say(d, "link ended (%s)", reason);
}

static void link_failed(struct deck *d, const char *fmt, const char *detail) {
    unlink_deck(d, NULL);
    problem(d, fmt, detail);
    d->retry_at = now_usec() + RETRY_DELAY;
}

/* Disconnect what this process connected, so the Deck sees the link end and advertises again. */
static void disconnect_deck(struct deck *d) {
    if (!d->we_connected || !d->connected || bus == NULL || sd_bus_is_open(bus) <= 0) return;
    sd_bus_message *m = NULL, *reply = NULL;
    sd_bus_error e = SD_BUS_ERROR_NULL;
    if (sd_bus_message_new_method_call(bus, &m, BLUEZ, d->path, DEVICE_IFACE, "Disconnect") >= 0)
        sd_bus_call(bus, m, 3 * SEC, &e, &reply);
    sd_bus_message_unref(m);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&e);
    d->we_connected = false;
}

static void free_deck(struct deck *d) {
    unlink_deck(d, NULL);
    free(d->path);
    free(d);
}

static void drop_all_decks(bool disconnect) {
    while (decks != NULL) {
        struct deck *d = decks;
        decks = d->next;
        bool linked = d->state == LINKED;
        unlink_deck(d, NULL);
        if (linked) say(d, "link closed");
        if (disconnect) disconnect_deck(d);
        free_deck(d);
    }
    none_logged = false;
}

/* ---- bytes ---- */

static void deliver(struct deck *d, const void *buf, size_t len) {
    if (!d->host_open) return; /* nobody reads the pty: the bytes would only go stale */
    ssize_t n = write(d->master, buf, len);
    if (n < 0 || (size_t)n < len) problem(d, "the host is not reading %s: bytes dropped", d->slave);
}

static int on_write_value(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct deck *d = userdata;
    d->write_call = sd_bus_slot_unref(d->write_call);
    if (sd_bus_message_is_method_error(m, NULL))
        problem(d, "deck_in write failed: %s", error_text(sd_bus_message_get_error(m)));
    d->tx_len = 0;
    return 0;
}

static void send_tx(struct deck *d) {
    if (d->tx_len == 0) return;
    if (d->write_fd >= 0) {
        ssize_t n = send(d->write_fd, d->tx, d->tx_len, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n == (ssize_t)d->tx_len)
            d->tx_len = 0;
        else if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            unlink_deck(d, n < 0 ? strerror(errno) : "short write on deck_in");
        return;
    }
    if (d->write_call != NULL) return;
    sd_bus_message *m = NULL;
    int r = sd_bus_message_new_method_call(bus, &m, BLUEZ, d->in_path, CHAR_IFACE, "WriteValue");
    if (r >= 0) r = sd_bus_message_append_array(m, 'y', d->tx, d->tx_len);
    if (r >= 0) r = sd_bus_message_append(m, "a{sv}", 1, "type", "s", "command");
    if (r >= 0) r = sd_bus_call_async(bus, &d->write_call, m, on_write_value, d, CALL_TIMEOUT);
    sd_bus_message_unref(m);
    if (r < 0) {
        problem(d, "deck_in write failed: %s", strerror(-r));
        d->tx_len = 0;
    }
}

static void read_master(struct deck *d) {
    ssize_t n = read(d->master, d->tx, d->chunk);
    if (n > 0) {
        d->tx_len = (size_t)n;
        send_tx(d);
    } else if (n < 0 && errno == EIO) {
        host_closed(d);
    }
}

static void read_notify(struct deck *d) {
    unsigned char buf[TX_MAX + 16];
    for (int i = 0; i < 16 && d->notify_fd >= 0; i++) {
        ssize_t n = recv(d->notify_fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            deliver(d, buf, (size_t)n);
        } else if (n == 0) {
            unlink_deck(d, "deck_out closed");
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) unlink_deck(d, strerror(errno));
            return;
        }
    }
}

/* ---- linking: Connect, AcquireWrite, AcquireNotify (or StartNotify), pty ---- */

static int call_async(struct deck *d, sd_bus_slot **slot, const char *path, const char *iface,
                      const char *member, bool options, sd_bus_message_handler_t cb, uint64_t timeout) {
    sd_bus_message *m = NULL;
    int r = sd_bus_message_new_method_call(bus, &m, BLUEZ, path, iface, member);
    if (r >= 0 && options) r = sd_bus_message_append(m, "a{sv}", 0);
    if (r >= 0) r = sd_bus_call_async(bus, slot, m, cb, d, timeout);
    sd_bus_message_unref(m);
    return r;
}

static void link_up(struct deck *d) {
    if (!open_pty(d) || !write_link_file(d)) {
        unlink_deck(d, NULL);
        d->retry_at = now_usec() + RETRY_DELAY;
        return;
    }
    d->state = LINKED;
    d->nproblems = 0;
    const char *how = !d->write_fallback && !d->notify_fallback ? "acquired"
                      : d->write_fallback && d->notify_fallback ? "WriteValue, StartNotify"
                      : d->write_fallback                       ? "WriteValue, notify acquired"
                                                                : "write acquired, StartNotify";
    say(d, "connected, pty %s, mtu %zu (%s)", d->slave, d->chunk, how);
}

static int on_start_notify(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct deck *d = userdata;
    d->call = sd_bus_slot_unref(d->call);
    if (sd_bus_message_is_method_error(m, NULL)) {
        link_failed(d, "cannot subscribe to deck_out: %s", error_text(sd_bus_message_get_error(m)));
        return 0;
    }
    d->notifying = true;
    link_up(d);
    return 0;
}

static int on_acquire_notify(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct deck *d = userdata;
    d->call = sd_bus_slot_unref(d->call);
    int fd = -1;
    uint16_t mtu = 0;
    if (!sd_bus_message_is_method_error(m, NULL) && sd_bus_message_read(m, "hq", &fd, &mtu) >= 0) {
        d->notify_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3); /* the message owns fd */
        if (d->notify_fd >= 0) {
            link_up(d);
            return 0;
        }
    }
    d->notify_fallback = true;
    int r = call_async(d, &d->call, d->out_path, CHAR_IFACE, "StartNotify", false, on_start_notify, CALL_TIMEOUT);
    if (r < 0) link_failed(d, "cannot subscribe to deck_out: %s", strerror(-r));
    return 0;
}

static int on_acquire_write(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct deck *d = userdata;
    d->call = sd_bus_slot_unref(d->call);
    int fd = -1;
    uint16_t mtu = 0;
    if (!sd_bus_message_is_method_error(m, NULL) && sd_bus_message_read(m, "hq", &fd, &mtu) >= 0)
        d->write_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (d->write_fd < 0) {
        d->write_fallback = true;
        mtu = d->in_mtu;
    }
    /* BlueZ gives the ATT MTU; a write without response carries 3 bytes less. */
    d->chunk = mtu > ATT_DEFAULT_MTU ? (size_t)mtu - 3 : ATT_DEFAULT_MTU - 3;
    if (d->chunk > TX_MAX) d->chunk = TX_MAX;
    int r = call_async(d, &d->call, d->out_path, CHAR_IFACE, "AcquireNotify", true, on_acquire_notify, CALL_TIMEOUT);
    if (r < 0) link_failed(d, "cannot acquire deck_out: %s", strerror(-r));
    return 0;
}

static int on_connect(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct deck *d = userdata;
    d->call = sd_bus_slot_unref(d->call);
    d->state = IDLE;
    if (sd_bus_message_is_method_error(m, NULL)) {
        problem(d, "not reachable (%s); retrying every %llu s, normal while the Deck is not on BLE",
                error_text(sd_bus_message_get_error(m)), RETRY_DELAY / SEC);
        d->retry_at = now_usec() + RETRY_DELAY;
        return 0;
    }
    d->we_connected = d->connected = true;
    d->retry_at = 0;
    request_scan(); /* to find the characteristics once services are resolved */
    return 0;
}

static void step_deck(struct deck *d, uint64_t now) {
    if (d->state != IDLE || now < d->retry_at) return;
    int r;
    if (!d->connected) {
        r = call_async(d, &d->call, d->path, DEVICE_IFACE, "Connect", false, on_connect, CONNECT_TIMEOUT);
        if (r >= 0) {
            d->state = CONNECTING;
            return;
        }
        problem(d, "cannot call Connect: %s", strerror(-r));
    } else if (!d->resolved) {
        /* Services still resolving: the ServicesResolved signal or the next scan moves on. */
    } else if (d->in_path[0] == '\0' || d->out_path[0] == '\0') {
        problem(d, "connected, but deck_in or deck_out is missing (firmware too old?)");
    } else {
        r = call_async(d, &d->call, d->in_path, CHAR_IFACE, "AcquireWrite", true, on_acquire_write, CALL_TIMEOUT);
        if (r >= 0) {
            d->state = ACQUIRING;
            return;
        }
        problem(d, "cannot acquire deck_in: %s", strerror(-r));
    }
    d->retry_at = now + RETRY_DELAY;
}

/* ---- scanning BlueZ's objects ---- */

struct chr {
    char path[160];
    bool out;
    uint16_t mtu;
};

static int parse_device(sd_bus_message *m, const char *path) {
    int paired = 0, connected = 0, resolved = 0, r;
    const char *address = NULL, *alias = NULL, *name = NULL, *key;
    bool has_service = false;

    if ((r = sd_bus_message_enter_container(m, 'a', "{sv}")) < 0) return r;
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        if ((r = sd_bus_message_read(m, "s", &key)) < 0) return r;
        if (strcmp(key, "Paired") == 0)
            r = sd_bus_message_read(m, "v", "b", &paired);
        else if (strcmp(key, "Connected") == 0)
            r = sd_bus_message_read(m, "v", "b", &connected);
        else if (strcmp(key, "ServicesResolved") == 0)
            r = sd_bus_message_read(m, "v", "b", &resolved);
        else if (strcmp(key, "Address") == 0)
            r = sd_bus_message_read(m, "v", "s", &address);
        else if (strcmp(key, "Alias") == 0)
            r = sd_bus_message_read(m, "v", "s", &alias);
        else if (strcmp(key, "Name") == 0)
            r = sd_bus_message_read(m, "v", "s", &name);
        else if (strcmp(key, "UUIDs") == 0) {
            const char *uuid;
            if ((r = sd_bus_message_enter_container(m, 'v', "as")) < 0) return r;
            if ((r = sd_bus_message_enter_container(m, 'a', "s")) < 0) return r;
            while ((r = sd_bus_message_read(m, "s", &uuid)) > 0)
                if (strcasecmp(uuid, SERVICE_UUID) == 0) has_service = true;
            if (r < 0) return r;
            if ((r = sd_bus_message_exit_container(m)) < 0) return r;
            r = sd_bus_message_exit_container(m);
        } else
            r = sd_bus_message_skip(m, "v");
        if (r < 0) return r;
        if ((r = sd_bus_message_exit_container(m)) < 0) return r;
    }
    if (r < 0) return r;
    if ((r = sd_bus_message_exit_container(m)) < 0) return r;

    if (!paired || !has_service || address == NULL) return 0;
    struct deck *d = find_deck(path);
    if (d == NULL) {
        d = calloc(1, sizeof *d);
        if (d == NULL || (d->path = strdup(path)) == NULL) {
            free(d);
            return -ENOMEM;
        }
        d->write_fd = d->notify_fd = d->master = -1;
        d->next = decks;
        decks = d;
        snprintf(d->address, sizeof d->address, "%s", address);
        snprintf(d->alias, sizeof d->alias, "%s", alias ? alias : name ? name : address);
        say(d, "paired Deck found");
    }
    /* The pty file keeps the name it was made with; a rename shows at the next link. */
    if (d->state != LINKED) snprintf(d->alias, sizeof d->alias, "%s", alias ? alias : name ? name : address);
    d->seen = true;
    d->connected = connected;
    d->resolved = resolved;
    if (!connected) d->we_connected = false;
    return 0;
}

static int parse_char(sd_bus_message *m, const char *path, struct chr *chars, size_t *n) {
    const char *key, *uuid = NULL;
    uint16_t mtu = 0;
    int r;
    if ((r = sd_bus_message_enter_container(m, 'a', "{sv}")) < 0) return r;
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        if ((r = sd_bus_message_read(m, "s", &key)) < 0) return r;
        if (strcmp(key, "UUID") == 0)
            r = sd_bus_message_read(m, "v", "s", &uuid);
        else if (strcmp(key, "MTU") == 0)
            r = sd_bus_message_read(m, "v", "q", &mtu);
        else
            r = sd_bus_message_skip(m, "v");
        if (r < 0) return r;
        if ((r = sd_bus_message_exit_container(m)) < 0) return r;
    }
    if (r < 0) return r;
    if ((r = sd_bus_message_exit_container(m)) < 0) return r;

    if (uuid == NULL || *n >= MAX_CHARS || strlen(path) >= sizeof chars[0].path) return 0;
    bool in = strcasecmp(uuid, DECK_IN_UUID) == 0, out = strcasecmp(uuid, DECK_OUT_UUID) == 0;
    if (!in && !out) return 0;
    snprintf(chars[*n].path, sizeof chars[*n].path, "%s", path);
    chars[*n].out = out;
    chars[*n].mtu = mtu;
    (*n)++;
    return 0;
}

static int parse_objects(sd_bus_message *m, struct chr *chars, size_t *nchars) {
    const char *path, *iface;
    int r;
    if ((r = sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}")) < 0) return r;
    while ((r = sd_bus_message_enter_container(m, 'e', "oa{sa{sv}}")) > 0) {
        if ((r = sd_bus_message_read(m, "o", &path)) < 0) return r;
        if ((r = sd_bus_message_enter_container(m, 'a', "{sa{sv}}")) < 0) return r;
        while ((r = sd_bus_message_enter_container(m, 'e', "sa{sv}")) > 0) {
            if ((r = sd_bus_message_read(m, "s", &iface)) < 0) return r;
            if (strcmp(iface, DEVICE_IFACE) == 0)
                r = parse_device(m, path);
            else if (strcmp(iface, CHAR_IFACE) == 0)
                r = parse_char(m, path, chars, nchars);
            else
                r = sd_bus_message_skip(m, "a{sv}");
            if (r < 0) return r;
            if ((r = sd_bus_message_exit_container(m)) < 0) return r;
        }
        if (r < 0) return r;
        if ((r = sd_bus_message_exit_container(m)) < 0) return r;
        if ((r = sd_bus_message_exit_container(m)) < 0) return r;
    }
    if (r < 0) return r;
    return sd_bus_message_exit_container(m);
}

static int on_scan(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata;
    (void)ret_error;
    uint64_t now = now_usec();
    scan_pending = false;
    scan_at = rescan_wanted ? now : now + SCAN_INTERVAL;
    rescan_wanted = false;

    if (sd_bus_message_is_method_error(m, NULL)) {
        if (!bluez_missing)
            printf("BlueZ (org.bluez) does not answer: %s; is bluetoothd running? Retrying every %llu s\n",
                   error_text(sd_bus_message_get_error(m)), BUS_RETRY / SEC);
        bluez_missing = true;
        drop_all_decks(false);
        scan_at = now + BUS_RETRY;
        return 0;
    }
    if (bluez_missing) printf("BlueZ answers again\n");
    bluez_missing = false;

    for (struct deck *d = decks; d != NULL; d = d->next) d->seen = false;
    struct chr chars[MAX_CHARS];
    size_t nchars = 0;
    int r = parse_objects(m, chars, &nchars);
    if (r < 0) {
        printf("cannot read BlueZ's objects: %s\n", strerror(-r));
        return 0;
    }

    for (struct deck **p = &decks; *p != NULL;) {
        struct deck *d = *p;
        if (!d->seen) {
            if (d->state == LINKED) unlink_deck(d, "no longer paired");
            say(d, "no longer paired, forgotten");
            *p = d->next;
            free_deck(d);
            continue;
        }
        if (d->state == LINKED && !d->connected) unlink_deck(d, "Deck disconnected");
        if (d->state == IDLE || d->state == CONNECTING) {
            /* The characteristics are re-found each time: BlueZ renumbers them per connection. */
            size_t len = strlen(d->path);
            d->in_path[0] = d->out_path[0] = '\0';
            for (size_t i = 0; i < nchars; i++) {
                if (strncmp(chars[i].path, d->path, len) != 0 || chars[i].path[len] != '/') continue;
                if (chars[i].out) {
                    memcpy(d->out_path, chars[i].path, sizeof d->out_path);
                } else {
                    memcpy(d->in_path, chars[i].path, sizeof d->in_path);
                    d->in_mtu = chars[i].mtu;
                }
            }
        }
        p = &d->next;
    }

    if (decks == NULL && !none_logged)
        printf("no paired Control Deck; pair one from the system's Bluetooth settings (code shown on the Deck)\n");
    none_logged = decks == NULL;
    return 0;
}

static void start_scan(void) {
    int r = sd_bus_call_method_async(bus, NULL, BLUEZ, "/", "org.freedesktop.DBus.ObjectManager",
                                     "GetManagedObjects", on_scan, NULL, "");
    if (r < 0) {
        printf("cannot ask BlueZ for its devices: %s\n", strerror(-r));
        scan_at = now_usec() + SCAN_INTERVAL;
        return;
    }
    scan_pending = true;
}

/* ---- signals from BlueZ ---- */

static int on_device_props(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata;
    (void)ret_error;
    struct deck *d = find_deck(sd_bus_message_get_path(m));
    const char *iface, *key;
    int r, value;
    if (sd_bus_message_read(m, "s", &iface) < 0) return 0;
    if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) return 0;
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        if (sd_bus_message_read(m, "s", &key) < 0) return 0;
        if (d != NULL && strcmp(key, "Connected") == 0) {
            if ((r = sd_bus_message_read(m, "v", "b", &value)) < 0) return 0;
            d->connected = value;
            if (!value) {
                d->we_connected = false;
                d->resolved = false;
                if (d->state == ACQUIRING || d->state == LINKED) unlink_deck(d, "Deck disconnected");
            }
        } else if (d != NULL && strcmp(key, "ServicesResolved") == 0) {
            if ((r = sd_bus_message_read(m, "v", "b", &value)) < 0) return 0;
            d->resolved = value;
            if (value) {
                d->retry_at = 0;
                request_scan();
            }
        } else {
            if (strcmp(key, "Paired") == 0 || strcmp(key, "UUIDs") == 0) request_scan();
            if (sd_bus_message_skip(m, "v") < 0) return 0;
        }
        if (sd_bus_message_exit_container(m) < 0) return 0;
    }
    return 0;
}

/* Only used on the StartNotify fallback: each notification arrives as a new Value. */
static int on_char_props(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata;
    (void)ret_error;
    const char *path = sd_bus_message_get_path(m), *iface, *key;
    struct deck *d = decks;
    while (d != NULL && !(d->notifying && d->state == LINKED && strcmp(d->out_path, path) == 0)) d = d->next;
    if (d == NULL) return 0;
    if (sd_bus_message_read(m, "s", &iface) < 0) return 0;
    if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) return 0;
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        if (sd_bus_message_read(m, "s", &key) < 0) return 0;
        if (strcmp(key, "Value") == 0) {
            const void *data;
            size_t size;
            if (sd_bus_message_enter_container(m, 'v', "ay") < 0) return 0;
            if (sd_bus_message_read_array(m, 'y', &data, &size) < 0) return 0;
            deliver(d, data, size);
            if (sd_bus_message_exit_container(m) < 0) return 0;
        } else if (sd_bus_message_skip(m, "v") < 0) {
            return 0;
        }
        if (sd_bus_message_exit_container(m) < 0) return 0;
    }
    return 0;
}

static int on_objects_changed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)m;
    (void)userdata;
    (void)ret_error;
    request_scan();
    return 0;
}

/* ---- the system bus ---- */

static void open_bus(void) {
    int r = sd_bus_open_system(&bus);
    if (r >= 0) r = sd_bus_add_match(bus, NULL,
                                     "type='signal',sender='" BLUEZ "',interface='org.freedesktop.DBus.Properties',"
                                     "member='PropertiesChanged',arg0='" DEVICE_IFACE "'",
                                     on_device_props, NULL);
    if (r >= 0) r = sd_bus_add_match(bus, NULL,
                                     "type='signal',sender='" BLUEZ "',interface='org.freedesktop.DBus.Properties',"
                                     "member='PropertiesChanged',arg0='" CHAR_IFACE "'",
                                     on_char_props, NULL);
    if (r >= 0) r = sd_bus_match_signal(bus, NULL, BLUEZ, "/", "org.freedesktop.DBus.ObjectManager",
                                        "InterfacesAdded", on_objects_changed, NULL);
    if (r >= 0) r = sd_bus_match_signal(bus, NULL, BLUEZ, "/", "org.freedesktop.DBus.ObjectManager",
                                        "InterfacesRemoved", on_objects_changed, NULL);
    /* bluetoothd starting or stopping: scan at once rather than at the next retry. */
    if (r >= 0) r = sd_bus_add_match(bus, NULL,
                                     "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
                                     "member='NameOwnerChanged',arg0='" BLUEZ "'",
                                     on_objects_changed, NULL);
    if (r < 0) {
        if (!bus_missing)
            printf("no system D-Bus (%s); retrying every %llu s\n", strerror(-r), BUS_RETRY / SEC);
        bus_missing = true;
        bus = sd_bus_flush_close_unref(bus);
        bus_retry_at = now_usec() + BUS_RETRY;
        return;
    }
    if (bus_missing) printf("system D-Bus connected\n");
    bus_missing = false;
    scan_at = 0;
    scan_pending = rescan_wanted = false;
}

static void close_bus(int error) {
    printf("lost the system D-Bus (%s); retrying every %llu s\n", strerror(-error), BUS_RETRY / SEC);
    bus_missing = true;
    bluez_missing = false;
    drop_all_decks(false);
    bus = sd_bus_flush_close_unref(bus);
    bus_retry_at = now_usec() + BUS_RETRY;
}

/* ---- main ---- */

enum watch_kind { W_MASTER, W_NOTIFY, W_WRITE };

struct watch {
    struct deck *d;
    enum watch_kind kind;
};

static void usage(FILE *out) {
    fprintf(out,
            "Usage: ble-bridge [--help]\n"
            "\n"
            "Connects to every Control Deck paired with this PC over Bluetooth LE and gives each\n"
            "one a pseudo terminal, named in $XDG_RUNTIME_DIR/control-deck-ble/<name> as\n"
            "\"<pid> <pty> <name> (BLE <address>)\". The Control Deck host opens it like a USB\n"
            "serial port. Pair a Deck once from the system's Bluetooth settings (the code is shown\n"
            "on the Deck); this program never pairs. Stops on SIGINT or SIGTERM.\n");
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }
        fprintf(stderr, "ble-bridge: unknown argument %s\n", argv[i]);
        usage(stderr);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGPIPE, SIG_IGN); /* a host that stops reading our output must not kill us */

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sfd < 0) {
        printf("signalfd: %s\n", strerror(errno));
        return 1;
    }

    struct pollfd *fds = NULL;
    struct watch *watches = NULL;
    size_t capacity = 0;
    bool quit = false;

    while (!quit) {
        uint64_t now = now_usec();
        if (bus == NULL && now >= bus_retry_at) open_bus();
        if (bus != NULL && !scan_pending && now >= scan_at) start_scan();

        size_t ndecks = 0;
        for (struct deck *d = decks; d != NULL; d = d->next) {
            ndecks++;
            if (bus != NULL) step_deck(d, now);
            if (d->state == LINKED && !d->host_open && now >= d->pty_check_at) {
                struct pollfd p = {.fd = d->master, .events = POLLIN};
                if (poll(&p, 1, 0) >= 0 && !(p.revents & POLLHUP)) {
                    d->host_open = true;
                    say(d, "the host opened %s", d->slave);
                } else {
                    d->pty_check_at = now + PTY_CHECK;
                }
            }
        }

        size_t need = 2 + 3 * ndecks;
        if (need > capacity) {
            struct pollfd *nf = realloc(fds, need * sizeof *fds);
            if (nf != NULL) fds = nf;
            struct watch *nw = realloc(watches, need * sizeof *watches);
            if (nw != NULL) watches = nw;
            if (nf == NULL || nw == NULL) {
                printf("out of memory\n");
                break;
            }
            capacity = need;
        }

        size_t n = 0;
        fds[n++] = (struct pollfd){.fd = sfd, .events = POLLIN};
        uint64_t next = UINT64_MAX;
        if (bus != NULL) {
            uint64_t t = UINT64_MAX;
            int events = sd_bus_get_events(bus);
            fds[n++] = (struct pollfd){.fd = sd_bus_get_fd(bus), .events = (short)(events > 0 ? events : 0)};
            if (sd_bus_get_timeout(bus, &t) >= 0 && t < next) next = t;
            if (!scan_pending && scan_at < next) next = scan_at;
        } else if (bus_retry_at < next) {
            next = bus_retry_at;
        }
        size_t first_watch = n;
        for (struct deck *d = decks; d != NULL; d = d->next) {
            if (d->state == IDLE && d->retry_at < next) next = d->retry_at;
            if (d->state != LINKED) continue;
            if (d->host_open) {
                /* While a chunk waits for deck_in, leave the rest in the pty: that is the backpressure. */
                fds[n] = (struct pollfd){.fd = d->master, .events = d->tx_len == 0 ? POLLIN : 0};
                watches[n++ - first_watch] = (struct watch){d, W_MASTER};
            } else if (d->pty_check_at < next) {
                next = d->pty_check_at;
            }
            if (d->notify_fd >= 0) {
                fds[n] = (struct pollfd){.fd = d->notify_fd, .events = POLLIN};
                watches[n++ - first_watch] = (struct watch){d, W_NOTIFY};
            }
            if (d->write_fd >= 0) {
                fds[n] = (struct pollfd){.fd = d->write_fd, .events = d->tx_len > 0 ? POLLOUT : 0};
                watches[n++ - first_watch] = (struct watch){d, W_WRITE};
            }
        }

        now = now_usec();
        int timeout = next == UINT64_MAX ? -1 : next <= now ? 0 : (int)((next - now + 999) / 1000);
        if (poll(fds, n, timeout) < 0 && errno != EINTR) {
            printf("poll: %s\n", strerror(errno));
            break;
        }

        if (fds[0].revents & POLLIN) {
            struct signalfd_siginfo info;
            if (read(sfd, &info, sizeof info) > 0) quit = true;
        }

        /* Deck fds before the bus: only a bus callback frees a deck, so the pointers hold. */
        for (size_t i = first_watch; i < n; i++) {
            struct deck *d = watches[i - first_watch].d;
            short re = fds[i].revents;
            if (re == 0 || d->state != LINKED) continue;
            switch (watches[i - first_watch].kind) {
            case W_MASTER:
                if (fds[i].fd != d->master || !d->host_open) break;
                if (re & POLLIN)
                    read_master(d);
                else if (re & (POLLHUP | POLLERR))
                    host_closed(d);
                break;
            case W_NOTIFY:
                if (fds[i].fd != d->notify_fd) break;
                if (re & POLLIN)
                    read_notify(d);
                else if (re & (POLLHUP | POLLERR))
                    unlink_deck(d, "deck_out closed by BlueZ");
                break;
            case W_WRITE:
                if (fds[i].fd != d->write_fd) break;
                if (re & (POLLHUP | POLLERR))
                    unlink_deck(d, "deck_in closed by BlueZ");
                else if (re & POLLOUT)
                    send_tx(d);
                break;
            }
        }

        if (bus != NULL) {
            int r;
            while ((r = sd_bus_process(bus, NULL)) > 0) {
            }
            if (r < 0) close_bus(r);
        }
    }

    drop_all_decks(true);
    if (bus != NULL) bus = sd_bus_flush_close_unref(bus);
    if (dir_made) rmdir(dir); /* only if we made it and it is empty */
    free(fds);
    free(watches);
    close(sfd);
    printf("stopped\n");
    return 0;
}
