; $Id$
; @file
; NemuGuestAdditionsNT4.nsh - Guest Additions installation for NT4.
;

;
; Copyright (C) 2006-2013 Oracle Corporation
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License (GPL) as published by the Free Software
; Foundation, in version 2 as it comes in the "COPYING" file of the
; VirtualBox OSE distribution. VirtualBox OSE is distributed in the
; hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
;

Function NT4_SetVideoResolution

  ; Check for all required parameters
  StrCmp $g_iScreenX "0" missingParms
  StrCmp $g_iScreenY "0" missingParms
  StrCmp $g_iScreenBpp "0" missingParms
  Goto haveParms

missingParms:

  ${LogVerbose} "Missing display parameters for NT4, setting default (640x480, 8 BPP) ..."

  StrCpy $g_iScreenX '640'   ; Default value
  StrCpy $g_iScreenY '480'   ; Default value
  StrCpy $g_iScreenBpp '8'   ; Default value

  ; Write setting into registry to show the desktop applet on next boot
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Control\GraphicsDrivers\NewDisplay" "" ""

haveParms:

  ${LogVerbose} "Setting display parameters for NT4 ($g_iScreenXx$g_iScreenY, $g_iScreenBpp BPP) ..."

  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\nemuvideo\Device0" "DefaultSettings.BitsPerPel" $g_iScreenBpp
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\nemuvideo\Device0" "DefaultSettings.Flags" 0x00000000
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\nemuvideo\Device0" "DefaultSettings.VRefresh" 0x00000001
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\nemuvideo\Device0" "DefaultSettings.XPanning" 0x00000000
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\nemuvideo\Device0" "DefaultSettings.XResolution" $g_iScreenX
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\nemuvideo\Device0" "DefaultSettings.YPanning" 0x00000000
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Hardware Profiles\Current\System\CurrentControlSet\Services\nemuvideo\Device0" "DefaultSettings.YResolution" $g_iScreenY

FunctionEnd

Function NT4_SaveMouseDriverInfo

  Push $0

  ; !!! NOTE !!!
  ; Due to some re-branding (see functions Uninstall_Sun, Uninstall_Innotek and
  ; Uninstall_SunXVM) the installer *has* to transport the very first saved i8042prt
  ; value to the current installer's "uninstall" directory in both mentioned
  ; functions above, otherwise NT4 will be screwed because it then would store
  ; "NemuMouseNT.sys" as the original i8042prt driver which obviously isn't there
  ; after uninstallation anymore
  ; !!! NOTE !!!

  ; Save current mouse driver info so we may restore it on uninstallation
  ; But first check if we already installed the additions otherwise we will
  ; overwrite it with the NemuMouseNT.sys
  ReadRegStr $0 HKLM "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH}
  StrCmp $0 "" 0 exists

  ${LogVerbose} "Saving mouse driver info ..."
  ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH} $0
  Goto exit

exists:

  ${LogVerbose} "Mouse driver info already saved."
  Goto exit

exit:

!ifdef _DEBUG
  ${LogVerbose} "Mouse driver info: $0"
!endif

  Pop $0

FunctionEnd

Function NT4_Prepare

  ${If} $g_bNoNemuServiceExit == "false"
    ; Stop / kill NemuService
    Call StopNemuService
  ${EndIf}

  ${If} $g_bNoNemuTrayExit == "false"
    ; Stop / kill NemuTray
    Call StopNemuTray
  ${EndIf}

  ; Delete NemuService from registry
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "NemuService"

  ; Delete old NemuService.exe from install directory (replaced by NemuTray.exe)
  Delete /REBOOTOK "$INSTDIR\NemuService.exe"

FunctionEnd

Function NT4_CopyFiles

  ${LogVerbose} "Copying files for NT4 ..."

  SetOutPath "$INSTDIR"
  FILE "$%PATH_OUT%\bin\additions\NemuGuestDrvInst.exe"
  FILE "$%PATH_OUT%\bin\additions\RegCleanup.exe"

  ; The files to install for NT 4, they go into the system directories
  SetOutPath "$SYSDIR"
  FILE "$%PATH_OUT%\bin\additions\NemuDisp.dll"
  FILE "$%PATH_OUT%\bin\additions\NemuTray.exe"
  FILE "$%PATH_OUT%\bin\additions\NemuHook.dll"
  FILE "$%PATH_OUT%\bin\additions\NemuControl.exe"

  ; NemuService
  FILE "$%PATH_OUT%\bin\additions\NemuServiceNT.exe"

  ; The drivers into the "drivers" directory
  SetOutPath "$SYSDIR\drivers"
  FILE "$%PATH_OUT%\bin\additions\NemuVideo.sys"
  FILE "$%PATH_OUT%\bin\additions\NemuMouseNT.sys"
  FILE "$%PATH_OUT%\bin\additions\NemuGuestNT.sys"
  ;FILE "$%PATH_OUT%\bin\additions\NemuSFNT.sys" ; Shared Folders not available on NT4!

FunctionEnd

