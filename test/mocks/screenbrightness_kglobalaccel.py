# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: org.kde.kglobalaccel with PowerDevil's component,
so tests can assert ScreenBrightness.turnScreenOff() invoked "Turn Off Screen"
(GetMethodCalls("invokeShortcut") on /component/org_kde_powerdevil). Nothing
is turned off - this is a mock.
"""

from dbusmock import mockobject

BUS_NAME = "org.kde.kglobalaccel"
MAIN_OBJ = "/kglobalaccel"
MAIN_IFACE = "org.kde.KGlobalAccel"
SYSTEM_BUS = False

COMPONENT = "/component/org_kde_powerdevil"
COMPONENT_IFACE = "org.kde.kglobalaccel.Component"


def load(mock, parameters):
    mock.AddObject(COMPONENT, COMPONENT_IFACE, {}, [])
    mockobject.objects[COMPONENT].AddMethod(COMPONENT_IFACE, "invokeShortcut", "s", "", "")
