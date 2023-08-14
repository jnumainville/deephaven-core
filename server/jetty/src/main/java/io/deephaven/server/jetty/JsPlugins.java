package io.deephaven.server.jetty;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.deephaven.configuration.ConfigDir;
import io.deephaven.configuration.Configuration;
import io.deephaven.internal.log.LoggerFactory;
import io.deephaven.io.logger.Logger;
import io.deephaven.plugin.type.JsPluginFile;
import io.deephaven.plugin.type.JsPluginInfo;
import org.eclipse.jetty.security.ConstraintSecurityHandler;
import org.eclipse.jetty.server.Handler;
import org.eclipse.jetty.servlet.DefaultServlet;
import org.eclipse.jetty.servlet.ErrorPageErrorHandler;
import org.eclipse.jetty.util.resource.PathResource;
import org.eclipse.jetty.util.resource.Resource;
import org.eclipse.jetty.webapp.WebAppContext;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.net.URI;
import java.nio.file.*;

import java.util.*;
import java.util.function.Consumer;

import static org.eclipse.jetty.servlet.ServletContextHandler.NO_SESSIONS;


class JsPlugins {

    private static final Logger log = LoggerFactory.getLogger(JsPlugins.class);

    private static final String PREFIX = "preloaded-plugins";

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

    private static Optional<Resource> createTempDir() {
        try {
            final String resourceBase =
                    Configuration.getInstance().getStringWithDefault("deephaven.jsPlugins.resourceBase", null);

            String sourceManifest = String.format("%s/manifest.json", resourceBase);

            ObjectMapper objectMapper = new ObjectMapper();

            File file = new File(sourceManifest);

            Map<String, List<JsPluginInfo>> jsPluginObj = objectMapper.readValue(file, new TypeReference<>(){});

            List<JsPluginInfo> jsPluginList = jsPluginObj.get("plugins");

            List<JsPluginInfo> newJsPluginList = new ArrayList<>();
            for (JsPluginInfo jsPlugin : jsPluginList) {
                String newName = String.format("%s/%s", PREFIX, jsPlugin.name());
                JsPluginInfo newJsPlugin = JsPluginInfo.of(newName, jsPlugin.version(), jsPlugin.main());
                newJsPluginList.add(newJsPlugin);
            }

            Map<String, List<JsPluginInfo>> newJsPluginObj = Map.of("plugins", newJsPluginList);
            //createNewManifest(Paths.get(sourceManifest), Paths.get(String.format("%s/manifest.json", resourceBase)));

            log.info(jsPluginList.toString());

            log.info().append("Creating JsPlugins top leveld '").append(resourceBase).append('\'').endl();

            JsPluginFile jsPluginFile = JsPluginFile.getInstance();

            Path tempDir = jsPluginFile.getTempDir();

            log.info().append("Creating JsPlugins temp dir '").append(tempDir.toString()).append('\'').endl();

            createNewManifest(newJsPluginObj, tempDir);

            Path plugins = Paths.get(resourceBase);

            Path symlink = tempDir.resolve(PREFIX);

            Files.createSymbolicLink(symlink, plugins);

            return Optional.of(Resource.newResource(tempDir.toFile()));
        } catch (IOException e) {
            throw new IllegalStateException(String.format("Unable to resolve resourceBase '%s'", "fwfw"), e);
        }

    }


    public static void add(Consumer<Handler> addHandler, ControlledCacheResource resource) {

        WebAppContext context =
                new WebAppContext(null, "/js-plugins/", null, null, null, new ErrorPageErrorHandler(), NO_SESSIONS);
        context.setBaseResource(resource);
        // Suppress warnings about security handlers
        context.setSecurityHandler(new ConstraintSecurityHandler());
        addHandler.accept(context);
    }

    public static void maybeAdd(Consumer<Handler> addHandler) {

        final String resourceBase =
                Configuration.getInstance().getStringWithDefault("deephaven.jsPlugins.resourceBase", null);

        Optional<Resource> plugins = Optional.of(Resource.newResource(JsPluginFile.addInitialPlugins(resourceBase)));

        Optional<Resource> test = plugins.map(ControlledCacheResource::wrap);

        //Optional<Resource> resource = resource().map(ControlledCacheResource::wrap);

        test
            .map(ControlledCacheResource::wrap)
            .ifPresent(res -> add(addHandler, res));

        /*test
            .map(ControlledCacheResource::wrap)
            .ifPresent(res -> addResource(addHandler, res));*/

    }

    private static void addResource(Consumer<Handler> addHandler, ControlledCacheResource resource) {
        log.info().append("Creating JsPlugins context with resource '").append(resource.toString()).append('\'').endl();
        WebAppContext context =
                new WebAppContext(null, "/js-plugins/loaded/", null, null, null, new ErrorPageErrorHandler(), NO_SESSIONS);
        context.setBaseResource(resource);
        context.setInitParameter(DefaultServlet.CONTEXT_INIT + "dirAllowed", "false");
        // Suppress warnings about security handlers
        context.setSecurityHandler(new ConstraintSecurityHandler());
        addHandler.accept(context);
    }

    private static Optional<Resource> resource() {
        // Note: this would probably be better to live in JettyConfig - but until we establish more formal expectations
        // for js plugin configuration and workflows, we'll keep this here.
        final String resourceBase =
                Configuration.getInstance().getStringWithDefault("deephaven.jsPlugins.resourceBase", null);
        log.info().append("JsPlugins resourceBase: '").append(resourceBase).append('\'').endl();
        
                if (resourceBase == null) {
            // defaults to "<configDir>/js-plugins/" if it exists
            return defaultJsPluginsDirectory()
                    .filter(Files::exists)
                    .map(PathResource::new);
        }
        try {
            return Optional.of(Resource.newResource(resourceBase));
        } catch (IOException e) {
            throw new IllegalStateException(String.format("Unable to resolve resourceBase '%s'", resourceBase), e);
        }
    }

    private static Optional<Path> defaultJsPluginsDirectory() {
        return ConfigDir.get().map(JsPlugins::jsPluginsDir);
    }

    private static Path jsPluginsDir(Path configDir) {
        return configDir.resolve("js-plugins");
    }
}


