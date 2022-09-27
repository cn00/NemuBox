#
# Makefile for the VirtualBox FreeBSD Guest Drivers.
#

#
#
# Copyright (C) 2009-2010 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

KBUILD_VERBOSE =

all:
	@echo "*** Building 'nemuguest' module ***"
	@$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemuguest
	@if [ -f nemuguest/nemuguest.ko ]; then \
	    cp nemuguest/nemuguest.ko .; \
	 fi
	@echo
	@if [ -d nemuvfs ]; then \
	    echo "*** Building 'nemuvfs' module ***"; \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemuvfs; \
	    if [ -f nemuvfs/nemuvfs.ko ]; then \
	        cp nemuvfs/nemuvfs.ko .; \
	    fi; \
	fi


install:
	@$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemuguest install
	@if [ -d nemuvfs ]; then \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemuvfs install; \
	fi

clean:
	@$(MAKE) -C nemuguest clean
	@if [ -d nemuvfs ]; then \
	    $(MAKE) -C nemuvfs clean; \
	fi
	rm -f nemuguest.*o nemuvfs.*o

load:
	@/sbin/kldunload nemuvfs || true
	@/sbin/kldunload nemuguest || true
	@/sbin/kldload ./nemuguest.ko
	@if [ -f nemuvfs.ko ]; then /sbin/kldload ./nemuvfs.ko; fi
