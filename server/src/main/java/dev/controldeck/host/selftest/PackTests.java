package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.pack.PackStore;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

/** The Decks' packs (screens and theme for the card): format, version, board files, assignment. */
final class PackTests {

    private PackTests() {
    }

    static void run(Runner r) {
        r.test("pack: files in order, a version from the content, a board's own files, per-Deck packs", () -> {
            Path dir = Files.createTempDirectory("cd-packs");
            Path def = Files.createDirectories(dir.resolve("default/screens"));
            Files.writeString(def.resolve("settings.scr"), "title SETTINGS\n");
            Files.writeString(def.resolve("about.scr"), "title ABOUT\n");
            Files.writeString(dir.resolve("default/version"), "ignored");
            Files.createDirectories(dir.resolve("default/@e32r35t/screens"));
            Files.writeString(dir.resolve("default/@e32r35t/screens/about.scr"), "title BIG ABOUT\n");
            PackStore store = new PackStore(dir, null);

            PackStore.Pack small = store.forDeck("AA", "es3c28p").orElseThrow();
            String text = new String(small.bytes(), StandardCharsets.US_ASCII);
            check(text.startsWith("DECKPACK 1\nversion " + small.version() + "\n"), "header");
            check(text.indexOf("file screens/about.scr 12\ntitle ABOUT\n") < text.indexOf("file screens/settings.scr"),
                    "files sorted, each with its size");
            check(text.endsWith("end\n"), "end marker");
            check(!text.contains("file version"), "the Deck writes its version itself");
            equal(12, small.version().length(), "short version");

            PackStore.Pack big = store.forDeck("AA", "e32r35t").orElseThrow();
            check(new String(big.bytes(), StandardCharsets.US_ASCII).contains("title BIG ABOUT"), "the board's own file");
            check(!big.version().equals(small.version()), "another content, another version");
            equal(small.version(), store.forDeck("BB", "es3c28p").orElseThrow().version(), "same content, same version");

            Files.createDirectories(dir.resolve("night/screens"));
            Files.writeString(dir.resolve("night/screens/settings.scr"), "title NIGHT\n");
            Files.writeString(dir.resolve("decks.properties"), "CC=night\n");
            equal("night", store.forDeck("CC", "es3c28p").orElseThrow().name(), "a Deck's own pack");
            equal("default", store.forDeck("AA", "es3c28p").orElseThrow().name(), "the others the default");
            check(new PackStore(dir.resolve("none"), null).forDeck("AA", "x").isEmpty(), "no packs: nothing");

            equal(java.util.List.of("default", "night"), store.names(), "the packs there are");
            store.assign("dd", "night");
            equal("night", store.assigned("DD"), "assigned, the id in capitals");
            store.assign("DD", "default");
            equal("default", store.assigned("DD"), "back to the default");
            check(!store.assignments().containsKey("DD"), "the default is not written");
            boolean refused = false;
            try {
                store.assign("DD", "missing");
            } catch (IllegalArgumentException e) {
                refused = true;
            }
            check(refused, "an unknown pack is refused");
        });
    }
}
