# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: the part of org.kde.Solid.PowerManagement that
ChargeLimit uses - reparseConfiguration() (called after a successful write;
asserted through GetMethodCalls) and the chargeStopThresholdChanged(i) /
chargeStartThresholdChanged(i) signals.

Extra org.freedesktop.DBus.Mock method: EmitThresholdChanged(which, value),
which = "stop" | "start".

No KAuth helper is mocked, ever: ChargeLimit tests replace the KAuth job
factory instead (test/cpp/tst_chargelimit.cpp).
"""

import dbus

from dbusmock import MOCK_IFACE

BUS_NAME = "org.kde.Solid.PowerManagement"
MAIN_OBJ = "/org/kde/Solid/PowerManagement"
MAIN_IFACE = "org.kde.Solid.PowerManagement"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.AddMethod(MAIN_IFACE, "reparseConfiguration", "", "", "")
    mock.AddMethod(MAIN_IFACE, "chargeStopThreshold", "", "i", "ret = 0")
    mock.AddMethod(MAIN_IFACE, "chargeStartThreshold", "", "i", "ret = 0")


@dbus.service.method(MOCK_IFACE, in_signature="si", out_signature="")
def EmitThresholdChanged(self, which, value):
    name = "chargeStartThresholdChanged" if which == "start" else "chargeStopThresholdChanged"
    self.EmitSignal(MAIN_IFACE, name, "i", [dbus.Int32(value)])
