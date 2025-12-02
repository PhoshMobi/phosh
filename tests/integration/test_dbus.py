#!/usr/bin/env python3

# Copyright (C) 2025 Phosh.mobi e.V.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Inspired by g-c-c's scenario tester which is
# Copyright (C) 2021 Red Hat Inc.
# Author: Bastien Nocera <hadess@hadess.net>
#
# Author: Guido Günther <agx@sigxcpu.org>

import gi
import os
import subprocess
import dbus
import dbusmock
import shutil
from collections import OrderedDict
from dbusmock import DBusTestCase
from dbusmock.templates.networkmanager import (
    InfrastructureMode,
    MANAGER_IFACE,
    NM80211ApSecurityFlags,
    SETTINGS_IFACE,
    SETTINGS_OBJ,
)

gi.require_version("UMockdev", "1.0")
gi.require_version("GUdev", "1.0")
from gi.repository import Gio, UMockdev  # noqa: 402
from . import Phosh, set_nonblock  # noqa: 402


class PhoshDBusTestCase(DBusTestCase):

    @classmethod
    def setup_udev_mock(klass):
        klass.umock = UMockdev.Testbed.new()

        # Torch
        klass.torch_syspath = klass.umock.add_device(
            "leds",
            "white:flash",
            None,
            ["brightness", "0", "max_brightness", "255"],
            ["GM_TORCH_MIN_BRIGHTNESS", "1"],
        )
        assert klass.torch_syspath == "/sys/devices/white:flash"

        # Backlight
        klass.backlight_syspath = klass.umock.add_device(
            "backlight",
            "intel_backlight",
            None,
            [
                "actual_brightness",
                "76255",
                "brightness",
                "76255",
                "max_brightness",
                "19200",
                "scale",
                "unknown",
                "type",
                "raw",
            ],
            [],
        )
        assert klass.backlight_syspath == "/sys/devices/intel_backlight"

        return klass.umock.get_root_dir()

    @classmethod
    def setUpClass(klass):
        klass.mocks = OrderedDict()
        topsrcdir = os.environ["TOPSRCDIR"]
        topbuilddir = os.environ["TOPBUILDDIR"]
        env = {}

        # Start system bus
        DBusTestCase.setUpClass()
        klass.test_bus = Gio.TestDBus.new(Gio.TestDBusFlags.NONE)
        klass.test_bus.up()
        os.environ["DBUS_SYSTEM_BUS_ADDRESS"] = klass.test_bus.get_bus_address()

        # Start session bus
        klass.session_test_bus = Gio.TestDBus.new(Gio.TestDBusFlags.NONE)
        klass.session_test_bus.up()
        os.environ["DBUS_SESSION_BUS_ADDRESS"] = (
            klass.session_test_bus.get_bus_address()
        )

        # Setup udev mocks
        env["UMOCKDEV_DIR"] = klass.setup_udev_mock()

        # TODO: start udev mock for e.g. backlight
        klass.start_from_template("bluez5")
        klass.start_from_template("gsd_rfkill")
        klass.start_from_template("modemmanager")
        klass.start_from_template("networkmanager")

        # Setup logging
        env["G_MESSAGES_DEBUG"] = " ".join(
            [
                "phosh-brightness-manager",
                "phosh-backlight",
                "phosh-backlight-sysfs",
                "phosh-bt-manager",
                "phosh-cell-broadcast-manager",
                "phosh-udev-manager",
                "phosh-torch-manager",
                "phosh-vpn-manager",
                "phosh-wifi-manager",
                "phosh-wwan-manager",
                "phosh-wwan-mm",
                "phosh-plugin-wifi-hotspot-quick-setting",
            ]
        )
        env["XDG_CURRENT_DESKTOP"] = "Phosh:GNOME"

        klass.phosh = Phosh(
            topsrcdir,
            topbuilddir,
            env,
            wrapper=["umockdev-wrapper"],
            gsettings_backend="keyfile",
        )

        # Install keyfile with gsettings
        assert os.environ.get("XDG_CONFIG_HOME") is None
        keyfile_dir = os.path.join(
            klass.phosh.homedir, ".config", "glib-2.0", "settings"
        )
        os.makedirs(keyfile_dir, exist_ok=True)

        srcdir = os.path.dirname(os.path.abspath(__file__))
        keyfile = os.path.join(srcdir, "keyfile")
        shutil.copy(keyfile, keyfile_dir)

        # Spawn phosh
        klass.phosh.spawn_nested()

    @classmethod
    def tearDownClass(klass):
        success = klass.phosh.teardown_nested()
        assert success is True

        for mock_server, mock_obj in reversed(klass.mocks.values()):
            mock_server.terminate()
            mock_server.wait()

        DBusTestCase.tearDownClass()

        criticals = klass.phosh.get_criticals()
        assert not criticals, f"Log contains criticals: {criticals}"

        warnings = klass.phosh.get_warnings()
        if warnings:
            print(f"Log contains warnings: {warnings}")

    @classmethod
    def start_from_template(klass, template, params={}):
        mock_server, mock_obj = klass.spawn_server_template(
            template, params, stdout=subprocess.PIPE
        )
        set_nonblock(mock_server.stdout)
        mocks = (mock_server, mock_obj)
        assert klass.mocks.setdefault(template, mocks) == mocks
        return mocks

    def __init__(self, methodName):
        super().__init__(methodName)

    def test_mm(self):
        mm = self.mocks["modemmanager"][1]
        mm.AddSimpleModem()

        assert self.phosh.wait_for_output(" Modem is present\n")
        assert self.phosh.check_for_stdout(" Enabling cell broadcast interface")

        # No data connection yet:
        assert self.phosh.check_for_stdout(" WWAN data connection present: 0")
        subprocess.check_output(["nmcli", "c", "add", "connection.type", "gsm"])
        # Data connection available:
        assert self.phosh.wait_for_output(" WWAN data connection present: 1")

        # Add cell broadcast message
        cbm_text = "Dies ist ein Test für Cellbroadcasts"
        cbm_channel = 4371
        mm.AddCbm(2, cbm_channel, cbm_text)
        assert self.phosh.wait_for_output(f" Received cbm {cbm_channel}: {cbm_text}")

    def test_vpn(self):
        self.mocks["networkmanager"][1]

        assert self.phosh.wait_for_output(
            " VPN present: 0, uuid: (null)\n", ignore_present=True
        )

        # Add a VPN connection
        connection = {
            "connection": {
                "timestamp": 1441979296,
                "type": "vpn",
                "id": "a",
                "uuid": "11111111-1111-1111-1111-111111111111",
            },
            "vpn": {
                "service-type": "org.freedesktop.NetworkManager.openvpn",
                "data": {"connection-type": "tls"},
            },
        }

        dbuscon = self.get_dbus(True)
        settings = dbus.Interface(
            dbuscon.get_object(MANAGER_IFACE, SETTINGS_OBJ), SETTINGS_IFACE
        )
        settings.AddConnection(connection)
        assert self.phosh.wait_for_output(
            " VPN present: 1, uuid: 11111111-1111-1111-1111-111111111111\n"
        )

        # Add a wireguard connection with newer timestamp
        connection = {
            "connection": {
                "timestamp": 1441979300,
                "type": "vpn",
                "id": "b",
                "uuid": "22222222-2222-2222-2222-222222222222",
            },
            "wireguard": {},
        }

        dbuscon = self.get_dbus(True)
        settings = dbus.Interface(
            dbuscon.get_object(MANAGER_IFACE, SETTINGS_OBJ), SETTINGS_IFACE
        )
        settings.AddConnection(connection)
        assert self.phosh.wait_for_output(
            " VPN present: 1, uuid: 22222222-2222-2222-2222-222222222222\n"
        )

    def test_wifi(self):
        nm = self.mocks["networkmanager"][1]

        assert self.phosh.check_for_stdout(" NM Wi-Fi enabled: 0, present: 0")

        # Add and enable Wi-Fi
        wifi = nm.AddWiFiDevice(
            "wifi0", "wlan0", dbusmock.templates.networkmanager.DeviceState.ACTIVATED
        )
        assert self.phosh.wait_for_output(" NM Wi-Fi enabled: 1, present: 1")
        assert self.phosh.check_for_stdout(" Wi-Fi device connected at 0")
        # From the hotspot quick setting
        assert self.phosh.check_for_stdout(" State: 0, Hotspot: 0 Wi-Fi: 0")

        nm.AddAccessPoint(
            wifi,
            "ap0",
            "SSID1",
            "00:de:ad:be:ef:00",
            InfrastructureMode.NM_802_11_MODE_INFRA,
            2425,
            5400,
            11,  # weak signal
            NM80211ApSecurityFlags.NM_802_11_AP_SEC_KEY_MGMT_PSK,
        )
        assert self.phosh.wait_for_output(" Creating network: SSID1\n")

        nm.AddAccessPoint(
            wifi,
            "ap1",
            "SSID1",
            "00:de:ad:be:ef:01",
            InfrastructureMode.NM_802_11_MODE_INFRA,
            2425,
            5400,
            82,  # stronger signal
            NM80211ApSecurityFlags.NM_802_11_AP_SEC_KEY_MGMT_PSK,
        )
        assert self.phosh.wait_for_output(
            " Adding access point to existing network: SSID1\n"
        )

        nm.AddAccessPoint(
            wifi,
            "ap2",
            "SSID2",
            "00:de:ad:be:ef:01",
            InfrastructureMode.NM_802_11_MODE_INFRA,
            2425,
            5400,
            82,
            NM80211ApSecurityFlags.NM_802_11_AP_SEC_KEY_MGMT_PSK,
        )
        assert self.phosh.wait_for_output(" Creating network: SSID2\n")

    def test_bt(self):
        adapter_name = "hci0"

        assert self.phosh.check_for_stdout(" BT enabled: 1")

        bt = self.mocks["bluez5"][1]
        bt.AddAdapter(adapter_name, "my-phone")
        assert self.phosh.wait_for_output(" State: BLUETOOTH_ADAPTER_STATE_ON")

    def test_torch(self):
        assert self.phosh.wait_for_output(
            " Found torch device 'white:flash' with min brightness 1 and max brightness 255",
            ignore_present=True,
        )

    def test_backlight(self):
        assert self.phosh.wait_for_output(
            " Backlight brightness maps to linear brightness curve",
            ignore_present=True,
        )

        assert self.phosh.wait_for_output(
            " Found HEADLESS-1 for brightness control",
            ignore_present=True,
        )

        subprocess.check_output(
            [
                "busctl",
                "call",
                "--user",
                "org.gnome.Shell",
                "/org/gnome/Shell/Brightness",
                "org.gnome.Shell.Brightness",
                "SetDimming",
                "b",
                "true",
            ]
        )
        # schema default of "org.gnome.settings-daemon.plugins.power" "idle-dim"
        # is enabled so the above DBus call should change brightness
        assert self.phosh.wait_for_output(" Setting target brightness to 764\n")
        assert self.phosh.wait_for_output(
            " Setting brightness via logind: 764\n",
            ignore_present=True,
        )
