# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: the part of org.kde.Solid.PowerManagement that
ScreenBrightness uses (isActionSupported("DPMSControl"), supportedActionsChanged).

Extra org.freedesktop.DBus.Mock method: SetDpmsSupported(bool) - changes the
answer and emits supportedActionsChanged.
"""

import dbus

from dbusmock import MOCK_IFACE

BUS_NAME = "org.kde.Solid.PowerManagement"
MAIN_OBJ = "/org/kde/Solid/PowerManagement"
MAIN_IFACE = "org.kde.Solid.PowerManagement"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.dpms = bool(parameters.get("dpms", True))
    mock.AddMethod(MAIN_IFACE, "isActionSupported", "s", "b", lambda obj, action: action == "DPMSControl" and obj.dpms)


@dbus.service.method(MOCK_IFACE, in_signature="b", out_signature="")
def SetDpmsSupported(self, supported):
    self.dpms = bool(supported)
    self.EmitSignal(MAIN_IFACE, "supportedActionsChanged", "", [])
