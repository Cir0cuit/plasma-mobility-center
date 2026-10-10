# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: power-profiles-daemon >= 0.20 on the system bus,
org.freedesktop.UPower.PowerProfiles. See powerprofiles_common.py for the
parameters and the extra org.freedesktop.DBus.Mock methods.
"""

import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ in the source tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from powerprofiles_common import DenyWrites, GetWrites, Set, SetProfiles, setup  # noqa: E402,F401

BUS_NAME = "org.freedesktop.UPower.PowerProfiles"
MAIN_OBJ = "/org/freedesktop/UPower/PowerProfiles"
MAIN_IFACE = "org.freedesktop.UPower.PowerProfiles"
SYSTEM_BUS = True


def load(mock, parameters):
    setup(mock, MAIN_IFACE, parameters)
