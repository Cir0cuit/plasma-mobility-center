# Test mocks

Backend tests never talk to the live session or system services. They run
on private buses and get every service they need from a mock.

## The safety net (test/CMakeLists.txt, for every test)

| Variable | Value | Protects |
|---|---|---|
| `DBUS_SESSION_BUS_ADDRESS` | private bus from `dbus-run-session` (tests registered with `DBUS`), else an unusable socket | the live session bus |
| `DBUS_SYSTEM_BUS_ADDRESS` | an unusable socket, until the test starts its own bus (`MockBus::SystemBus`) | the live system bus (UPower, ppd, NetworkManager, KAuth helpers, BlueZ) |
| `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_STATE_HOME` | `build/test/test-home/…` | `~/.config/kwinrc`, `plasma-nm`, `plasmaparc`, … |
| `PULSE_SERVER` | an unusable socket | the live PipeWire/PulseAudio volume |
| `KSCREEN_BACKEND`, `KSCREEN_BACKEND_INPROCESS` | `Fake`, `1` | the live display configuration |
| `MC_TEST_MOCKS_DIR` | this directory | — |

A test registered with `DBUS` is not registered at all when
`dbus-run-session` is missing - it never falls back to the live bus.

## `test/cpp/mockbus.h`

Shared helper (do not fork it; `tst_mockbus` checks it):

* `MockBus::SystemBus` - a private `dbus-daemon` of type system; `start()`
  sets `DBUS_SYSTEM_BUS_ADDRESS`. Start it in `initTestCase()` **before**
  the first `QDBusConnection::systemBus()` use in the process (Qt caches the
  connection; NetworkManagerQt, Solid and KAuth connect on first use).
* `MockBus::Mock` - one `python3 -m dbusmock` process on the private session
  or system bus; `start()` refuses to run unless that bus is private, and
  waits for the bus name. `stop()` + `start()` simulates a service restart.
* `MC_REQUIRE_PRIVATE_SESSION_BUS()`, `MC_REQUIRE_PRIVATE_SYSTEM_BUS()`,
  `MC_REQUIRE_DBUSMOCK()` (skips when python3-dbusmock is missing).

## python-dbusmock templates

`python3-dbusmock` (Fedora, Debian) / `python-dbusmock` (Arch):

```
{"--template", "upower"}                  system: org.freedesktop.UPower (+ KbdBacklight, DisplayDevice)
{"--template", "power_profiles_daemon"}   system: net.hadess.PowerProfiles (+ org.freedesktop.UPower.PowerProfiles on new versions)
{"--template", "networkmanager"}          system: org.freedesktop.NetworkManager
{"--template", "notification_daemon"}     session: org.freedesktop.Notifications
```

Drive a mock through its `org.freedesktop.DBus.Mock` interface
(`AddProperty`, `SetProperty`, `EmitSignal`, `AddMethod`, `AddObject`,
`GetCalls`, `ClearCalls`) to set up state and to assert what the backend
sent.

## Custom mocks: one set per backend

No template exists for these. Each backend writes its OWN files here, named
`<backend>_<what>.py` (dbusmock templates, loaded with
`{"--template", MockBus::mockFile("<backend>_<what>.py")}`), and mocks only
the parts of a service it uses. Several backends talk to PowerDevil; they do
**not** share a `powerdevil.py`, so each backend's tests stay independent.

| Backend (owner) | Its mock files | Mocks |
|---|---|---|
| ScreenBrightness | `screenbrightness_*.py` | `org.kde.ScreenBrightness` `/org/kde/ScreenBrightness` (+ `/org/kde/ScreenBrightness/displayN`); `org.kde.Solid.PowerManagement` `isActionSupported("DPMSControl")`, `supportedActionsChanged`; `org.kde.kglobalaccel` `/component/org_kde_powerdevil` `invokeShortcut` |
| KeyboardBacklight | `keyboardbacklight_*.py` | `org.kde.Solid.PowerManagement` incl. `Actions/KeyboardBrightnessControl`, `isActionSupported`; UPower `KbdBacklight` + `DaemonVersion` (or the `upower` template) |
| BatteryMonitor | `batterymonitor_*.py` | PowerDevil `smoothedBatteryRemainingTime(Changed)`; UPower devices via the `upower` template |
| PowerProfiles | `powerprofiles_*.py` | usually just the `power_profiles_daemon` template (+ `AddProperty` for holds / degradation / placeholder driver); PowerDevil `Actions/PowerProfile configuredProfile` |
| ChargeLimit | `chargelimit_*.py` | PowerDevil `chargeStopThresholdChanged`, `reparseConfiguration`. Never a KAuth helper - see below |
| NightLight | `nightlight_*.py` | `org.kde.KWin.NightLight` `/org/kde/KWin/NightLight` (properties, `inhibit`/`uninhibit`) |
| PresentationMode | `presentationmode_*.py` | `org.freedesktop.PowerManagement.Inhibit`, `org.freedesktop.ScreenSaver` (`Inhibit`/`UnInhibit`), `Inhibit`/`UnInhibit` + `Inhibited` on top of the `notification_daemon` template, PowerDevil `PolicyAgent` |
| Network | `network_*.py` | extras on top of the `networkmanager` template |

## Rules for every mock-based test

* never fall back to the real bus when the mock fails to start - fail or
  `QSKIP` instead;
* never call KAuth helpers (charge limit): test the argument builder
  (`tst_chargelimit_args`), point `ChargeLimitSysfs::read()` at a fake sysfs
  tree in a `QTemporaryDir`, and do not run `setStopThreshold()`/
  `checkAuthorization()` in tests (with the unusable default system bus they
  fail harmlessly, which a test may assert);
* never create an `AudioBackend` in a test: PulseAudioQt's context is a
  process singleton bound to the real sound server. Test
  `DeviceFilterModel` on a fake source model instead;
* KConfig writes (kwinrc, plasma-nm, plasmaparc) land in the scratch
  `XDG_CONFIG_HOME`; a test that checks them should still use its own
  `QTemporaryDir` and set `XDG_CONFIG_HOME` before the first KConfig use.
