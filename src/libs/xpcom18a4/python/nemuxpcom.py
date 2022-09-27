"""
Copyright (C) 2008-2012 Oracle Corporation

This file is part of VirtualBox Open Source Edition (OSE), as
available from http://www.virtualbox.org. This file is free software;
you can redistribute it and/or modify it under the terms of the GNU
General Public License (GPL) as published by the Free Software
Foundation, in version 2 as it comes in the "COPYING" file of the
VirtualBox OSE distribution. VirtualBox OSE is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
"""

import xpcom
import sys
import platform

#
# This code overcomes somewhat unlucky feature of Python, where it searches
# for binaries in the same place as platfom independent modules, while
# rest of Python bindings expect _xpcom to be inside xpcom module
#

_asNemuPythons = [
    'NemuPython' + str(sys.version_info[0]) + '_' + str(sys.version_info[1]),
    'NemuPython' + str(sys.version_info[0]),
    'NemuPython'
];

# On platforms where we ship both 32-bit and 64-bit API bindings, we have to
# look for the right set if we're a 32-bit process.
if platform.system() in [ 'SunOS', ] and sys.maxsize <= 2**32:
    _asNew = [ sCandidate + '_x86' for sCandidate in _asNemuPythons ];
    _asNew.extend(_asNemuPythons);
    _asNemuPythons = _asNew;
    del _asNew;

# On Darwin (aka Mac OS X) we know exactly where things are in a normal
# VirtualBox installation.
## @todo Edit this at build time to the actual Nemu location set in the make files.
## @todo We know the location for most hardened builds, not just darwin!
if platform.system() == 'Darwin':
    sys.path.append('/Applications/VirtualBox.app/Contents/MacOS')

_oNemuPythonMod = None
for m in _asNemuPythons:
    try:
        _oNemuPythonMod =  __import__(m)
        break
    except Exception, x:
        print 'm=%s x=%s' % (m, x);
    #except:
    #    pass

if platform.system() == 'Darwin':
    sys.path.remove('/Applications/VirtualBox.app/Contents/MacOS')

if _oNemuPythonMod == None:
    raise Exception('Cannot find NemuPython module (tried: %s)' % (', '.join(_asNemuPythons),));

sys.modules['xpcom._xpcom'] = _oNemuPythonMod;
xpcom._xpcom = _oNemuPythonMod;

