/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Alexander Mikhaylenko <alexm@gnome.org>
 */
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_ANIMATION (phosh_animation_get_type())

/**
 * PhoshAnimationType:
 * @PHOSH_ANIMATION_TYPE_EASE_OUT_CUBIC: Use ease out cubic interpolation.
 * @PHOSH_ANIMATION_TYPE_EASE_IN_QUINTIC: Use ease in quintic interpolation.
 * @PHOSH_ANIMATION_TYPE_EASE_OUT_QUINTIC: Use ease out quintic interpolation.
 * @PHOSH_ANIMATION_TYPE_EASE_OUT_BOUNCE: Use easeOutBounce interpolation.
 *
 * The animation type of #PhoshAnimationType.
 */
typedef enum {
  PHOSH_ANIMATION_TYPE_EASE_OUT_CUBIC,
  PHOSH_ANIMATION_TYPE_EASE_IN_QUINTIC,
  PHOSH_ANIMATION_TYPE_EASE_OUT_QUINTIC,
  PHOSH_ANIMATION_TYPE_EASE_OUT_BOUNCE,
} PhoshAnimationType;

typedef struct _PhoshAnimation PhoshAnimation;

typedef void (*PhoshAnimationValueCallback) (double   value,
                                             gpointer user_data);
typedef void (*PhoshAnimationDoneCallback)  (gpointer user_data);

GType           phosh_animation_get_type  (void);

PhoshAnimation *phosh_animation_new       (GtkWidget                   *widget,
                                           double                       from,
                                           double                       to,
                                           gint64                       duration,
                                           PhoshAnimationType           type,
                                           PhoshAnimationValueCallback  value_cb,
                                           PhoshAnimationDoneCallback   done_cb,
                                           gpointer                     user_data);

PhoshAnimation *phosh_animation_ref       (PhoshAnimation *self);
void            phosh_animation_unref     (PhoshAnimation *self);

void            phosh_animation_start     (PhoshAnimation *self);
void            phosh_animation_stop      (PhoshAnimation *self);

double          phosh_animation_get_value (PhoshAnimation *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PhoshAnimation, phosh_animation_unref)

G_END_DECLS
