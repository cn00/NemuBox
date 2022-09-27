/** @file
 * VirtualBox OpenGL command pack/unpack header
 */

/*
 *
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___Nemu_HostService_NemuOGLOp_h
#define ___Nemu_HostService_NemuOGLOp_h

#ifdef NEMU_OGL_GUEST_SIDE
/************************************************************************************************************
 * Guest side macro's for packing OpenGL function calls into the command buffer.                            *
 *                                                                                                          *
 ************************************************************************************************************/

#define NEMU_OGL_NAME_PREFIX(Function)                    gl##Function

#define OGL_CMD(op, numpar, size)                                                           \
    NemuCmdStart(NEMU_OGL_OP_##op, numpar, size);

#define OGL_PARAM(val, size)                                                                \
    NemuCmdSaveParameter((uint8_t *)&val, size);

#define OGL_MEMPARAM(ptr, size)                                                             \
    NemuCmdSaveMemParameter((uint8_t *)ptr, size);

#define OGL_CMD_END(op)                                                                     \
    NemuCmdStop(NEMU_OGL_OP_##op);


#define NEMU_OGL_GEN_OP(op)                                                                 \
    OGL_CMD(op, 0, 0);                                                                      \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP1(op, p1)                                                            \
    OGL_CMD(op, 1, sizeof(p1));                                                             \
    OGL_PARAM(p1, sizeof(p1));                                                              \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP2(op, p1, p2)                                                        \
    OGL_CMD(op, 2, sizeof(p1)+sizeof(p2));                                                  \
    OGL_PARAM(p1, sizeof(p1));                                                              \
    OGL_PARAM(p2, sizeof(p2));                                                              \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP3(op, p1, p2, p3)                                                    \
    OGL_CMD(op, 3, sizeof(p1)+sizeof(p2)+sizeof(p3));                                       \
    OGL_PARAM(p1, sizeof(p1));                                                              \
    OGL_PARAM(p2, sizeof(p2));                                                              \
    OGL_PARAM(p3, sizeof(p3));                                                              \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP4(op, p1, p2, p3, p4)                                                \
    OGL_CMD(op, 4, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4));                            \
    OGL_PARAM(p1, sizeof(p1));                                                              \
    OGL_PARAM(p2, sizeof(p2));                                                              \
    OGL_PARAM(p3, sizeof(p3));                                                              \
    OGL_PARAM(p4, sizeof(p4));                                                              \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP5(op, p1, p2, p3, p4, p5)                                            \
    OGL_CMD(op, 5, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+sizeof(p5));                 \
    OGL_PARAM(p1, sizeof(p1));                                                              \
    OGL_PARAM(p2, sizeof(p2));                                                              \
    OGL_PARAM(p3, sizeof(p3));                                                              \
    OGL_PARAM(p4, sizeof(p4));                                                              \
    OGL_PARAM(p5, sizeof(p5));                                                              \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP6(op, p1, p2, p3, p4, p5, p6)                                        \
    OGL_CMD(op, 6, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+sizeof(p5)+sizeof(p6));      \
    OGL_PARAM(p1, sizeof(p1));                                                              \
    OGL_PARAM(p2, sizeof(p2));                                                              \
    OGL_PARAM(p3, sizeof(p3));                                                              \
    OGL_PARAM(p4, sizeof(p4));                                                              \
    OGL_PARAM(p5, sizeof(p5));                                                              \
    OGL_PARAM(p6, sizeof(p6));                                                              \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP7(op, p1, p2, p3, p4, p5, p6, p7)                                    \
    OGL_CMD(op, 7, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+sizeof(p5)+sizeof(p6)+sizeof(p7));    \
    OGL_PARAM(p1, sizeof(p1));                                                              \
    OGL_PARAM(p2, sizeof(p2));                                                              \
    OGL_PARAM(p3, sizeof(p3));                                                              \
    OGL_PARAM(p4, sizeof(p4));                                                              \
    OGL_PARAM(p5, sizeof(p5));                                                              \
    OGL_PARAM(p6, sizeof(p6));                                                              \
    OGL_PARAM(p7, sizeof(p7));                                                              \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP8(op, p1, p2, p3, p4, p5, p6, p7, p8)                                \
    OGL_CMD(op, 8, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+sizeof(p5)+sizeof(p6)+sizeof(p7)+sizeof(p8));    \
    OGL_PARAM(p1, sizeof(p1));                                                              \
    OGL_PARAM(p2, sizeof(p2));                                                              \
    OGL_PARAM(p3, sizeof(p3));                                                              \
    OGL_PARAM(p4, sizeof(p4));                                                              \
    OGL_PARAM(p5, sizeof(p5));                                                              \
    OGL_PARAM(p6, sizeof(p6));                                                              \
    OGL_PARAM(p7, sizeof(p7));                                                              \
    OGL_PARAM(p8, sizeof(p8));                                                              \
    OGL_CMD_END(op);


/* last parameter is a memory block */
#define NEMU_OGL_GEN_OP1PTR(op, size, p1ptr)                                                        \
    OGL_CMD(op, 1, size);                                                                         \
    OGL_MEMPARAM(p1ptr, size);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP2PTR(op, p1, size, p2ptr)                                                    \
    OGL_CMD(op, 2, sizeof(p1)+size);                                                              \
    OGL_PARAM(p1, sizeof(p1));                                                                     \
    OGL_MEMPARAM(p2ptr, size);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP3PTR(op, p1, p2, size, p3ptr)                                                \
    OGL_CMD(op, 3, sizeof(p1)+sizeof(p2)+size);                                                   \
    OGL_PARAM(p1, sizeof(p1));                                                                     \
    OGL_PARAM(p2, sizeof(p2));                                                                     \
    OGL_MEMPARAM(p3ptr, size);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP4PTR(op, p1, p2, p3, size, p4ptr)                                            \
    OGL_CMD(op, 4, sizeof(p1)+sizeof(p2)+sizeof(p3)+size);                                        \
    OGL_PARAM(p1, sizeof(p1));                                                                     \
    OGL_PARAM(p2, sizeof(p2));                                                                     \
    OGL_PARAM(p3, sizeof(p3));                                                                     \
    OGL_MEMPARAM(p4ptr, size);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP5PTR(op, p1, p2, p3, p4, size, p5ptr)                                        \
    OGL_CMD(op, 5, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+size);                             \
    OGL_PARAM(p1, sizeof(p1));                                                                     \
    OGL_PARAM(p2, sizeof(p2));                                                                     \
    OGL_PARAM(p3, sizeof(p3));                                                                     \
    OGL_PARAM(p4, sizeof(p4));                                                                     \
    OGL_MEMPARAM(p5ptr, size);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP6PTR(op, p1, p2, p3, p4, p5, size, p6ptr)                                    \
    OGL_CMD(op, 6, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+sizeof(p5)+size);                  \
    OGL_PARAM(p1, sizeof(p1));                                                                     \
    OGL_PARAM(p2, sizeof(p2));                                                                     \
    OGL_PARAM(p3, sizeof(p3));                                                                     \
    OGL_PARAM(p4, sizeof(p4));                                                                     \
    OGL_PARAM(p5, sizeof(p5));                                                                     \
    OGL_MEMPARAM(p6ptr, size);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP7PTR(op, p1, p2, p3, p4, p5, p6, size, p7ptr)                                \
    OGL_CMD(op, 7, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+sizeof(p5)+sizeof(p6)+size);       \
    OGL_PARAM(p1, sizeof(p1));                                                                     \
    OGL_PARAM(p2, sizeof(p2));                                                                     \
    OGL_PARAM(p3, sizeof(p3));                                                                     \
    OGL_PARAM(p4, sizeof(p4));                                                                     \
    OGL_PARAM(p5, sizeof(p5));                                                                     \
    OGL_PARAM(p6, sizeof(p6));                                                                     \
    OGL_MEMPARAM(p7ptr, size);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP8PTR(op, p1, p2, p3, p4, p5, p6, p7, size, p8ptr)                            \
    OGL_CMD(op, 8, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+sizeof(p5)+sizeof(p6)+sizeof(p7)+size);       \
    OGL_PARAM(p1, sizeof(p1));                                                                     \
    OGL_PARAM(p2, sizeof(p2));                                                                     \
    OGL_PARAM(p3, sizeof(p3));                                                                     \
    OGL_PARAM(p4, sizeof(p4));                                                                     \
    OGL_PARAM(p5, sizeof(p5));                                                                     \
    OGL_PARAM(p6, sizeof(p6));                                                                     \
    OGL_PARAM(p7, sizeof(p7));                                                                     \
    OGL_MEMPARAM(p8ptr, size);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP9PTR(op, p1, p2, p3, p4, p5, p6, p7, p8, size, p9ptr)                        \
    OGL_CMD(op, 9, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+sizeof(p5)+sizeof(p6)+sizeof(p7)+sizeof(p8)+size);       \
    OGL_PARAM(p1, sizeof(p1));                                                                     \
    OGL_PARAM(p2, sizeof(p2));                                                                     \
    OGL_PARAM(p3, sizeof(p3));                                                                     \
    OGL_PARAM(p4, sizeof(p4));                                                                     \
    OGL_PARAM(p5, sizeof(p5));                                                                     \
    OGL_PARAM(p6, sizeof(p6));                                                                     \
    OGL_PARAM(p7, sizeof(p7));                                                                     \
    OGL_PARAM(p8, sizeof(p8));                                                                     \
    OGL_MEMPARAM(p9ptr, size);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP10PTR(op, p1, p2, p3, p4, p5, p6, p7, p8, p9, size, p10ptr)                  \
    OGL_CMD(op, 10, sizeof(p1)+sizeof(p2)+sizeof(p3)+sizeof(p4)+sizeof(p5)+sizeof(p6)+sizeof(p7)+sizeof(p8)+sizeof(p9)+size);       \
    OGL_PARAM(p1, sizeof(p1));                                                                     \
    OGL_PARAM(p2, sizeof(p2));                                                                     \
    OGL_PARAM(p3, sizeof(p3));                                                                     \
    OGL_PARAM(p4, sizeof(p4));                                                                     \
    OGL_PARAM(p5, sizeof(p5));                                                                     \
    OGL_PARAM(p6, sizeof(p6));                                                                     \
    OGL_PARAM(p7, sizeof(p7));                                                                     \
    OGL_PARAM(p8, sizeof(p8));                                                                     \
    OGL_PARAM(p9, sizeof(p9));                                                                     \
    OGL_MEMPARAM(p10ptr, size);                                                                    \
    OGL_CMD_END(op);


/* two memory blocks */
#define NEMU_OGL_GEN_OP2PTRPTR(op, size1, p1ptr, size2, p2ptr)                                      \
    OGL_CMD(op, 2, size1+size2);                                                                    \
    OGL_MEMPARAM(p1ptr, size1);                                                                     \
    OGL_MEMPARAM(p2ptr, size2);                                                                     \
    OGL_CMD_END(op);

#define NEMU_OGL_GEN_OP3PTRPTR(op, p1, size2, p2ptr, size3, p3ptr)                                  \
    OGL_CMD(op, 3, sizeof(p1)+size2+size3);                                                         \
    OGL_PARAM(p1, sizeof(p1));                                                                      \
    OGL_MEMPARAM(p2ptr, size2);                                                                     \
    OGL_MEMPARAM(p3ptr, size3);                                                                     \
    OGL_CMD_END(op);

/* Note: sync operations always set the last error */
/* sync operation that returns a value */
#define NEMU_OGL_GEN_SYNC_OP_RET(rettype, op)                                                       \
    NEMU_OGL_GEN_OP(op)                                                                             \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP1_RET(rettype, op, p1)                                                  \
    NEMU_OGL_GEN_OP1(op, p1)                                                                        \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP2_RET(rettype, op, p1, p2)                                              \
    NEMU_OGL_GEN_OP2(op, p1, p2)                                                                    \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP3_RET(rettype, op, p1, p2, p3)                                          \
    NEMU_OGL_GEN_OP3(op, p1, p2, p3)                                                                \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP4_RET(rettype, op, p1, p2, p3, p4)                                      \
    NEMU_OGL_GEN_OP4(op, p1, p2, p3, p4)                                                            \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP5_RET(rettype, op, p1, p2, p3, p4, p5)                                  \
    NEMU_OGL_GEN_OP5(op, p1, p2, p3, p4, p5)                                                        \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP6_RET(rettype, op, p1, p2, p3, p4, p5, p6)                              \
    NEMU_OGL_GEN_OP6(op, p1, p2, p3, p4, p5, p6)                                                    \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP7_RET(rettype, op, p1, p2, p3, p4, p5, p6, p7)                          \
    NEMU_OGL_GEN_OP7(op, p1, p2, p3, p4, p5, p6, p7)                                                \
    rettype retval = (rettype)NemuOGLFlush();


#define NEMU_OGL_GEN_SYNC_OP(op)                                                                    \
    NEMU_OGL_GEN_OP(op)                                                                             \
    NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP1(op, p1)                                                               \
    NEMU_OGL_GEN_OP1(op, p1)                                                                        \
    NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP2(op, p1, p2)                                                           \
    NEMU_OGL_GEN_OP2(op, p1, p2)                                                                    \
    NemuOGLFlush();


/* Sync operation whose last parameter is a block of memory */
#define NEMU_OGL_GEN_SYNC_OP2_PTR(op, p1, size, p2ptr)                                              \
    NEMU_OGL_GEN_OP2PTR(op, p1, size, p2ptr);                                                       \
    NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP5_PTR(op, p1, p2, p3, p4, size, p5ptr)                                  \
    NEMU_OGL_GEN_OP2PTR(op, p1, p2, p3, p4, size, p5ptr);                                           \
    NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP6_PTR(op, p1, p2, p3, p4, p5, size, p6ptr)                              \
    NEMU_OGL_GEN_OP6PTR(op, p1, p2, p3, p4, p5, size, p6ptr);                                       \
    NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP7_PTR(op, p1, p2, p3, p4, p5, p6, size, p7ptr)                          \
    NEMU_OGL_GEN_OP7PTR(op, p1, p2, p3, p4, p5, p6, size, p7ptr);                                   \
    NemuOGLFlush();

/* Sync operation whose last parameter is a block of memory in which results are returned */
#define NEMU_OGL_GEN_SYNC_OP1_PASS_PTR(op, size, p1ptr)                                             \
    NEMU_OGL_GEN_OP(op);                                                                            \
    NemuOGLFlushPtr(p1ptr, size);

#define NEMU_OGL_GEN_SYNC_OP2_PASS_PTR(op, p1, size, p2ptr)                                         \
    NEMU_OGL_GEN_OP1(op, p1);                                                                       \
    NemuOGLFlushPtr(p2ptr, size);

#define NEMU_OGL_GEN_SYNC_OP3_PASS_PTR(op, p1, p2, size, p3ptr)                                     \
    NEMU_OGL_GEN_OP2(op, p1, p2);                                                                   \
    NemuOGLFlushPtr(p3ptr, size);

#define NEMU_OGL_GEN_SYNC_OP4_PASS_PTR(op, p1, p2, p3, size, p4ptr)                                 \
    NEMU_OGL_GEN_OP3(op, p1, p2, p3);                                                               \
    NemuOGLFlushPtr(p4ptr, size);

#define NEMU_OGL_GEN_SYNC_OP5_PASS_PTR(op, p1, p2, p3, p4, size, p5ptr)                             \
    NEMU_OGL_GEN_OP4(op, p1, p2, p3, p4);                                                           \
    NemuOGLFlushPtr(p5ptr, size);

#define NEMU_OGL_GEN_SYNC_OP6_PASS_PTR(op, p1, p2, p3, p4, p5, size, p6ptr)                         \
    NEMU_OGL_GEN_OP5(op, p1, p2, p3, p4, p5);                                                       \
    NemuOGLFlushPtr(p6ptr, size);

#define NEMU_OGL_GEN_SYNC_OP7_PASS_PTR(op, p1, p2, p3, p4, p5, p6, size, p7ptr)                     \
    NEMU_OGL_GEN_OP6(op, p1, p2, p3, p4, p5, p6);                                                   \
    NemuOGLFlushPtr(p7ptr, size);


/* Sync operation whose last parameter is a block of memory and return a value */
#define NEMU_OGL_GEN_SYNC_OP2_PTR_RET(rettype, op, p1, size, p2ptr)                                 \
    NEMU_OGL_GEN_OP2PTR(op, p1, size, p2ptr);                                                       \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP4_PTR_RET(rettype, op, p1, p2, p3, size, p4ptr)                         \
    NEMU_OGL_GEN_OP4PTR(op, p1, p2, p3, size, p4ptr);                                               \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP5_PTR_RET(rettype, op, p1, p2, p3, p4, size, p5ptr)                     \
    NEMU_OGL_GEN_OP5PTR(op, p1, p2, p3, p4, size, p5ptr);                                           \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP6_PTR_RET(rettype, op, p1, p2, p3, p4, p5, size, p6ptr)                 \
    NEMU_OGL_GEN_OP6PTR(op, p1, p2, p3, p4, p5, size, p6ptr);                                       \
    rettype retval = (rettype)NemuOGLFlush();

#define NEMU_OGL_GEN_SYNC_OP7_PTR_RET(rettype, op, p1, p2, p3, p4, p5, p6, size, p7ptr)             \
    NEMU_OGL_GEN_OP7PTR(op, p1, p2, p3, p4, p5, p6, size, p7ptr);                                   \
    rettype retval = (rettype)NemuOGLFlush();


/* Sync operation whose last parameter is a block of memory in which results are returned and return a value */
#define NEMU_OGL_GEN_SYNC_OP2_PASS_PTR_RET(rettype, op, p1, size, p2ptr)                            \
    NEMU_OGL_GEN_OP1(op, p1);                                                                       \
    rettype retval = (rettype)NemuOGLFlushPtr(p2ptr, size);

#define NEMU_OGL_GEN_SYNC_OP4_PASS_PTR_RET(rettype, op, p1, p2, p3, size, p4ptr)                    \
    NEMU_OGL_GEN_OP3(op, p1, p2, p3);                                                               \
    rettype retval = (rettype)NemuOGLFlushPtr(p4ptr, size);

#define NEMU_OGL_GEN_SYNC_OP5_PASS_PTR_RET(rettype, op, p1, p2, p3, p4, size, p5ptr)                \
    NEMU_OGL_GEN_OP4(op, p1, p2, p3, p4);                                                           \
    rettype retval = (rettype)NemuOGLFlushPtr(p5ptr, size);

#define NEMU_OGL_GEN_SYNC_OP6_PASS_PTR_RET(rettype, op, p1, p2, p3, p4, p5, size, p6ptr)            \
    NEMU_OGL_GEN_OP5(op, p1, p2, p3, p4, p5);                                                       \
    rettype retval = (rettype)NemuOGLFlushPtr(p6ptr, size);

#define NEMU_OGL_GEN_SYNC_OP7_PASS_PTR_RET(rettype, op, p1, p2, p3, p4, p5, p6, size, p7ptr)        \
    NEMU_OGL_GEN_OP6(op, p1, p2, p3, p4, p5, p6);                                                   \
    rettype retval = (rettype)NemuOGLFlushPtr(p7ptr, size);


/* Generate async functions elements in the command queue */
#define GL_GEN_FUNC(Function)                                                       \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (void)                                               \
    {                                                                               \
        NEMU_OGL_GEN_OP(Function);                                                  \
    }

#define GL_GEN_FUNC1(Function, Type)                                                \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type a)                                             \
    {                                                                               \
        NEMU_OGL_GEN_OP1(Function, a);                                              \
    }

#define GL_GEN_FUNC1V(Function, Type)                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type a)                                             \
    {                                                                               \
        NEMU_OGL_GEN_OP1(Function, a);                                              \
    }                                                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function)##v (const Type *v)                                   \
    {                                                                               \
        NEMU_OGL_GEN_OP1(Function, v[0]);                                           \
    }                                                                               \

