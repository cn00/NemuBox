#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdSmokeTest1.py $

"""
VirtualBox Validation Kit - Smoke Test #1.
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
import os;
import sys;

# Only the main script needs to modify the path.
try:    __file__
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter;
from testdriver import base;
from testdriver import nemu;
from testdriver import nemucon;


class tdSmokeTest1(nemu.TestDriver):
    """
    Nemu Smoke Test #1.
    """

    def __init__(self):
        nemu.TestDriver.__init__(self);
        self.asRsrcs            = None;
        self.oTestVmSet         = self.oTestVmManager.getSmokeVmSet();
        self.sNicAttachmentDef  = 'mixed';
        self.sNicAttachment     = self.sNicAttachmentDef;
        self.fQuick             = False;

    #
    # Overridden methods.
    #
    def showUsage(self):
        rc = nemu.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('Smoke Test #1 options:');
        reporter.log('  --nic-attachment <bridged|nat|mixed>');
        reporter.log('      Default: %s' % (self.sNicAttachmentDef));
        reporter.log('  --quick');
        reporter.log('      Very selective testing.')
        return rc;

    def parseOption(self, asArgs, iArg):
        if asArgs[iArg] == '--nic-attachment':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--nic-attachment" takes an argument');
            self.sNicAttachment = asArgs[iArg];
            if self.sNicAttachment not in ('bridged', 'nat', 'mixed'):
                raise base.InvalidOption('The "--nic-attachment" value "%s" is not supported. Valid values are: bridged, nat' \
                        % (self.sNicAttachment));
        elif asArgs[iArg] == '--quick':
            # Disable all but a few VMs and configurations.
            for oTestVm in self.oTestVmSet.aoTestVms:
                if oTestVm.sVmName == 'tst-win2k3ent':          # 32-bit paging
                    oTestVm.asVirtModesSup  = [ 'hwvirt' ];
                    oTestVm.acCpusSup       = range(1, 2);
                elif oTestVm.sVmName == 'tst-rhel5':            # 32-bit paging
                    oTestVm.asVirtModesSup  = [ 'raw' ];
                    oTestVm.acCpusSup       = range(1, 2);
                elif oTestVm.sVmName == 'tst-win2k8':           # 64-bit
                    oTestVm.asVirtModesSup  = [ 'hwvirt-np' ];
                    oTestVm.acCpusSup       = range(1, 2);
                elif oTestVm.sVmName == 'tst-sol10-64':         # SMP, 64-bit
                    oTestVm.asVirtModesSup  = [ 'hwvirt' ];
                    oTestVm.acCpusSup       = range(2, 3);
                elif oTestVm.sVmName == 'tst-sol10':            # SMP, 32-bit
                    oTestVm.asVirtModesSup  = [ 'hwvirt-np' ];
                    oTestVm.acCpusSup       = range(2, 3);
                else:
                    oTestVm.fSkip = True;
        else:
            return nemu.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def actionVerify(self):
        if self.sNemuValidationKitIso is None or not os.path.isfile(self.sNemuValidationKitIso):
            reporter.error('Cannot find the NemuValidationKit.iso! (%s)'
                           'Please unzip a Validation Kit build in the current directory or in some parent one.'
                           % (self.sNemuValidationKitIso,) );
            return False;
        return nemu.TestDriver.actionVerify(self);

    def actionConfig(self):
        # Make sure nemuapi has been imported so we can use the constants.
        if not self.importNemuApi():
            return False;

        # Do the configuring.
        if self.sNicAttachment == 'nat':        eNic0AttachType = nemucon.NetworkAttachmentType_NAT;
        elif self.sNicAttachment == 'bridged':  eNic0AttachType = nemucon.NetworkAttachmentType_Bridged;
        else:                                   eNic0AttachType = None;
        assert self.sNemuValidationKitIso is not None;
        return self.oTestVmSet.actionConfig(self, eNic0AttachType = eNic0AttachType, sDvdImage = self.sNemuValidationKitIso);

    def actionExecute(self):
        """
        Execute the testcase.
        """
        return self.oTestVmSet.actionExecute(self, self.testOneVmConfig)


    #
    # Test execution helpers.
    #

    def testOneVmConfig(self, oVM, oTestVm):
        """
        Runs the specified VM thru test #1.
        """

        # Simple test.
        self.logVmInfo(oVM);
        oSession, oTxsSession = self.startVmAndConnectToTxsViaTcp(oTestVm.sVmName, fCdWait = True);
        if oSession is not None:
            self.addTask(oSession);

            ## @todo do some quick tests: save, restore, execute some test program, shut down the guest.

            # cleanup.
            self.removeTask(oTxsSession);
            self.terminateVmBySession(oSession)
            return True;
        return None;

if __name__ == '__main__':
    sys.exit(tdSmokeTest1().main(sys.argv));

