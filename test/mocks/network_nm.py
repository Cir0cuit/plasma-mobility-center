# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: python-dbusmock's `networkmanager` template plus
the extras WifiBackend needs (system bus).

Everything of the stock template is re-exported (AddWiFiDevice,
AddAccessPoint, AddWiFiConnection, AddActiveConnection, RemoveAccessPoint,
SetDeviceDisconnected, ...). Mock methods live on the main object
/org/freedesktop. Additions:

  * NM Version defaults to 1.46.0 (NetworkManagerQt switches behaviour on it);
  * Device.Disconnect on Wi-Fi devices (removes the active connection);
  * Manager.GetPermissions returns what SetPermissions stored;
  * SetPermissions(a{ss})                 also emits CheckPermissions
  * AddAccessPointEx(dev, name, ssid(ay), mode, freq, strength, flags, wpa, rsn)
                                           separate WPA / RSN flags (SAE etc.)
  * AddSavedConnection(name, ssid(ay), uuid, timestamp)
                                           a stored Wi-Fi profile; no AP needed
                                           (out-of-range saved networks)
  * AddTypedDevice(name, iface, type)      e.g. 5 = Bluetooth, 8 = Modem
  * FailDevice(dev, reason)                device -> FAILED (120) with reason,
                                           drops its active connection
