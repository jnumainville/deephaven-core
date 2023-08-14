package io.deephaven.server.jetty;

import com.fasterxml.jackson.databind.ObjectMapper;
import io.deephaven.plugin.type.JsPluginInfo;

import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.URI;
import java.nio.file.FileSystem;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;

public class ContentPluginsZipFilesystem {

    private static final String PLUGINS = "plugins";
    private static final String MANIFEST_JSON = "manifest.json";

    /**
     * Creates a new js plugins instance with a temporary zip filesystem.
     *
     * @return the js plugins
     * @throws IOException if an I/O exception occurs
     */
    public static ContentPluginsZipFilesystem create() throws IOException {
        final Path tempDir = Files.createTempDirectory(ContentPluginsZipFilesystem.class.getName());
        tempDir.toFile().deleteOnExit();
        final Path fsZip = tempDir.resolve("deephaven-js-plugins.zip");
        fsZip.toFile().deleteOnExit();
        final URI uri = URI.create(String.format("jar:file:%s!/", fsZip));
        final ContentPluginsZipFilesystem jsPlugins = new ContentPluginsZipFilesystem(uri);
        jsPlugins.init();
        return jsPlugins;
    }

    private final URI filesystem;
    private final List<JsPluginInfo> plugins;

    private ContentPluginsZipFilesystem(URI filesystem) {
        this.filesystem = Objects.requireNonNull(filesystem);
        this.plugins = new ArrayList<>();
    }

    private void init() throws IOException {
        try (final FileSystem fs = FileSystems.newFileSystem(filesystem, Map.of("create", "true"))) {
            writeManifest(fs);
        }
    }

    private void writeManifest(FileSystem fs) throws IOException {
        final Path manifestPath = fs.getPath("/", MANIFEST_JSON);
        try (final OutputStream out = new BufferedOutputStream(Files.newOutputStream(manifestPath))) {
            new ObjectMapper().writeValue(out, Map.of(PLUGINS, plugins));
            out.flush();
        }
    }
}