#define GL_GEN_FUNC2(Function, Type)                                                \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type a, Type b)                                     \
    {                                                                               \
        NEMU_OGL_GEN_OP2(Function, a, b);                                           \
    }

#define GL_GEN_FUNC2V(Function, Type)                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type a, Type b)                                     \
    {                                                                               \
        NEMU_OGL_GEN_OP2(Function, a, b);                                           \
    }                                                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function)##v (const Type *v)                                   \
    {                                                                               \
        NEMU_OGL_GEN_OP2(Function, v[0], v[1]);                                     \
    }                                                                               \

#define GL_GEN_FUNC3(Function, Type)                                                \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type a, Type b, Type c)                             \
    {                                                                               \
        NEMU_OGL_GEN_OP3(Function, a, b, c);                                        \
    }

#define GL_GEN_FUNC3V(Function, Type)                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type a, Type b, Type c)                             \
    {                                                                               \
        NEMU_OGL_GEN_OP3(Function, a, b, c);                                        \
    }                                                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function)##v (const Type *v)                                   \
    {                                                                               \
        NEMU_OGL_GEN_OP3(Function, v[0], v[1], v[2]);                               \
    }                                                                               \

#define GL_GEN_FUNC4(Function, Type)                                                \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type a, Type b, Type c, Type d)                     \
    {                                                                               \
        NEMU_OGL_GEN_OP4(Function, a, b, c, d);                                     \
    }

