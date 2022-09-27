@echo off
REM $Id: comregister.cmd $
REM
REM Script to register the VirtualBox COM classes
REM (both inproc and out-of-process)
REM

REM
REM Copyright (C) 2006-2015 Oracle Corporation
REM
REM This file is part of VirtualBox Open Source Edition (OSE), as
REM available from http://www.virtualbox.org. This file is free software;
REM you can redistribute it and/or modify it under the terms of the GNU
REM General Public License (GPL) as published by the Free Software
REM Foundation, in version 2 as it comes in the "COPYING" file of the
REM VirtualBox OSE distribution. VirtualBox OSE is distributed in the
REM hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
REM

setlocal

REM Check if the current user is an administrator. Otherwise
REM all the COM registration will fail silently.
REM %windir%\system32\NET FILE 1>NUL 2>NUL & IF ERRORLEVEL 1 (ECHO Must be run as Administrator. Exiting.) & GOTO end

set err=0

REM
REM Figure out where the script lives first, so that we can invoke the
REM correct NemuSVC and register the right NemuC.dll.
REM

REM Determine the current directory.
set _SCRIPT_CURDIR=%CD%
for /f "tokens=*" %%d in ('cd') do set _SCRIPT_CURDIR=%%d

REM Determine a correct self - by %0.
set _SCRIPT_SELF=%0
if exist "%_SCRIPT_SELF%" goto found_self
set _SCRIPT_SELF=%_SCRIPT_SELF%.cmd
if exist "%_SCRIPT_SELF%" goto found_self

REM Determine a correct self - by current working directory.
set _SCRIPT_SELF=%_SCRIPT_CURDIR%\comregister.cmd
if exist "%_SCRIPT_SELF%" goto found_self

echo Warning: Not able to determin the comregister.cmd location.
set _NEMU_DIR=
goto end

:found_self
set _NEMU_DIR=
cd "%_SCRIPT_SELF%\.."
for /f "tokens=*" %%d in ('cd') do set _NEMU_DIR=%%d\
cd "%_SCRIPT_CURDIR%"

REM
REM Check for 64-bitness.
REM
set fIs64BitWindows=0
if not "%ProgramW6432%x" == "x" set fIs64BitWindows=1
if exist "%windir\syswow64\kernel32.dll" set fIs64BitWindows=1

REM
REM Parse arguments.
REM
set fUninstallOnly=0

:arg_loop
if "%1x" == "x"             goto arg_done
if "%1" == "-u"             goto arg_uninstall
if "%1" == "--uninstall"    goto arg_uninstall
echo syntax error: Unknown option %1
echo usage: comregister.cmd [-u,--uninstall] [--no-proxy] [--proxy]
goto end

:arg_uninstall
set fUninstallOnly=1
goto arg_next

:arg_next
shift
goto arg_loop
:arg_done

REM
REM Do the registrations.
REM
@if %fIs64BitWindows% == 1 goto register_amd64

:register_x86
@echo on
"%_NEMU_DIR%NemuSVC.exe" /UnregServer
regsvr32 /s /u "%_NEMU_DIR%NemuC.dll"
@if %fUninstallOnly% == 1 goto end

"%_NEMU_DIR%NemuSVC.exe" /RegServer
@echo off && call :check_regsvr32_ret NemuSVC x86 install && @echo on
regsvr32 /s    "%_NEMU_DIR%NemuC.dll"
@echo off && call :check_regsvr32_ret NemuC x86 install && @echo on
@echo off
goto end

REM Unregister all first, then register them. The order matters here.
:register_amd64
@echo on
"%_NEMU_DIR%NemuSVC.exe" /UnregServer
%windir%\syswow64\regsvr32 /s /u "%_NEMU_DIR%x86\NemuClient-x86.dll"
%windir%\system32\regsvr32 /s /u "%_NEMU_DIR%NemuC.dll"
@if %fUninstallOnly% == 1 goto end

"%_NEMU_DIR%NemuSVC.exe" /RegServer
@echo off && call :check_regsvr32_ret NemuSVC amd64 install && @echo on
%windir%\system32\regsvr32 /s    "%_NEMU_DIR%NemuC.dll"
@echo off && call :check_regsvr32_ret NemuC amd64 install && @echo on
%windir%\syswow64\regsvr32 /s    "%_NEMU_DIR%x86\NemuClient-x86.dll"
@echo off && call :check_regsvr32_ret NemuClient-x86 amd64 install && @echo on
@echo off

:end
@endlocal
exit /b %err%

:check_regsvr32_ret
set "err=%errorlevel%"
if %err% == 0 exit /b
echo "%1 %2 %3 returns error code %err%"
goto end
