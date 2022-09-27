## @file
# Makefile for a sample/testcase using the 'glue' Java API bindings

#
# Copyright (C) 2010-2012 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

#
# User serviceable parts: adjust the following lines appropriately
#

# The host OS: linux, solaris, win, darwin, freebsd
HOSTOS=linux
# Absolute (!) path to the VirtualBox install directory
NEMU_BIN = /opt/VirtualBox
# Path to the sdk directory of the unpacked VirtualBox SDK
NEMU_SDK = ../../..
# On Windows, if you want to use the COM API: install directory of Jacob lib
JACOB_DIR =
# Extra classpath entries needed for compiling/running the sample
CLASSPATH =
# Java compiler to use
JAVAC = javac
# Java VM to use
JAVA = java


# 
# No changes should be necessary after this point.
#

ifeq ($(HOSTOS),win)
 JACOB_JAR=$(JACOB_DIR)/jacob.jar
 NEMU_JAR=$(NEMU_SDK)/bindings/mscom/java/nemujmscom.jar
 SEP=\;
 JAVA_COM_ARGS += -Djava.library.path=$(JACOB_DIR)
 CLASSPATH += $(JACOB_JAR)$(SEP)
else
 NEMU_JAR=$(NEMU_SDK)/bindings/xpcom/java/nemujxpcom.jar
 SEP=:
 JAVA_COM_ARGS += -Dnemu.home=$(NEMU_BIN)
endif

NEMU_JAR_WS=$(NEMU_SDK)/bindings/webservice/java/jax-ws/nemujws.jar


all: ws/TestNemu.class com/TestNemu.class

test: ws/test-TestNemu com/test-TestNemu

com/TestNemu.class:
	@mkdir com 2>/dev/null || true
	$(JAVAC) -d com -cp $(NEMU_JAR)$(SEP)$(CLASSPATH) TestNemu.java

com/test-TestNemu: com/TestNemu.class
	$(JAVA) $(JAVA_COM_ARGS) -cp com$(SEP)$(NEMU_JAR)$(SEP)$(CLASSPATH) TestNemu

ws/TestNemu.class:
	@mkdir ws 2>/dev/null || true
	$(JAVAC) -d ws -cp $(NEMU_JAR_WS)$(SEP)$(CLASSPATH) TestNemu.java

ws/test-TestNemu: ws/TestNemu.class
	$(JAVA) $(JAVA_WS_ARGS) -cp ws$(SEP)$(NEMU_JAR_WS)$(SEP)$(CLASSPATH) TestNemu -w -url http://localhost:18083/
