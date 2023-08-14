/**
 * Copyright (c) 2016-2022 Deephaven Data Labs and Patent Pending
 */
package io.deephaven.server.plugin;

import io.deephaven.base.log.LogOutput;
import io.deephaven.base.log.LogOutputAppendable;
import io.deephaven.internal.log.LoggerFactory;
import io.deephaven.io.logger.Logger;
import io.deephaven.plugin.Plugin;
import io.deephaven.plugin.Registration;
import io.deephaven.plugin.Registration.Callback;
import io.deephaven.plugin.type.JsPluginFile;
import io.deephaven.plugin.type.JsPluginInfo;
import io.deephaven.plugin.type.ObjectType;

import javax.inject.Inject;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Set;

/**
 * Provides a {@link #registerAll()} entrypoint to invoke all {@link Registration registrations} with a
 * {@link Callback}. Logs details.
 */
public final class PluginRegistration {
    private static final Logger log = LoggerFactory.getLogger(PluginRegistration.class);

    private final Set<Registration> registrations;
    private final Registration.Callback callback;

    @Inject
    public PluginRegistration(Set<Registration> registrations, Registration.Callback callback) {
        this.registrations = Objects.requireNonNull(registrations);
        this.callback = Objects.requireNonNull(callback);
    }

    public void registerAll() {
        log.info().append("Registering plugins...").endl();
        final Counting counting = new Counting();
        for (Registration registration : registrations) {
            log.info().append("Invoking registration: ").append(registration.toString()).endl();
            registration.registerInto(counting);
            counting.registerJs();
        }

        log.info().append("Registered plugins: ").append(counting).endl();
    }

    private class Counting implements Registration.Callback, LogOutputAppendable, Plugin.Visitor<Counting> {

        private int objectTypeCount = 0;

        private List<JsPluginInfo> jsPlugins = new ArrayList<>();

        private List<String> paths = new ArrayList<>();

        public void registerJs() {

            JsPluginFile.addPlugins(jsPlugins, paths);


        
        }

        @Override
        public void register(Plugin plugin) {
            plugin.walk(this);
        }

        @Override
        public Counting visit(ObjectType objectType) {
            String path = objectType.getJsPath();
            log.info().append("Path is currently: ").append(path).endl();
            JsPluginInfo jsPluginInfo = objectType.getJsPluginInfo();
            if (paths != null) {
                this.paths.add(path);
            }
            if (jsPluginInfo != null) {
                this.jsPlugins.add(jsPluginInfo);
            }

            log.info().append("Registering object type: ")
                    .append(objectType.name()).append(" / ")
                    .append(objectType.toString())
                    .endl();
            callback.register(objectType);
            ++objectTypeCount;
            return this;
        }

        @Override
        public LogOutput append(LogOutput logOutput) {
            return logOutput.append("objectType=").append(objectTypeCount);
        }
    }
}
