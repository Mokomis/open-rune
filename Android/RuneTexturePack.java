package org.runeclassic.port;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import java.net.HttpURLConnection;
import java.net.URL;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.HashSet;
import java.util.function.Consumer;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import org.json.JSONArray;
import org.json.JSONObject;

final class RuneTexturePack {
    private static final long MAX_VOLUME_BYTES = 1750L * 1024 * 1024;
    private static final int MAX_PAYLOAD_BYTES = 24 * 1024 * 1024;
    private static final int MAX_MANIFEST_BYTES = 2 * 1024 * 1024;
    private static final Pattern HASH = Pattern.compile("[0-9a-f]{64}");
    private static final Pattern TEXTURE = Pattern.compile(
        "[a-z0-9_][a-z0-9_.-]{1,239}/[124]x/(color|masked|normal|response|relief)\\.astc\\.ktx");

    private RuneTexturePack() {}

    static boolean ready(File game) {
        return new File(game, "TexturePacks/OpenRune-release/index.tsv").isFile();
    }

    static void prepare(Context context, File game, Consumer<String> progress) throws Exception {
        if (ready(game)) return;
        JSONObject receipt;
        try (InputStream input = context.getAssets().open("texture-release.json")) {
            ByteArrayOutputStream raw = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            for (int n; (n = input.read(buffer)) != -1;) raw.write(buffer, 0, n);
            receipt = new JSONObject(new String(raw.toByteArray(), StandardCharsets.UTF_8));
        }
        prepare(game, new File(context.getFilesDir(), "texture-download"), receipt,
                context.getSystemService(ConnectivityManager.class), progress);
    }

    static void prepare(File game, File cache, JSONObject receipt, ConnectivityManager connection,
                        Consumer<String> progress) throws Exception {
        if (ready(game)) return;
        JSONArray records = receipt.getJSONArray("parts");
        if (!cache.isDirectory() && !cache.mkdirs()) throw new IOException("Could not create the download folder.");

        File[] parts = new File[records.length()];
        long required = receipt.getLong("unpacked_bytes") + 128L * 1024 * 1024;
        for (int i = 0; i < parts.length; i++) {
            JSONObject part = records.getJSONObject(i);
            parts[i] = new File(cache, part.getString("file"));
            required += Math.max(0L, part.getLong("bytes") - parts[i].length());
        }
        if (cache.getUsableSpace() < required) {
            progress.accept("Free about " + (required / 1_000_000_000L + 1) + " GB to prepare remastered textures.");
            return;
        }
        for (int i = 0; i < parts.length; i++) {
            File file = parts[i];
            JSONObject part = records.getJSONObject(i);
            if (matches(file, part)) continue;
            Network network = connection.getActiveNetwork();
            if (!unmetered(connection, network)) {
                progress.accept("Remastered textures will download on an unmetered connection.");
                return;
            }
            progress.accept("Downloading remastered textures · " + (i + 1) + " / " + parts.length);
            fetch(connection, network, new URL(receipt.getString("url") + file.getName()), file, part.getLong("bytes"));
            if (!matches(file, part)) {
                Files.deleteIfExists(file.toPath());
                throw new IOException("Texture download checksum mismatch.");
            }
        }
        install(parts, game, receipt.getString("pack"), progress);
        for (File part : parts) Files.deleteIfExists(part.toPath());
        progress.accept("WIP textures are ready.");
    }

    private static boolean matches(File file, JSONObject part) throws Exception {
        if (!file.isFile() || file.length() != part.getLong("bytes")) return false;
        MessageDigest sha = MessageDigest.getInstance("SHA-256");
        byte[] bytes = new byte[131072];
        try (InputStream in = new BufferedInputStream(new FileInputStream(file))) {
            int count;
            while ((count = in.read(bytes)) != -1) sha.update(bytes, 0, count);
        }
        return hex(sha.digest()).equals(part.getString("sha256"));
    }