#define GL_GEN_FUNC4V(Function, Type)                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type a, Type b, Type c, Type d)                     \
    {                                                                               \
        NEMU_OGL_GEN_OP4(Function, a, b, c, d);                                     \
    }                                                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function)##v (const Type *v)                                   \
    {                                                                               \
        NEMU_OGL_GEN_OP4(Function, v[0], v[1], v[2], v[3]);                         \
    }                                                                               \

#define GL_GEN_FUNC6(Function, Type)                                                \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type a, Type b, Type c, Type d, Type e, Type f)     \
    {                                                                               \
        NEMU_OGL_GEN_OP6(Function, a, b, c, d, e, f);                               \
    }

#define GL_GEN_VPAR_FUNC2(Function, Type1, Type2)                                   \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b)                                   \
    {                                                                               \
        NEMU_OGL_GEN_OP2(Function, a, b);                                           \
    }

#define GL_GEN_VPAR_FUNC2V(Function, Type1, Type2)                                  \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b)                                   \
    {                                                                               \
        NEMU_OGL_GEN_OP2(Function, a, b);                                           \
    }                                                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function)##v (Type1 a, const Type2 *v)                         \
    {                                                                               \
        NEMU_OGL_GEN_OP3(Function, a, v[0], v[1]);                                  \
    }                                                                               \

#define GL_GEN_VPAR_FUNC3(Function, Type1, Type2, Type3)                            \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b, Type3 c)                          \
    {                                                                               \
        NEMU_OGL_GEN_OP3(Function, a, b, c);                                        \
    }

#define GL_GEN_VPAR_FUNC3V(Function, Type1, Type2, Type3)                           \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b, Type3 c)                          \
    {                                                                               \
        NEMU_OGL_GEN_OP3(Function, a, b, c);                                        \
    }                                                                               \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function)##v (Type1 a, Type2 b, const Type3 *v)                \
    {                                                                               \
        NEMU_OGL_GEN_OP3(Function, a, v[0], v[1]);                                  \
    }                                                                               \

#define GL_GEN_VPAR_FUNC4(Function, Type1, Type2, Type3, Type4)                     \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b, Type3 c, Type4 d)                 \
    {                                                                               \
        NEMU_OGL_GEN_OP4(Function, a, b, c, d);                                     \
    }

#define GL_GEN_VPAR_FUNC5(Function, Type1, Type2, Type3, Type4, Type5)              \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b, Type3 c, Type4 d, Type5 e)        \
    {                                                                               \
        NEMU_OGL_GEN_OP5(Function, a, b, c, d, e);                                  \
    }

#define GL_GEN_VPAR_FUNC6(Function, Type1, Type2, Type3, Type4, Type5, Type6)       \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b, Type3 c, Type4 d, Type5 e, Type6 f)        \
    {                                                                               \
        NEMU_OGL_GEN_OP6(Function, a, b, c, d, e, f);                               \
    }

#define GL_GEN_VPAR_FUNC7(Function, Type1, Type2, Type3, Type4, Type5, Type6, Type7)       \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b, Type3 c, Type4 d, Type5 e, Type6 f, Type7 g)        \
    {                                                                               \
        NEMU_OGL_GEN_OP7(Function, a, b, c, d, e, f, g);                               \
    }

#define GL_GEN_VPAR_FUNC8(Function, Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8)       \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b, Type3 c, Type4 d, Type5 e, Type6 f, Type7 g, Type8 h)        \
    {                                                                               \
        NEMU_OGL_GEN_OP8(Function, a, b, c, d, e, f, g, h);                               \
    }

#define GL_GEN_VPAR_FUNC9(Function, Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8 ,Type9)       \
    void APIENTRY NEMU_OGL_NAME_PREFIX(Function) (Type1 a, Type2 b, Type3 c, Type4 d, Type5 e, Type6 f, Type7 g, Type8 h, Type9 i)        \
    {                                                                               \
        NEMU_OGL_GEN_OP9(Function, a, b, c, d, e, f, g, h, i);                               \
    }

#elif NEMU_OGL_HOST_SIDE

/************************************************************************************************************
 * Host side macro's for generating OpenGL function calls from the packed commands in the command buffer.   *
 *                                                                                                          *
 ************************************************************************************************************/

#include <iprt/assert.h>

#define NEMU_OGL_NAME_PREFIX(Function)                    nemugl##Function

#ifdef NEMU_OGL_CMD_STRICT
#define NEMU_OGL_CHECK_MAGIC(pParVal)                       Assert(pParVal->Magic == NEMU_OGL_CMD_MAGIC)
#else
#define NEMU_OGL_CHECK_MAGIC(pParVal)
#endif

