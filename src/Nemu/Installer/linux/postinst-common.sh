# Oracle VM VirtualBox
# VirtualBox Linux post-installer common portions
#

# Copyright (C) 2015 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.

# Put bits of the post-installation here which should work the same for all of
# the Linux installers.  We do not use special helpers (e.g. dh_* on Debian),
# but that should not matter, as we know what those helpers actually do, and we
# have to work on those systems anyway when installed using the all
# distributions installer.
#
# We assume that all required files are in the same folder as this script
# (e.g. /opt/VirtualBox, /usr/lib/VirtualBox, the build output directory).

# This is GNU-specific, sorry Solaris.  It fails on directories ending in '\n'.
MY_PATH="$(dirname $(readlink -f -- "${0}"))"
cd "${MY_PATH}"
. "./routines.sh"

START=true
TARGET="${MY_PATH}"
while test -n "${1}"; do
    case "${1}" in
        --nostart)
            START=
            ;;
        *)
            echo "Bad argument ${1}" >&2
            exit 1
            ;;
    esac
    shift
done

# Install runlevel scripts and systemd unit files
install_init_script "${TARGET}/vboxdrv.sh" vboxdrv
install_init_script "${TARGET}/vboxballoonctrl-service.sh" vboxballoonctrl-service
install_init_script "${TARGET}/vboxautostart-service.sh" vboxautostart-service
install_init_script "${TARGET}/vboxweb-service.sh" vboxweb-service

delrunlevel vboxdrv
addrunlevel vboxdrv
delrunlevel vboxballoonctrl-service
addrunlevel vboxballoonctrl-service
delrunlevel vboxautostart-service
addrunlevel vboxautostart-service
delrunlevel vboxweb-service
addrunlevel vboxweb-service

ln -sf "${MY_PATH}/postinst-common.sh" /sbin/rcvboxdrv

test -n "${START}" &&
{
    start_init_script vboxdrv
    start_init_script vboxballoonctrl-service
    start_init_script vboxautostart-service
    start_init_script vboxweb-service
}
