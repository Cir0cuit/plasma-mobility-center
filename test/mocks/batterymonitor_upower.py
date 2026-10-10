# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: org.freedesktop.UPower for BatteryMonitor.

The stock `upower` template (loaded underneath, so OnBattery, DisplayDevice,
EnumerateDevices, SetupDisplayDevice, SetDeviceProperties and RemoveDevice
behave exactly as there), plus one extra org.freedesktop.DBus.Mock method:

    AddBattery(name, properties a{sv}) -> object path
        Adds /org/freedesktop/UPower/devices/<name> with EVERY property Solid
        reads (the stock AddDischargingBattery lacks EnergyFullDesign,
        EnergyRate, Capacity, ChargeCycles, Technology, Vendor, ...), then
        emits DeviceAdded - so Solid's one GetAll sees the full set.
        `properties` overrides the defaults below.

Change OnBattery with a plain org.freedesktop.DBus.Properties.Set on
/org/freedesktop/UPower (dbusmock emits PropertiesChanged for it).
"""

import dbus

from dbusmock import MOCK_IFACE
from dbusmock.templates import upower as _upower

BUS_NAME = _upower.BUS_NAME
MAIN_OBJ = _upower.MAIN_OBJ
MAIN_IFACE = _upower.MAIN_IFACE
SYSTEM_BUS = True
DEVICE_IFACE = _upower.DEVICE_IFACE

# Re-exported, so AddTemplate attaches them to the mock as well.
SetupDisplayDevice = _upower.SetupDisplayDevice
SetDeviceProperties = _upower.SetDeviceProperties
RemoveDevice = _upower.RemoveDevice
AddAC = _upower.AddAC


def load(mock, parameters):
    _upower.load(mock, parameters)


def _defaults(name):
    return {
        "NativePath": dbus.String(name),
        "Vendor": dbus.String("ACME"),
        "Model": dbus.String("Battery " + name),
        "Serial": dbus.String("0001"),
        "PowerSupply": dbus.Boolean(True),
        "HasHistory": dbus.Boolean(False),
        "HasStatistics": dbus.Boolean(False),
        "Online": dbus.Boolean(False),
        "IsPresent": dbus.Boolean(True),
        "IsRechargeable": dbus.Boolean(True),
        # UP_DEVICE_KIND_BATTERY
        "Type": dbus.UInt32(2),
        # UP_DEVICE_STATE_DISCHARGING
        "State": dbus.UInt32(2),
        "Percentage": dbus.Double(50.0),
        "Energy": dbus.Double(30.0),
        "EnergyEmpty": dbus.Double(0.0),
        "EnergyFull": dbus.Double(60.0),
        "EnergyFullDesign": dbus.Double(75.0),
        "EnergyRate": dbus.Double(10.0),
        "Voltage": dbus.Double(12.0),
        "Temperature": dbus.Double(0.0),
        "Capacity": dbus.Double(80.0),
        "ChargeCycles": dbus.Int32(123),
        "TimeToEmpty": dbus.Int64(3 * 3600),
        "TimeToFull": dbus.Int64(0),
        # UP_DEVICE_TECHNOLOGY_LITHIUM_ION
        "Technology": dbus.UInt32(1),
        "WarningLevel": dbus.UInt32(1),
        "IconName": dbus.String("battery-good-symbolic"),
        "UpdateTime": dbus.UInt64(0),
    }


@dbus.service.method(MOCK_IFACE, in_signature="sa{sv}", out_signature="s")
def AddBattery(self, device_name, properties):
    path = "/org/freedesktop/UPower/devices/" + device_name
    props = _defaults(device_name)
    for key, value in properties.items():
        props[str(key)] = value
    self.AddObject(path, DEVICE_IFACE, dbus.Dictionary(props, signature="sv"), [("Refresh", "", "", "")])
    self.EmitSignal(MAIN_IFACE, "DeviceAdded", "o", [path])
    return path
