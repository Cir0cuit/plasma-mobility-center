# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: the part of org.freedesktop.UPower (system bus)
that PowerProfiles uses - the OnBattery property and its change signal.

Extra org.freedesktop.DBus.Mock method: SetOnBattery(bool).
"""

import dbus

from dbusmock import MOCK_IFACE

BUS_NAME = "org.freedesktop.UPower"
MAIN_OBJ = "/org/freedesktop/UPower"
MAIN_IFACE = "org.freedesktop.UPower"
SYSTEM_BUS = True


def load(mock, parameters):
    mock.AddProperties(MAIN_IFACE, {"OnBattery": dbus.Boolean(bool((parameters or {}).get("OnBattery", False)))})


@dbus.service.method(MOCK_IFACE, in_signature="b", out_signature="")
def SetOnBattery(self, on_battery):
    self.UpdateProperties(MAIN_IFACE, {"OnBattery": dbus.Boolean(on_battery)})
