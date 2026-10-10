# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: PowerDevil's org.kde.ScreenBrightness (session bus).

Only what ScreenBrightness uses. Driven from tests through these extra
org.freedesktop.DBus.Mock methods on the root object:

  AddDisplay(name, label, isInternal, max, value)
  RemoveDisplay(name)
  SetEchoSender(sender)   source_client reported for writes made through
                          SetBrightness[WithContext] (the mock cannot see the
                          real caller); empty by default
"""

import dbus

from dbusmock import MOCK_IFACE, mockobject

BUS_NAME = "org.kde.ScreenBrightness"
MAIN_OBJ = "/org/kde/ScreenBrightness"
MAIN_IFACE = "org.kde.ScreenBrightness"
DISPLAY_IFACE = "org.kde.ScreenBrightness.Display"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.echo_sender = ""
    mock.AddProperties(MAIN_IFACE, {"DisplaysDBusNames": dbus.Array([], signature="s")})


def _names(root):
    return list(root.Get(MAIN_IFACE, "DisplaysDBusNames"))


def _set_names(root, names):
    root.UpdateProperties(MAIN_IFACE, {"DisplaysDBusNames": dbus.Array(names, signature="s")})


def _write(display, value, context):
    root = mockobject.objects[MAIN_OBJ]
    display.Set(DISPLAY_IFACE, "Brightness", dbus.Int32(value))
    root.EmitSignal(MAIN_IFACE, "BrightnessChanged", "siss", [display.display_name, value, root.echo_sender, context])


@dbus.service.method(MOCK_IFACE, in_signature="ssbii", out_signature="")
def AddDisplay(self, name, label, internal, maximum, value):
    path = MAIN_OBJ + "/" + name
    self.AddObject(
        path,
        DISPLAY_IFACE,
        {
            "Label": dbus.String(label),
            "IsInternal": dbus.Boolean(internal),
            "MaxBrightness": dbus.Int32(maximum),
            "Brightness": dbus.Int32(value),
        },
        [],
    )
    display = mockobject.objects[path]
    display.display_name = name
    display.AddMethod(DISPLAY_IFACE, "SetBrightness", "iu", "", lambda obj, v, flags: _write(obj, v, ""))
    display.AddMethod(DISPLAY_IFACE, "SetBrightnessWithContext", "ius", "", lambda obj, v, flags, ctx: _write(obj, v, ctx))
    _set_names(self, _names(self) + [name])
    self.EmitSignal(MAIN_IFACE, "DisplayAdded", "s", [name])


@dbus.service.method(MOCK_IFACE, in_signature="s", out_signature="")
def RemoveDisplay(self, name):
    self.RemoveObject(MAIN_OBJ + "/" + name)
    _set_names(self, [n for n in _names(self) if n != name])
    self.EmitSignal(MAIN_IFACE, "DisplayRemoved", "s", [name])


@dbus.service.method(MOCK_IFACE, in_signature="s", out_signature="")
def SetEchoSender(self, sender):
    self.echo_sender = sender