#define OGL_CMD(op, numpar)                                                                 \
    PNEMU_OGL_CMD pCmd = (PNEMU_OGL_CMD)pCmdBuffer;                                         \
    Assert(pCmd->enmOp == NEMU_OGL_OP_##op);                                                \
    Assert(pCmd->cParams == numpar);                                                        \
    uint8_t      *pParam = (uint8_t *)(pCmd+1);                                             \
    NOREF(pParam)

#define OGL_PARAM(Type, par)                                                                \
    Type         par;                                                                       \
    par = *(Type *)pParam;                                                                  \
    pParam += sizeof(par);                                                                  \
    pParam = RT_ALIGN_PT(pParam, NEMU_OGL_CMD_ALIGN, uint8_t *);

#define OGL_MEMPARAM(Type, par)                                                             \
    PNEMU_OGL_VAR_PARAM pParVal = (PNEMU_OGL_VAR_PARAM)pParam;                              \
    Type        *par;                                                                       \
    NEMU_OGL_CHECK_MAGIC(pParVal);                                                          \
    if (pParVal->cbParam)                                                                   \
        par = (Type *)(pParVal+1);                                                          \
    else                                                                                    \
        par = NULL;                                                                         \
    pParam += sizeof(*pParVal) + pParVal->cbParam;                                          \
    pParam = RT_ALIGN_PT(pParam, NEMU_OGL_CMD_ALIGN, uint8_t *);

#define OGL_MEMPARAM_NODEF(Type, par)                                                       \
    pParVal = (PNEMU_OGL_VAR_PARAM)pParam;                                                  \
    Type        *par;                                                                       \
    NEMU_OGL_CHECK_MAGIC(pParVal);                                                          \
    if (pParVal->cbParam)                                                                   \
        par = (Type *)(pParVal+1);                                                          \
    else                                                                                    \
        par = NULL;                                                                         \
    pParam += sizeof(*pParVal) + pParVal->cbParam;                                          \
    pParam = RT_ALIGN_PT(pParam, NEMU_OGL_CMD_ALIGN, uint8_t *);

#define NEMU_OGL_GEN_OP(op)                                                                 \
    OGL_CMD(op, 0);                                                                         \
    gl##op();

#define NEMU_OGL_GEN_OP1(op, Type1)                                                         \
    OGL_CMD(op, 1);                                                                         \
    OGL_PARAM(Type1, p1);                                                                   \
    gl##op(p1);

#define NEMU_OGL_GEN_OP2(op, Type1, Type2)                                                  \
    OGL_CMD(op, 2);                                                                         \
    OGL_PARAM(Type1, p1);                                                                   \
    OGL_PARAM(Type2, p2);                                                                   \
    gl##op(p1, p2);

#define NEMU_OGL_GEN_OP3(op, Type1, Type2, Type3)                                           \
    OGL_CMD(op, 3);                                                                         \
    OGL_PARAM(Type1, p1);                                                                   \
    OGL_PARAM(Type2, p2);                                                                   \
    OGL_PARAM(Type3, p3);                                                                   \
    gl##op(p1, p2, p3);

#define NEMU_OGL_GEN_OP4(op, Type1, Type2, Type3, Type4)                                    \
    OGL_CMD(op, 4);                                                                         \
    OGL_PARAM(Type1, p1);                                                                   \
    OGL_PARAM(Type2, p2);                                                                   \
    OGL_PARAM(Type3, p3);                                                                   \
    OGL_PARAM(Type4, p4);                                                                   \
    gl##op(p1, p2, p3, p4);

#define NEMU_OGL_GEN_OP5(op, Type1, Type2, Type3, Type4, Type5)                             \
    OGL_CMD(op, 5);                                                                         \
    OGL_PARAM(Type1, p1);                                                                   \
    OGL_PARAM(Type2, p2);                                                                   \
    OGL_PARAM(Type3, p3);                                                                   \
    OGL_PARAM(Type4, p4);                                                                   \
    OGL_PARAM(Type5, p5);                                                                   \
    gl##op(p1, p2, p3, p4, p5);

#define NEMU_OGL_GEN_OP6(op, Type1, Type2, Type3, Type4, Type5, Type6)                      \
    OGL_CMD(op, 6);                                                                         \
    OGL_PARAM(Type1, p1);                                                                   \
    OGL_PARAM(Type2, p2);                                                                   \
    OGL_PARAM(Type3, p3);                                                                   \
    OGL_PARAM(Type4, p4);                                                                   \
    OGL_PARAM(Type5, p5);                                                                   \
    OGL_PARAM(Type6, p6);                                                                   \
    gl##op(p1, p2, p3, p4, p5, p6);

#define NEMU_OGL_GEN_OP7(op, Type1, Type2, Type3, Type4, Type5, Type6, Type7)               \
    OGL_CMD(op, 7);                                                                         \
    OGL_PARAM(Type1, p1);                                                                   \
    OGL_PARAM(Type2, p2);                                                                   \
    OGL_PARAM(Type3, p3);                                                                   \
    OGL_PARAM(Type4, p4);                                                                   \
    OGL_PARAM(Type5, p5);                                                                   \
    OGL_PARAM(Type6, p6);                                                                   \
    OGL_PARAM(Type7, p7);                                                                   \
    gl##op(p1, p2, p3, p4, p5, p6, p7);

#define NEMU_OGL_GEN_OP8(op, Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8)        \
    OGL_CMD(op, 8);                                                                         \
    OGL_PARAM(Type1, p1);                                                                   \
    OGL_PARAM(Type2, p2);                                                                   \
    OGL_PARAM(Type3, p3);                                                                   \
    OGL_PARAM(Type4, p4);                                                                   \
    OGL_PARAM(Type5, p5);                                                                   \
    OGL_PARAM(Type6, p6);                                                                   \
    OGL_PARAM(Type7, p7);                                                                   \
    OGL_PARAM(Type8, p8);                                                                   \
    gl##op(p1, p2, p3, p4, p5, p6, p7, p8);


/* last parameter is a memory block */
#define NEMU_OGL_GEN_OP1PTR(op, Type1)                                                          \
    OGL_CMD(op, 1);                                                                             \
    OGL_MEMPARAM(Type1, p1);                                                                        \
    gl##op(p1);

#define NEMU_OGL_GEN_OP2PTR(op, Type1, Type2)                                                   \
    OGL_CMD(op, 2);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_MEMPARAM(Type2, p2);                                                                        \
    gl##op(p1, p2);

#define NEMU_OGL_GEN_OP3PTR(op, Type1, Type2, Type3)                                            \
    OGL_CMD(op, 3);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    OGL_MEMPARAM(Type3, p3);                                                                        \
    gl##op(p1, p2, p3);

#define NEMU_OGL_GEN_OP4PTR(op, Type1, Type2, Type3, Type4)                                     \
    OGL_CMD(op, 4);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    OGL_PARAM(Type3, p3);                                                                       \
    OGL_MEMPARAM(Type4, p4);                                                                        \
    gl##op(p1, p2, p3, p4);

#define NEMU_OGL_GEN_OP5PTR(op, Type1, Type2, Type3, Type4, Type5)                              \
    OGL_CMD(op, 5);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    OGL_PARAM(Type3, p3);                                                                       \
    OGL_PARAM(Type4, p4);                                                                       \
    OGL_MEMPARAM(Type5, p5);                                                                        \
    gl##op(p1, p2, p3, p4, p5);

#define NEMU_OGL_GEN_OP6PTR(op, Type1, Type2, Type3, Type4, Type5, Type6)                       \
    OGL_CMD(op, 6);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    OGL_PARAM(Type3, p3);                                                                       \
    OGL_PARAM(Type4, p4);                                                                       \
    OGL_PARAM(Type5, p5);                                                                       \
    OGL_MEMPARAM(Type6, p6);                                                                        \
    gl##op(p1, p2, p3, p4, p5, p6);

#define NEMU_OGL_GEN_OP7PTR(op, Type1, Type2, Type3, Type4, Type5, Type6, Type7)                \
    OGL_CMD(op, 7);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    OGL_PARAM(Type3, p3);                                                                       \
    OGL_PARAM(Type4, p4);                                                                       \
    OGL_PARAM(Type5, p5);                                                                       \
    OGL_PARAM(Type6, p6);                                                                       \
    OGL_MEMPARAM(Type7, p7);                                                                        \
    gl##op(p1, p2, p3, p4, p5, p6, p7);

#define NEMU_OGL_GEN_OP8PTR(op, Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8)         \
    OGL_CMD(op, 8);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    OGL_PARAM(Type3, p3);                                                                       \
    OGL_PARAM(Type4, p4);                                                                       \
    OGL_PARAM(Type5, p5);                                                                       \
    OGL_PARAM(Type6, p6);                                                                       \
    OGL_PARAM(Type7, p7);                                                                       \
    OGL_MEMPARAM(Type8, p8);                                                                        \
    gl##op(p1, p2, p3, p4, p5, p6, p7, p8);

#define NEMU_OGL_GEN_OP9PTR(op, Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9)  \
    OGL_CMD(op, 9);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    OGL_PARAM(Type3, p3);                                                                       \
    OGL_PARAM(Type4, p4);                                                                       \
    OGL_PARAM(Type5, p5);                                                                       \
    OGL_PARAM(Type6, p6);                                                                       \
    OGL_PARAM(Type7, p7);                                                                       \
    OGL_PARAM(Type8, p8);                                                                       \
    OGL_MEMPARAM(Type9, p9);                                                                        \
    gl##op(p1, p2, p3, p4, p5, p6, p7, p8 ,p9);

#define NEMU_OGL_GEN_OP10PTR(op, Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9, Type10)                  \
    OGL_CMD(op, 10);                                                                            \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    OGL_PARAM(Type3, p3);                                                                       \
    OGL_PARAM(Type4, p4);                                                                       \
    OGL_PARAM(Type5, p5);                                                                       \
    OGL_PARAM(Type6, p6);                                                                       \
    OGL_PARAM(Type7, p7);                                                                       \
    OGL_PARAM(Type8, p8);                                                                       \
    OGL_PARAM(Type9, p9);                                                                       \
    OGL_MEMPARAM(Type10, p10);                                                                       \
    gl##op(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);


/* two memory blocks */
#define NEMU_OGL_GEN_OP2PTRPTR(op, Type1, Type2)                                                \
    OGL_CMD(op, 2);                                                                             \
    OGL_MEMPARAM(Type1, p1);                                                                        \
    OGL_MEMPARAM_NODEF(Type2, p2);                                                                        \
    gl##op(p1, p2);

#define NEMU_OGL_GEN_OP3PTRPTR(op, Type1, Type2, Type3)                                         \
    OGL_CMD(op, 3);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_MEMPARAM(Type2, p2);                                                                        \
    OGL_MEMPARAM_NODEF(Type3, p3);                                                                        \
    gl##op(p1, p2, p3);

/* Note: sync operations always set the last error */
/* sync operation that returns a value */
#define NEMU_OGL_GEN_SYNC_OP_RET(rettype, op)                                                   \
    OGL_CMD(op, 0);                                                                             \
    pClient->lastretval = gl##op();

#define NEMU_OGL_GEN_SYNC_OP1_RET(rettype, op, Type1)                                              \
    OGL_CMD(op, 1);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    pClient->lastretval = gl##op(p1);

#define NEMU_OGL_GEN_SYNC_OP2_RET(rettype, op, Type1, Type2)                                          \
    OGL_CMD(op, 2);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    pClient->lastretval = gl##op(p1, p2);

#define NEMU_OGL_GEN_SYNC_OP3_RET(rettype, op, Type1, Type2, Type3)                                          \
    OGL_CMD(op, 3);                                                                             \
    OGL_PARAM(Type1, p1);                                                                       \
    OGL_PARAM(Type2, p2);                                                                       \
    OGL_MEMPARAM(Type3, p3);                                                                        \
    pClient->lastretval = gl##op(p1, p2, p3);

#define NEMU_OGL_GEN_SYNC_OP(op)                                                                    \
    NEMU_OGL_GEN_OP(op);

#define NEMU_OGL_GEN_SYNC_OP1(op, p1)                                                               \
    NEMU_OGL_GEN_OP1(op, p1);

#define NEMU_OGL_GEN_SYNC_OP2(op, p1, p2)                                                           \
    NEMU_OGL_GEN_OP2(op, p1, p2);


/* Sync operation whose last parameter is a block of memory */
#define NEMU_OGL_GEN_SYNC_OP2_PTR(op, p1, p2ptr)                                                    \
    NEMU_OGL_GEN_OP2PTR(op, p1, p2ptr);

#define NEMU_OGL_GEN_SYNC_OP5_PTR(op, p1, p2, p3, p4, p5ptr)                                        \
    NEMU_OGL_GEN_OP2PTR(op, p1, p2, p3, p4, size, p5ptr);

#define NEMU_OGL_GEN_SYNC_OP6_PTR(op, p1, p2, p3, p4, p5, p6ptr)                                    \
    NEMU_OGL_GEN_OP6PTR(op, p1, p2, p3, p4, p5, size, p6ptr);

#define NEMU_OGL_GEN_SYNC_OP7_PTR(op, p1, p2, p3, p4, p5, p6, p7ptr)                                \
    NEMU_OGL_GEN_OP7PTR(op, p1, p2, p3, p4, p5, p6, p7ptr);


/* Sync operation whose last parameter is a block of memory in which results are returned */
#define NEMU_OGL_GEN_SYNC_OP1_PASS_PTR(op, Type1)                                                   \
    OGL_CMD(op, 0);                                                                                 \
    Assert(pClient->pLastParam && pClient->cbLastParam);                                            \
    gl##op((Type1 *)pClient->pLastParam);

#define NEMU_OGL_GEN_SYNC_OP2_PASS_PTR(op, Type1, Type2)                                            \
    OGL_CMD(op, 1);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    Assert(pClient->pLastParam && pClient->cbLastParam);                                            \
    gl##op(p1, (Type2 *)pClient->pLastParam);

#define NEMU_OGL_GEN_SYNC_OP3_PASS_PTR(op, Type1, Type2, Type3)                                     \
    OGL_CMD(op, 2);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_PARAM(Type2, p2);                                                                           \
    Assert(pClient->pLastParam && pClient->cbLastParam);                                            \
    gl##op(p1, p2, (Type3 *)pClient->pLastParam);

#define NEMU_OGL_GEN_SYNC_OP4_PASS_PTR(op, Type1, Type2, Type3, Type4)                              \
    OGL_CMD(op, 3);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_PARAM(Type2, p2);                                                                           \
    OGL_PARAM(Type3, p3);                                                                           \
    Assert(pClient->pLastParam && pClient->cbLastParam);                                            \
    gl##op(p1, p2, p3, (Type4 *)pClient->pLastParam);

#define NEMU_OGL_GEN_SYNC_OP5_PASS_PTR(op, Type1, Type2, Type3, Type4, Type5)                       \
    OGL_CMD(op, 4);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_PARAM(Type2, p2);                                                                           \
    OGL_PARAM(Type3, p3);                                                                           \
    OGL_PARAM(Type4, p4);                                                                           \
    Assert(pClient->pLastParam && pClient->cbLastParam);                                            \
    gl##op(p1, p2, p3, p4, (Type5 *)pClient->pLastParam);

#define NEMU_OGL_GEN_SYNC_OP6_PASS_PTR(op, Type1, Type2, Type3, Type4, Type5, Type6)                \
    OGL_CMD(op, 5);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_PARAM(Type2, p2);                                                                           \
    OGL_PARAM(Type3, p3);                                                                           \
    OGL_PARAM(Type4, p4);                                                                           \
    OGL_PARAM(Type5, p5);                                                                           \
    Assert(pClient->pLastParam && pClient->cbLastParam);                                            \
    gl##op(p1, p2, p3, p4, p5, (Type6 *)pClient->pLastParam);

#define NEMU_OGL_GEN_SYNC_OP7_PASS_PTR(op, Type1, Type2, Type3, Type4, Type5, Type6, Type7)         \
    OGL_CMD(op, 6);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_PARAM(Type2, p2);                                                                           \
    OGL_PARAM(Type3, p3);                                                                           \
    OGL_PARAM(Type4, p4);                                                                           \
    OGL_PARAM(Type5, p5);                                                                           \
    OGL_PARAM(Type6, p6);                                                                           \
    Assert(pClient->pLastParam && pClient->cbLastParam);                                            \
    gl##op(p1, p2, p3, p4, p5, p6, (Type7 *)pClient->pLastParam);


/* Sync operation whose last parameter is a block of memory and returns a value */
#define NEMU_OGL_GEN_SYNC_OP2_PTR_RET(rettype, op, Type1, Type2)                                    \
    OGL_CMD(op, 2);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_MEMPARAM(Type2, p2);                                                                        \
    pClient->lastretval = gl##op(p1);

#define NEMU_OGL_GEN_SYNC_OP4_PTR_RET(rettype, op, Type1, Type2, Type3, Type4)                      \
    OGL_CMD(op, 4);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_PARAM(Type2, p2);                                                                           \
    OGL_PARAM(Type3, p3);                                                                           \
    OGL_MEMPARAM(Type4, p4);                                                                        \
    pClient->lastretval = gl##op(p1, p2, p3, p4);

#define NEMU_OGL_GEN_SYNC_OP5_PTR_RET(rettype, op, Type1, Type2, Type3, Type4, Type5)               \
    OGL_CMD(op, 5);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_PARAM(Type2, p2);                                                                           \
    OGL_PARAM(Type3, p3);                                                                           \
    OGL_PARAM(Type4, p4);                                                                           \
    OGL_MEMPARAM(Type5, p5);                                                                        \
    pClient->lastretval = gl##op(p1, p2, p3, p4, p5);

#define NEMU_OGL_GEN_SYNC_OP6_PTR_RET(rettype, op, Type1, Type2, Type3, Type4, Type5, Type6)        \
    OGL_CMD(op, 6);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_PARAM(Type2, p2);                                                                           \
    OGL_PARAM(Type3, p3);                                                                           \
    OGL_PARAM(Type4, p4);                                                                           \
    OGL_PARAM(Type5, p5);                                                                           \
    OGL_MEMPARAM(Type6, p6);                                                                        \
    pClient->lastretval = gl##op(p1, p2, p3, p4, p5, p6);

#define NEMU_OGL_GEN_SYNC_OP7_PTR_RET(rettype, op, Type1, Type2, Type3, Type4, Type5, Type6, Type7) \
    OGL_CMD(op, 7);                                                                                 \
    OGL_PARAM(Type1, p1);                                                                           \
    OGL_PARAM(Type2, p2);                                                                           \
    OGL_PARAM(Type3, p3);                                                                           \
    OGL_PARAM(Type4, p4);                                                                           \
    OGL_PARAM(Type5, p5);                                                                           \
    OGL_PARAM(Type6, p6);                                                                           \
    OGL_MEMPARAM(Type7, p7);                                                                        \
    pClient->lastretval = gl##op(p1, p2, p3, p4, p5, p6, p7);





/* Generate async functions elements in the command queue */
#define GL_GEN_FUNC(Function)                                                                           \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP(Function);                                                                      \
    }

