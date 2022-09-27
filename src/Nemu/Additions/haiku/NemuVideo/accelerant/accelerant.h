/* $Id: accelerant.h $ */
/** @file
 * NemuVideo Accelerant; Haiku Guest Additions, header.
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    Franois Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ___NEMU_ACCELERANT_H
#define ___NEMU_ACCELERANT_H

#include <Accelerant.h>
#include "../common/NemuVideo_common.h"

struct AccelerantInfo
{
    /** @todo doxygen document these fields  */
    int deviceFD;
    bool isClone;

    SharedInfo *sharedInfo;
    area_id sharedInfoArea;
};
extern AccelerantInfo gInfo;

/* General */
status_t nemuvideo_init_accelerant(int fd);
ssize_t nemuvideo_accelerant_clone_info_size(void);
void nemuvideo_get_accelerant_clone_info(void *data);
status_t nemuvideo_clone_accelerant(void *data);
void nemuvideo_uninit_accelerant(void);
status_t nemuvideo_get_accelerant_device_info(accelerant_device_info *adi);
sem_id nemuvideo_accelerant_retrace_semaphore(void);

/* Modes & constraints */
uint32 nemuvideo_accelerant_mode_count(void);
status_t nemuvideo_get_mode_list(display_mode *dm);
status_t nemuvideo_set_display_mode(display_mode *modeToSet);
status_t nemuvideo_get_display_mode(display_mode *currentMode);
status_t nemuvideo_get_edid_info(void *info, size_t size, uint32 *_version);
status_t nemuvideo_get_frame_buffer_config(frame_buffer_config *config);
status_t nemuvideo_get_pixel_clock_limits(display_mode *dm, uint32 *low, uint32 *high);

/* Cursor */
status_t nemuvideo_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY, uint8 *andMask, uint8 *xorMask);
void nemuvideo_move_cursor(uint16 x, uint16 y);
void nemuvideo_show_cursor(bool is_visible);

/* Accelerant engine */
uint32 nemuvideo_accelerant_engine_count(void);
status_t nemuvideo_acquire_engine(uint32 capabilities, uint32 maxWait, sync_token *st, engine_token **et);
status_t nemuvideo_release_engine(engine_token *et, sync_token *st);
void nemuvideo_wait_engine_idle(void);
status_t nemuvideo_get_sync_token(engine_token *et, sync_token *st);
status_t nemuvideo_sync_to_token(sync_token *st);

/* 2D acceleration */
void nemuvideo_screen_to_screen_blit(engine_token *et, blit_params *list, uint32 count);
void nemuvideo_fill_rectangle(engine_token *et, uint32 color, fill_rect_params *list, uint32 count);
void nemuvideo_invert_rectangle(engine_token *et, fill_rect_params *list, uint32 count);
void nemuvideo_fill_span(engine_token *et, uint32 color, uint16 *list, uint32 count);

#endif /* ___NEMU_ACCELERANT_H */

