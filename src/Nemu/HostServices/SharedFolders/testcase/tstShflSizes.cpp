/** @file
 * tstShflSize - Testcase for shared folder structure sizes.
 * Run this on Linux and Windows, then compare.
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <Nemu/shflsvc.h>
#include <iprt/string.h>
#include <stdio.h>

#define STRUCT(t, size)   \
    do { \
        if (fPrintChecks) \
            printf("    STRUCT(" #t ", %d);\n", (int)sizeof(t)); \
        else if ((size) != sizeof(t)) \
        { \
            printf("%30s: %d expected %d!\n", #t, (int)sizeof(t), (size)); \
            cErrors++; \
        } \
        else if (!fQuiet)\
            printf("%30s: %d\n", #t, (int)sizeof(t)); \
    } while (0)


int main(int argc, char **argv)
{
    unsigned cErrors = 0;

    /*
     * Prints the code below if any argument was giving.
     */
    bool fQuiet = argc == 2 && !strcmp(argv[1], "quiet");
    bool fPrintChecks = !fQuiet && argc != 1;

    printf("tstShflSizes: TESTING\n");

    /*
     * The checks.
     */
    STRUCT(SHFLROOT, 4);
    STRUCT(SHFLHANDLE, 8);
    STRUCT(SHFLSTRING, 6);
    STRUCT(SHFLCREATERESULT, 4);
    STRUCT(SHFLCREATEPARMS, 108);
    STRUCT(SHFLMAPPING, 8);
    STRUCT(SHFLDIRINFO, 128);
    STRUCT(SHFLVOLINFO, 40);
    STRUCT(SHFLFSOBJATTR, 44);
    STRUCT(SHFLFSOBJINFO, 92);
#ifdef NEMU_WITH_64_BITS_GUESTS
/* The size of the guest structures depends on the current architecture bit count (ARCH_BITS)
 * because the HGCMFunctionParameter structure differs in 32 and 64 bit guests.
 * The host VMMDev device takes care about this.
 *
 * Therefore this testcase verifies whether structure sizes are correct for the current ARCH_BITS.
 */
# if ARCH_BITS == 64
    STRUCT(NemuSFQueryMappings, 64);
    STRUCT(NemuSFQueryMapName, 48);
    STRUCT(NemuSFMapFolder_Old, 64);
    STRUCT(NemuSFMapFolder, 80);
    STRUCT(NemuSFUnmapFolder, 32);
    STRUCT(NemuSFCreate, 64);
    STRUCT(NemuSFClose, 48);
    STRUCT(NemuSFRead, 96);
    STRUCT(NemuSFWrite, 96);
    STRUCT(NemuSFLock, 96);
    STRUCT(NemuSFFlush, 48);
    STRUCT(NemuSFList, 144);
    STRUCT(NemuSFInformation, 96);
    STRUCT(NemuSFRemove, 64);
    STRUCT(NemuSFRename, 80);
# elif ARCH_BITS == 32
    STRUCT(NemuSFQueryMappings, 52);
    STRUCT(NemuSFQueryMapName, 40); /* this was changed from 52 in 21976 after Nemu-1.4. */
    STRUCT(NemuSFMapFolder_Old, 52);
    STRUCT(NemuSFMapFolder, 64);
    STRUCT(NemuSFUnmapFolder, 28);
    STRUCT(NemuSFCreate, 52);
    STRUCT(NemuSFClose, 40);
    STRUCT(NemuSFRead, 76);
    STRUCT(NemuSFWrite, 76);
    STRUCT(NemuSFLock, 76);
    STRUCT(NemuSFFlush, 40);
    STRUCT(NemuSFList, 112);
    STRUCT(NemuSFInformation, 76);
    STRUCT(NemuSFRemove, 52);
    STRUCT(NemuSFRename, 64);
# else
#  error "Unsupported ARCH_BITS"
# endif /* ARCH_BITS */
#else
    STRUCT(NemuSFQueryMappings, 52);
    STRUCT(NemuSFQueryMapName, 40); /* this was changed from 52 in 21976 after Nemu-1.4. */
    STRUCT(NemuSFMapFolder_Old, 52);
    STRUCT(NemuSFMapFolder, 64);
    STRUCT(NemuSFUnmapFolder, 28);
    STRUCT(NemuSFCreate, 52);
    STRUCT(NemuSFClose, 40);
    STRUCT(NemuSFRead, 76);
    STRUCT(NemuSFWrite, 76);
    STRUCT(NemuSFLock, 76);
    STRUCT(NemuSFFlush, 40);
    STRUCT(NemuSFList, 112);
    STRUCT(NemuSFInformation, 76);
    STRUCT(NemuSFRemove, 52);
    STRUCT(NemuSFRename, 64);
#endif /* NEMU_WITH_64_BITS_GUESTS */

    /*
     * The summary.
     */
    if (!cErrors)
        printf("tstShflSizes: SUCCESS\n");
    else
        printf("tstShflSizes: FAILURE - %d errors\n", cErrors);
    return !!cErrors;
}

