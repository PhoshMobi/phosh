# What problem did you encounter?

Please describe the problem in general terms.

## How to reproduce?

Please provide steps to reproduce the issue. If it's a graphical issue please
attach screenshot.

## What is the (wrong) result?

## What is the expected behaviour?

# Expectations

If you're unsure about the component that triggers this bug please
check in our Matrix channel or with your distribution upfront to
clarify. If the issue is in a closely related software component we
can reassign the issue but if it's in a component "further away" you
might otherwise need to refile the issue as the project might reside
in a completely different issue tracker.

Note that we only support the latest upstream version, see
[our releases](https://phosh.mobi/releases). If you report an issue in
an older version it will get closed unless it is obvious to the
developers that the issue is still present in the current version.

If your device is running Halium (Droidian, FuriOS) please ensure with
your distribution first that the issue isn't triggered by a downstrem
modification.

# Context

Please describe your setup, or write additional information relevant
to this bug.  Here, you can also write why this is actually a bug,
if that might not be immediately clear to the reader.

# Which version did you encounter the bug in?

- [ ] I used a distribution package. Please paste the output of
  ``phosh-session --version`` below.

- [ ] I compiled it myself. If you compiled phosh from source please provide the
  git revision e.g. by running ``git log -1 --pretty=oneline`` and pasting
  the output below.

```
  Phosh Version:
```

# How are you running phosh?

## Device

- [ ] On a mobile phone. Please specify the model (e.g. PinePhone, OnePlus 6, Librem 5):
- [ ] On a tablet/convertible. Please specify the model (e.g. IdeaPad Duet):
- [ ] In a nested session. Please specify the command you start phosh with:
- [ ] As a VM image (e.g. [Phosh Weekly images][]). Please elaborate:
- [ ] Something else. Please elaborate:

## Distribution

Which Linux distribution are you using? If Nura please also specify
if you're using systemd or OpenRC:

## How is Phosh started on your system

- [ ] Through a display manager

  - [ ] phrog
  - [ ] gdm
  - [ ] lightdm
  - [ ] other:

- [ ] other (e.g. systemd unit, please elaborate):

# Relevant logfiles

Please provide relevant logs. You can get the logs since last boot
with ``journalctl -b 0`` and `journalctl --user -b 0`.

See `phosh(1)` on debugging related environment variables and `phosh-session(1)`
on where you can set them.

# System information

Please attach the output of

```
phosh-mobile-settings --debug-info
```

[Phosh Weekly Image]: https://images.phosh.mobi/nightly/
