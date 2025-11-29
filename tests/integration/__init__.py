#!/usr/bin/python3
#
# (C) 2025 The Phosh Developers
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Author: Guido Günther <agx@sigxcpu.org>


import dbus
import time
import fcntl
import os
import subprocess
import tempfile
import sys
from dbus.mainloop.glib import DBusGMainLoop

DBusGMainLoop(set_as_default=True)


def set_nonblock(fd):
    fl = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)


class Phosh:
    """
    Wrap phosh startup and teardown
    """

    wl_display = None
    process = None

    def __init__(
        self, topsrcdir, topbuilddir, env={}, wrapper=[], gsettings_backend="memory"
    ):
        self.topsrcdir = topsrcdir
        self.topbuilddir = topbuilddir
        self.tmpdir = tempfile.TemporaryDirectory(dir=topbuilddir)
        self.rundir = os.path.join(self.tmpdir.name, "run", "user")
        self.homedir = os.path.join(self.tmpdir.name, "home")
        self.stdout = ""
        self.stderr = ""
        self.env = env
        self.wrapper = wrapper
        self.gsettings_backend = gsettings_backend

        # Set Wayland socket
        os.makedirs(self.rundir, exist_ok=True)
        self.wl_display = os.path.join(self.rundir, "wayland-socket")
        os.environ["XDG_RUNTIME_DIR"] = self.rundir

        os.makedirs(self.homedir, exist_ok=True)
        os.environ["HOME"] = self.homedir

    def teardown_nested(self):
        self.process.send_signal(15)
        out = self.process.communicate()
        # TODO: Older phoc sends -15, drop this later
        if self.process.returncode not in [0, -15]:
            print(f"Exited with {self.process.returncode}", file=sys.stderr)
            print(f"stdout: {out[0].decode('utf-8')}", file=sys.stderr)
            print(f"stderr: {out[1].decode('utf-8')}", file=sys.stderr)
            return False
        if os.getenv("SAVE_TEST_LOGS"):
            with open("log.stdout", "w") as f:
                f.write(self.stdout)
            with open("log.stderr", "w") as f:
                f.write(self.stderr)
        return True

    def find_wlr_backend(self):
        if os.getenv("WLR_BACKENDS"):
            return os.getenv("WLR_BACKENDS")
        elif os.getenv("WAYLAND_DISPLAY"):
            return "wayland"
        elif os.getenv("DISPLAY"):
            return "x11"
        else:
            return "headless"

    def spawn_nested(self):
        phoc_ini = os.path.join(self.topsrcdir, "data", "phoc.ini")
        runscript = os.path.join(self.topbuilddir, "run")

        env = os.environ.copy()
        env["GSETTINGS_BACKEND"] = self.gsettings_backend
        backend = self.find_wlr_backend()
        env["WLR_BACKENDS"] = backend

        # Add test's special requirements
        env = env | self.env

        # Spawn phoc -E .../run
        cmd = self.wrapper + [
            "phoc",
            "--no-xwayland",
            "-C",
            phoc_ini,
            "--socket",
            self.wl_display,
            "-E",
            runscript,
        ]
        print(f"Launching '{' '.join(cmd)}' with '{backend}' backend")
        self.process = subprocess.Popen(
            cmd, env=env, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        )

        for fd in [self.process.stdout.fileno(), self.process.stderr.fileno()]:
            set_nonblock(fd)

        assert self.wait_for_output(
            stderr_msg="Phosh ready after"
        ), f"""Phosh did not start: exit status: {self.process.returncode}
            stderr: {self.stderr}
            stdout: {self.stdout}"""

        bus = dbus.SessionBus()
        proxy = bus.get_object(
            "mobi.phosh.Shell.DebugControl", "/mobi/phosh/Shell/DebugControl"
        )
        iface = dbus.Interface(proxy, "org.freedesktop.DBus.Properties")
        props = iface.GetAll("mobi.phosh.Shell.DebugControl", timeout=5)
        if "LogDomains" not in props:
            return None

        print("Phosh ready")
        return self

    def wait_for_output(
        self, stdout_msg=None, stderr_msg=None, timeout=5, ignore_present=False
    ):
        found_stdout = not stdout_msg
        found_stderr = not stderr_msg

        # Make sure output is not already present
        if stdout_msg and not ignore_present:
            assert stdout_msg not in self.stdout

        if stderr_msg and not ignore_present:
            assert stderr_msg not in self.stderr

        while timeout >= 0:

            out = self.process.stdout.read()
            if out:
                self.stdout += out.decode("utf-8")

            out = self.process.stderr.read()
            if out:
                self.stderr += out.decode("utf-8")

            # Phosh still running?
            if self.process.poll() is not None:
                return False

            if stdout_msg and stdout_msg in self.stdout:
                found_stdout = True

            if stderr_msg and stderr_msg in self.stderr:
                found_stderr = True

            if found_stderr and found_stdout:
                return True

            time.sleep(1)
            timeout -= 1

        return False

    def check_for_stdout(self, stdout_msg):
        return stdout_msg in self.stdout

    def get_criticals(self):
        return [line for line in self.stderr.split("\n") if "-CRITICAL **" in line]

    def get_warnings(self):
        return [line for line in self.stderr.split("\n") if "-WARNING **" in line]
