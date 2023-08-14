#
# Copyright (c) 2016-2022 Deephaven Data Labs and Patent Pending
#

import jpy
import deephaven.plugin

from typing import Union, Type
from deephaven.plugin import Plugin, Registration
from deephaven.plugin.object import ObjectType
from .object import ObjectTypeAdapter

_JCallbackAdapter = jpy.get_type('io.deephaven.server.plugin.python.CallbackAdapter')


def initialize_all_and_register_into(callback: _JCallbackAdapter):
    deephaven.plugin.register_all_into(RegistrationAdapter(callback))


class RegistrationAdapter(Registration.Callback):
    def __init__(self, callback: _JCallbackAdapter):
        self._callback = callback

    def register(self, plugin: Union[Plugin, Type[Plugin]]):
        if isinstance(plugin, type):
            # If registering a class, instantiate it before adapting it and passing to java
            plugin = plugin()
        if isinstance(plugin, ObjectType):

            js_plugin_info = {}
            try:
                js_plugin_info = {
                    "name": plugin.js_name(),
                    "main": plugin.js_main(),
                    "path": str(plugin.js_path()),
                    "version": plugin.js_version()
                }

                print(str(plugin.js_path()))
            except Exception as e:
                # Js plugin is optional, so if it fails to load, continue
                pass
            self._callback.registerObjectType(plugin.name, ObjectTypeAdapter(plugin), js_plugin_info)
        else:
            raise NotImplementedError

    def __str__(self):
        return str(self._callback)
