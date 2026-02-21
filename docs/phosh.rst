.. _phosh(1):

=====
phosh
=====

---------------------
Phosh - A phone shell
---------------------

SYNOPSIS
--------
|   **phosh** [OPTIONS...]


DESCRIPTION
-----------

``phosh`` is a Wayland shell for mobile devices using GNOME technologies.

OPTIONS
-------

``-h``, ``--help``
   Print help and exit

``-U``, ``--unlocked``
   Don't start with screen locked

``-L``, ``--locked``
   Start with screen locked, no matter what

``--version``
   Show version information

CONFIGURATION
-------------

``phosh`` is configured via ``GSettings``, see ``phosh-config(5)``.

Plugins
^^^^^^^

Plugins are configured via the ``mobi.phosh.plugins`` and ``sm.puri.phosh.plugins`` gsettings
schema. The ``lock-screen`` key enables the plugins on the lock screen
e.g.

::

    gsettings set  sm.puri.phosh.plugins lock-screen "['ticket-box', 'upcoming-events']"

DBUS INTERFACE
--------------

``phosh`` allows to enable and disable certain debug flags at runtime via DBus. To see a list of
available flags use:

::

  busctl --user introspect mobi.phosh.Shell.DebugControl /mobi/phosh/Shell/DebugControl mobi.phosh.Shell.DebugControl

To toggle individual values:

::

  busctl --user set-property mobi.phosh.Shell.DebugControl /mobi/phosh/Shell/DebugControl mobi.phosh.Shell.DebugControl LogDomains as 2 phosh-shell phosh-brightness-manager

Note that the flags are not considered stable API so can change
between releases.


ENVIRONMENT VARIABLES
---------------------

``phosh`` honors the following environment variables for debugging purposes:

- ``PHOSH_DEBUG``: A comma separated list of flags:

    - ``always-splash``: Always use splash screen when starting apps
      (even when in docked mode)
    - ``app-activation``: Use longer timeout before force-closing the app
      splash. Enable this flag to help debug activation issues.
    - ``backlight-non-linear``: Assume backlight brightness changes `non-linear`.
      Set if kernel should report the backlight as `non-linear` but doesn't.
    - ``fake-builtin``: Fake a builtin screen when using a virtual output like
      in a nested Wayland session.
- ``PHOSH_FAKE_CLOCK``: Allowed values are ISO8601 formatted strings
  or ``now``. Setting this variable sets the shell's clocs to the
  given fixed value. For the clock format see ``g_date_time_new_from_iso8601()``.
- ``G_MESSAGES_DEBUG``, ``G_DEBUG`` and other environment variables supported
  by glib. https://docs.gtk.org/glib/running.html
- ``GTK_DEBUG`` and other environment variables supported by GTK, see
  https://docs.gtk.org/gtk3/running.html

See also
--------

``gsettings(1)`` ``phosh-session(1)`` ``phosh.gsettings(5)``
