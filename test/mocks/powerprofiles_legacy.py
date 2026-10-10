# SPDX-License-Identifier: GPL-2.0-or-later
"""python-dbusmock template: power-profiles-daemon < 0.20 (or tuned-ppd) on the system bus,
net.hadess.PowerProfiles only. See powerprofiles_common.py for the
parameters and the extra org.freedesktop.DBus.Mock methods.
"""

import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ in the source tree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from powerprofiles_common import DenyWrites, GetWrites, Set, SetProfiles, setup  # noqa: E402,F401

BUS_NAME = "net.hadess.PowerProfiles"
MAIN_OBJ = "/net/hadess/PowerProfiles"
MAIN_IFACE = "net.hadess.PowerProfiles"
SYSTEM_BUS = True


def load(mock, parameters):
    setup(mock, MAIN_IFACE, parameters)
