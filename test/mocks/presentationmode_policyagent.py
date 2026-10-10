# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: PowerDevil's PolicyAgent
(org.kde.Solid.PowerManagement.PolicyAgent, session bus) - only what
PresentationMode reads to tell whether the user blocked its inhibition.

Parameter "api" (-p '{"api": "6.4"}'):
  "6.5" (default)  property RequestedInhibitions a(ssssu)
                   (what, who, why, mode, flags; flags 0x2 = Allowed).
                   Like PowerDevil, changes are announced by PropertiesChanged
                   with the property only in the *invalidated* list.
  "6.4"            ListPermanentlyBlockedInhibitions() / ListTemporarily...
                   -> a(ss) (who, why) and the *BlockedInhibitionsChanged
                   (a(ss) added, a(ss) removed) signals; no RequestedInhibitions.

Extra org.freedesktop.DBus.Mock methods:
  SetRequested(as pairs, au flags)   6.5: pairs = [who1, why1, who2, why2, ...],
                                     one flags value per pair
  SetBlocked(as permanent, as temporary)
                                     6.4: flat [who, why, ...] lists
(flat string lists, so a C++ test can send them without custom D-Bus types)
"""

import dbus

from dbusmock import MOCK_IFACE

BUS_NAME = "org.kde.Solid.PowerManagement.PolicyAgent"
MAIN_OBJ = "/org/kde/Solid/PowerManagement/PolicyAgent"
MAIN_IFACE = "org.kde.Solid.PowerManagement.PolicyAgent"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.api = str(parameters.get("api", "6.5"))
    mock.permanent = dbus.Array([], signature="(ss)")
    mock.temporary = dbus.Array([], signature="(ss)")
    if mock.api == "6.4":
        mock.AddMethods(
            MAIN_IFACE,
            [
                ("ListPermanentlyBlockedInhibitions", "", "a(ss)", "ret = self.permanent"),
                ("ListTemporarilyBlockedInhibitions", "", "a(ss)", "ret = self.temporary"),
            ],
        )
    else:
        mock.AddProperties(MAIN_IFACE, {"RequestedInhibitions": dbus.Array([], signature="(ssssu)")})
    mock.AddMethod(MAIN_IFACE, "HasInhibition", "u", "b", "ret = False")


def _pairs(flat):
    return [(flat[i], flat[i + 1]) for i in range(0, len(flat) - 1, 2)]


@dbus.service.method(MOCK_IFACE, in_signature="asau", out_signature="")
def SetRequested(self, pairs, flags):
    requested = [
        dbus.Struct(("sleep:idle", who, why, "block", dbus.UInt32(f)), signature="ssssu")
        for (who, why), f in zip(_pairs(pairs), flags)
    ]
    self.props[MAIN_IFACE]["RequestedInhibitions"] = dbus.Array(requested, signature="(ssssu)")
    self.EmitSignal(
        dbus.PROPERTIES_IFACE,
        "PropertiesChanged",
        "sa{sv}as",
        [MAIN_IFACE, dbus.Dictionary({}, signature="sv"), dbus.Array(["RequestedInhibitions"], signature="s")],
    )


@dbus.service.method(MOCK_IFACE, in_signature="asas", out_signature="")
def SetBlocked(self, permanent, temporary):
    old_permanent, old_temporary = self.permanent, self.temporary
    self.permanent = dbus.Array([dbus.Struct(p, signature="ss") for p in _pairs(permanent)], signature="(ss)")
    self.temporary = dbus.Array([dbus.Struct(p, signature="ss") for p in _pairs(temporary)], signature="(ss)")
    self.EmitSignal(MAIN_IFACE, "PermanentlyBlockedInhibitionsChanged", "a(ss)a(ss)", [self.permanent, old_permanent])
    self.EmitSignal(MAIN_IFACE, "TemporarilyBlockedInhibitionsChanged", "a(ss)a(ss)", [self.temporary, old_temporary])
