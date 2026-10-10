# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: PowerDevil's power-profile action (session bus),
org.kde.Solid.PowerManagement /org/kde/Solid/PowerManagement/Actions/PowerProfile.
Only configuredProfile() and configuredProfileChanged(s).

Template parameter: configuredProfile (default "balanced").
Extra org.freedesktop.DBus.Mock method: SetConfiguredProfile(s) - changes the
answer and emits configuredProfileChanged.
"""

import dbus

from dbusmock import MOCK_IFACE

BUS_NAME = "org.kde.Solid.PowerManagement"
MAIN_OBJ = "/org/kde/Solid/PowerManagement/Actions/PowerProfile"
MAIN_IFACE = "org.kde.Solid.PowerManagement.Actions.PowerProfile"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.configured = str((parameters or {}).get("configuredProfile", "balanced"))
    mock.AddMethod(MAIN_IFACE, "configuredProfile", "", "s", lambda obj: obj.configured)


@dbus.service.method(MOCK_IFACE, in_signature="s", out_signature="")
def SetConfiguredProfile(self, profile):
    self.configured = str(profile)
    self.EmitSignal(MAIN_IFACE, "configuredProfileChanged", "s", [self.configured])
