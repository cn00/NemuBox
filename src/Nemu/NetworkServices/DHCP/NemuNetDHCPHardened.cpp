/* $Id: NemuNetDHCPHardened.cpp $ */
/** @file
 * NemuNetDHCP - Hardened main().
 */

/*
 * Copyright (C) 2009-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <Nemu/sup.h>


int main(int argc, char **argv, char **envp)
{
    return SUPR3HardenedMain("NemuNetDHCP", 0 /* fFlags */, argc, argv, envp);
}

