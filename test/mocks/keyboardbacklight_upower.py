# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: the part of org.freedesktop.UPower (system bus)
that KeyboardBacklight uses - the DaemonVersion property and the composite
/org/freedesktop/UPower/KbdBacklight object (never the per-LED objects).

  /org/freedesktop/UPower              org.freedesktop.UPower: DaemonVersion (s)
  /org/freedesktop/UPower/KbdBacklight org.freedesktop.UPower.KbdBacklight:
      GetMaxBrightness()->i, GetBrightness()->i, SetBrightness(i),
      BrightnessChanged(i), BrightnessChangedWithSource(i, s)

SetBrightness behaves like UPower: it stores the value and emits
BrightnessChanged(i) + BrightnessChangedWithSource(i, "external").

Parameters (-p JSON): version (str, default "1.90.2"), kbd (bool, default
true: export KbdBacklight at all), max (int, default 3), brightness (int,
default 2).

Extra org.freedesktop.DBus.Mock methods on the root object:
  PressFnKey(value, signal)  a hardware change; signal=true emits
                             BrightnessChangedWithSource(value, "internal"),
                             false models UPower 1.91.4 (no signal at all)
"""

import dbus

from dbusmock import MOCK_IFACE, mockobject

BUS_NAME = "org.freedesktop.UPower"
MAIN_OBJ = "/org/freedesktop/UPower"
MAIN_IFACE = "org.freedesktop.UPower"
SYSTEM_BUS = True

KBD_OBJ = MAIN_OBJ + "/KbdBacklight"
KBD_IFACE = "org.freedesktop.UPower.KbdBacklight"


def _set(obj, value):
    obj.kbd_value = int(value)
    obj.EmitSignal(KBD_IFACE, "BrightnessChanged", "i", [obj.kbd_value])
    obj.EmitSignal(KBD_IFACE, "BrightnessChangedWithSource", "is", [obj.kbd_value, "external"])


def load(mock, parameters):
    mock.AddProperties(MAIN_IFACE, {"DaemonVersion": dbus.String(parameters.get("version", "1.90.2"))})
    if not parameters.get("kbd", True):
        return
    mock.AddObject(KBD_OBJ, KBD_IFACE, {}, [])
    kbd = mockobject.objects[KBD_OBJ]
    kbd.kbd_max = int(parameters.get("max", 3))
    kbd.kbd_value = int(parameters.get("brightness", 2))
    kbd.AddMethod(KBD_IFACE, "GetMaxBrightness", "", "i", lambda obj: dbus.Int32(obj.kbd_max))
    kbd.AddMethod(KBD_IFACE, "GetBrightness", "", "i", lambda obj: dbus.Int32(obj.kbd_value))
    kbd.AddMethod(KBD_IFACE, "SetBrightness", "i", "", _set)


@dbus.service.method(MOCK_IFACE, in_signature="ib", out_signature="")
def PressFnKey(self, value, signal):
    kbd = mockobject.objects[KBD_OBJ]
    kbd.kbd_value = int(value)
    if signal:
        kbd.EmitSignal(KBD_IFACE, "BrightnessChangedWithSource", "is", [kbd.kbd_value, "internal"])
