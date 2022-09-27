#
# Spec file for creating VirtualBox rpm packages
#

#
# Copyright (C) 2006-2015 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

%define %SPEC% 1
%define %OSE% 1
%{!?python_sitelib: %define python_sitelib %(%{__python} -c "from distutils.sysconfig import get_python_lib; print get_python_lib()")}

Summary:   Oracle VM VirtualBox
Name:      %NAME%
Version:   %BUILDVER%_%BUILDREL%
Release:   1
URL:       http://www.virtualbox.org/
Source:    VirtualBox.tar
License:   GPLv2
Group:     Applications/System
Vendor:    Oracle Corporation
BuildRoot: %BUILDROOT%
Requires:  %INITSCRIPTS% %LIBASOUND% net-tools

%if %{?rpm_suse:1}%{!?rpm_suse:0}
%debug_package
%endif

%MACROSPYTHON%


%description
VirtualBox is a powerful PC virtualization solution allowing
you to run a wide range of PC operating systems on your Linux
system. This includes Windows, Linux, FreeBSD, DOS, OpenBSD
and others. VirtualBox comes with a broad feature set and
excellent performance, making it the premier virtualization
software solution on the market.


%prep
%setup -q
DESTDIR=""
unset DESTDIR


%build


%install
# Mandriva: prevent replacing 'echo' by 'gprintf'
export DONT_GPRINTIFY=1
rm -rf $RPM_BUILD_ROOT
install -m 755 -d $RPM_BUILD_ROOT/sbin
install -m 755 -d $RPM_BUILD_ROOT%{_initrddir}
install -m 755 -d $RPM_BUILD_ROOT/lib/modules
install -m 755 -d $RPM_BUILD_ROOT/etc/nemu
install -m 755 -d $RPM_BUILD_ROOT/usr/bin
install -m 755 -d $RPM_BUILD_ROOT/usr/src
install -m 755 -d $RPM_BUILD_ROOT/usr/share/applications
install -m 755 -d $RPM_BUILD_ROOT/usr/share/pixmaps
install -m 755 -d $RPM_BUILD_ROOT/usr/share/icons/hicolor
install -m 755 -d $RPM_BUILD_ROOT%{_defaultdocdir}/virtualbox
install -m 755 -d $RPM_BUILD_ROOT/usr/lib/virtualbox
install -m 755 -d $RPM_BUILD_ROOT/usr/lib/virtualbox/ExtensionPacks
install -m 755 -d $RPM_BUILD_ROOT/usr/share/virtualbox
install -m 755 -d $RPM_BUILD_ROOT/usr/share/mime/packages
mv NemuEFI32.fd $RPM_BUILD_ROOT/usr/lib/virtualbox || true
mv NemuEFI64.fd $RPM_BUILD_ROOT/usr/lib/virtualbox || true
mv *.rc $RPM_BUILD_ROOT/usr/lib/virtualbox
mv *.r0 $RPM_BUILD_ROOT/usr/lib/virtualbox
mv *.rel $RPM_BUILD_ROOT/usr/lib/virtualbox || true
install -m 755 -d $RPM_BUILD_ROOT/usr/lib/debug/usr/lib/virtualbox
%if %{?rpm_suse:1}%{!?rpm_suse:0}
rm *.debug
%else
mv *.debug $RPM_BUILD_ROOT/usr/lib/debug/usr/lib/virtualbox
%endif
mv NemuNetDHCP $RPM_BUILD_ROOT/usr/lib/virtualbox
mv NemuNetNAT $RPM_BUILD_ROOT/usr/lib/virtualbox
mv NemuNetAdpCtl $RPM_BUILD_ROOT/usr/lib/virtualbox
if [ -f NemuVolInfo ]; then
  mv NemuVolInfo $RPM_BUILD_ROOT/usr/lib/virtualbox
fi
mv NemuXPCOMIPCD $RPM_BUILD_ROOT/usr/lib/virtualbox
mv components $RPM_BUILD_ROOT/usr/lib/virtualbox/components
mv *.so $RPM_BUILD_ROOT/usr/lib/virtualbox
mv *.so.4 $RPM_BUILD_ROOT/usr/lib/virtualbox || true
ln -s ../NemuVMM.so $RPM_BUILD_ROOT/usr/lib/virtualbox/components/NemuVMM.so
mv NemuTestOGL $RPM_BUILD_ROOT/usr/lib/virtualbox
mv nemushell.py $RPM_BUILD_ROOT/usr/lib/virtualbox
(export NEMU_INSTALL_PATH=/usr/lib/virtualbox && \
  cd ./sdk/installer && \
  %{__python} ./nemuapisetup.py install --prefix %{_prefix} --root $RPM_BUILD_ROOT)
