package dev.controldeck.host.firmware;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.security.spec.ECGenParameterSpec;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.Arrays;

/**
 * Signs firmware images for over-the-air updates (docs/firmware/OTA.md). ECDSA P-256 with SHA-256.
 *
 * <p>A signed image is the image followed by an 80-byte trailer: the ASCII text {@code DECKSIG1},
 * one byte with the length of the signature, the DER signature (at most 72 bytes), zero padding.
 * The signature covers the image bytes only. The Deck checks it with the public key built into its
 * firmware before it commits the new image; the private key never leaves this computer.
 *
 * <pre>
 *   java -cp control-deck-host-*-all.jar dev.controldeck.host.firmware.FirmwareSigner keygen KEY.pk8 HEADER.h
 *   java -cp ...                          ...FirmwareSigner sign   KEY.pk8 IN.bin OUT.bin
 *   java -cp ...                          ...FirmwareSigner verify KEY.pk8 SIGNED.bin
 * </pre>
 */
public final class FirmwareSigner {
    public static final int TRAILER = 80;
    private static final byte[] MAGIC = "DECKSIG1".getBytes(StandardCharsets.US_ASCII);

    private FirmwareSigner() {
    }

    /** True when the file ends with a trailer (not that the signature is valid). */
    public static boolean hasTrailer(byte[] bytes) {
        if (bytes.length <= TRAILER) {
            return false;
        }
        int at = bytes.length - TRAILER;
        for (int i = 0; i < MAGIC.length; i++) {
            if (bytes[at + i] != MAGIC[i]) {
                return false;
            }
        }
        int len = bytes[at + MAGIC.length] & 0xFF;
        return len >= 8 && len <= TRAILER - MAGIC.length - 1;
    }

    public static byte[] sign(PrivateKey key, byte[] image) throws Exception {
        // ECDSA signatures are randomised and a DER P-256 signature is 70 to 72 bytes; the trailer holds 71:
        // sign again until it fits (about 1 in 4 tries needs a retry).
        byte[] der = null;
        for (int tries = 0; tries < 50 && (der == null || der.length > TRAILER - MAGIC.length - 1); tries++) {
            Signature s = Signature.getInstance("SHA256withECDSA");
            s.initSign(key);
            s.update(image);
            der = s.sign();
        }
        if (der == null || der.length > TRAILER - MAGIC.length - 1) {
            throw new IllegalStateException("signature too long: " + (der == null ? -1 : der.length));
        }
        byte[] out = Arrays.copyOf(image, image.length + TRAILER);
        int at = image.length;
        System.arraycopy(MAGIC, 0, out, at, MAGIC.length);
        out[at + MAGIC.length] = (byte) der.length;
        System.arraycopy(der, 0, out, at + MAGIC.length + 1, der.length);
        return out;
    }

    public static boolean verify(PublicKey key, byte[] signed) throws Exception {
        if (!hasTrailer(signed)) {
            return false;
        }
        int at = signed.length - TRAILER;
        int len = signed[at + MAGIC.length] & 0xFF;
        byte[] der = Arrays.copyOfRange(signed, at + MAGIC.length + 1, at + MAGIC.length + 1 + len);
        Signature s = Signature.getInstance("SHA256withECDSA");
        s.initVerify(key);
        s.update(signed, 0, at);
        return s.verify(der);
    }

    public static void main(String[] a) throws Exception {
        if (a.length < 2) {
            usage();
            return;
        }
        switch (a[0]) {
            case "keygen" -> keygen(Path.of(a[1]), Path.of(a[2]));
            case "sign" -> {
                PrivateKey key = readPrivate(Path.of(a[1]));
                Files.write(Path.of(a[3]), sign(key, Files.readAllBytes(Path.of(a[2]))));
                System.out.println("signed " + a[3]);
            }
            case "verify" -> {
                byte[] signed = Files.readAllBytes(Path.of(a[2]));
                PublicKey pub = readPublic(Path.of(a[1]));
                boolean ok = verify(pub, signed);
                System.out.println(ok ? "signature OK" : "signature NOT valid");
                System.exit(ok ? 0 : 1);
            }
            default -> usage();
        }
    }

    private static void usage() {
        System.err.println("usage: FirmwareSigner keygen KEY.pk8 HEADER.h | sign KEY.pk8 IN.bin OUT.bin | verify KEY.pk8 SIGNED.bin");
    }

    /** A new key pair: the private key (PKCS#8) for the server, the public key (DER) as a C header. */
    private static void keygen(Path keyFile, Path header) throws Exception {
        if (Files.exists(keyFile)) {
            throw new IOException(keyFile + " exists: refusing to replace a signing key (delete it by hand to rotate)");
        }
        KeyPairGenerator g = KeyPairGenerator.getInstance("EC");
        g.initialize(new ECGenParameterSpec("secp256r1"));
        KeyPair pair = g.generateKeyPair();
        Files.createDirectories(keyFile.toAbsolutePath().getParent());
        Files.write(keyFile, pair.getPrivate().getEncoded());
        try {
            Files.setPosixFilePermissions(keyFile, PosixFilePermissions.fromString("rw-------"));
        } catch (UnsupportedOperationException ignored) {
            // not POSIX: the folder permissions are the only protection
        }
        byte[] der = pair.getPublic().getEncoded();
        Files.write(keyFile.resolveSibling(keyFile.getFileName() + ".pub"), der);
        StringBuilder h = new StringBuilder("// Public key for firmware signatures (ECDSA P-256, DER SubjectPublicKeyInfo).\n"
                + "// Generated by FirmwareSigner keygen; the private key stays on the server. Safe to publish.\n"
                + "#pragma once\n#include <stdint.h>\nstatic const uint8_t OTA_PUBLIC_KEY[] = {");
        for (int i = 0; i < der.length; i++) {
            h.append(i % 12 == 0 ? "\n    " : " ").append(String.format("0x%02X,", der[i] & 0xFF));
        }
        h.append("\n};\nstatic const unsigned OTA_PUBLIC_KEY_LEN = ").append(der.length).append(";\n");
        Files.writeString(header, h.toString());
        System.out.println("key " + keyFile + " (private, keep it secret) and header " + header);
    }

    private static PrivateKey readPrivate(Path file) throws Exception {
        return KeyFactory.getInstance("EC").generatePrivate(new PKCS8EncodedKeySpec(Files.readAllBytes(file)));
    }

    /** The public key written beside the private one by keygen (KEY.pk8.pub, DER SubjectPublicKeyInfo). */
    private static PublicKey readPublic(Path privateKeyFile) throws Exception {
        Path pub = privateKeyFile.resolveSibling(privateKeyFile.getFileName() + ".pub");
        return KeyFactory.getInstance("EC").generatePublic(new X509EncodedKeySpec(Files.readAllBytes(pub)));
    }
}
