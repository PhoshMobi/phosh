/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <gtk/gtk.h>

#define PHOSH_TYPE_FEEDBACK_MANAGER (phosh_feedback_manager_get_type())

G_DECLARE_FINAL_TYPE (PhoshFeedbackManager,
                      phosh_feedback_manager,
                      PHOSH,
                      FEEDBACK_MANAGER,
                      GObject)

PhoshFeedbackManager *phosh_feedback_manager_new (void);
void                  phosh_feedback_manager_toggle (PhoshFeedbackManager *self);
const char *          phosh_feedback_manager_get_icon_name (PhoshFeedbackManager *self);
const char *          phosh_feedback_manager_get_profile (PhoshFeedbackManager *self);
void                  phosh_feedback_manager_set_profile (PhoshFeedbackManager *self, const char *profile);
void                  phosh_feedback_manager_trigger_feedback (PhoshFeedbackManager *self, const char *event);
void                  phosh_trigger_feedback (const char *name, gboolean important);
void                  phosh_feedback_manager_setup_event_hooks (PhoshFeedbackManager *self);
