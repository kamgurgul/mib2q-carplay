/*
 * AltScreen map zoom bridge.
 *
 * Java sends the steering-wheel roller's MapScale step as CMD_ALT_ZOOM while
 * the CarPlay cluster video is live. This module receives it on the bus and
 * hands it to the AltScreen hook (libaltscreen111_mhi2q.so, preloaded into the
 * same dio_manager), which owns the stream-111 display UUID and sends
 * changeMapZoomLevel to the phone.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ALTZOOM_HOOK_H
#define ALTZOOM_HOOK_H

#include "../framework/hook_framework.h"

extern const hook_module_def_t altzoom_module_def;

#endif /* ALTZOOM_HOOK_H */
