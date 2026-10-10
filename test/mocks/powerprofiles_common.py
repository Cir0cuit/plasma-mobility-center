# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared body of powerprofiles_daemon.py (org.freedesktop.UPower.PowerProfiles)
and powerprofiles_legacy.py (net.hadess.PowerProfiles). Not a template itself.

Template parameters (--parameters JSON):
  ActiveProfile         default "balanced"
  PerformanceDegraded   default ""
  Profiles              [[profile, driver], ...], default the three real ones
  BatteryAware          bool; the property only exists when given (ppd >= 0.30)

Behaves like power-profiles-daemon where PowerProfiles relies on it:
Properties.Set(ActiveProfile) rejects unknown profiles, releases all holds and
emits PropertiesChanged; HoldProfile/ReleaseProfile update ActiveProfileHolds
with a change signal. Extra org.freedesktop.DBus.Mock methods:

  SetProfiles(as, emit)     replace Profiles with "profile:driver" entries;
                            emit=false changes them WITHOUT PropertiesChanged
                            (tuned-ppd swapping Profiles on an AC change)
  DenyWrites(message)       later Sets fail with AccessDenied (polkit "no");
                            "" allows them again
  GetWrites() -> a(sv)      every Properties.Set received, oldest first
"""

import dbus

from dbusmock import MOCK_IFACE

DEFAULT_PROFILES = [["power-saver", "dbusmock"], ["balanced", "dbusmock"], ["performance", "dbusmock"]]


def _profiles(pairs):
    return dbus.Array(
        [dbus.Dictionary({"Profile": p, "Driver": d, "CpuDriver": d}, signature="sv") for p, d in pairs],
        signature="a{sv}",
    )


def _iface(mock):
    return mock.mc_iface


def _emit(mock, changed):
    mock.EmitSignal(
        dbus.PROPERTIES_IFACE,
        "PropertiesChanged",
        "sa{sv}as",
        [_iface(mock), dbus.Dictionary(changed, signature="sv"), dbus.Array([], signature="s")],
    )


def _holds_value(mock):
    return dbus.Array(
        [dbus.Dictionary(h, signature="sv") for h in mock.mc_holds.values()],
        signature="a{sv}",
    )


def _publish_holds(mock):
    value = _holds_value(mock)
    mock.props[_iface(mock)]["ActiveProfileHolds"] = value
    _emit(mock, {"ActiveProfileHolds": value})


def setup(mock, iface, parameters):
    parameters = parameters or {}
    mock.mc_iface = iface
    mock.mc_cookie = 0
    mock.mc_holds = {}
    mock.mc_deny = ""
    mock.mc_writes = []

    props = {
        "ActiveProfile": dbus.String(parameters.get("ActiveProfile", "balanced")),
        "PerformanceDegraded": dbus.String(parameters.get("PerformanceDegraded", "")),
        "PerformanceInhibited": dbus.String(""),
        "Profiles": _profiles(parameters.get("Profiles", DEFAULT_PROFILES)),
        "Actions": dbus.Array([], signature="s"),
        "ActiveProfileHolds": dbus.Array([], signature="a{sv}"),
        "Version": dbus.String("0.30"),
    }
    if "BatteryAware" in parameters:
        props["BatteryAware"] = dbus.Boolean(parameters["BatteryAware"])
    mock.AddProperties(iface, dbus.Dictionary(props, signature="sv"))

    def hold(obj, profile, reason, application_id):
        obj.mc_cookie += 1
        obj.mc_holds[obj.mc_cookie] = {"Profile": profile, "Reason": reason, "ApplicationId": application_id}
        obj.props[iface]["ActiveProfile"] = dbus.String(profile)
        _publish_holds(obj)
        _emit(obj, {"ActiveProfile": dbus.String(profile)})
        return dbus.UInt32(obj.mc_cookie)

    def release(obj, cookie):
        obj.mc_holds.pop(int(cookie), None)
        _publish_holds(obj)

    mock.AddMethod(iface, "HoldProfile", "sss", "u", hold)
    mock.AddMethod(iface, "ReleaseProfile", "u", "", release)


# Replaces the generic dbusmock Set for this (single-object) mock process.
@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="ssv", out_signature="")
def Set(self, interface_name, property_name, value, *_, **__):
    self.mc_writes.append((property_name, value))
    if self.mc_deny:
        raise dbus.exceptions.DBusException(self.mc_deny, name="org.freedesktop.DBus.Error.AccessDenied")
    props = self.props.get(interface_name)
    if props is None or property_name not in props:
        raise dbus.exceptions.DBusException("no such property " + property_name, name="org.freedesktop.DBus.Error.InvalidArgs")
    if property_name == "ActiveProfile":
        known = [str(p["Profile"]) for p in props["Profiles"]]
        if str(value) not in known:
            raise dbus.exceptions.DBusException("Invalid profile name '%s'" % value, name="org.freedesktop.DBus.Error.InvalidArgs")
        value = dbus.String(value)
        if self.mc_holds:
            self.mc_holds.clear()
            _publish_holds(self)
    props[property_name] = value
    _emit(self, {property_name: value})


@dbus.service.method(MOCK_IFACE, in_signature="asb", out_signature="")
def SetProfiles(self, entries, emit):
    value = _profiles([str(e).split(":", 1) for e in entries])
    self.props[self.mc_iface]["Profiles"] = value
    if emit:
        _emit(self, {"Profiles": value})


@dbus.service.method(MOCK_IFACE, in_signature="s", out_signature="")
def DenyWrites(self, message):
    self.mc_deny = str(message)


@dbus.service.method(MOCK_IFACE, in_signature="", out_signature="a(sv)")
def GetWrites(self):
    return dbus.Array([dbus.Struct((n, v), signature="sv") for n, v in self.mc_writes], signature="(sv)")
