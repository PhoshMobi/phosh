.. _phosh-session(1):

=============
phosh-session
=============

--------------------------------
Session startup script for phosh
--------------------------------

SYNOPSIS
--------
|   **phosh-session** [OPTIONS...]


DESCRIPTION
-----------

``phosh-session`` is the script to startup phosh including required
components like the Wayland compositor ``phoc`` and an on screen
keyboard like ``stevia`` or ``squeekboard``. The script is rarely invoked by a
user but rather activated by a display manager or systemd unit. However to
diagnose session startup and debugging it is perfectly valid to e.g. log into
a tty and invoke ``phosh-session`` to bring up phosh and needed components.
Note that your session might then lack environment variables set by the
display manager so behavior can differ.

OPTIONS
-------

``-h``, ``--help``
   Print help and exit

``--version``
   Print version and exit

CONFIGURATION FILES
-------------------
The session script looks at the following configuration files:

- ``/usr/share/phosh/phoc.ini``: Configuration passed to the compositor
- ``/etc/phosh/phoc.ini``: Used instead of the above if present

ENVIRONMENT VARIABLES
---------------------

``phosh-session`` honors the following environment variables:

- ``WLR_BACKENDS``: The backends the wlroots library should use when phoc launches. See
  https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/docs/env_vars.md

For debugging purposes you can put environment variables into
``~/.phoshdebug`` which is read at session startup.

See also
--------

``phosh(1)`` ``phoc(1)`` ``phoc.ini(5)`` ``phosh-osk-stevia(1)``