rm -rf sdk/installer
mv sdk $RPM_BUILD_ROOT/usr/lib/virtualbox
mv nls $RPM_BUILD_ROOT/usr/share/virtualbox
cp -a src $RPM_BUILD_ROOT/usr/share/virtualbox
mv Nemu.sh $RPM_BUILD_ROOT/usr/bin/Nemu
mv NemuSysInfo.sh $RPM_BUILD_ROOT/usr/share/virtualbox
mv NemuCreateUSBNode.sh $RPM_BUILD_ROOT/usr/share/virtualbox
cp icons/128x128/virtualbox.png $RPM_BUILD_ROOT/usr/share/pixmaps/virtualbox.png
cd icons
  for i in *; do
    if [ -f $i/virtualbox.* ]; then
      install -d $RPM_BUILD_ROOT/usr/share/icons/hicolor/$i/apps
      mv $i/virtualbox.* $RPM_BUILD_ROOT/usr/share/icons/hicolor/$i/apps
    fi
    install -d $RPM_BUILD_ROOT/usr/share/icons/hicolor/$i/mimetypes
    mv $i/* $RPM_BUILD_ROOT/usr/share/icons/hicolor/$i/mimetypes || true
    rmdir $i
  done
cd -
rmdir icons
mv virtualbox.xml $RPM_BUILD_ROOT/usr/share/mime/packages
for i in NemuManage NemuSVC VirtualBox NemuHeadless NemuDTrace NemuExtPackHelperApp NemuBalloonCtrl NemuAutostart nemu-img; do
  mv $i $RPM_BUILD_ROOT/usr/lib/virtualbox; done
if %WEBSVC%; then
  for i in nemuwebsrv webtest; do
    mv $i $RPM_BUILD_ROOT/usr/lib/virtualbox; done
fi
test -f NemuSDL && mv NemuSDL $RPM_BUILD_ROOT/usr/lib/virtualbox
for i in VirtualBox NemuHeadless NemuNetDHCP NemuNetNAT NemuNetAdpCtl; do
  chmod 4511 $RPM_BUILD_ROOT/usr/lib/virtualbox/$i; done
if [ -f $RPM_BUILD_ROOT/usr/lib/virtualbox/NemuVolInfo ]; then
  chmod 4511 $RPM_BUILD_ROOT/usr/lib/virtualbox/NemuVolInfo
fi
test -f NemuSDL && chmod 4511 $RPM_BUILD_ROOT/usr/lib/virtualbox/NemuSDL
if [ -d ExtensionPacks/VNC ]; then
  mv ExtensionPacks/VNC $RPM_BUILD_ROOT/usr/lib/virtualbox/ExtensionPacks
fi
mv NemuTunctl $RPM_BUILD_ROOT/usr/bin
%if %{?is_ose:0}%{!?is_ose:1}
for d in /lib/modules/*; do
  if [ -L $d/build ]; then
    rm -f /tmp/nemudrv-Module.symvers
    ./src/nemuhost/build_in_tmp \
      --save-module-symvers /tmp/nemudrv-Module.symvers \
      --module-source `pwd`/src/nemuhost/nemudrv \
      KBUILD_VERBOSE= KERN_DIR=$d/build MODULE_DIR=$RPM_BUILD_ROOT/$d/misc -j4 \
      %INSTMOD%
    ./src/nemuhost/build_in_tmp \
      --use-module-symvers /tmp/nemudrv-Module.symvers \
      --module-source `pwd`/src/nemuhost/nemunetflt \
      KBUILD_VERBOSE= KERN_DIR=$d/build MODULE_DIR=$RPM_BUILD_ROOT/$d/misc -j4 \
      %INSTMOD%
    ./src/nemuhost/build_in_tmp \
      --use-module-symvers /tmp/nemudrv-Module.symvers \
      --module-source `pwd`/src/nemuhost/nemunetadp \
      KBUILD_VERBOSE= KERN_DIR=$d/build MODULE_DIR=$RPM_BUILD_ROOT/$d/misc -j4 \
      %INSTMOD%
    ./src/nemuhost/build_in_tmp \
      --use-module-symvers /tmp/nemudrv-Module.symvers \
      --module-source `pwd`/src/nemuhost/nemupci \
      KBUILD_VERBOSE= KERN_DIR=$d/build MODULE_DIR=$RPM_BUILD_ROOT/$d/misc -j4 \
      %INSTMOD%
  fi
done
%endif
%if %{?is_ose:0}%{!?is_ose:1}
  mv kchmviewer $RPM_BUILD_ROOT/usr/lib/virtualbox
  for i in rdesktop-vrdp.tar.gz rdesktop-vrdp-keymaps; do
    mv $i $RPM_BUILD_ROOT/usr/share/virtualbox; done
  mv rdesktop-vrdp $RPM_BUILD_ROOT/usr/bin
%endif
for i in additions/NemuGuestAdditions.iso; do
  mv $i $RPM_BUILD_ROOT/usr/share/virtualbox; done
if [ -d accessible ]; then
  mv accessible $RPM_BUILD_ROOT/usr/lib/virtualbox
fi
mv nemudrv.sh $RPM_BUILD_ROOT/usr/lib/virtualbox
mv nemuballoonctrl-service.sh $RPM_BUILD_ROOT/usr/lib/virtualbox
mv nemuautostart-service.sh $RPM_BUILD_ROOT/usr/lib/virtualbox
mv nemuweb-service.sh $RPM_BUILD_ROOT/usr/lib/virtualbox
mv postinst-common.sh $RPM_BUILD_ROOT/usr/lib/virtualbox
mv prerm-common.sh $RPM_BUILD_ROOT/usr/lib/virtualbox
mv routines.sh $RPM_BUILD_ROOT/usr/lib/virtualbox
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/VirtualBox
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/virtualbox
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/NemuManage
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/nemumanage
test -f NemuSDL && ln -s Nemu $RPM_BUILD_ROOT/usr/bin/NemuSDL
test -f NemuSDL && ln -s Nemu $RPM_BUILD_ROOT/usr/bin/nemusdl
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/NemuVRDP
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/NemuHeadless
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/nemuheadless
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/NemuDTrace
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/nemudtrace
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/NemuBalloonCtrl
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/nemuballoonctrl
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/NemuAutostart
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/nemuautostart
ln -s Nemu $RPM_BUILD_ROOT/usr/bin/nemuwebsrv
ln -s /usr/lib/virtualbox/nemu-img $RPM_BUILD_ROOT/usr/bin/nemu-img
ln -s /usr/share/virtualbox/src/nemuhost $RPM_BUILD_ROOT/usr/src/nemuhost-%VER%
mv virtualbox.desktop $RPM_BUILD_ROOT/usr/share/applications/virtualbox.desktop
mv Nemu.png $RPM_BUILD_ROOT/usr/share/pixmaps/Nemu.png


%pre
# defaults
[ -r /etc/default/virtualbox ] && . /etc/default/virtualbox

# check for old installation
if [ -r /etc/nemu/nemu.cfg ]; then
  . /etc/nemu/nemu.cfg
  if [ "x$INSTALL_DIR" != "x" -a -d "$INSTALL_DIR" ]; then
    echo "An old installation of VirtualBox was found. To install this package the"
    echo "old package has to be removed first. Have a look at /etc/nemu/nemu.cfg to"
    echo "determine the installation directory of the previous installation. After"
    echo "uninstalling the old package remove the file /etc/nemu/nemu.cfg."
    exit 1
  fi
fi

# check for active VMs of the installed (old) package
# Execute the installed packages pre-uninstaller if present.
/usr/lib/virtualbox/prerm-common.sh 2>/dev/null
# Stop services from older versions without pre-uninstaller.
/etc/init.d/nemuballoonctrl-service stop 2>/dev/null
/etc/init.d/nemuautostart-service stop 2>/dev/null
/etc/init.d/nemuweb-service stop 2>/dev/null
NEMUSVC_PID=`pidof NemuSVC 2>/dev/null || true`
if [ -n "$NEMUSVC_PID" ]; then
  # ask the daemon to terminate immediately
  kill -USR1 $NEMUSVC_PID
  sleep 1
  if pidof NemuSVC > /dev/null 2>&1; then
    echo "A copy of VirtualBox is currently running.  Please close it and try again."
    echo "Please note that it can take up to ten seconds for VirtualBox (in particular"
    echo "the NemuSVC daemon) to finish running."
    exit 1
  fi
fi

# XXX remove old modules from previous versions (disable with INSTALL_NO_NEMUDRV=1 in /etc/default/virtualbox)
if [ "$INSTALL_NO_NEMUDRV" != "1" ]; then
  find /lib/modules -name "nemudrv\.*" 2>/dev/null|xargs rm -f 2> /dev/null || true
  find /lib/modules -name "nemunetflt\.*" 2>/dev/null|xargs rm -f 2> /dev/null || true
  find /lib/modules -name "nemunetadp\.*" 2>/dev/null|xargs rm -f 2> /dev/null || true
  find /lib/modules -name "nemupci\.*" 2>/dev/null|xargs rm -f 2> /dev/null || true
fi


%post
#include installer-common.sh

LOG="/var/log/nemu-install.log"

# defaults
[ -r /etc/default/virtualbox ] && . /etc/default/virtualbox

# remove old cruft
if [ -f /etc/init.d/nemudrv.sh ]; then
  echo "Found old version of /etc/init.d/nemudrv.sh, removing."
  rm /etc/init.d/nemudrv.sh
fi
if [ -f /etc/nemu/nemu.cfg ]; then
  echo "Found old version of /etc/nemu/nemu.cfg, removing."
  rm /etc/nemu/nemu.cfg
fi
rm -f /etc/nemu/module_not_compiled

# XXX SELinux: allow text relocation entries
%if %{?rpm_redhat:1}%{!?rpm_redhat:0}
set_selinux_permissions /usr/lib/virtualbox /usr/share/virtualbox
%endif

# create users groups (disable with INSTALL_NO_GROUP=1 in /etc/default/virtualbox)
if [ "$INSTALL_NO_GROUP" != "1" ]; then
  echo
  echo "Creating group 'nemuusers'. VM users must be member of that group!"
  echo
  groupadd -r -f nemuusers 2> /dev/null
fi

# install udev rule (disable with INSTALL_NO_UDEV=1 in /etc/default/virtualbox)
# and /dev/nemudrv and /dev/nemuusb/*/* device nodes
install_device_node_setup root 0600 /usr/share/virtualbox "${usb_group}"
%if %{?rpm_mdv:1}%{!?rpm_mdv:0}
/sbin/ldconfig
%update_menus
%endif
update-mime-database /usr/share/mime &> /dev/null || :
update-desktop-database -q > /dev/null 2>&1 || :
touch --no-create /usr/share/icons/hicolor
gtk-update-icon-cache -q /usr/share/icons/hicolor 2> /dev/null || :

