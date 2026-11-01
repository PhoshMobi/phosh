/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

/*
 * The list of headers here must match what is exposed in Phosh-0.gir
 * for Rust binding generation.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define __LIBPHOSH_H_INSIDE__

#include "layersurface.h"
#include "lockscreen-manager.h"
#include "lockscreen.h"
#include "quick-setting.h"
#include "quick-setting-selection-row.h"
#include "screenshot-manager.h"
#include "shell.h"
#include "status-icon.h"
#include "status-page.h"
#include "wall-clock.h"

G_END_DECLS