#define GL_GEN_FUNC1(Function, Type)                                                                    \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP1(Function, Type);                                                               \
    }

#define GL_GEN_FUNC1V(Function, Type)       GL_GEN_FUNC1(Function, Type)

#define GL_GEN_FUNC2(Function, Type)                                                                    \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP2(Function, Type, Type);                                                         \
    }

#define GL_GEN_FUNC2V(Function, Type)       GL_GEN_FUNC2(Function, Type)

#define GL_GEN_FUNC3(Function, Type)                                                                    \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP3(Function, Type, Type, Type);                                                   \
    }

#define GL_GEN_FUNC3V(Function, Type)       GL_GEN_FUNC3(Function, Type)

#define GL_GEN_FUNC4(Function, Type)                                                                    \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP4(Function, Type, Type, Type, Type);                                             \
    }

#define GL_GEN_FUNC4V(Function, Type)       GL_GEN_FUNC4(Function, Type)

#define GL_GEN_FUNC6(Function, Type)                                                                    \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP6(Function, Type, Type, Type, Type, Type, Type);                                 \
    }

#define GL_GEN_VPAR_FUNC2(Function, Type1, Type2)                                                       \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP2(Function, Type1, Type2);                                                       \
    }

#define GL_GEN_VPAR_FUNC2V(Function, Type)       GL_GEN_VPAR_FUNC2(Function, Type)

#define GL_GEN_VPAR_FUNC3(Function, Type1, Type2, Type3)                                                \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP3(Function, Type1, Type2, Type3);                                                \
    }

#define GL_GEN_VPAR_FUNC3V(Function, Type)       GL_GEN_VPAR_FUNC3(Function, Type)

#define GL_GEN_VPAR_FUNC4(Function, Type1, Type2, Type3, Type4)                                         \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP4(Function, Type1, Type2, Type3, Type4);                                         \
    }

#define GL_GEN_VPAR_FUNC5(Function, Type1, Type2, Type3, Type4, Type5)                                  \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP5(Function, Type1, Type2, Type3, Type4 ,Type5);                                  \
    }

#define GL_GEN_VPAR_FUNC6(Function, Type1, Type2, Type3, Type4, Type5, Type6)                           \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP6(Function, Type1, Type2, Type3, Type4 ,Type5, Type6);                           \
    }

#define GL_GEN_VPAR_FUNC7(Function, Type1, Type2, Type3, Type4, Type5, Type6, Type7)                    \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP7(Function, Type1, Type2, Type3, Type4 ,Type5, Type6, Type7);                    \
    }

#define GL_GEN_VPAR_FUNC8(Function, Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8)             \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP8(Function, Type1, Type2, Type3, Type4 ,Type5, Type6, Type7, Type8);             \
    }

#define GL_GEN_VPAR_FUNC9(Function, Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8 ,Type9)      \
    void  NEMU_OGL_NAME_PREFIX(Function) (NEMUOGLCTX *pClient, uint8_t *pCmdBuffer)             \
    {                                                                                                   \
        NEMU_OGL_GEN_OP9(Function, Type1, Type2, Type3, Type4 ,Type5, Type6, Type7, Type8, Type9);      \
    }

#endif /* NEMU_OGL_HOST_SIDE */




