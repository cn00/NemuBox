"""
Copyright (C) 2009-2015 Oracle Corporation

This file is part of VirtualBox Open Source Edition (OSE), as
available from http://www.virtualbox.org. This file is free software;
you can redistribute it and/or modify it under the terms of the GNU
General Public License (GPL) as published by the Free Software
Foundation, in version 2 as it comes in the "COPYING" file of the
VirtualBox OSE distribution. VirtualBox OSE is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
"""

import os,sys
from distutils.core import setup

def cleanupComCache():
    import shutil
    from distutils.sysconfig import get_python_lib
    comCache1 = os.path.join(get_python_lib(), 'win32com', 'gen_py')
    comCache2 = os.path.join(os.environ.get("TEMP", "c:\\tmp"), 'gen_py')
    print "Cleaning COM cache at",comCache1,"and",comCache2
    shutil.rmtree(comCache1, True)
    shutil.rmtree(comCache2, True)

def patchWith(file,install,sdk):
    newFile=file + ".new"
    install=install.replace("\\", "\\\\")
    try:
        os.remove(newFile)
    except:
        pass
    oldF = open(file, 'r')
    newF = open(newFile, 'w')
    for line in oldF:
        line = line.replace("%NEMU_INSTALL_PATH%", install)
        line = line.replace("%NEMU_SDK_PATH%", sdk)
        newF.write(line)
    newF.close()
    oldF.close()
    try:
        os.remove(file)
    except:
        pass
    os.rename(newFile, file)

# See http://docs.python.org/distutils/index.html
def main(argv):
    nemuDest = os.environ.get("NEMU_INSTALL_PATH", None)
    if nemuDest is None:
        raise Exception("No NEMU_INSTALL_PATH defined, exiting")

    nemuVersion = os.environ.get("NEMU_VERSION", None)
    if nemuVersion is None:
        # Should we use Nemu version for binding module versioning?
        nemuVersion = "1.0"

    import platform

    if platform.system() == 'Windows':
        cleanupComCache()

    # Darwin: Patched before installation. Modifying bundle is not allowed, breaks signing and upsets gatekeeper.
    if platform.system() != 'Darwin':
        nemuSdkDest = os.path.join(nemuDest, "sdk")
        patchWith(os.path.join(os.path.dirname(sys.argv[0]), 'nemuapi', '__init__.py'), nemuDest, nemuSdkDest)

    setup(name='nemuapi',
          version=nemuVersion,
          description='Python interface to VirtualBox',
          author='Oracle Corp.',
          author_email='nemu-dev@virtualbox.org',
          url='http://www.virtualbox.org',
          packages=['nemuapi']
          )

if __name__ == '__main__':
    main(sys.argv)

