# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: KWin's org.kde.KWin.NightLight (session bus), so
the NightLight object that PresentationMode drives in tests talks to a mock
instead of finding nothing. Night Light enabled and running, not inhibited;
inhibit() -> cookie / uninhibit(cookie) toggle "inhibited".

Extra org.freedesktop.DBus.Mock method: ActiveCookies() -> au.
"""

import dbus

from dbusmock import MOCK_IFACE

BUS_NAME = "org.kde.KWin.NightLight"
MAIN_OBJ = "/org/kde/KWin/NightLight"
MAIN_IFACE = "org.kde.KWin.NightLight"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.next_cookie = 1
    mock.cookies = set()
    mock.AddProperties(
        MAIN_IFACE,
        {
            "available": dbus.Boolean(True),
            "enabled": dbus.Boolean(True),
            "running": dbus.Boolean(True),
            "inhibited": dbus.Boolean(False),
            "mode": dbus.UInt32(0),
            "daylight": dbus.Boolean(False),
            "currentTemperature": dbus.UInt32(4500),
            "targetTemperature": dbus.UInt32(4500),
            "previousTransitionDateTime": dbus.UInt64(0),
            "previousTransitionDuration": dbus.UInt32(0),
            "scheduledTransitionDateTime": dbus.UInt64(0),
            "scheduledTransitionDuration": dbus.UInt32(0),
        },
    )


def _update(mock):
    inhibited = bool(mock.cookies)
    if bool(mock.Get(MAIN_IFACE, "inhibited")) != inhibited:
        mock.UpdateProperties(MAIN_IFACE, {"inhibited": dbus.Boolean(inhibited)})


@dbus.service.method(MAIN_IFACE, in_signature="", out_signature="u")
def inhibit(self):
    cookie = self.next_cookie
    self.next_cookie += 1
    self.cookies.add(cookie)
    _update(self)
    return dbus.UInt32(cookie)


@dbus.service.method(MAIN_IFACE, in_signature="u", out_signature="")
def uninhibit(self, cookie):
    self.cookies.discard(cookie)
    _update(self)


@dbus.service.method(MOCK_IFACE, in_signature="", out_signature="au")
def ActiveCookies(self):
    return dbus.Array(sorted(self.cookies), signature="u")
