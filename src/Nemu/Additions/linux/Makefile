#
# Makefile for the VirtualBox Linux Guest Drivers.
#

#
#
# Copyright (C) 2009-2012 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

ifneq ($(KBUILD_EXTMOD),)

# DKMS

obj-m = nemuguest/ nemusf/ nemuvideo/

else # ! KBUILD_EXTMOD

KBUILD_VERBOSE =

all:
	@echo "*** Building 'nemuguest' module ***"
	@$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemuguest
	@if [ -f nemuguest/nemuguest.ko ]; then \
	    cp nemuguest/nemuguest.ko .; \
	 else \
	    cp nemuguest/nemuguest.o .; \
	 fi
	@echo
	@if [ -d nemusf ]; then \
	    if [ -f nemuguest/Module.symvers ]; then \
	        cp nemuguest/Module.symvers nemusf; \
	    fi; \
	    echo "*** Building 'nemusf' module ***"; \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemusf; \
	    if [ -f nemusf/nemusf.ko ]; then \
	        cp nemusf/nemusf.ko .; \
	    else \
	        cp nemusf/nemusf.o .; \
	    fi; \
	    echo; \
	fi
	@if [ -d nemuvideo ]; then \
	    if [ -f nemuguest/Module.symvers ]; then \
	        cp nemuguest/Module.symvers nemuvideo; \
	    fi; \
	    echo "*** Building 'nemuvideo' module ***"; \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemuvideo; \
	    if [ -f nemuvideo/nemuvideo.ko ]; then \
	        cp nemuvideo/nemuvideo.ko .; \
	    else \
	        cp nemuvideo/nemuvideo.o .; \
	    fi; \
	    echo; \
	fi

install:
	@$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemuguest install
	@if [ -d nemusf ]; then \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemusf install; \
	fi
	@if [ -d nemuvideo ]; then \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemuvideo install; \
	fi

clean:
	@$(MAKE) -C nemuguest clean
	@if [ -d nemusf ]; then \
	    $(MAKE) -C nemusf clean; \
	fi
	@if [ -d nemuvideo ]; then \
	    $(MAKE) -C nemuvideo clean; \
	fi
	rm -f nemuguest.*o nemusf.*o nemuvideo.*o

check:
	@$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemuguest check

load:
	@/sbin/rmmod nemuvideo || true
	@/sbin/rmmod nemuvfs || true
	@/sbin/rmmod nemusf || true
	@/sbin/rmmod nemuguest || true
	@/sbin/insmod nemuguest.ko
	@if [ -f nemusf.ko ]; then /sbin/insmod nemusf.ko; fi
	@if [ -f nemuvideo.ko ]; then /sbin/insmod nemuvideo.ko; fi

endif # ! KBUILD_EXTMOD