/* OpenGL opcodes */
/* Note: keep all three tables in sync! */
typedef enum
{
    NEMU_OGL_OP_Illegal                     = 0,
    NEMU_OGL_OP_ArrayElement,
    NEMU_OGL_OP_Begin,
    NEMU_OGL_OP_BindTexture,
    NEMU_OGL_OP_BlendFunc,
    NEMU_OGL_OP_CallList,
    NEMU_OGL_OP_Color3b,
    NEMU_OGL_OP_Color3d,
    NEMU_OGL_OP_Color3f,
    NEMU_OGL_OP_Color3i,
    NEMU_OGL_OP_Color3s,
    NEMU_OGL_OP_Color3ub,
    NEMU_OGL_OP_Color3ui,
    NEMU_OGL_OP_Color3us,
    NEMU_OGL_OP_Color4b,
    NEMU_OGL_OP_Color4d,
    NEMU_OGL_OP_Color4f,
    NEMU_OGL_OP_Color4i,
    NEMU_OGL_OP_Color4s,
    NEMU_OGL_OP_Color4ub,
    NEMU_OGL_OP_Color4ui,
    NEMU_OGL_OP_Color4us,
    NEMU_OGL_OP_Clear,
    NEMU_OGL_OP_ClearAccum,
    NEMU_OGL_OP_ClearColor,
    NEMU_OGL_OP_ClearDepth,
    NEMU_OGL_OP_ClearIndex,
    NEMU_OGL_OP_ClearStencil,
    NEMU_OGL_OP_Accum,
    NEMU_OGL_OP_AlphaFunc,
    NEMU_OGL_OP_Vertex2d,
    NEMU_OGL_OP_Vertex2f,
    NEMU_OGL_OP_Vertex2i,
    NEMU_OGL_OP_Vertex2s,
    NEMU_OGL_OP_Vertex3d,
    NEMU_OGL_OP_Vertex3f,
    NEMU_OGL_OP_Vertex3i,
    NEMU_OGL_OP_Vertex3s,
    NEMU_OGL_OP_Vertex4d,
    NEMU_OGL_OP_Vertex4f,
    NEMU_OGL_OP_Vertex4i,
    NEMU_OGL_OP_Vertex4s,
    NEMU_OGL_OP_TexCoord1d,
    NEMU_OGL_OP_TexCoord1f,
    NEMU_OGL_OP_TexCoord1i,
    NEMU_OGL_OP_TexCoord1s,
    NEMU_OGL_OP_TexCoord2d,
    NEMU_OGL_OP_TexCoord2f,
    NEMU_OGL_OP_TexCoord2i,
    NEMU_OGL_OP_TexCoord2s,
    NEMU_OGL_OP_TexCoord3d,
    NEMU_OGL_OP_TexCoord3f,
    NEMU_OGL_OP_TexCoord3i,
    NEMU_OGL_OP_TexCoord3s,
    NEMU_OGL_OP_TexCoord4d,
    NEMU_OGL_OP_TexCoord4f,
    NEMU_OGL_OP_TexCoord4i,
    NEMU_OGL_OP_TexCoord4s,
    NEMU_OGL_OP_Normal3b,
    NEMU_OGL_OP_Normal3d,
    NEMU_OGL_OP_Normal3f,
    NEMU_OGL_OP_Normal3i,
    NEMU_OGL_OP_Normal3s,
    NEMU_OGL_OP_RasterPos2d,
    NEMU_OGL_OP_RasterPos2f,
    NEMU_OGL_OP_RasterPos2i,
    NEMU_OGL_OP_RasterPos2s,
    NEMU_OGL_OP_RasterPos3d,
    NEMU_OGL_OP_RasterPos3f,
    NEMU_OGL_OP_RasterPos3i,
    NEMU_OGL_OP_RasterPos3s,
    NEMU_OGL_OP_RasterPos4d,
    NEMU_OGL_OP_RasterPos4f,
    NEMU_OGL_OP_RasterPos4i,
    NEMU_OGL_OP_RasterPos4s,
    NEMU_OGL_OP_EvalCoord1d,
    NEMU_OGL_OP_EvalCoord1f,
    NEMU_OGL_OP_EvalCoord2d,
    NEMU_OGL_OP_EvalCoord2f,
    NEMU_OGL_OP_EvalPoint1,
    NEMU_OGL_OP_EvalPoint2,
    NEMU_OGL_OP_Indexd,
    NEMU_OGL_OP_Indexf,
    NEMU_OGL_OP_Indexi,
    NEMU_OGL_OP_Indexs,
    NEMU_OGL_OP_Indexub,
    NEMU_OGL_OP_Rotated,
    NEMU_OGL_OP_Rotatef,
    NEMU_OGL_OP_Scaled,
    NEMU_OGL_OP_Scalef,
    NEMU_OGL_OP_Translated,
    NEMU_OGL_OP_Translatef,
    NEMU_OGL_OP_DepthFunc,
    NEMU_OGL_OP_DepthMask,
    NEMU_OGL_OP_Finish,
    NEMU_OGL_OP_Flush,
    NEMU_OGL_OP_DeleteLists,
    NEMU_OGL_OP_CullFace,
    NEMU_OGL_OP_DeleteTextures,
    NEMU_OGL_OP_DepthRange,
    NEMU_OGL_OP_DisableClientState,
    NEMU_OGL_OP_EnableClientState,
    NEMU_OGL_OP_EvalMesh1,
    NEMU_OGL_OP_EvalMesh2,
    NEMU_OGL_OP_Fogf,
    NEMU_OGL_OP_Fogfv,
    NEMU_OGL_OP_Fogi,
    NEMU_OGL_OP_Fogiv,
    NEMU_OGL_OP_LightModelf,
    NEMU_OGL_OP_LightModelfv,
    NEMU_OGL_OP_LightModeli,
    NEMU_OGL_OP_LightModeliv,
    NEMU_OGL_OP_Lightf,
    NEMU_OGL_OP_Lightfv,
    NEMU_OGL_OP_Lighti,
    NEMU_OGL_OP_Lightiv,
    NEMU_OGL_OP_LineStipple,
    NEMU_OGL_OP_LineWidth,
    NEMU_OGL_OP_ListBase,
    NEMU_OGL_OP_DrawArrays,
    NEMU_OGL_OP_DrawBuffer,
    NEMU_OGL_OP_EdgeFlag,
    NEMU_OGL_OP_End,
    NEMU_OGL_OP_EndList,
    NEMU_OGL_OP_CopyTexImage1D,
    NEMU_OGL_OP_CopyTexImage2D,
    NEMU_OGL_OP_ColorMaterial,
    NEMU_OGL_OP_Materiali,
    NEMU_OGL_OP_Materialf,
    NEMU_OGL_OP_Materialfv,
    NEMU_OGL_OP_Materialiv,
    NEMU_OGL_OP_PopAttrib,
    NEMU_OGL_OP_PopClientAttrib,
    NEMU_OGL_OP_PopMatrix,
    NEMU_OGL_OP_PopName,
    NEMU_OGL_OP_PushAttrib,
    NEMU_OGL_OP_PushClientAttrib,
    NEMU_OGL_OP_PushMatrix,
    NEMU_OGL_OP_PushName,
    NEMU_OGL_OP_ReadBuffer,
    NEMU_OGL_OP_TexGendv,
    NEMU_OGL_OP_TexGenf,
    NEMU_OGL_OP_TexGend,
    NEMU_OGL_OP_TexGeni,
    NEMU_OGL_OP_TexEnvi,
    NEMU_OGL_OP_TexEnvf,
    NEMU_OGL_OP_TexEnviv,
    NEMU_OGL_OP_TexEnvfv,
    NEMU_OGL_OP_TexGeniv,
    NEMU_OGL_OP_TexGenfv,
    NEMU_OGL_OP_TexParameterf,
    NEMU_OGL_OP_TexParameteri,
    NEMU_OGL_OP_TexParameterfv,
    NEMU_OGL_OP_TexParameteriv,
    NEMU_OGL_OP_LoadIdentity,
    NEMU_OGL_OP_LoadName,
    NEMU_OGL_OP_LoadMatrixd,
    NEMU_OGL_OP_LoadMatrixf,
    NEMU_OGL_OP_StencilFunc,
    NEMU_OGL_OP_ShadeModel,
    NEMU_OGL_OP_StencilMask,
    NEMU_OGL_OP_StencilOp,
    NEMU_OGL_OP_Scissor,
    NEMU_OGL_OP_Viewport,
    NEMU_OGL_OP_Rectd,
    NEMU_OGL_OP_Rectf,
    NEMU_OGL_OP_Recti,
    NEMU_OGL_OP_Rects,
    NEMU_OGL_OP_Rectdv,
    NEMU_OGL_OP_Rectfv,
    NEMU_OGL_OP_Rectiv,
    NEMU_OGL_OP_Rectsv,
    NEMU_OGL_OP_MultMatrixd,
    NEMU_OGL_OP_MultMatrixf,
    NEMU_OGL_OP_NewList,
    NEMU_OGL_OP_Hint,
    NEMU_OGL_OP_IndexMask,
    NEMU_OGL_OP_InitNames,
    NEMU_OGL_OP_TexCoordPointer,
    NEMU_OGL_OP_VertexPointer,
    NEMU_OGL_OP_ColorPointer,
    NEMU_OGL_OP_EdgeFlagPointer,
    NEMU_OGL_OP_IndexPointer,
    NEMU_OGL_OP_NormalPointer,
    NEMU_OGL_OP_PolygonStipple,
    NEMU_OGL_OP_CallLists,
    NEMU_OGL_OP_ClipPlane,
    NEMU_OGL_OP_Frustum,
    NEMU_OGL_OP_GenTextures,
    NEMU_OGL_OP_Map1d,
    NEMU_OGL_OP_Map1f,
    NEMU_OGL_OP_Map2d,
    NEMU_OGL_OP_Map2f,
    NEMU_OGL_OP_MapGrid1d,
    NEMU_OGL_OP_MapGrid1f,
    NEMU_OGL_OP_MapGrid2d,
    NEMU_OGL_OP_MapGrid2f,
    NEMU_OGL_OP_CopyPixels,
    NEMU_OGL_OP_TexImage1D,
    NEMU_OGL_OP_TexImage2D,
    NEMU_OGL_OP_TexSubImage1D,
    NEMU_OGL_OP_TexSubImage2D,
    NEMU_OGL_OP_FeedbackBuffer,
    NEMU_OGL_OP_SelectBuffer,
    NEMU_OGL_OP_IsList,
    NEMU_OGL_OP_IsTexture,
    NEMU_OGL_OP_RenderMode,
    NEMU_OGL_OP_ReadPixels,
    NEMU_OGL_OP_IsEnabled,
    NEMU_OGL_OP_GenLists,
    NEMU_OGL_OP_PixelTransferf,
    NEMU_OGL_OP_PixelTransferi,
    NEMU_OGL_OP_PixelZoom,
    NEMU_OGL_OP_PixelStorei,
    NEMU_OGL_OP_PixelStoref,
    NEMU_OGL_OP_PixelMapfv,
    NEMU_OGL_OP_PixelMapuiv,
    NEMU_OGL_OP_PixelMapusv,
    NEMU_OGL_OP_PointSize,
    NEMU_OGL_OP_PolygonMode,
    NEMU_OGL_OP_PolygonOffset,
    NEMU_OGL_OP_PassThrough,
    NEMU_OGL_OP_Ortho,
    NEMU_OGL_OP_MatrixMode,
    NEMU_OGL_OP_LogicOp,
    NEMU_OGL_OP_ColorMask,
    NEMU_OGL_OP_CopyTexSubImage1D,
    NEMU_OGL_OP_CopyTexSubImage2D,
    NEMU_OGL_OP_FrontFace,
    NEMU_OGL_OP_Disable,
    NEMU_OGL_OP_Enable,
    NEMU_OGL_OP_PrioritizeTextures,
    NEMU_OGL_OP_GetBooleanv,
    NEMU_OGL_OP_GetDoublev,
    NEMU_OGL_OP_GetFloatv,
    NEMU_OGL_OP_GetIntegerv,
    NEMU_OGL_OP_GetLightfv,
    NEMU_OGL_OP_GetLightiv,
    NEMU_OGL_OP_GetMaterialfv,
    NEMU_OGL_OP_GetMaterialiv,
    NEMU_OGL_OP_GetPixelMapfv,
    NEMU_OGL_OP_GetPixelMapuiv,
    NEMU_OGL_OP_GetPixelMapusv,
    NEMU_OGL_OP_GetTexEnviv,
    NEMU_OGL_OP_GetTexEnvfv,
    NEMU_OGL_OP_GetTexGendv,
    NEMU_OGL_OP_GetTexGenfv,
    NEMU_OGL_OP_GetTexGeniv,
    NEMU_OGL_OP_GetTexParameterfv,
    NEMU_OGL_OP_GetTexParameteriv,
    NEMU_OGL_OP_GetClipPlane,
    NEMU_OGL_OP_GetPolygonStipple,
    NEMU_OGL_OP_GetTexLevelParameterfv,
    NEMU_OGL_OP_GetTexLevelParameteriv,
    NEMU_OGL_OP_GetTexImage,

    /* Windows ICD exports */
    NEMU_OGL_OP_DrvReleaseContext,
    NEMU_OGL_OP_DrvCreateContext,
    NEMU_OGL_OP_DrvDeleteContext,
    NEMU_OGL_OP_DrvCopyContext,
    NEMU_OGL_OP_DrvSetContext,
    NEMU_OGL_OP_DrvCreateLayerContext,
    NEMU_OGL_OP_DrvShareLists,
    NEMU_OGL_OP_DrvDescribeLayerPlane,
    NEMU_OGL_OP_DrvSetLayerPaletteEntries,
    NEMU_OGL_OP_DrvGetLayerPaletteEntries,
    NEMU_OGL_OP_DrvRealizeLayerPalette,
    NEMU_OGL_OP_DrvSwapLayerBuffers,
    NEMU_OGL_OP_DrvDescribePixelFormat,
    NEMU_OGL_OP_DrvSetPixelFormat,
    NEMU_OGL_OP_DrvSwapBuffers,

    /* OpenGL Extensions */
    NEMU_OGL_OP_wglSwapIntervalEXT,
    NEMU_OGL_OP_wglGetSwapIntervalEXT,

    NEMU_OGL_OP_Last,

    NEMU_OGL_OP_SizeHack                     = 0x7fffffff
} NEMU_OGL_OP;

