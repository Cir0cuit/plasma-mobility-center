# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: KWin's org.kde.KWin.NightLight (session bus).

Only what NightLight uses. Behaves like KWin's NightLightDBusInterface:
inhibit() hands out increasing cookies per caller, uninhibit(cookie) only
releases the caller's own cookie, and a caller leaving the bus loses all of
its cookies. "inhibited" and "running" follow the inhibitor count.

Extra org.freedesktop.DBus.Mock methods on the main object:

  SetEnabled(enabled)   Night Light switched on/off in its settings
  GetInhibitors()       a(us): (cookie, unique name of the caller), by cookie
"""

import dbus

from dbusmock import MOCK_IFACE, mockobject

BUS_NAME = "org.kde.KWin.NightLight"
MAIN_OBJ = "/org/kde/KWin/NightLight"
MAIN_IFACE = "org.kde.KWin.NightLight"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.nl_inhibitors = {}  # cookie -> sender
    mock.nl_watches = {}  # sender -> name-owner watch
    mock.nl_last_cookie = 0
    enabled = bool(parameters.get("enabled", True))
    mock.AddProperties(
        MAIN_IFACE,
        {
            "available": dbus.Boolean(parameters.get("available", True)),
            "enabled": dbus.Boolean(enabled),
            "running": dbus.Boolean(enabled),
            "inhibited": dbus.Boolean(False),
            "mode": dbus.UInt32(parameters.get("mode", 0)),
            "currentTemperature": dbus.UInt32(parameters.get("currentTemperature", 4500)),
            "targetTemperature": dbus.UInt32(parameters.get("targetTemperature", 4500)),
            "daylight": dbus.Boolean(False),
            "previousTransitionDateTime": dbus.UInt64(0),
            "previousTransitionDuration": dbus.UInt32(0),
            "scheduledTransitionDateTime": dbus.UInt64(0),
            "scheduledTransitionDuration": dbus.UInt32(0),
        },
    )


def _root():
    return mockobject.objects[MAIN_OBJ]


def _update(root):
    inhibited = bool(root.nl_inhibitors)
    enabled = bool(root.Get(MAIN_IFACE, "enabled"))
    changed = {}
    if bool(root.Get(MAIN_IFACE, "inhibited")) != inhibited:
        changed["inhibited"] = dbus.Boolean(inhibited)
    running = enabled and not inhibited
    if bool(root.Get(MAIN_IFACE, "running")) != running:
        changed["running"] = dbus.Boolean(running)
    if changed:
        root.UpdateProperties(MAIN_IFACE, changed)


def _drop_sender(root, sender):
    for cookie in [c for c, s in root.nl_inhibitors.items() if s == sender]:
        del root.nl_inhibitors[cookie]
    watch = root.nl_watches.pop(sender, None)
    if watch is not None:
        watch.cancel()
    _update(root)


@dbus.service.method(MAIN_IFACE, in_signature="", out_signature="u", sender_keyword="sender")
def inhibit(self, sender=None):
    root = _root()
    root.nl_last_cookie += 1
    cookie = root.nl_last_cookie
    root.nl_inhibitors[cookie] = sender
    if sender not in root.nl_watches:

        def owner_changed(new_owner, sender=sender):
            if not new_owner:
                _drop_sender(root, sender)

        root.nl_watches[sender] = root.connection.watch_name_owner(sender, owner_changed)
    _update(root)
    return dbus.UInt32(cookie)


@dbus.service.method(MAIN_IFACE, in_signature="u", out_signature="", sender_keyword="sender")
def uninhibit(self, cookie, sender=None):
    root = _root()
    if root.nl_inhibitors.get(int(cookie)) == sender:
        del root.nl_inhibitors[int(cookie)]
    _update(root)


@dbus.service.method(MOCK_IFACE, in_signature="b", out_signature="")
def SetEnabled(self, enabled):
    self.UpdateProperties(MAIN_IFACE, {"enabled": dbus.Boolean(enabled)})
    _update(self)


@dbus.service.method(MOCK_IFACE, in_signature="", out_signature="a(us)")
def GetInhibitors(self):
    return dbus.Array(
        [dbus.Struct((dbus.UInt32(c), dbus.String(s)), signature="us") for c, s in sorted(self.nl_inhibitors.items())],
        signature="(us)",
    )