# Disable module compilation with INSTALL_NO_NEMUDRV=1 in /etc/default/virtualbox
BUILD_MODULES=0
REGISTER_MODULES=1
if [ ! -f /lib/modules/`uname -r`/misc/nemudrv.ko ]; then
  REGISTER_MODULES=0
  if [ "$INSTALL_NO_NEMUDRV" != "1" ]; then
    # compile problem
    cat << EOF
No precompiled module for this kernel found -- trying to build one. Messages
emitted during module compilation will be logged to $LOG.

EOF
    BUILD_MODULES=1
  fi
fi
# if INSTALL_NO_NEMUDRV is set to 1, remove all shipped modules
if [ "$INSTALL_NO_NEMUDRV" = "1" ]; then
  rm -f /lib/modules/*/misc/nemudrv.ko
  rm -f /lib/modules/*/misc/nemunetflt.ko
  rm -f /lib/modules/*/misc/nemunetadp.ko
  rm -f /lib/modules/*/misc/nemupci.ko
fi
if [ $BUILD_MODULES -eq 1 ]; then
  /usr/lib/virtualbox/nemudrv.sh setup || true
else
  if lsmod | grep -q "nemudrv[^_-]"; then
    /usr/lib/virtualbox/nemudrv.sh stop || true
  fi
