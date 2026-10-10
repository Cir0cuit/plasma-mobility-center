# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: the part of org.kde.Solid.PowerManagement
(PowerDevil, session bus) that KeyboardBacklight uses.

  /org/kde/Solid/PowerManagement                                isActionSupported(s)->b,
                                                                supportedActionsChanged()
  /org/kde/Solid/PowerManagement/Actions/KeyboardBrightnessControl
      keyboardBrightness()->i, keyboardBrightnessMax()->i, keyboardBrightnessSteps()->i,
      setKeyboardBrightness(i), setKeyboardBrightnessSilent(i),
      keyboardBrightnessChanged(i), keyboardBrightnessMaxChanged(i)

Like the real PowerDevil, a write emits keyboardBrightnessChanged AND
keyboardBrightnessMaxChanged (the latter on every change).

Parameters (-p JSON): supported (bool, default true), max (int, default 3),
brightness (int, default 1).

Extra org.freedesktop.DBus.Mock methods on the root object:
  SetSupported(b)        change isActionSupported's answer, emit supportedActionsChanged
  SetHardware(max, value) change max/value WITHOUT any signal
  EmitBrightness(value)  an external change (Fn key, another client): store + signal
"""

import dbus

from dbusmock import MOCK_IFACE, mockobject

BUS_NAME = "org.kde.Solid.PowerManagement"
MAIN_OBJ = "/org/kde/Solid/PowerManagement"
MAIN_IFACE = "org.kde.Solid.PowerManagement"
SYSTEM_BUS = False

KBD_OBJ = MAIN_OBJ + "/Actions/KeyboardBrightnessControl"
KBD_IFACE = "org.kde.Solid.PowerManagement.Actions.KeyboardBrightnessControl"


def _kbd():
    return mockobject.objects[KBD_OBJ]


def _write(obj, value):
    obj.kbd_value = int(value)
    obj.EmitSignal(KBD_IFACE, "keyboardBrightnessChanged", "i", [obj.kbd_value])
    obj.EmitSignal(KBD_IFACE, "keyboardBrightnessMaxChanged", "i", [obj.kbd_max])


def load(mock, parameters):
    mock.kbd_supported = bool(parameters.get("supported", True))
    mock.AddMethod(
        MAIN_IFACE,
        "isActionSupported",
        "s",
        "b",
        lambda obj, action: action == "KeyboardBrightnessControl" and obj.kbd_supported,
    )

    mock.AddObject(KBD_OBJ, KBD_IFACE, {}, [])
    kbd = _kbd()
    kbd.kbd_max = int(parameters.get("max", 3))
    kbd.kbd_value = int(parameters.get("brightness", 1))
    kbd.AddMethod(KBD_IFACE, "keyboardBrightness", "", "i", lambda obj: dbus.Int32(obj.kbd_value))
    kbd.AddMethod(KBD_IFACE, "keyboardBrightnessMax", "", "i", lambda obj: dbus.Int32(obj.kbd_max))
    kbd.AddMethod(KBD_IFACE, "keyboardBrightnessSteps", "", "i", lambda obj: dbus.Int32(obj.kbd_max))
    kbd.AddMethod(KBD_IFACE, "setKeyboardBrightness", "i", "", _write)
    kbd.AddMethod(KBD_IFACE, "setKeyboardBrightnessSilent", "i", "", _write)


@dbus.service.method(MOCK_IFACE, in_signature="b", out_signature="")
def SetSupported(self, supported):
    self.kbd_supported = bool(supported)
    self.EmitSignal(MAIN_IFACE, "supportedActionsChanged", "", [])


@dbus.service.method(MOCK_IFACE, in_signature="ii", out_signature="")
def SetHardware(self, maximum, value):
    kbd = _kbd()
    kbd.kbd_max = int(maximum)
    kbd.kbd_value = int(value)


@dbus.service.method(MOCK_IFACE, in_signature="i", out_signature="")
def EmitBrightness(self, value):
    _write(_kbd(), value)
