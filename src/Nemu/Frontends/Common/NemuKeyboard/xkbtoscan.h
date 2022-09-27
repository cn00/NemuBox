/* $Id: xkbtoscan.h $ */
/** @file
 * Nemu/Frontends/Common - X11 keyboard driver translation tables (XT scan
 *                         code mappings for XKB key names).
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */

#ifndef ___Nemu_keyboard_tables_h
# error This file must be included from within keyboard-tables.h
#endif /* ___Nemu_keyboard_tables_h */

enum { XKB_NAME_SIZE = 4 };

/**
 * This table contains a list of mappings of XKB key names to XT scan codes.
 */
struct
{
    const char cszName[XKB_NAME_SIZE];
    unsigned uScan;
} xkbMap[] =
{
    { "ESC", 0x1 },
    { "AE01", 0x2 },
    { "AE02", 0x3 },
    { "AE03", 0x4 },
    { "AE04", 0x5 },
    { "AE05", 0x6 },
    { "AE06", 0x7 },
    { "AE07", 0x8 },
    { "AE08", 0x9 },
    { "AE09", 0xa },
    { "AE10", 0xb },
    { "AE11", 0xc },
    { "AE12", 0xd },
    { "BKSP", 0xe },
    { "TAB", 0xf },
    { "AD01", 0x10 },
    { "AD02", 0x11 },
    { "AD03", 0x12 },
    { "AD04", 0x13 },
    { "AD05", 0x14 },
    { "AD06", 0x15 },
    { "AD07", 0x16 },
    { "AD08", 0x17 },
    { "AD09", 0x18 },
    { "AD10", 0x19 },
    { "AD11", 0x1a },
    { "AD12", 0x1b },
    { "RTRN", 0x1c },
    { "LCTL", 0x1d },
    { "AC01", 0x1e },
    { "AC02", 0x1f },
    { "AC03", 0x20 },
    { "AC04", 0x21 },
    { "AC05", 0x22 },
    { "AC06", 0x23 },
    { "AC07", 0x24 },
    { "AC08", 0x25 },
    { "AC09", 0x26 },
    { "AC10", 0x27 },
    { "AC11", 0x28 },
    { "AC12", 0x2b },
    { "TLDE", 0x29 },
    { "LFSH", 0x2a },
    { "BKSL", 0x2b },
    { "AB01", 0x2c },
    { "AB02", 0x2d },
    { "AB03", 0x2e },
    { "AB04", 0x2f },
    { "AB05", 0x30 },
    { "AB06", 0x31 },
    { "AB07", 0x32 },
    { "AB08", 0x33 },
    { "AB09", 0x34 },
    { "AB10", 0x35 },
    { "RTSH", 0x36 },
    { "KPMU", 0x37 },
    { "LALT", 0x38 },
    { "SPCE", 0x39 },
    { "CAPS", 0x3a },
    { "FK01", 0x3b },
    { "FK02", 0x3c },
    { "FK03", 0x3d },
    { "FK04", 0x3e },
    { "FK05", 0x3f },
    { "FK06", 0x40 },
    { "FK07", 0x41 },
    { "FK08", 0x42 },
    { "FK09", 0x43 },
    { "FK10", 0x44 },
    { "NMLK", 0x145 },
    { "SCLK", 0x46 },
    { "KP7", 0x47 },
    { "KP8", 0x48 },
    { "KP9", 0x49 },
    { "KPSU", 0x4a },
    { "KP4", 0x4b },
    { "KP5", 0x4c },
    { "KP6", 0x4d },
    { "KPAD", 0x4e },
    { "KP1", 0x4f },
    { "KP2", 0x50 },
    { "KP3", 0x51 },
    { "KP0", 0x52 },
    { "KPDL", 0x53 },
    { "KPPT", 0x7e },
    { "LVL3", 0x138 },
    { "LSGT", 0x56 },
    { "FK11", 0x57 },
    { "FK12", 0x58 },
    { "AB11", 0x73 },
    { "KATA", 0x0 },
    { "HIRA", 0x0 },
    { "HENK", 0x79 },
    { "HKTG", 0x70 },
    { "MUHE", 0x7b },
    { "HZTG", 0x29 },
    { "JPCM", 0x0 },
    { "KPEN", 0x11c },
    { "RCTL", 0x11d },
    { "KPDV", 0x135 },
    { "PRSC", 0x137 },
    { "RALT", 0x138 },
    { "ALGR", 0x138 },
    { "LNFD", 0x0 },
    { "HOME", 0x147 },
    { "UP", 0x148 },
    { "PGUP", 0x149 },
    { "LEFT", 0x14b },
    { "RGHT", 0x14d },
    { "END", 0x14f },
    { "DOWN", 0x150 },
    { "PGDN", 0x151 },
    { "INS", 0x152 },
    { "DELE", 0x153 },
    { "I120", 0x0 },
    { "MUTE", 0x120 },
    { "VOL-", 0x12e },
    { "VOL+", 0x130 },
    { "POWR", 0x15e },
    { "KPEQ", 0x0 },
    { "I126", 0x0 },
    { "PAUS", 0x45 },
    { "I128", 0x0 },
    { "I129", 0x7e },
    { "KPPT", 0x7e },
    { "HNGL", 0xf2 },
    { "HJCV", 0xf1 },
    { "AE13", 0x7d },
    { "LWIN", 0x15b },
    { "LMTA", 0x15b },
    { "RWIN", 0x15c },
    { "RMTA", 0x15c },
    { "COMP", 0x15d },
    { "MENU", 0x15d },
    { "STOP", 0x168 },
    { "AGAI", 0x105 },
    { "PROP", 0x106 },
    { "UNDO", 0x107 },
    { "FRNT", 0x10c },
    { "COPY", 0x118 },
    { "OPEN", 0x65 },
    { "PAST", 0x10a },
    { "FIND", 0x110 },
    { "CUT", 0x117 },
    { "HELP", 0x175 },
    { "I147", 0x0 },
    { "I148", 0x0 },
    { "I149", 0x0 },
    { "I150", 0x15f },
    { "I151", 0x163 },
    { "I152", 0x0 },
    { "I153", 0x119 },
    { "I154", 0x0 },
    { "I155", 0x0 },
    { "I156", 0x0 },
    { "I157", 0x0 },
    { "I158", 0x0 },
    { "I159", 0x0 },
    { "I160", 0x120 },
    { "I161", 0x0 },
    { "I162", 0x122 },
    { "I163", 0x16c },
    { "I164", 0x124 },
    { "I165", 0x15f },
    { "I166", 0x16a },
    { "I167", 0x169 },
    { "I168", 0x0 },
    { "I169", 0x0 },
    { "I170", 0x0 },
    { "I171", 0x119 },
    { "I172", 0x122 },
    { "I173", 0x110 },
    { "I174", 0x12e },
    { "I175", 0x0 },
    { "I176", 0x130 },
    { "I177", 0x0 },
    { "I178", 0x0 },
    { "I179", 0x0 },
    { "I180", 0x132 },
    { "I181", 0x167 },
    { "I182", 0x140 },
    { "I183", 0x0 },
    { "I184", 0x0 },
    { "I185", 0x10b },
    { "I186", 0x18b },
    { "I187", 0x0 },
    { "I188", 0x0 },
    { "I189", 0x0 },
    { "I190", 0x105 },
    { "FK13", 0x0 },
    { "FK14", 0x0 },
    { "FK15", 0x0 },
    { "FK16", 0x0 },
    { "FK17", 0x0 },
    { "FK18", 0x0 },
    { "FK19", 0x0 },
    { "FK20", 0x0 },
    { "FK21", 0x0 },
    { "FK22", 0x0 },
    { "FK23", 0x0 },
    { "FK24", 0x0 },
    { "MDSW", 0x138 },
    { "ALT", 0x0 },
    { "META", 0x0 },
    { "SUPR", 0x0 },
    { "HYPR", 0x0 },
    { "I208", 0x122 },
    { "I209", 0x122 },
    { "I210", 0x0 },
    { "I211", 0x0 },
    { "I212", 0x0 },
    { "I213", 0x0 },
    { "I214", 0x140 },
    { "I215", 0x122 },
    { "I216", 0x169 },
    { "I217", 0x0 },
    { "I218", 0x137 },
    { "I219", 0x0 },
    { "I220", 0x0 },
    { "I221", 0x0 },
    { "I222", 0x0 },
    { "I223", 0x0 },
    { "I224", 0x0 },
    { "I225", 0x165 },
    { "I226", 0x0 },
    { "I227", 0x0 },
    { "I228", 0x0 },
    { "I229", 0x165 },
    { "I230", 0x166 },
    { "I231", 0x167 },
    { "I232", 0x168 },
    { "I233", 0x169 },
    { "I234", 0x16a },
    { "I235", 0x16b },
    { "I236", 0x16c },
    { "I237", 0x16d },
    { "I238", 0x0 },
    { "I239", 0x143 },
    { "I240", 0x141 },
    { "I241", 0x0 },
    { "I242", 0x157 },
    { "I243", 0x105 },
    { "I244", 0x0 },
    { "I245", 0x0 },
    { "I246", 0x0 },
    { "I247", 0x0 },
    { "I248", 0x0 },
    { "I249", 0x0 },
    { "I250", 0x0 },
    { "I251", 0x0 },
    { "I252", 0x0 },
    { "I253", 0x0 }
};