    private static boolean unmetered(ConnectivityManager mgr, Network network) {
        NetworkCapabilities caps = network == null ? null : mgr.getNetworkCapabilities(network);
        return caps != null && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
            && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED);
    }

    private static void fetch(ConnectivityManager mgr, Network network, URL url, File file, long expected) throws Exception {
        long offset = file.length();
        if (offset >= expected) {
            Files.deleteIfExists(file.toPath());
            offset = 0;
        }
        for (int redirects = 0; redirects <= 5; redirects++) {
            if (!"https".equals(url.getProtocol())) throw new IOException("Texture downloads require HTTPS.");
            HttpURLConnection http = (HttpURLConnection) network.openConnection(url);
            try {
                http.setInstanceFollowRedirects(false);
                http.setConnectTimeout(10_000);
                http.setReadTimeout(20_000);
                http.setRequestProperty("Accept-Encoding", "identity");
                if (offset != 0) http.setRequestProperty("Range", "bytes=" + offset + "-");
                int status = http.getResponseCode();
                if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                    String location = http.getHeaderField("Location");
                    if (location == null) throw new IOException("Texture download redirect is missing.");
                    url = new URL(url, location);
                    continue;
                }
                if (status == 200) offset = 0;
                else if (status != 206 || offset == 0 ||
                         !("bytes " + offset + "-" + (expected - 1) + "/" + expected).equals(http.getHeaderField("Content-Range")))
                    throw new IOException("Texture download response " + status);
                if (http.getContentLengthLong() != expected - offset)
                    throw new IOException("Texture download size mismatch.");
                try (InputStream in = new BufferedInputStream(http.getInputStream());
                     OutputStream out = new BufferedOutputStream(new FileOutputStream(file, offset != 0))) {
                    byte[] bytes = new byte[131072];
                    long written = offset, nextCheck = offset;
                    for (int count; (count = in.read(bytes)) != -1;) {
                        if (written >= nextCheck) {
                            if (!unmetered(mgr, network)) throw new IOException("Waiting for an unmetered connection.");
                            nextCheck = written + 8L * 1024 * 1024;
                        }
                        if (written + count > expected) throw new IOException("Texture download exceeds expected size.");
                        out.write(bytes, 0, count);
                        written += count;
                    }
                    if (written != expected) throw new IOException("Texture download was interrupted.");
                }
                return;
            } finally {
                http.disconnect();
            }
        }
        throw new IOException("Too many redirects while downloading textures.");
    }

    private static void install(File[] volumes, File game, String expectedPack, Consumer<String> progress) throws Exception {
        if (volumes.length == 0 || volumes.length > 64) throw new IOException("Invalid texture volume count.");
        if (!game.isDirectory())
            throw new IOException("Import your Rune Classic game files first.");
        File parent = new File(game, "TexturePacks");
        if (!parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Could not create the texture folder.");
        File destination = new File(parent, "OpenRune-release");
        if (ready(game))
            throw new IOException("The release texture pack is already installed.");
        File stage = Files.createTempDirectory(parent.toPath(), ".release-import-").toFile();
        HashSet<Integer> parts = new HashSet<>();
        
        byte[] bytes = new byte[131072];
        try {
            for (File volume : volumes) {
                try (InputStream raw = new FileInputStream(volume)) {
                    try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(raw))) {
                        ZipEntry first = zip.getNextEntry();
                        if (first == null || !first.getName().equals("pack-part.json"))
                            throw new IOException("Missing texture pack manifest.");
                        ByteArrayOutputStream manifest = new ByteArrayOutputStream();
                        int amount;
                        while ((amount = zip.read(bytes)) != -1) {
                            if (manifest.size() + amount > MAX_MANIFEST_BYTES)
                                throw new IOException("Texture pack manifest is too large.");
                            manifest.write(bytes, 0, amount);
                        }
                        JSONObject part = new JSONObject(manifest.toString("UTF-8"));
                        if (part.getInt("version") != 1 || !part.getString("platform").equals("android"))
                            throw new IOException("The downloaded texture pack is for a different platform.");
                        String packId = part.getString("pack");
                        int number = part.getInt("part");
                        int count = part.getInt("parts");
                        if (count != volumes.length || number < 1 || number > count || !parts.add(number)
                            || !HASH.matcher(packId).matches() || !expectedPack.equals(packId))
                            throw new IOException("The texture pack has missing or mismatched parts.");
                        progress.accept("Installing texture pack · " + parts.size() + " / " + count);
                        JSONObject files = part.getJSONObject("files");
                        if (files.length() == 0 || files.length() > 16000)
                            throw new IOException("Invalid texture inventory.");
                        HashSet<String> copied = new HashSet<>();
                        long volumeBytes = 0;
                        ZipEntry entry;
                        while ((entry = zip.getNextEntry()) != null) {
                            String name = entry.getName();
                            boolean index = name.equals("index.tsv");
                            if (entry.isDirectory() || (!index && !TEXTURE.matcher(name).matches())
                                || !copied.add(name) || !files.has(name))
                                throw new IOException("Unexpected file in the texture pack.");
                            JSONArray record = files.getJSONArray(name);
                            int expected = record.getInt(0);
                            String hash = record.getString(1);
                            if (expected <= 0 || expected > (index ? MAX_MANIFEST_BYTES : MAX_PAYLOAD_BYTES)
                                || !HASH.matcher(hash).matches()
                                || (volumeBytes += expected) > MAX_VOLUME_BYTES)
                                throw new IOException("Invalid texture size or checksum.");
                            if (stage.getUsableSpace() < expected + 32L * 1024 * 1024)
                                throw new IOException("The device is out of space for the texture pack.");
                            File output = new File(stage, name);
                            if (output.exists() || (!output.getParentFile().isDirectory() && !output.getParentFile().mkdirs()))
                                throw new IOException("Duplicate or inaccessible texture file.");
                            MessageDigest digest = MessageDigest.getInstance("SHA-256");
                            int written = 0;
                            try (OutputStream file = new BufferedOutputStream(new FileOutputStream(output))) {
                                int n;
                                while ((n = zip.read(bytes)) != -1) {
                                    written += n;
                                    if (written > expected) throw new IOException("Invalid texture payload size.");
                                    digest.update(bytes, 0, n);
                                    file.write(bytes, 0, n);
                                }
                            }
                            if (written != expected || !hex(digest.digest()).equals(hash))
                                throw new IOException("A texture ZIP is incomplete or damaged.");
                        }
                        if (copied.size() != files.length())
                            throw new IOException("Files are missing from a texture ZIP.");
                    }
                }
            }
            File index = new File(stage, "index.tsv");
            try (BufferedReader in = new BufferedReader(new InputStreamReader(new FileInputStream(index), StandardCharsets.UTF_8))) {
                if (!"OPENRUNE_TEXTURES_1".equals(in.readLine()) || in.readLine() == null)
                    throw new IOException("The texture index is invalid.");
            }
            if (destination.exists()) throw new IOException("A release texture folder already exists.");
            Files.move(stage.toPath(), destination.toPath());
        } finally {
            if (stage.exists()) RuneGameFiles.removeStage(stage);
        }
    }

    private static String hex(byte[] hash) {
        final String digits = "0123456789abcdef";
        StringBuilder text = new StringBuilder(64);
        for (byte value : hash) {
            text.append(digits.charAt((value & 255) >>> 4));
            text.append(digits.charAt(value & 15));
        }
        return text.toString();
    }
}