#if defined(DEBUG) && defined(NEMU_OGL_WITH_CMD_STRINGS)
static const char *pszNemuOGLCmd[NEMU_OGL_OP_Last] =
{
    "ILLEGAL",
    "glArrayElement",
    "glBegin",
    "glBindTexture",
    "glBlendFunc",
    "glCallList",
    "glColor3b",
    "glColor3d",
    "glColor3f",
    "glColor3i",
    "glColor3s",
    "glColor3ub",
    "glColor3ui",
    "glColor3us",
    "glColor4b",
    "glColor4d",
    "glColor4f",
    "glColor4i",
    "glColor4s",
    "glColor4ub",
    "glColor4ui",
    "glColor4us",
    "glClear",
    "glClearAccum",
    "glClearColor",
    "glClearDepth",
    "glClearIndex",
    "glClearStencil",
    "glAccum",
    "glAlphaFunc",
    "glVertex2d",
    "glVertex2f",
    "glVertex2i",
    "glVertex2s",
    "glVertex3d",
    "glVertex3f",
    "glVertex3i",
    "glVertex3s",
    "glVertex4d",
    "glVertex4f",
    "glVertex4i",
    "glVertex4s",
    "glTexCoord1d",
    "glTexCoord1f",
    "glTexCoord1i",
    "glTexCoord1s",
    "glTexCoord2d",
    "glTexCoord2f",
    "glTexCoord2i",
    "glTexCoord2s",
    "glTexCoord3d",
    "glTexCoord3f",
    "glTexCoord3i",
    "glTexCoord3s",
    "glTexCoord4d",
    "glTexCoord4f",
    "glTexCoord4i",
    "glTexCoord4s",
    "glNormal3b",
    "glNormal3d",
    "glNormal3f",
    "glNormal3i",
    "glNormal3s",
    "glRasterPos2d",
    "glRasterPos2f",
    "glRasterPos2i",
    "glRasterPos2s",
    "glRasterPos3d",
    "glRasterPos3f",
    "glRasterPos3i",
    "glRasterPos3s",
    "glRasterPos4d",
    "glRasterPos4f",
    "glRasterPos4i",
    "glRasterPos4s",
    "glEvalCoord1d",
    "glEvalCoord1f",
    "glEvalCoord2d",
    "glEvalCoord2f",
    "glEvalPoint1",
    "glEvalPoint2",
    "glIndexd",
    "glIndexf",
    "glIndexi",
    "glIndexs",
    "glIndexub",
    "glRotated",
    "glRotatef",
    "glScaled",
    "glScalef",
    "glTranslated",
    "glTranslatef",
    "glDepthFunc",
    "glDepthMask",
    "glFinish",
    "glFlush",
    "glDeleteLists",
    "glCullFace",
    "glDeleteTextures",
    "glDepthRange",
    "glDisableClientState",
    "glEnableClientState",
    "glEvalMesh1",
    "glEvalMesh2",
    "glFogf",
    "glFogfv",
    "glFogi",
    "glFogiv",
    "glLightModelf",
    "glLightModelfv",
    "glLightModeli",
    "glLightModeliv",
    "glLightf",
    "glLightfv",
    "glLighti",
    "glLightiv",
    "glLineStipple",
    "glLineWidth",
    "glListBase",
    "glDrawArrays",
    "glDrawBuffer",
    "glEdgeFlag",
    "glEnd",
    "glEndList",
    "glCopyTexImage1D",
    "glCopyTexImage2D",
    "glColorMaterial",
    "glMateriali",
    "glMaterialf",
    "glMaterialfv",
    "glMaterialiv",
    "glPopAttrib",
    "glPopClientAttrib",
    "glPopMatrix",
    "glPopName",
    "glPushAttrib",
    "glPushClientAttrib",
    "glPushMatrix",
    "glPushName",
    "glReadBuffer",
    "glTexGendv",
    "glTexGenf",
    "glTexGend",
    "glTexGeni",
    "glTexEnvi",
    "glTexEnvf",
    "glTexEnviv",
    "glTexEnvfv",
    "glTexGeniv",
    "glTexGenfv",
    "glTexParameterf",
    "glTexParameteri",
    "glTexParameterfv",
    "glTexParameteriv",
    "glLoadIdentity",
    "glLoadName",
    "glLoadMatrixd",
    "glLoadMatrixf",
    "glStencilFunc",
    "glShadeModel",
    "glStencilMask",
    "glStencilOp",
    "glScissor",
    "glViewport",
    "glRectd",
    "glRectf",
    "glRecti",
    "glRects",
    "glRectdv",
    "glRectfv",
    "glRectiv",
    "glRectsv",
    "glMultMatrixd",
    "glMultMatrixf",
    "glNewList",
    "glHint",
    "glIndexMask",
    "glInitNames",
    "glTexCoordPointer",
    "glVertexPointer",
    "glColorPointer",
    "glEdgeFlagPointer",
    "glIndexPointer",
    "glNormalPointer",
    "glPolygonStipple",
    "glCallLists",
    "glClipPlane",
    "glFrustum",
    "glGenTextures",
    "glMap1d",
    "glMap1f",
    "glMap2d",
    "glMap2f",
    "glMapGrid1d",
    "glMapGrid1f",
    "glMapGrid2d",
    "glMapGrid2f",
    "glCopyPixels",
    "glTexImage1D",
    "glTexImage2D",
    "glTexSubImage1D",
    "glTexSubImage2D",
    "glFeedbackBuffer",
    "glSelectBuffer",
    "glIsList",
    "glIsTexture",
    "glRenderMode",
    "glReadPixels",
    "glIsEnabled",
    "glGenLists",
    "glPixelTransferf",
    "glPixelTransferi",
    "glPixelZoom",
    "glPixelStorei",
    "glPixelStoref",
    "glPixelMapfv",
    "glPixelMapuiv",
    "glPixelMapusv",
    "glPointSize",
    "glPolygonMode",
    "glPolygonOffset",
    "glPassThrough",
    "glOrtho",
    "glMatrixMode",
    "glLogicOp",
    "glColorMask",
    "glCopyTexSubImage1D",
    "glCopyTexSubImage2D",
    "glFrontFace",
    "glDisable",
    "glEnable",
    "glPrioritizeTextures",
    "glGetBooleanv",
    "glGetDoublev",
    "glGetFloatv",
    "glGetIntegerv",
    "glGetLightfv",
    "glGetLightiv",
    "glGetMaterialfv",
    "glGetMaterialiv",
    "glGetPixelMapfv",
    "glGetPixelMapuiv",
    "glGetPixelMapusv",
    "glGetTexEnviv",
    "glGetTexEnvfv",
    "glGetTexGendv",
    "glGetTexGenfv",
    "glGetTexGeniv",
    "glGetTexParameterfv",
    "glGetTexParameteriv",
    "glGetClipPlane",
    "glGetPolygonStipple",
    "glGetTexLevelParameterfv",
    "glGetTexLevelParameteriv",
    "glGetTexImage",

    /* Windows ICD exports */
    "DrvReleaseContext",
    "DrvCreateContext",
    "DrvDeleteContext",
    "DrvCopyContext",
    "DrvSetContext",
    "DrvCreateLayerContext",
    "DrvShareLists",
    "DrvDescribeLayerPlane",
    "DrvSetLayerPaletteEntries",
    "DrvGetLayerPaletteEntries",
    "DrvRealizeLayerPalette",
    "DrvSwapLayerBuffers",
    "DrvDescribePixelFormat",
    "DrvSetPixelFormat",
    "DrvSwapBuffers",

    /* OpenGL Extensions */
    "wglSwapIntervalEXT",
    "wglGetSwapIntervalEXT",
};
#endif

