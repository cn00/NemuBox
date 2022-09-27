#
# Makefile for the VirtualBox Linux Host Drivers.
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

ifneq ($(KBUILD_EXTMOD),)

# DKMS

obj-m = nemudrv/
ifneq ($(wildcard $(KBUILD_EXTMOD)/nemunetflt/Makefile),)
 obj-m += nemunetflt/
endif
ifneq ($(wildcard $(KBUILD_EXTMOD)/nemunetadp/Makefile),)
 obj-m += nemunetadp/
endif
ifneq ($(wildcard $(KBUILD_EXTMOD)/nemupci/Makefile),)
 obj-m += nemupci/
endif
else # ! KBUILD_EXTMOD

# convenience Makefile without DKMS

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
	@if [ -d nemupci ]; then \
	    if [ -f nemudrv/Module.symvers ]; then \
	        cp nemudrv/Module.symvers nemupci; \
	    fi; \
	    echo "*** Building 'nemupci' module ***"; \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemupci; \
	    cp nemupci/nemupci.ko .; \
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
	@if [ -d nemupci ]; then \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemupci install; \
	fi

clean:
	@$(MAKE) -C nemudrv clean
	@if [ -d nemunetflt ]; then \
	    $(MAKE) -C nemunetflt clean; \
	fi
	@if [ -d nemunetadp ]; then \
	    $(MAKE) -C nemunetadp clean; \
	fi
	@if [ -d nemupci ]; then \
	    $(MAKE) -C nemupci clean; \
	fi
	rm -f nemudrv.ko nemunetflt.ko nemunetadp.ko nemupci.ko 

check:
	@$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C nemudrv check

unload:
	@for module in nemupci nemunetadp nemunetflt nemudrv; do \
		if grep "^$$module " /proc/modules >/dev/null; then \
			echo "Removing previously installed $$module module"; \
			/sbin/rmmod $$module; \
		fi; \
	done

load: unload
	@for module in nemudrv nemunetflt nemunetadp nemupci; do \
		if test -f $$module.ko; then \
			echo "Installing $$module module"; \
			/sbin/insmod $$module.ko; \
		fi; \
	done

endif # ! KBUILD_EXTMOD
