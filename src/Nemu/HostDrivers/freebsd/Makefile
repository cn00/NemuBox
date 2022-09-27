#
# Makefile for the VirtualBox FreeBSD Host Drivers.
#

#
#
# Copyright (C) 2008-2015 Oracle Corporation
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
	@echo "*** Building 'nemudrv' module ***"
	@$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemudrv
	@cp nemudrv/nemudrv.ko .
	@echo
	@if [ -d nemunetflt ]; then \
	    if [ -f nemudrv/Module.symvers ]; then \
	        cp nemudrv/Module.symvers nemunetflt; \
	    fi; \
	    echo "*** Building 'nemunetflt' module ***"; \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemunetflt; \
	    cp nemunetflt/nemunetflt.ko .; \
	    echo; \
	fi
	@if [ -d nemunetadp ]; then \
	    if [ -f nemudrv/Module.symvers ]; then \
	        cp nemudrv/Module.symvers nemunetadp; \
	    fi; \
	    echo "*** Building 'nemunetadp' module ***"; \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemunetadp; \
	    cp nemunetadp/nemunetadp.ko .; \
	    echo; \
	fi


install:
	@$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemudrv install
	@if [ -d nemunetflt ]; then \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemunetflt install; \
	fi
	@if [ -d nemunetadp ]; then \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemunetadp install; \
	fi

clean:
	@$(MAKE) -C nemudrv clean
	@if [ -d nemunetflt ]; then \
	    $(MAKE) -C nemunetflt clean; \
	fi
	@if [ -d nemunetadp ]; then \
	    $(MAKE) -C nemunetadp clean; \
	fi
	rm -f nemudrv.ko nemunetflt.ko nemunetadp.ko

load:
	@for module in nemunetadp nemunetflt nemudrv; do \
		if kldstat -n "$$module" >/dev/null; then \
			echo "Removing previously installed $$module module"; \
			/sbin/kldunload $$module; \
		fi; \
	done
	@for module in nemudrv nemunetflt nemunetadp; do \
		if test -f $$module.ko; then \
			echo "Installing $$module module"; \
			/sbin/kldload ./$$module.ko; \
		fi; \
	done
