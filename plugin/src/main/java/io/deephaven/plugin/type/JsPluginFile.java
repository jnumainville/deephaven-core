package io.deephaven.plugin.type;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;
import io.deephaven.internal.log.LoggerFactory;
import io.deephaven.io.logger.Logger;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;

public class JsPluginFile {

    private static final Logger log = LoggerFactory.getLogger(JsPluginFile.class);

    private static final String PREFIX = "preloaded-plugins";

    private static final String PRE = "plugin";

    private static JsPluginFile instance = null;

    private static List<JsPluginInfo> jsPluginInfoList;

    private static Path tempDir;

    private JsPluginFile()
    {
        try {
            tempDir =  Files.createTempDirectory("js-plugins");
            tempDir.toFile().deleteOnExit();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private static void createNewManifest(Map<String, List<JsPluginInfo>> jsPluginObj, Path target) {

        try {
            String manifestPath = String.format("%s/manifest.json", target.toString());
            try (final OutputStream out = new BufferedOutputStream(Files.newOutputStream(Paths.get(manifestPath)))) {
                new ObjectMapper().writeValue(out, jsPluginObj);
                out.flush();
            }
        } catch (IOException e) {
            throw new IllegalStateException(String.format("Unable to copy manifest '%s' to '%s'", jsPluginObj, target), e);
        }

    }

    public static void writePlugins(List<JsPluginInfo> newJsPluginInfoList) {
        Map<String, List<JsPluginInfo>> newJsPluginObj = Map.of("plugins", newJsPluginInfoList);

        createNewManifest(newJsPluginObj, tempDir);


    }

    public static File addInitialPlugins(String resourceBase) {
        JsPluginFile jsPluginFile = getInstance();

        String sourceManifest = String.format("%s/manifest.json", resourceBase);

        ObjectMapper objectMapper = new ObjectMapper();

        File file = new File(sourceManifest);

        Map<String, List<JsPluginInfo>> jsPluginObj = null;

        try {
            jsPluginObj = objectMapper.readValue(file, new TypeReference<>(){});
        } catch (IOException e) {
            e.printStackTrace();
        }

        List<JsPluginInfo> jsPluginList = jsPluginObj.get("plugins");

        List<JsPluginInfo> newJsPluginList = new ArrayList<>();
        for (JsPluginInfo jsPlugin : jsPluginList) {
            String newName = String.format("%s/%s", PREFIX, jsPlugin.name());
            JsPluginInfo newJsPlugin = JsPluginInfo.of(newName, jsPlugin.version(), jsPlugin.main());
            newJsPluginList.add(newJsPlugin);
        }

        jsPluginInfoList.addAll(newJsPluginList);

        writePlugins(newJsPluginList);

        Path plugins = Paths.get(resourceBase);

        Path symlink = tempDir.resolve(PREFIX);

        try {
            Files.createSymbolicLink(symlink, plugins);
        } catch (IOException e) {
            log.info().append("Unable to create symlink to plugins directory");
        }

        return tempDir.toFile();
    }

    public static void addPlugins(List<JsPluginInfo> newJsPluginInfoList, List<String> newJsPluginPaths) {
        AtomicInteger i = new AtomicInteger();
        newJsPluginPaths.forEach(path -> {
            /*
                pull the name of the plugin, create sym link to that, then add that to location

             */

            if (path != null && !path.isEmpty() && !path.equals("None")) {
                Path plugin = Paths.get(path);

                Path newPluginDir = null;
                try {
                    newPluginDir = Files.createTempDirectory("plugin");
                    newPluginDir.toFile().deleteOnExit();
                } catch (IOException e) {
                    throw new RuntimeException(e);
                }

                Path toLinkTo = newPluginDir.resolve(PRE);

                try {
                    log.info().append("Creating symlink to plugins directory: ").append(plugin.toString()).endl();
                    Files.createSymbolicLink(toLinkTo, plugin);
                } catch (IOException e) {
                    log.info().append("Unable to create symlink to plugsdsdins directory");
                    throw new IllegalStateException(String.format("Unable to create symlink '%s' to '%s'",
                            plugin, newPluginDir), e);
                }

                String tmpPluginDirName = newPluginDir.getFileName().toString();

                log.info().append("Creating symlink to plugins directory2: ").append(tmpPluginDirName).endl();

                Path newJsPluginsDir = tempDir.resolve(tmpPluginDirName);

                try {
                    Files.createSymbolicLink(newJsPluginsDir, newPluginDir);
                } catch (IOException e) {
                    throw new IllegalStateException(String.format("Unable to create symlink '%s' to '%s'",
                            newPluginDir, newJsPluginsDir), e);
                }

                JsPluginInfo jsPlugin = newJsPluginInfoList.get(i.get());

                log.info().append("Adding plugin: ").append(jsPlugin.toString()).endl();

                String newName = String.format("%s/%s/%s", tmpPluginDirName, PRE, jsPlugin.name());
                JsPluginInfo newJsPlugin = JsPluginInfo.of(newName, jsPlugin.version(), jsPlugin.main());
                jsPluginInfoList.add(newJsPlugin);

                i.getAndIncrement();
            }
        });

        writePlugins(jsPluginInfoList);

    }

    public static Path getTempDir() {
        return tempDir;
    }

    // Static method
    // Static method to create instance of Singleton class
    public static synchronized JsPluginFile getInstance()
    {
        if (instance == null) {
            instance = new JsPluginFile();
            jsPluginInfoList = new ArrayList<>();
        }

        return instance;
    }

}