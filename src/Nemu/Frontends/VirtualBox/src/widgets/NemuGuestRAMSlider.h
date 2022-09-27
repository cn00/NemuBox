/* $Id: NemuGuestRAMSlider.h $ */
/** @file
 * Nemu Qt GUI - NemuGuestRAMSlider class declaration.
 */

/*
 * Copyright (C) 2009-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


#ifndef __NemuGuestRAMSlider_h__
#define __NemuGuestRAMSlider_h__

/* Nemu includes */
#include "QIAdvancedSlider.h"

class NemuGuestRAMSlider: public QIAdvancedSlider
{
public:
    NemuGuestRAMSlider (QWidget *aParent = 0);
    NemuGuestRAMSlider (Qt::Orientation aOrientation, QWidget *aParent = 0);

    uint minRAM() const;
    uint maxRAMOpt() const;
    uint maxRAMAlw() const;
    uint maxRAM() const;

private:
    /* Private methods */
    void init();
    int calcPageStep (int aMax) const;

    /* Private member vars */
    uint mMinRAM;
    uint mMaxRAMOpt;
    uint mMaxRAMAlw;
    uint mMaxRAM;
};

#endif /* __NemuGuestRAMSlider_h__ */

