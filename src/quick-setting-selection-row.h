/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <handy.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_QUICK_SETTING_SELECTION_ROW (phosh_quick_setting_selection_row_get_type ())

G_DECLARE_FINAL_TYPE (PhoshQuickSettingSelectionRow,
                      phosh_quick_setting_selection_row,
                      PHOSH, QUICK_SETTING_SELECTION_ROW,
                      HdyActionRow)

PhoshQuickSettingSelectionRow *phosh_quick_setting_selection_row_new (const char *title,
                                                                       const char *icon_name,
                                                                       gboolean    selected);

void     phosh_quick_setting_selection_row_set_icon_name (PhoshQuickSettingSelectionRow *self,
                                                           const char                    *icon_name);
const char *phosh_quick_setting_selection_row_get_icon_name (PhoshQuickSettingSelectionRow *self);
void     phosh_quick_setting_selection_row_set_selected (PhoshQuickSettingSelectionRow *self,
                                                         gboolean                       selected);
gboolean phosh_quick_setting_selection_row_get_selected (PhoshQuickSettingSelectionRow *self);

G_END_DECLS
