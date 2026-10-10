# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: the parts of plasmashell's
org.freedesktop.Notifications that PresentationMode uses (session bus).

  GetServerInformation() -> (name, vendor, version, spec); name is "Plasma"
      unless the "name" parameter says otherwise (-p '{"name": "dunst"}')
  Inhibit(desktop_entry, reason, hints) -> cookie, UnInhibit(cookie)
  property Inhibited (b), with PropertiesChanged; cookies are dropped when
      their sender leaves the bus, as plasmashell does

Extra org.freedesktop.DBus.Mock methods:
  ActiveCookies() -> au
  Holders() -> as      unique bus names holding cookies
  ClearInhibitions()   the user switched Do Not Disturb off in the
                       notifications applet: every inhibition is dropped
"""

import dbus

from dbusmock import MOCK_IFACE

BUS_NAME = "org.freedesktop.Notifications"
MAIN_OBJ = "/org/freedesktop/Notifications"
MAIN_IFACE = "org.freedesktop.Notifications"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.server_name = str(parameters.get("name", "Plasma"))
    mock.next_cookie = 1
    mock.cookies = {}  # cookie -> sender
    mock.watches = {}
    mock.AddProperties(MAIN_IFACE, {"Inhibited": dbus.Boolean(False)})
    mock.AddMethods(
        MAIN_IFACE,
        [
            ("GetCapabilities", "", "as", 'ret = ["body", "inhibitions"]'),
            ("GetServerInformation", "", "ssss", 'ret = (self.server_name, "KDE", "6.7.0", "1.2")'),
        ],
    )


def _update(mock):
    inhibited = bool(mock.cookies)
    if bool(mock.Get(MAIN_IFACE, "Inhibited")) != inhibited:
        mock.UpdateProperties(MAIN_IFACE, {"Inhibited": dbus.Boolean(inhibited)})


def _sender_gone(mock, sender):
    for cookie in [c for c, s in mock.cookies.items() if s == sender]:
        del mock.cookies[cookie]
    watch = mock.watches.pop(sender, None)
    if watch is not None:
        watch.cancel()
    _update(mock)


@dbus.service.method(MAIN_IFACE, in_signature="ssa{sv}", out_signature="u", sender_keyword="sender")
def Inhibit(self, desktop_entry, reason, hints, sender=None):
    if not desktop_entry:
        raise dbus.exceptions.DBusException("empty desktop_entry", name="org.freedesktop.DBus.Error.InvalidArgs")
    cookie = self.next_cookie
    self.next_cookie += 1
    self.cookies[cookie] = sender
    if sender and sender not in self.watches:
        self.watches[sender] = self.connection.watch_name_owner(
            sender, lambda owner, s=sender: _sender_gone(self, s) if not owner else None
        )
    _update(self)
    return dbus.UInt32(cookie)


@dbus.service.method(MAIN_IFACE, in_signature="u", out_signature="")
def UnInhibit(self, cookie):
    self.cookies.pop(cookie, None)
    _update(self)


@dbus.service.method(MOCK_IFACE, in_signature="", out_signature="au")
def ActiveCookies(self):
    return dbus.Array(sorted(self.cookies), signature="u")


@dbus.service.method(MOCK_IFACE, in_signature="", out_signature="as")
def Holders(self):
    return dbus.Array(sorted({s for s in self.cookies.values() if s}), signature="s")


@dbus.service.method(MOCK_IFACE, in_signature="", out_signature="")
def ClearInhibitions(self):
    self.cookies.clear()
    _update(self)
