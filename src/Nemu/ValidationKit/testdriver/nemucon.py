# -*- coding: utf-8 -*-
# $Id: nemucon.py $

"""
VirtualBox Constants.

See NemuConstantWrappingHack for details.
"""

__copyright__ = \
"""
Copyright (C) 2010-2015 Oracle Corporation

This file is part of VirtualBox Open Source Edition (OSE), as
available from http://www.virtualbox.org. This file is free software;
you can redistribute it and/or modify it under the terms of the GNU
General Public License (GPL) as published by the Free Software
Foundation, in version 2 as it comes in the "COPYING" file of the
VirtualBox OSE distribution. VirtualBox OSE is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL) only, as it comes in the "COPYING.CDDL" file of the
VirtualBox OSE distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.
"""
__version__ = "$Revision: 100880 $"


# Standard Python imports.
import sys


class NemuConstantWrappingHack(object):                                         # pylint: disable=R0903
    """
    This is a hack to avoid the self.oNemuMgr.constants.MachineState_Running
    uglyness that forces one into the rigth margin...  Anyone using this module
    can get to the constants easily by:

        import testdriver.nemu as nemu
        if self.o.machine.state == nemu.MachineState_Running:
            do stuff;

    For our own convenience we have a global variable 'nemu' that refers to the
    instance of this class so we can do the same thing from within the module
    as well (if we didn't we'd have to do testdriver.nemu.MachineState_Running).
    """
    def __init__(self, oWrapped):
        self.oWrapped = oWrapped;
        self.oNemuMgr = None;
        self.fpApiVer = 99.0;

    def __getattr__(self, sName):
        # Our self.
        try:
            return getattr(self.oWrapped, sName)
        except AttributeError:
            # The Nemu constants.
            if self.oNemuMgr is None:
                raise;
            try:
                return getattr(self.oNemuMgr.constants, sName);
            except AttributeError:
                # Do some compatability mappings to keep it working with
                # older versions.
                if self.fpApiVer < 3.3:
                    if sName == 'SessionState_Locked':
                        return getattr(self.oNemuMgr.constants, 'SessionState_Open');
                    if sName == 'SessionState_Unlocked':
                        return getattr(self.oNemuMgr.constants, 'SessionState_Closed');
                    if sName == 'SessionState_Unlocking':
                        return getattr(self.oNemuMgr.constants, 'SessionState_Closing');
                raise;


goHackModuleClass = NemuConstantWrappingHack(sys.modules[__name__]);                         # pylint: disable=C0103
sys.modules[__name__] = goHackModuleClass;