Function NT4_InstallFiles

  ${LogVerbose} "Installing drivers for NT4 ..."

  ; Install guest driver
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service create $\"NemuGuest$\" $\"NemuGuest Support Driver$\" 1 1 $\"$SYSDIR\drivers\NemuGuestNT.sys$\" $\"Base$\"" "false"

  ; Bugfix: Set "Start" to 1, otherwise, NemuGuest won't start on boot-up!
  ; Bugfix: Correct invalid "ImagePath" (\??\C:\WINNT\...)
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Services\NemuGuest" "Start" 1
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\NemuGuest" "ImagePath" "System32\Drivers\NemuGuestNT.sys"

  ; Run NemuTray when Windows NT starts
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "NemuTray" '"$SYSDIR\NemuTray.exe"'

  ; Video driver
  ${CmdExecute} "$\"$INSTDIR\NemuGuestDrvInst.exe$\" /i" "false"

  ${LogVerbose} "Installing VirtualBox service ..."

  ; Create the NemuService service
  ; No need to stop/remove the service here! Do this only on uninstallation!
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service create $\"NemuService$\" $\"VirtualBox Guest Additions Service$\" 16 2 $\"system32\NemuServiceNT.exe$\" $\"Base$\"" "false"

   ; Create the Shared Folders service ...
  ;nsSCM::Install /NOUNLOAD "NemuSF" "VirtualBox Shared Folders" 1 1 "$SYSDIR\drivers\NemuSFNT.sys" "Network" "" "" ""
  ;Pop $0                      ; Ret value

!ifdef _DEBUG
  ;${LogVerbose} "SCM::Install NemuSFNT.sys: $0"
!endif

  ;IntCmp $0 0 +1 error error  ; Check ret value (0=OK, 1=Error)

  ; ... and the link to the network provider
  ;WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\NemuSF\NetworkProvider" "DeviceName" "\Device\NemuMiniRdr"
  ;WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\NemuSF\NetworkProvider" "Name" "VirtualBox Shared Folders"
  ;WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\NemuSF\NetworkProvider" "ProviderPath" "$SYSDIR\NemuMRXNP.dll"

  ; Add the shared folders network provider
  ;${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" netprovider add NemuSF" "false"

  Goto done

error:
  Abort "ERROR: Could not install files for Windows NT4! Installation aborted."

done:

FunctionEnd

Function NT4_Main

  SetOutPath "$INSTDIR"

  Call NT4_Prepare
  Call NT4_CopyFiles

  ; This removes the flag "new display driver installed on the next bootup
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\RunOnce" "NemuGuestInst" '"$INSTDIR\RegCleanup.exe"'

  Call NT4_SaveMouseDriverInfo
  Call NT4_InstallFiles
  Call NT4_SetVideoResolution

  ; Write mouse driver name to registry overwriting the default name
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath" "System32\DRIVERS\NemuMouseNT.sys"

FunctionEnd

!macro NT4_UninstallInstDir un
Function ${un}NT4_UninstallInstDir

  ; Delete remaining files
  Delete /REBOOTOK "$INSTDIR\NemuGuestDrvInst.exe"
  Delete /REBOOTOK "$INSTDIR\RegCleanup.exe"

FunctionEnd
!macroend
!insertmacro NT4_UninstallInstDir ""
!insertmacro NT4_UninstallInstDir "un."

!macro NT4_Uninstall un
Function ${un}NT4_Uninstall

  Push $0

  ; Remove the guest driver service
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuGuest" "true"
  Delete /REBOOTOK "$SYSDIR\drivers\NemuGuestNT.sys"

  ; Delete the NemuService service
  Call ${un}StopNemuService
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuService" "true"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "NemuService"
  Delete /REBOOTOK "$SYSDIR\NemuServiceNT.exe"

  ; Delete the NemuTray app
  Call ${un}StopNemuTray
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "NemuTray"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\RunOnce" "NemuTrayDel" "$SYSDIR\cmd.exe /c del /F /Q $SYSDIR\NemuTray.exe"
  Delete /REBOOTOK "$SYSDIR\NemuTray.exe" ; If it can't be removed cause it's running, try next boot with "RunOnce" key above!
  Delete /REBOOTOK "$SYSDIR\NemuHook.dll"

  ; Delete the NemuControl utility
  Delete /REBOOTOK "$SYSDIR\NemuControl.exe"

  ; Delete the NemuVideo service
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuVideo" "true"

  ; Delete the Nemu video driver files
  Delete /REBOOTOK "$SYSDIR\drivers\NemuVideo.sys"
  Delete /REBOOTOK "$SYSDIR\NemuDisp.dll"

  ; Get original mouse driver info and restore it
  ReadRegStr $0 ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" ${ORG_MOUSE_PATH}
  ; If we still got our driver stored in $0 then this will *never* work, so
  ; warn the user and set it to the default driver to not screw up NT4 here
  ${If} $0 == "System32\DRIVERS\NemuMouseNT.sys"
    WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath" "System32\DRIVERS\i8042prt.sys"
    ${LogVerbose} "Old mouse driver is set to NemuMouseNT.sys, defaulting to i8042prt.sys ..."
  ${Else}
    WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\i8042prt" "ImagePath" $0
  ${EndIf}
  Delete /REBOOTOK "$SYSDIR\drivers\NemuMouseNT.sys"

  Pop $0

FunctionEnd
!macroend
!insertmacro NT4_Uninstall ""
!insertmacro NT4_Uninstall "un."
