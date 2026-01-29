Title: Notes for Application Developers
Slug: appdev

# Notes for application development

Phosh relies on freedesktop.org and Wayland protocols where
possible. Sometimes we have phosh specific extensions though until
changes made it into the spec.

## Desktop Entry Specification

Phosh aims to parse app entries according to the [Desktop Entry
Specification][]. For some mobile needs phosh honors the following
additional entries:

* `X-Purism-FormFactor`: Allows app to specify that they are suitable for use
  on phones. Example: `X-Purism-FormFactor=Workstation;Mobile;`. Note that this
  will be phased out in favour of supplying this information
  [via appstream metadata][].
* `X-KDE-FormFactor`: Similar to the above (used by some KDE applications)
* `X-Geoclue-Reason`: Used to indicate the reason why an application
  wants to access location data.
* `X-Phosh-Lockscreen-Actions`: Used to allow actions to be used on the lock screen.
  Example: `X-Phosh-Lockscreen-Actions=app.stop-alarm::;app.snooze-alarm::;`
* `X-Phosh-UsesFeedback`: Used to indicate that application submit events to
  feedbackd to trigger audio, haptic or led feedback. This is useful for setting
  applications so they can show entries for individual apps.
  Example: `X-Phosh-UsesFeedback=true`
* `X-GNOME-UsesNotifications`: Used to indicate that application submit notifications
  This is useful for setting applications so they can show entries for
  individual apps to fine tune their feedback.
  Example: `X-GNOME-UsesNotifications=true`

## Desktop Notification Specification

Phosh's notification server aims to adhere to the [Desktop Notification
Specification][]. On mobile we need some extensions to handle haptic and led feedback
for those notifications. So phosh honors the following additional hint:

* `x-phosh-fb-profile`: The feedback profile to use for events associated with
  this notification. See the [Feedback Theme Specification][] for details. The
  additional value `none` is used to indicate that the event that would be
  triggered should be suppressed and hence no feedback emitted to the user.

[Desktop Entry Specification]: https://specifications.freedesktop.org/desktop-entry-spec/latest/
[Desktop Notification Specification]: https://specifications.freedesktop.org/notification-spec/notification-spec-latest.html#hints
[Feedback Theme Specification]: https://honk.sigxcpu.org/projects/feedbackd/doc/Feedback-theme-spec-0.0.0.html
[via appstream metadata]: https://gitlab.gnome.org/World/Phosh/phosh/-/merge_requests/950