#ifdef NEMU_OGL_WITH_FUNCTION_WRAPPERS
/* OpenGL function wrappers. */
static PFN_NEMUGLWRAPPER pfnOGLWrapper[NEMU_OGL_OP_Last] =
{
    NULL,
    nemuglArrayElement,
    nemuglBegin,
    nemuglBindTexture,
    nemuglBlendFunc,
    nemuglCallList,
    nemuglColor3b,
    nemuglColor3d,
    nemuglColor3f,
    nemuglColor3i,
    nemuglColor3s,
    nemuglColor3ub,
    nemuglColor3ui,
    nemuglColor3us,
    nemuglColor4b,
    nemuglColor4d,
    nemuglColor4f,
    nemuglColor4i,
    nemuglColor4s,
    nemuglColor4ub,
    nemuglColor4ui,
    nemuglColor4us,
    nemuglClear,
    nemuglClearAccum,
    nemuglClearColor,
    nemuglClearDepth,
    nemuglClearIndex,
    nemuglClearStencil,
    nemuglAccum,
    nemuglAlphaFunc,
    nemuglVertex2d,
    nemuglVertex2f,
    nemuglVertex2i,
    nemuglVertex2s,
    nemuglVertex3d,
    nemuglVertex3f,
    nemuglVertex3i,
    nemuglVertex3s,
    nemuglVertex4d,
    nemuglVertex4f,
    nemuglVertex4i,
    nemuglVertex4s,
    nemuglTexCoord1d,
    nemuglTexCoord1f,
    nemuglTexCoord1i,
    nemuglTexCoord1s,
    nemuglTexCoord2d,
    nemuglTexCoord2f,
    nemuglTexCoord2i,
    nemuglTexCoord2s,
    nemuglTexCoord3d,
    nemuglTexCoord3f,
    nemuglTexCoord3i,
    nemuglTexCoord3s,
    nemuglTexCoord4d,
    nemuglTexCoord4f,
    nemuglTexCoord4i,
    nemuglTexCoord4s,
    nemuglNormal3b,
    nemuglNormal3d,
    nemuglNormal3f,
    nemuglNormal3i,
    nemuglNormal3s,
    nemuglRasterPos2d,
    nemuglRasterPos2f,
    nemuglRasterPos2i,
    nemuglRasterPos2s,
    nemuglRasterPos3d,
    nemuglRasterPos3f,
    nemuglRasterPos3i,
    nemuglRasterPos3s,
    nemuglRasterPos4d,
    nemuglRasterPos4f,
    nemuglRasterPos4i,
    nemuglRasterPos4s,
    nemuglEvalCoord1d,
    nemuglEvalCoord1f,
    nemuglEvalCoord2d,
    nemuglEvalCoord2f,
    nemuglEvalPoint1,
    nemuglEvalPoint2,
    nemuglIndexd,
    nemuglIndexf,
    nemuglIndexi,
    nemuglIndexs,
    nemuglIndexub,
    nemuglRotated,
    nemuglRotatef,
    nemuglScaled,
    nemuglScalef,
    nemuglTranslated,
    nemuglTranslatef,
    nemuglDepthFunc,
    nemuglDepthMask,
    nemuglFinish,
    nemuglFlush,
    nemuglDeleteLists,
    nemuglCullFace,
    nemuglDeleteTextures,
    nemuglDepthRange,
    nemuglDisableClientState,
    nemuglEnableClientState,
    nemuglEvalMesh1,
    nemuglEvalMesh2,
    nemuglFogf,
    nemuglFogfv,
    nemuglFogi,
    nemuglFogiv,
    nemuglLightModelf,
    nemuglLightModelfv,
    nemuglLightModeli,
    nemuglLightModeliv,
    nemuglLightf,
    nemuglLightfv,
    nemuglLighti,
    nemuglLightiv,
    nemuglLineStipple,
    nemuglLineWidth,
    nemuglListBase,
    nemuglDrawArrays,
    nemuglDrawBuffer,
    nemuglEdgeFlag,
    nemuglEnd,
    nemuglEndList,
    nemuglCopyTexImage1D,
    nemuglCopyTexImage2D,
    nemuglColorMaterial,
    nemuglMateriali,
    nemuglMaterialf,
    nemuglMaterialfv,
    nemuglMaterialiv,
    nemuglPopAttrib,
    nemuglPopClientAttrib,
    nemuglPopMatrix,
    nemuglPopName,
    nemuglPushAttrib,
    nemuglPushClientAttrib,
    nemuglPushMatrix,
    nemuglPushName,
    nemuglReadBuffer,
    nemuglTexGendv,
    nemuglTexGenf,
    nemuglTexGend,
    nemuglTexGeni,
    nemuglTexEnvi,
    nemuglTexEnvf,
    nemuglTexEnviv,
    nemuglTexEnvfv,
    nemuglTexGeniv,
    nemuglTexGenfv,
    nemuglTexParameterf,
    nemuglTexParameteri,
    nemuglTexParameterfv,
    nemuglTexParameteriv,
    nemuglLoadIdentity,
    nemuglLoadName,
    nemuglLoadMatrixd,
    nemuglLoadMatrixf,
    nemuglStencilFunc,
    nemuglShadeModel,
    nemuglStencilMask,
    nemuglStencilOp,
    nemuglScissor,
    nemuglViewport,
    nemuglRectd,
    nemuglRectf,
    nemuglRecti,
    nemuglRects,
    nemuglRectdv,
    nemuglRectfv,
    nemuglRectiv,
    nemuglRectsv,
    nemuglMultMatrixd,
    nemuglMultMatrixf,
    nemuglNewList,
    nemuglHint,
    nemuglIndexMask,
    nemuglInitNames,
    nemuglTexCoordPointer,
    nemuglVertexPointer,
    nemuglColorPointer,
    nemuglEdgeFlagPointer,
    nemuglIndexPointer,
    nemuglNormalPointer,
    nemuglPolygonStipple,
    nemuglCallLists,
    nemuglClipPlane,
    nemuglFrustum,
    nemuglGenTextures,
    nemuglMap1d,
    nemuglMap1f,
    nemuglMap2d,
    nemuglMap2f,
    nemuglMapGrid1d,
    nemuglMapGrid1f,
    nemuglMapGrid2d,
    nemuglMapGrid2f,
    nemuglCopyPixels,
    nemuglTexImage1D,
    nemuglTexImage2D,
    nemuglTexSubImage1D,
    nemuglTexSubImage2D,
    nemuglFeedbackBuffer,
    nemuglSelectBuffer,
    nemuglIsList,
    nemuglIsTexture,
    nemuglRenderMode,
    nemuglReadPixels,
    nemuglIsEnabled,
    nemuglGenLists,
    nemuglPixelTransferf,
    nemuglPixelTransferi,
    nemuglPixelZoom,
    nemuglPixelStorei,
    nemuglPixelStoref,
    nemuglPixelMapfv,
    nemuglPixelMapuiv,
    nemuglPixelMapusv,
    nemuglPointSize,
    nemuglPolygonMode,
    nemuglPolygonOffset,
    nemuglPassThrough,
    nemuglOrtho,
    nemuglMatrixMode,
    nemuglLogicOp,
    nemuglColorMask,
    nemuglCopyTexSubImage1D,
    nemuglCopyTexSubImage2D,
    nemuglFrontFace,
    nemuglDisable,
    nemuglEnable,
    nemuglPrioritizeTextures,
    nemuglGetBooleanv,
    nemuglGetDoublev,
    nemuglGetFloatv,
    nemuglGetIntegerv,
    nemuglGetLightfv,
    nemuglGetLightiv,
    nemuglGetMaterialfv,
    nemuglGetMaterialiv,
    nemuglGetPixelMapfv,
    nemuglGetPixelMapuiv,
    nemuglGetPixelMapusv,
    nemuglGetTexEnviv,
    nemuglGetTexEnvfv,
    nemuglGetTexGendv,
    nemuglGetTexGenfv,
    nemuglGetTexGeniv,
    nemuglGetTexParameterfv,
    nemuglGetTexParameteriv,
    nemuglGetClipPlane,
    nemuglGetPolygonStipple,
    nemuglGetTexLevelParameterfv,
    nemuglGetTexLevelParameteriv,
    nemuglGetTexImage,

    /* Windows ICD exports */
    nemuglDrvReleaseContext,
    nemuglDrvCreateContext,
    nemuglDrvDeleteContext,
    nemuglDrvCopyContext,
    nemuglDrvSetContext,
    nemuglDrvCreateLayerContext,
    nemuglDrvShareLists,
    nemuglDrvDescribeLayerPlane,
    nemuglDrvSetLayerPaletteEntries,
    nemuglDrvGetLayerPaletteEntries,
    nemuglDrvRealizeLayerPalette,
    nemuglDrvSwapLayerBuffers,
    nemuglDrvDescribePixelFormat,
    nemuglDrvSetPixelFormat,
    nemuglDrvSwapBuffers,

#ifdef RT_OS_WINDOWS
    /* OpenGL Extensions */
    nemuwglSwapIntervalEXT,
    nemuwglGetSwapIntervalEXT,
#endif
};
#endif


#ifdef NEMU_OGL_WITH_EXTENSION_ARRAY
typedef struct
{
    const char *pszExtName;
    const char *pszExtFunctionName;
#ifdef NEMU_OGL_GUEST_SIDE
    RTUINTPTR   pfnFunction;
#else
    RTUINTPTR  *ppfnFunction;
#endif
    bool        fAvailable;
} OPENGL_EXT, *POPENGL_EXT;

#ifdef NEMU_OGL_GUEST_SIDE
#define NEMU_OGL_EXTENSION(a)   (RTUINTPTR)a
#else
#define NEMU_OGL_EXTENSION(a)   (RTUINTPTR *)&pfn##a

static PFNWGLSWAPINTERVALEXTPROC        pfnwglSwapIntervalEXT       = NULL;
static PFNWGLGETSWAPINTERVALEXTPROC     pfnwglGetSwapIntervalEXT    = NULL;

#endif

static OPENGL_EXT OpenGLExtensions[] =
{
    {   "WGL_EXT_swap_control",             "wglSwapIntervalEXT",               NEMU_OGL_EXTENSION(wglSwapIntervalEXT),                      false },
    {   "WGL_EXT_swap_control",             "wglGetSwapIntervalEXT",            NEMU_OGL_EXTENSION(wglGetSwapIntervalEXT),                   false },
};
#endif /* NEMU_OGL_WITH_EXTENSION_ARRAY */

#endif

