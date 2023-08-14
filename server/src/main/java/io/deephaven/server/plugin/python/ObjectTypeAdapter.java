/**
 * Copyright (c) 2016-2022 Deephaven Data Labs and Patent Pending
 */
package io.deephaven.server.plugin.python;

import io.deephaven.internal.log.LoggerFactory;
import io.deephaven.io.logger.Logger;
import io.deephaven.plugin.type.JsPluginInfo;
import io.deephaven.plugin.type.ObjectTypeBase;
import io.deephaven.server.plugin.PluginRegistration;
import org.jpy.PyObject;

import java.io.IOException;
import java.io.OutputStream;
import java.util.Objects;

final class ObjectTypeAdapter extends ObjectTypeBase implements AutoCloseable {

    private static final Logger log = LoggerFactory.getLogger(PluginRegistration.class);

    private final String name;
    private final PyObject objectTypeAdapter;

    private final JsPluginInfo jsPluginInfo;

    private final String jsPath;

    public ObjectTypeAdapter(String name, PyObject objectTypeAdapter, PyObject jsPluginInfo) {
        this.name = Objects.requireNonNull(name);
        this.objectTypeAdapter = Objects.requireNonNull(objectTypeAdapter);

        this.jsPath = String.valueOf(jsPluginInfo.call( "get", "path", null));

        if (this.jsPath != null) {
            String js_name = String.valueOf(jsPluginInfo.call( "get", "name", null));
            String version = String.valueOf(jsPluginInfo.call("get", "version", null));
            String main = String.valueOf(jsPluginInfo.call( "get", "main", null));
            this.jsPluginInfo = JsPluginInfo.of(js_name, version, main);
        } else {
            this.jsPluginInfo = null;
        }

        log.info().append("loggin time over").endl();

    }

    @Override
    public String name() {
        return name;
    }

    @Override
    public boolean isType(Object object) {
        if (!(object instanceof PyObject)) {
            return false;
        }
        return objectTypeAdapter.call(boolean.class, "is_type", PyObject.class, (PyObject) object);
    }

    @Override
    public void writeCompatibleObjectTo(Exporter exporter, Object object, OutputStream out) throws IOException {
        final byte[] bytes = objectTypeAdapter.call(byte[].class, "to_bytes",
                ExporterAdapter.class, new ExporterAdapter(exporter),
                PyObject.class, (PyObject) object);
        out.write(bytes);
    }

    @Override
    public String toString() {
        return objectTypeAdapter.toString();
    }

    @Override
    public void close() {
        objectTypeAdapter.close();
    }


    @Override
    public JsPluginInfo getJsPluginInfo() {
        return jsPluginInfo;
    }

    @Override
    public String getJsPath() {
        return jsPath;
    }
}
