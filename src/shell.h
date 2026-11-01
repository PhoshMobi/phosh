/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include <glib-object.h>

#include "lockscreen-manager.h"
#include "screenshot-manager.h"

G_BEGIN_DECLS

#define PHOSH_TYPE_SHELL phosh_shell_get_type ()

G_DECLARE_DERIVABLE_TYPE (PhoshShell, phosh_shell, PHOSH, SHELL, GObject)

struct _PhoshShellClass {
  GObjectClass parent_class;
  GType        (*get_lockscreen_type) (PhoshShell *self);

  /* Padding for future expansion */
  void (*_phosh_reserved1) (void);
  void (*_phosh_reserved2) (void);
  void (*_phosh_reserved3) (void);
  void (*_phosh_reserved4) (void);
  void (*_phosh_reserved5) (void);
  void (*_phosh_reserved6) (void);
  void (*_phosh_reserved7) (void);
  void (*_phosh_reserved8) (void);
  void (*_phosh_reserved9) (void);
};

/**
 * PhoshShellStateFlags:
 * @PHOSH_STATE_NONE: No other state
 * @PHOSH_STATE_MODAL_SYSTEM_PROMPT: any modal prompt shown
 * @PHOSH_STATE_BLANKED: primary display off
 * @PHOSH_STATE_LOCKED: displays locked
 * @PHOSH_STATE_SETTINGS: settings menu unfolded from top bar
 * @PHOSH_STATE_OVERVIEW: overview unfolded from bottom bar
 *
 * These flags are used to keep track of the state
 * the #PhoshShell is in.
 */
typedef enum {
  PHOSH_STATE_NONE                = 0,
  PHOSH_STATE_MODAL_SYSTEM_PROMPT = 1 << 0,
  PHOSH_STATE_BLANKED             = 1 << 1,
  PHOSH_STATE_LOCKED              = 1 << 2,
  PHOSH_STATE_SETTINGS            = 1 << 3,
  PHOSH_STATE_OVERVIEW            = 1 << 4,
} PhoshShellStateFlags;

PhoshShell *phosh_shell_new                 (void);

void        phosh_shell_set_default         (PhoshShell *self);
PhoshShell *phosh_shell_get_default         (void);

GType       phosh_shell_get_lockscreen_type (PhoshShell *self);
gboolean    phosh_shell_get_locked          (PhoshShell *self);
void        phosh_shell_get_usable_area     (PhoshShell *self,
                                             int        *x,
                                             int        *y,
                                             int        *width,
                                             int        *height);

void        phosh_shell_fade_out            (PhoshShell *self, guint timeout);

/* Created by the shell on startup */
PhoshLockscreenManager *phosh_shell_get_lockscreen_manager (PhoshShell *self);
PhoshScreenshotManager *phosh_shell_get_screenshot_manager (PhoshShell *self);
PhoshShellStateFlags     phosh_shell_get_state              (PhoshShell *self);
void                     phosh_shell_fold_top_panel         (PhoshShell *self);

G_END_DECLS
