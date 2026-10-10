# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: PowerDevil's org.freedesktop.PowerManagement.Inhibit
(session bus), as used by PresentationMode.

Inhibit(app, reason) -> cookie and UnInhibit(cookie). Like PowerDevil, a
cookie is dropped when the client that took it leaves the bus.

Extra org.freedesktop.DBus.Mock methods: ActiveCookies() -> au, the cookies
currently held (released, or dropped with their sender, are gone);
Holders() -> as, the unique bus names holding them.
"""

import dbus

from dbusmock import MOCK_IFACE

BUS_NAME = "org.freedesktop.PowerManagement.Inhibit"
MAIN_OBJ = "/org/freedesktop/PowerManagement/Inhibit"
MAIN_IFACE = "org.freedesktop.PowerManagement.Inhibit"
SYSTEM_BUS = False


def load(mock, parameters):
    mock.next_cookie = 1
    mock.cookies = {}  # cookie -> sender
    mock.watches = {}  # sender -> name owner watch


def _sender_gone(mock, sender):
    for cookie in [c for c, s in mock.cookies.items() if s == sender]:
        del mock.cookies[cookie]
    watch = mock.watches.pop(sender, None)
    if watch is not None:
        watch.cancel()


@dbus.service.method(MAIN_IFACE, in_signature="ss", out_signature="u", sender_keyword="sender")
def Inhibit(self, app, reason, sender=None):
    cookie = self.next_cookie
    self.next_cookie += 1
    self.cookies[cookie] = sender
    if sender and sender not in self.watches:
        self.watches[sender] = self.connection.watch_name_owner(
            sender, lambda owner, s=sender: _sender_gone(self, s) if not owner else None
        )
    return dbus.UInt32(cookie)


@dbus.service.method(MAIN_IFACE, in_signature="u", out_signature="")
def UnInhibit(self, cookie):
    if cookie not in self.cookies:
        raise dbus.exceptions.DBusException("unknown cookie %d" % cookie, name="org.freedesktop.DBus.Error.InvalidArgs")
    del self.cookies[cookie]


@dbus.service.method(MOCK_IFACE, in_signature="", out_signature="au")
def ActiveCookies(self):
    return dbus.Array(sorted(self.cookies), signature="u")


@dbus.service.method(MOCK_IFACE, in_signature="", out_signature="as")
def Holders(self):
    return dbus.Array(sorted({s for s in self.cookies.values() if s}), signature="s")
