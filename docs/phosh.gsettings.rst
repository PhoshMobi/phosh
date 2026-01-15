.. _phosh.gsettings(5):

===============
phosh.gsettings
===============

-----------------------
GSettings used by phosh
-----------------------

DESCRIPTION
-----------

Phosh uses gsettings for most of its configuration. You can use the ``gsettings(1)`` command
to change them or to get details about the option. To get more details about an option use
`gsettings describe`, e.g.:

::

   gsettings describe mobi.phosh.shell.cell-broadcast enabled

These are the currently used gsettings schema and keys:

GSettings
~~~~~~~~~

These gsettings are used by ``phosh``:

- `mobi.phosh.shell.brightness`

    - `auto-brightness-offset`: Offset added to the calculated auto brightness value

- `mobi.phosh.shell.cell-broadcast`

    - `enabled`: Whether receiving cell broadcasts is enabled

- `sm.puri.phosh`

   - `app-filter-mode`: Application filtering in overview. This shows all applications:

     ::

          gsettings set sm.puri.phosh app-filter-mode '[]'

   - `automatic-high-contrast-threshold`: The brightness threshold to trigger automatic high contrast
   - `automatic-high-contrast`: Whether automatic high contrast is enabled
   - `enable-suspend`: Whether suspend is enabled in the system menu:
   - `favorites`: List of favorite applications
   - `force-adaptive`: List of app-id's phosh considers adaptive (so they are always shown):

     ::

          gsettings set sm.puri.phosh force-adaptive "['chromium.desktop', 'firefox-esr.desktop']"

   - `osk-unfold-delay`: How long to press bottom bar pill for OSK to unfold
   - `quick-silent`: Silence device by pression Vol- on incoming calls
   - `shell-layout`: How to layout shell UI elements around cutouts and notches.
     `device`: Automatic based on device settings, `none`: No automatic layout.
   - `wwan-backend`: WWan backend to use

- `sm.puri.phosh.emergency-calls`

   - `enabled`: Whether emergency calls are enabled. This is
     experimental, please check if it works for your povider / in your
     country / for your sim card before relying on it.

- `sm.puri.phosh.lockscreen`

   - `require-unlock`: Whether unlock via password/pin is required
   - `shuffle-keypad`: Shuffle keys on keypad

- `sm.puri.phosh.plugins`

   - `lockscreen`: The enabled lock screen plugins
   - `quick-settings`: The enabled custom quick settings

- `sm.puri.phosh.notifications`

   - `wakeup-screen-categories`: Notification categories that trigger screen wakeup
   - `wakeup-screen-triggers`: What properties of a notification trigger screen wake-up
   - `wakeup-screen-urgency`: The lowest urgency that triggers screen wake-up (if via `wakeup-screen-triggers`)

- `sm.puri.phosh.plugins.launcher-box`

   - `folder`: The folder containing the desktop files to show in the launcher box

- `sm.puri.phosh.plugins.ticket-box`

   - `folder`: The folder containing the tickets to be presented on the lock screen.

- `sm.puri.phosh.plugins.upcoming-events`

   - `days`: Number of days to show in the upcoming events list

- `org.gnome.desktop.app-folders.folder`: Folder support

- `org.gnome.desktop.background`: Background wallpaper

   - `picture-uri`
   - `picture-uri-dark`
   - `primary-color`

       E.g. to get the "classic" black overview back use:

   ::

      gsettings set org.gnome.desktop.background picture-options none
      gsettings set org.gnome.desktop.background primary-color '#000000'

- `org.gnome.desktop.interface1`

   - `show-battery-percentage`

- `org.gnome.desktop.media-handling`

   - `automount`

- `org.gnome.desktop.notifications`

  - `show-banners` : Whether to show notification banners
  - `show-in-lockscreen` : Whether notifications should be shown on the lock screen

- `org.gnome.desktop.peripherals.touchscreen`: Touch screen to output mappings. Example for NexDock 360:

    ::

       gsettings set org.gnome.desktop.peripherals.touchscreen:/org/gnome/desktop/peripherals/touchscreens/27c0:0819/ output "\['Unknown', 'NexDock', '8R33926O00Q'\]"

- `org.gnome.desktop.screensaver`:

   - `lock-delay`
   - `lock-enabled`
   - `picture-uri`: Lockscreen background image / wallpaper
   - `picture-options`: `none` disable lockscreen image

- `org.gnome.shell.keybindings`

   - `toggle-message-tray`
   - `toggle-overview`

See also
--------

``phosh(1)`` ``phoc.gsettings(5)`` ``gsettings(1)``