"""

import dbus

import dbusmock
from dbusmock import MOCK_IFACE
from dbusmock.templates import networkmanager as _nm
from dbusmock.templates.networkmanager import *  # noqa: F401,F403 pylint: disable=wildcard-import,unused-wildcard-import

BUS_NAME = _nm.BUS_NAME
MAIN_OBJ = _nm.MAIN_OBJ
SYSTEM_BUS = True
IS_OBJECT_MANAGER = True

MANAGER_OBJ = _nm.MANAGER_OBJ
MANAGER_IFACE = _nm.MANAGER_IFACE
SETTINGS_OBJ = _nm.SETTINGS_OBJ
SETTINGS_IFACE = _nm.SETTINGS_IFACE
DEVICE_IFACE = _nm.DEVICE_IFACE
WIRELESS_DEVICE_IFACE = _nm.WIRELESS_DEVICE_IFACE
ACCESS_POINT_IFACE = _nm.ACCESS_POINT_IFACE
CSETTINGS_IFACE = _nm.CSETTINGS_IFACE


def load(mock, parameters):
    params = dict(parameters)
    params.setdefault("Version", "1.46.0")
    _nm.load(mock, params)
    manager = dbusmock.get_object(MANAGER_OBJ)
    manager.permissions = {}
    manager.AddMethod(MANAGER_IFACE, "GetPermissions", "", "a{ss}", "ret = dbus.Dictionary(self.permissions, signature='ss')")


def _disconnect_device(dev):
    manager = dbusmock.get_object(MANAGER_OBJ)
    active = str(dev.Get(DEVICE_IFACE, "ActiveConnection"))
    if active != "/":
        manager.RemoveActiveConnection(dev.path, active)
    else:
        manager.SetDeviceDisconnected(dev.path)


@dbus.service.method(MOCK_IFACE, in_signature="ssi", out_signature="s")
def AddWiFiDevice(self, device_name, iface_name, state):
    path = _nm.AddWiFiDevice(self, device_name, iface_name, state)
    dev = dbusmock.get_object(path)
    dev.disconnect_device = _disconnect_device
    dev.AddMethod(DEVICE_IFACE, "Disconnect", "", "", "self.disconnect_device(self)")
    return path


@dbus.service.method(MOCK_IFACE, in_signature="ssi", out_signature="s")
def AddTypedDevice(self, device_name, iface_name, device_type):
    path = "/org/freedesktop/NetworkManager/Devices/" + device_name
    self.AddObject(
        path,
        DEVICE_IFACE,
        {
            "DeviceType": dbus.UInt32(device_type),
            "State": dbus.UInt32(_nm.DeviceState.DISCONNECTED),
            "StateReason": (dbus.UInt32(_nm.DeviceState.DISCONNECTED), dbus.UInt32(0)),
            "Interface": iface_name,
            "IpInterface": "",
            "ActiveConnection": dbus.ObjectPath("/"),
            "AvailableConnections": dbus.Array([], signature="o"),
            "AutoConnect": False,
            "Managed": True,
            "Driver": "dbusmock",
        },
        [],
    )
    self.object_manager_emit_added(path)
    manager = dbusmock.get_object(MANAGER_OBJ)
    devices = manager.Get(MANAGER_IFACE, "Devices")
    devices.append(path)
    manager.Set(MANAGER_IFACE, "Devices", devices)
    manager.EmitSignal(MANAGER_IFACE, "DeviceAdded", "o", [path])
    return path


@dbus.service.method(MOCK_IFACE, in_signature="a{ss}", out_signature="")
def SetPermissions(_self, permissions):
    manager = dbusmock.get_object(MANAGER_OBJ)
    manager.permissions = {str(k): str(v) for k, v in permissions.items()}
    manager.EmitSignal(MANAGER_IFACE, "CheckPermissions", "", [])


@dbus.service.method(MOCK_IFACE, in_signature="ssayuuyuuu", out_signature="s")
def AddAccessPointEx(self, dev_path, ap_name, ssid, mode, frequency, strength, flags, wpa, rsn):
    dev = dbusmock.get_object(dev_path)
    ap_path = "/org/freedesktop/NetworkManager/AccessPoint/" + ap_name
    self.AddObject(
        ap_path,
        ACCESS_POINT_IFACE,
        {
            "Ssid": dbus.ByteArray(bytes(ssid)),
            "HwAddress": dbus.String("00:11:22:33:44:%02X" % (len(dev.access_points) & 0xFF)),
            "Flags": dbus.UInt32(flags),
            "LastSeen": dbus.Int32(1),
            "Frequency": dbus.UInt32(frequency),
            "MaxBitrate": dbus.UInt32(540000),
            "Mode": dbus.UInt32(mode),
            "WpaFlags": dbus.UInt32(wpa),
            "RsnFlags": dbus.UInt32(rsn),
            "Strength": dbus.Byte(strength),
        },
        [],
    )
    self.object_manager_emit_added(ap_path)
    dev.access_points.append(ap_path)
    aps = dev.Get(WIRELESS_DEVICE_IFACE, "AccessPoints")
    aps.append(ap_path)
    dev.Set(WIRELESS_DEVICE_IFACE, "AccessPoints", aps)
    dev.EmitSignal(WIRELESS_DEVICE_IFACE, "AccessPointAdded", "o", [ap_path])
    return ap_path


@dbus.service.method(MOCK_IFACE, in_signature="sayst", out_signature="s")
def AddSavedConnection(self, name, ssid, conn_uuid, timestamp):
    """A stored Wi-Fi profile (WPA-PSK when the name ends in '_psk')."""
    path = SETTINGS_OBJ + "/" + name
    settings = {
        "connection": {
            "id": name,
            "uuid": conn_uuid,
            "type": "802-11-wireless",
            "timestamp": dbus.UInt64(timestamp),
            "autoconnect": False,
        },
        "802-11-wireless": {
            "ssid": dbus.ByteArray(bytes(ssid)),
            "mode": "infrastructure",
        },
        "ipv4": {"method": "auto"},
        "ipv6": {"method": "auto"},
    }
    if name.endswith("_psk"):
        settings["802-11-wireless"]["security"] = "802-11-wireless-security"
        settings["802-11-wireless-security"] = {"key-mgmt": "wpa-psk"}
    self.AddObject(
        path,
        CSETTINGS_IFACE,
        {"Unsaved": False},
        [
            ("Delete", "", "", "self.ConnectionDelete(self)"),
            ("GetSettings", "", "a{sa{sv}}", "ret = self.ConnectionGetSettings(self)"),
            ("GetSecrets", "s", "a{sa{sv}}", "ret = self.ConnectionGetSecrets(self, args[0])"),
            ("Update", "a{sa{sv}}", "", "self.ConnectionUpdate(self, args[0])"),
        ],
    )
    self.object_manager_emit_added(path)
    obj = dbusmock.get_object(path)
    obj.settings = settings
    obj.connection_path = path
    obj.ConnectionDelete = _nm.ConnectionDelete
    obj.ConnectionGetSettings = _nm.ConnectionGetSettings
    obj.ConnectionGetSecrets = _nm.ConnectionGetSecrets
    obj.ConnectionUpdate = _nm.ConnectionUpdate

    settings_obj = dbusmock.get_object(SETTINGS_OBJ)
    connections = settings_obj.ListConnections()
    connections.append(path)
    settings_obj.Set(SETTINGS_IFACE, "Connections", connections)
    settings_obj.EmitSignal(SETTINGS_IFACE, "NewConnection", "o", [path])
    return path


@dbus.service.method(MOCK_IFACE, in_signature="su", out_signature="")
def FailDevice(_self, dev_path, reason):
    dev = dbusmock.get_object(dev_path)
    manager = dbusmock.get_object(MANAGER_OBJ)
    old_state = dev.Get(DEVICE_IFACE, "State")
    active = str(dev.Get(DEVICE_IFACE, "ActiveConnection"))
    if active != "/":
        manager.RemoveActiveConnection(dev_path, active)
    dev.Set(DEVICE_IFACE, "State", dbus.UInt32(_nm.DeviceState.FAILED))
    dev.Set(DEVICE_IFACE, "StateReason", (dbus.UInt32(_nm.DeviceState.FAILED), dbus.UInt32(reason)))
    dev.EmitSignal(DEVICE_IFACE, "StateChanged", "uuu", [dbus.UInt32(_nm.DeviceState.FAILED), old_state, dbus.UInt32(reason)])
