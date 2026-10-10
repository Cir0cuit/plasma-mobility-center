# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: the part of org.kde.Solid.PowerManagement that
BatteryMonitor uses (smoothedBatteryRemainingTime() -> t in ms,
smoothedBatteryRemainingTimeChanged(t)).

Parameters: {"remaining": <ms>} (default 0).

Extra org.freedesktop.DBus.Mock method: SetSmoothedRemainingTime(t) - changes
the answer and emits smoothedBatteryRemainingTimeChanged.
"""

import dbus

from dbusmock import MOCK_IFACE

BUS_NAME = "org.kde.Solid.PowerManagement"
MAIN_OBJ = "/org/kde/Solid/PowerManagement"
MAIN_IFACE = "org.kde.Solid.PowerManagement"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.remaining = int(parameters.get("remaining", 0))
    mock.AddMethod(MAIN_IFACE, "smoothedBatteryRemainingTime", "", "t", lambda obj: dbus.UInt64(obj.remaining))
    mock.AddMethod(MAIN_IFACE, "batteryRemainingTime", "", "t", lambda obj: dbus.UInt64(obj.remaining))


@dbus.service.method(MOCK_IFACE, in_signature="t", out_signature="")
def SetSmoothedRemainingTime(self, msec):
    self.remaining = int(msec)
    self.EmitSignal(MAIN_IFACE, "smoothedBatteryRemainingTimeChanged", "t", [dbus.UInt64(msec)])
