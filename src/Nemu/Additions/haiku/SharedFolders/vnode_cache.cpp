/* $Id: vnode_cache.cpp $ */
/** @file
 * Shared folders - Haiku Guest Additions, vnode cache header.
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

#include "nemusf.h"
#include "OpenHashTable.h"

struct HashTableDefinition
{
    typedef uint32       KeyType;
    typedef nemusf_vnode ValueType;

    size_t HashKey(uint32 key) const
    {
        return key;
    }

    size_t Hash(nemusf_vnode* value) const
    {
        return HashKey(value->vnode);
    }

    bool Compare(uint32 key, nemusf_vnode* value) const
    {
        return value->vnode == key;
    }

    nemusf_vnode*& GetLink(nemusf_vnode* value) const
    {
        return value->next;
    }
};

static BOpenHashTable<HashTableDefinition> g_cache;
static ino_t g_nextVnid = 1;
mutex g_vnodeCacheLock;


extern "C" status_t nemusf_new_vnode(PVBSFMAP map, PSHFLSTRING path, PSHFLSTRING name, nemusf_vnode** p)
{
    nemusf_vnode* vn = (nemusf_vnode*)malloc(sizeof(nemusf_vnode));
    if (vn == NULL)
        return B_NO_MEMORY;

    dprintf("creating new vnode at %p with path=%p (%s)\n", vn, path->String.utf8, path->String.utf8);
    vn->map = map;
    vn->path = path;
    if (name)
        vn->name = name;
    else
    {
        const char* cname = strrchr((char*)path->String.utf8, '/');
        if (!cname)
            vn->name = path; // no slash, assume this *is* the filename
        else
            vn->name = make_shflstring(cname);
    }

    if (mutex_lock(&g_vnodeCacheLock) < B_OK)
    {
        free(vn);
        return B_ERROR;
    }

    vn->vnode = g_nextVnid++;
    *p = vn;
    dprintf("nemusf: allocated %p (path=%p name=%p)\n", vn, vn->path, vn->name);
    status_t rv = g_cache.Insert(vn);

    mutex_unlock(&g_vnodeCacheLock);

    return rv;
}


extern "C" status_t nemusf_get_vnode(fs_volume* volume, ino_t id, fs_vnode* vnode,
                                     int* _type, uint32* _flags, bool reenter)
{
    nemusf_vnode* vn = g_cache.Lookup(id);
    if (vn)
    {
        vnode->private_node = vn;
        return B_OK;
    }
    return B_ERROR;
}


extern "C" status_t nemusf_put_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter)
{
    g_cache.Remove((nemusf_vnode*)vnode->private_node);
}