fi
# Install and start the new service scripts.
PRERM_DKMS=
test "${REGISTER_MODULES}" = 1 && PRERM_DKMS="--dkms %VER%"
POSTINST_START=
test "${INSTALL_NO_NEMUDRV}" = 1 && POSTINST_START=--nostart
/usr/lib/virtualbox/prerm-common.sh ${PRERM_DKMS} || true
/usr/lib/virtualbox/postinst-common.sh ${POSTINST_START} > /dev/null || true


%preun
# Called before the package is removed, or during upgrade after (not before)
# the new version's "post" scriptlet. 
# $1==0: remove the last version of the package
# $1>=1: upgrade
if [ "$1" = 0 ]; then
  /usr/lib/virtualbox/prerm-common.sh --dkms %VER% || exit 1
  rm -f /etc/udev/rules.d/60-nemudrv.rules
  rm -f /etc/nemu/license_agreed
  rm -f /etc/nemu/module_not_compiled
fi

%postun
%if %{?rpm_mdv:1}%{!?rpm_mdv:0}
/sbin/ldconfig
%{clean_desktop_database}
%clean_menus
%endif
update-mime-database /usr/share/mime &> /dev/null || :
update-desktop-database -q > /dev/null 2>&1 || :
touch --no-create /usr/share/icons/hicolor
gtk-update-icon-cache -q /usr/share/icons/hicolor 2> /dev/null || :
rm -rf /usr/lib/virtualbox/ExtensionPacks


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root)
%doc %{!?is_ose: LICENSE}
%doc UserManual*.pdf
%doc %{!?is_ose: VirtualBox*.chm}
%{?rpm_suse: %{py_sitedir}/*}
%{!?rpm_suse: %{python_sitelib}/*}
/etc/nemu
/usr/bin/*
/usr/src/nemu*
/usr/lib/virtualbox
/usr/share/applications/*
/usr/share/icons/hicolor/*/apps/*
/usr/share/icons/hicolor/*/mimetypes/*
/usr/share/mime/packages/*
/usr/share/pixmaps/*
/usr/share/virtualbox
