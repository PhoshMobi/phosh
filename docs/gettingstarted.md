Title: Getting started with Phosh development
Slug: gettingstarted

# Getting started with Phosh development

## Overview

Phosh is a graphical shell for
[Wayland](https://wayland.freedesktop.org/). It's target at mobile
devices with small screens like Purism's Librem 5 running adaptive
[GNOME](https://gitlab.gnome.org/) applications.

It's purpose is to provide a graphical user interface to launch
applications, display status information (time, battery status, ...),
to provide a lock screen and to make often used functionality quickly
accessible. It's also meant to provide a proper interface when either
connecting a keyboard and monitor to a phone or (but to a lesser
extend) when running on a laptop.

Since it acts as a Wayland client it needs a compositor to function
that provides the necessary protocols (most notably
wlr-layer-shell). It's usually used with
[phoc](https://gitlab.gnome.org/World/Phosh/phoc) (the PHOne Compositor).

On the GNOME side it interfaces with the usual components
(e.g. [gnome-settings-daemon](https://gitlab.gnome.org/GNOME/gnome-settings-daemon),
[upower](https://gitlab.freedesktop.org/upower/upower),
[iio-sensor-proxy](https://gitlab.freedesktop.org/hadess/iio-sensor-proxy/))
via DBus. For haptic feedback it uses
[feedbackd](https://source.puri.sm/Librem5/feedbackd/).

It uses [GTK](https://www.gtk.org/) as it's GUI toolkit and
[libhandy](https://gitlab.gnome.org/GNOME/libhandy) for adaptive
widgets.

Although targeted at touch devices Phosh does not implement a
on screen keyboard (OSK) but leaves this to separate on-screen
keyboards like [stevia](https://gitlab.gnome.org/World/Phosh/stevia)
or [squeekboard](https://gitlab.gnome.org/World/Phosh/squeekboard).

The above combination of software is also often (a bit imprecisely)
named Phosh. For a high level overview see [Phosh Overview](https://honk.sigxcpu.org/con/phosh_overview.html).

### Wayland protocols

Since Phoc (in contrast to some other solutions) aims to be a minimal
Wayland compositor that manages rendering, handle physical and virtual
input and display devices but not much more it needs to provide some
more protocols to enable graphical shells like Phosh. These are usually
from [wlr-protocols](https://github.com/swaywm/wlr-protocols) and
Phoc uses the [wlroots](https://github.com/swaywm/wlroots) library for
implementing them.

These are the most prominent ones use by Phosh:

- wlr-layer-shell: Usually Wayland clients have little influence on where
  the compositor places them. This protocol gives Phosh enough room
  to build the top bar via #PhoshTopPanel, the home bar #PhoshHome at
  the bottom, system modal dialogs e.g. #PhoshSystemPrompt and
  lock screens via #PhoshLockscreen.
- wlr-foreign-toplevel-management: This allows the management of
  toplevels (windows). Phosh uses this to build an application switcher
  called #PhoshOverview.
- wlr-output-management: This allows to manage a monitors power
  state (to e.g.turn it off when unused).

Besides those Phosh uses a number of "regular" Wayland client
protocols like `xdg_output`, `wl_output` or `wl_seat` (see
`PhoshWayland` for the full list).

### Session startup

Since Phosh is in many aspects a regular GTK application it's started
as part GNOME session so the start sequence looks like

```sh
phoc (compositor) -> gnome-session -> phosh (and other session components)
```

## Running and Testing Phosh

The following might useful when you want to make changes to Phosh and
test the changes:

### Running phosh

For development purposes you can run phosh nested on your desktop. See
[this blog post](https://phosh.mobi/posts/phosh-dev-part-0/) for
details. You can also run it in a container via toolbox:

Create and enter a container

```sh
distrobox create --image docker.io/library/fedora:latest
distrobox enter fedora-latest
sudo dnf install phosh gtk4-demo
# Runs phoc window to attach Phosh to in the next step:
WLR_BACKENDS=wayland GSETTINGS_BACKEND=memory \
                     phoc -C /usr/share/phosh/phoc.ini -E gtk4-demo
```

#### Option 1: Run nested Phosh session with local Phosh git checkout

```sh
sudo dnf install dnf-plugins-core git
sudo dnf builddep phosh
git clone https://gitlab.gnome.org/World/Phosh/phosh && cd phosh
meson setup _build
# re-build after every code change, then re-run the build to see your changes.
meson compile -C _build
WAYLAND_DISPLAY=wayland-1 GSETTINGS_BACKEND=memory ./_build/run
```

#### Option 2: Run nested Phosh session with packaged phosh

```sh
sudo dnf install phosh
WAYLAND_DISPLAY=wayland-1 GSETTINGS_BACKEND=memory /usr/libexec/phosh
```

You can use several helper scripts to test the running
instance:

#### Checking DBus Interfaces

The `tools/` directory contains short snippets to test various DBus interfaces
e.g. `check-osd` to test the OSD overlay (`PhoshOsdWindow`) or `check-screenshot`
to check the screenshot API (`PhoshScreenshotManager`).

### Mocking DBus Services

To mock DBus services used by phosh like `org.freedesktop.ModemManager`
or `net.hadess.SensorProxy` you can use [python-dbusmock][].

`tests/mock-mm-nm.py` uses this to mock `ModemManager` and
`NetworkManager` to simulate a mobile data connection:

```sh
sudo tests/mock-mm-nm.py
```

Make sure you stop `ModemManager` and `NetworkManager` before running
the above.

You can also use the mocks from `python-dbusmock` directly, e.g. to mock
settings daemon's rfkill state:

```sh
python3 -m dbusmock --template gsd_rfkill
```

to e.g. mock `gsd-rfkill`. To simulate airplane mode you could use:

```sh
gdbus call --session -d org.gnome.SettingsDaemon.Rfkill \
                     -o /org/gnome/SettingsDaemon/Rfkill \
                     -m org.freedesktop.DBus.Mock.SetAirplaneMode true
```

### Integration tests

#### Running with DBus mocks

We have some tests checking integration with system services like
ModemManager using python-dbusmock. You can run them using

```sh
_build/tests/integration/run-pytest -v -s tests/integration/test_dbus.py
```

If you want to inspect phosh's log output use

```sh
SAVE_TEST_LOGS=1 _build-test/tests/integration/run-pytest -s tests/integration/test_dbus.py
```

and `stderr` and `stdout` will be written into `log.stderr` and
`log.stdout` in the current directory.

#### Running the Screenshot Tests

One testcase brings up different parts of the shell and takes screenshots from them
to ease finding visual regressions. You can run them locally via

```sh
PHOSH_TEST_TYPE=doesnotmatter _build-test/tests/test-take-screenshots
```

### GTK Inspector

Since phosh is a GTK application you can use
[GtkInspector](https://developer.gnome.org/documentation/tools/inspector.html).
You can use the `GTK_INSPECTOR_DISPLAY` environment variable to use a different
Wayland display for the inspector window. This can be useful to have the
inspector windows outside of a nested Wayland session.

## Development Hints

### Manager Objects

Phosh uses several manager objects e.g. `PhoshBackgroundManager`,
`PhoshMonitorManager`, `PhoshLockscreenManager` to keep track
of certain objects (monitors, lock screens, backgrounds) and to
trigger events on those when needed. They're usually created and
disposed by #PhoshShell. Some of them like
`PhoshWayland` are singletons so you can access them from basically
anywhere in the codebase.

### Status Information Widgets

Several widgets listen to changes on DBus objects in order to e.g. display the
current connectivity - see #PhoshConnectivityInfo for an example that monitors
network connectivity.

Sometimes it is no longer useful to show the widget (since
e.g. the corresponding DBus service went away). In that case the
widget should flip a boolean property so the parent container can
hide the object via #g_object_bind_property().

### Screen Locking and Blanking

For details see PhoshScreenSaverManager.

[python-dbusmock]: https://github.com/martinpitt/python-dbusmock
