; $Id$
;; @file
; NemuGuestAdditionsW2KXP.nsh - Guest Additions installation for Windows 2000/XP.
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

Function W2K_SetVideoResolution

  ; NSIS only supports global vars, even in functions -- great
  Var /GLOBAL i
  Var /GLOBAL tmp
  Var /GLOBAL tmppath
  Var /GLOBAL dev_id
  Var /GLOBAL dev_desc

  ; Check for all required parameters
  StrCmp $g_iScreenX "0" exit
  StrCmp $g_iScreenY "0" exit
  StrCmp $g_iScreenBpp "0" exit

  ${LogVerbose} "Setting display parameters ($g_iScreenXx$g_iScreenY, $g_iScreenBpp BPP) ..."

  ; Enumerate all video devices (up to 32 at the moment, use key "MaxObjectNumber" key later)
  ${For} $i 0 32

    ReadRegStr $tmp HKLM "HARDWARE\DEVICEMAP\VIDEO" "\Device\Video$i"
    StrCmp $tmp "" dev_not_found

    ; Extract path to video settings
    ; Ex: \Registry\Machine\System\CurrentControlSet\Control\Video\{28B74D2B-F0A9-48E0-8028-D76F6BB1AE65}\0000
    ; Or: \Registry\Machine\System\CurrentControlSet\Control\Video\nemuvideo\Device0
    ; Result: Machine\System\CurrentControlSet\Control\Video\{28B74D2B-F0A9-48E0-8028-D76F6BB1AE65}\0000
    Push "$tmp" ; String
    Push "\" ; SubString
    Push ">" ; SearchDirection
    Push ">" ; StrInclusionDirection
    Push "0" ; IncludeSubString
    Push "2" ; Loops
    Push "0" ; CaseSensitive
    Call StrStrAdv
    Pop $tmppath ; $1 only contains the full path
    StrCmp $tmppath "" dev_not_found

    ; Get device description
    ReadRegStr $dev_desc HKLM "$tmppath" "Device Description"
!ifdef _DEBUG
    ${LogVerbose} "Registry path: $tmppath"
    ${LogVerbose} "Registry path to device name: $temp"
!endif
    ${LogVerbose} "Detected video device: $dev_desc"

    ${If} $dev_desc == "VirtualBox Graphics Adapter"
      ${LogVerbose} "VirtualBox video device found!"
      Goto dev_found
    ${EndIf}
  ${Next}
  Goto dev_not_found

dev_found:

  ; If we're on Windows 2000, skip the ID detection ...
  ${If} $g_strWinVersion == "2000"
    Goto change_res
  ${EndIf}
  Goto dev_found_detect_id

dev_found_detect_id:

  StrCpy $i 0 ; Start at index 0
  ${LogVerbose} "Detecting device ID ..."

dev_found_detect_id_loop:

  ; Resolve real path to hardware instance "{GUID}"
  EnumRegKey $dev_id HKLM "SYSTEM\CurrentControlSet\Control\Video" $i
  StrCmp $dev_id "" dev_not_found ; No more entries? Jump out
!ifdef _DEBUG
  ${LogVerbose} "Got device ID: $dev_id"
!endif
  ReadRegStr $dev_desc HKLM "SYSTEM\CurrentControlSet\Control\Video\$dev_id\0000" "Device Description" ; Try to read device name
  ${If} $dev_desc == "VirtualBox Graphics Adapter"
    ${LogVerbose} "Device ID of $dev_desc: $dev_id"
    Goto change_res
  ${EndIf}

  IntOp $i $i + 1 ; Increment index
  goto dev_found_detect_id_loop

dev_not_found:

  ${LogVerbose} "No VirtualBox video device (yet) detected! No custom mode set."
  Goto exit

change_res:

!ifdef _DEBUG
  ${LogVerbose} "Device description: $dev_desc"
  ${LogVerbose} "Device ID: $dev_id"
!endif

  Var /GLOBAL reg_path_device
  Var /GLOBAL reg_path_monitor

  ${LogVerbose} "Custom mode set: Platform is Windows $g_strWinVersion"
  ${If} $g_strWinVersion == "2000"
  ${OrIf} $g_strWinVersion == "Vista"
    StrCpy $reg_path_device "SYSTEM\CurrentControlSet\SERVICES\NemuVideo\Device0"
    StrCpy $reg_path_monitor "SYSTEM\CurrentControlSet\SERVICES\NemuVideo\Device0\Mon00000001"
  ${ElseIf} $g_strWinVersion == "XP"
  ${OrIf} $g_strWinVersion == "7"
  ${OrIf} $g_strWinVersion == "8"
  ${OrIf} $g_strWinVersion == "8_1"
  ${OrIf} $g_strWinVersion == "10"
    StrCpy $reg_path_device "SYSTEM\CurrentControlSet\Control\Video\$dev_id\0000"
    StrCpy $reg_path_monitor "SYSTEM\CurrentControlSet\Control\VIDEO\$dev_id\0000\Mon00000001"
  ${Else}
    ${LogVerbose} "Custom mode set: Windows $g_strWinVersion not supported yet"
    Goto exit
  ${EndIf}

  ; Write the new value in the adapter config (NemuVideo.sys) using hex values in binary format
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" registry write HKLM $reg_path_device CustomXRes REG_BIN $g_iScreenX DWORD" "false"
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" registry write HKLM $reg_path_device CustomYRes REG_BIN $g_iScreenY DWORD" "false"
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" registry write HKLM $reg_path_device CustomBPP REG_BIN $g_iScreenBpp DWORD" "false"

  ; ... and tell Windows to use that mode on next start!
  WriteRegDWORD HKCC $reg_path_device "DefaultSettings.XResolution" "$g_iScreenX"
  WriteRegDWORD HKCC $reg_path_device "DefaultSettings.YResolution" "$g_iScreenY"
  WriteRegDWORD HKCC $reg_path_device "DefaultSettings.BitsPerPixel" "$g_iScreenBpp"

  WriteRegDWORD HKCC $reg_path_monitor "DefaultSettings.XResolution" "$g_iScreenX"
  WriteRegDWORD HKCC $reg_path_monitor "DefaultSettings.YResolution" "$g_iScreenY"
  WriteRegDWORD HKCC $reg_path_monitor "DefaultSettings.BitsPerPixel" "$g_iScreenBpp"

  ${LogVerbose} "Custom mode set to $g_iScreenXx$g_iScreenY, $g_iScreenBpp BPP on next restart."

exit:

FunctionEnd

Function W2K_Prepare

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

Function W2K_CopyFiles

  Push $0
  SetOutPath "$INSTDIR"

  ; Video driver
  FILE "$%PATH_OUT%\bin\additions\NemuVideo.sys"
  FILE "$%PATH_OUT%\bin\additions\NemuDisp.dll"

  ; Mouse driver
  FILE "$%PATH_OUT%\bin\additions\NemuMouse.sys"
  FILE "$%PATH_OUT%\bin\additions\NemuMouse.inf"
!ifdef NEMU_SIGN_ADDITIONS
  FILE "$%PATH_OUT%\bin\additions\NemuMouse.cat"
!endif

  ; Guest driver
  FILE "$%PATH_OUT%\bin\additions\NemuGuest.sys"
  FILE "$%PATH_OUT%\bin\additions\NemuGuest.inf"
!ifdef NEMU_SIGN_ADDITIONS
  FILE "$%PATH_OUT%\bin\additions\NemuGuest.cat"
!endif

  ; Guest driver files
  FILE "$%PATH_OUT%\bin\additions\NemuTray.exe"
  FILE "$%PATH_OUT%\bin\additions\NemuControl.exe" ; Not used by W2K and up, but required by the .INF file

  ; WHQL fake
!ifdef WHQL_FAKE
  FILE "$%PATH_OUT%\bin\additions\NemuWHQLFake.exe"
!endif

  SetOutPath $g_strSystemDir

  ; NemuService
  ${If} $g_bNoNemuServiceExit == "false"
    ; NemuService has been terminated before, so just install the file
    ; in the regular way
    FILE "$%PATH_OUT%\bin\additions\NemuService.exe"
  ${Else}
    ; NemuService is in use and wasn't terminated intentionally. So extract the
    ; new version into a temporary location and install it on next reboot
    Push $0
    ClearErrors
    GetTempFileName $0
    IfErrors 0 +3
      ${LogVerbose} "Error getting temp file for NemuService.exe"
      StrCpy "$0" "$INSTDIR\NemuServiceTemp.exe"
    ${LogVerbose} "NemuService is in use, will be installed on next reboot (from '$0')"
    File "/oname=$0" "$%PATH_OUT%\bin\additions\NemuService.exe"
    IfErrors 0 +2
      ${LogVerbose} "Error copying NemuService.exe to '$0'"
    Rename /REBOOTOK "$0" "$g_strSystemDir\NemuService.exe"
    IfErrors 0 +2
      ${LogVerbose} "Error renaming '$0' to '$g_strSystemDir\NemuService.exe'"
    Pop $0
  ${EndIf}

!if $%NEMU_WITH_WDDM% == "1"
  ${If} $g_bWithWDDM == "true"
    ; WDDM Video driver
    SetOutPath "$INSTDIR"

  !if $%NEMU_WITH_WDDM_W8% == "1"
    ${If} $g_strWinVersion == "8"
    ${OrIf} $g_strWinVersion == "8_1"
    ${OrIf} $g_strWinVersion == "10"
      !ifdef NEMU_SIGN_ADDITIONS
        FILE "$%PATH_OUT%\bin\additions\NemuVideoW8.cat"
      !endif
      FILE "$%PATH_OUT%\bin\additions\NemuVideoW8.sys"
      FILE "$%PATH_OUT%\bin\additions\NemuVideoW8.inf"
    ${Else}
  !endif
      !ifdef NEMU_SIGN_ADDITIONS
        FILE "$%PATH_OUT%\bin\additions\NemuVideoWddm.cat"
      !endif
      FILE "$%PATH_OUT%\bin\additions\NemuVideoWddm.sys"
      FILE "$%PATH_OUT%\bin\additions\NemuVideoWddm.inf"
  !if $%NEMU_WITH_WDDM_W8% == "1"
    ${EndIf}
  !endif

    FILE "$%PATH_OUT%\bin\additions\NemuDispD3D.dll"

    !if $%NEMU_WITH_CROGL% == "1"
      FILE "$%PATH_OUT%\bin\additions\NemuOGLarrayspu.dll"
      FILE "$%PATH_OUT%\bin\additions\NemuOGLcrutil.dll"
      FILE "$%PATH_OUT%\bin\additions\NemuOGLerrorspu.dll"
      FILE "$%PATH_OUT%\bin\additions\NemuOGLpackspu.dll"
      FILE "$%PATH_OUT%\bin\additions\NemuOGLpassthroughspu.dll"
      FILE "$%PATH_OUT%\bin\additions\NemuOGLfeedbackspu.dll"
      FILE "$%PATH_OUT%\bin\additions\NemuOGL.dll"

      FILE "$%PATH_OUT%\bin\additions\NemuD3D9wddm.dll"
      FILE "$%PATH_OUT%\bin\additions\wined3dwddm.dll"
    !endif ; $%NEMU_WITH_CROGL% == "1"

    !if $%BUILD_TARGET_ARCH% == "amd64"
      FILE "$%PATH_OUT%\bin\additions\NemuDispD3D-x86.dll"

      !if $%NEMU_WITH_CROGL% == "1"
        FILE "$%PATH_OUT%\bin\additions\NemuOGLarrayspu-x86.dll"
        FILE "$%PATH_OUT%\bin\additions\NemuOGLcrutil-x86.dll"
        FILE "$%PATH_OUT%\bin\additions\NemuOGLerrorspu-x86.dll"
        FILE "$%PATH_OUT%\bin\additions\NemuOGLpackspu-x86.dll"
        FILE "$%PATH_OUT%\bin\additions\NemuOGLpassthroughspu-x86.dll"
        FILE "$%PATH_OUT%\bin\additions\NemuOGLfeedbackspu-x86.dll"
        FILE "$%PATH_OUT%\bin\additions\NemuOGL-x86.dll"

        FILE "$%PATH_OUT%\bin\additions\NemuD3D9wddm-x86.dll"
        FILE "$%PATH_OUT%\bin\additions\wined3dwddm-x86.dll"
      !endif ; $%NEMU_WITH_CROGL% == "1"
    !endif ; $%BUILD_TARGET_ARCH% == "amd64"

    Goto doneCr
  ${EndIf}
!endif ; $%NEMU_WITH_WDDM% == "1"

!if $%NEMU_WITH_CROGL% == "1"
  ; crOpenGL
  !if $%BUILD_TARGET_ARCH% == "amd64"
    !define LIBRARY_X64 ; Enable installation of 64-bit libraries
  !endif
  StrCpy $0 "$TEMP\NemuGuestAdditions\NemuOGL"
  CreateDirectory "$0"
  !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%PATH_OUT%\bin\additions\NemuOGLarrayspu.dll"       "$g_strSystemDir\NemuOGLarrayspu.dll"       "$0"
  !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%PATH_OUT%\bin\additions\NemuOGLcrutil.dll"         "$g_strSystemDir\NemuOGLcrutil.dll"         "$0"
  !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%PATH_OUT%\bin\additions\NemuOGLerrorspu.dll"       "$g_strSystemDir\NemuOGLerrorspu.dll"       "$0"
  !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%PATH_OUT%\bin\additions\NemuOGLpackspu.dll"        "$g_strSystemDir\NemuOGLpackspu.dll"        "$0"
  !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%PATH_OUT%\bin\additions\NemuOGLpassthroughspu.dll" "$g_strSystemDir\NemuOGLpassthroughspu.dll" "$0"
  !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%PATH_OUT%\bin\additions\NemuOGLfeedbackspu.dll"    "$g_strSystemDir\NemuOGLfeedbackspu.dll"    "$0"
  !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%PATH_OUT%\bin\additions\NemuOGL.dll"               "$g_strSystemDir\NemuOGL.dll"               "$0"
  !if $%BUILD_TARGET_ARCH% == "amd64"
    !undef LIBRARY_X64 ; Disable installation of 64-bit libraries
  !endif

  !if $%BUILD_TARGET_ARCH% == "amd64"
    StrCpy $0 "$TEMP\NemuGuestAdditions\NemuOGL32"
    CreateDirectory "$0"
    ; Only 64-bit installer: Also copy 32-bit DLLs on 64-bit target arch in
    ; Wow64 node (32-bit sub system). Note that $SYSDIR contains the 32-bit
    ; path after calling EnableX64FSRedirection
    ${EnableX64FSRedirection}
    !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%NEMU_PATH_ADDITIONS_WIN_X86%\NemuOGLarrayspu.dll"       "$SYSDIR\NemuOGLarrayspu.dll"       "$0"
    !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%NEMU_PATH_ADDITIONS_WIN_X86%\NemuOGLcrutil.dll"         "$SYSDIR\NemuOGLcrutil.dll"         "$0"
    !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%NEMU_PATH_ADDITIONS_WIN_X86%\NemuOGLerrorspu.dll"       "$SYSDIR\NemuOGLerrorspu.dll"       "$0"
    !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%NEMU_PATH_ADDITIONS_WIN_X86%\NemuOGLpackspu.dll"        "$SYSDIR\NemuOGLpackspu.dll"        "$0"
    !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%NEMU_PATH_ADDITIONS_WIN_X86%\NemuOGLpassthroughspu.dll" "$SYSDIR\NemuOGLpassthroughspu.dll" "$0"
    !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%NEMU_PATH_ADDITIONS_WIN_X86%\NemuOGLfeedbackspu.dll"    "$SYSDIR\NemuOGLfeedbackspu.dll"    "$0"
    !insertmacro InstallLib DLL NOTSHARED REBOOT_PROTECTED "$%NEMU_PATH_ADDITIONS_WIN_X86%\NemuOGL.dll"               "$SYSDIR\NemuOGL.dll"               "$0"
    ${DisableX64FSRedirection}
  !endif

!endif ; NEMU_WITH_CROGL

doneCr:

  Pop $0

FunctionEnd

!ifdef WHQL_FAKE

Function W2K_WHQLFakeOn

  StrCmp $g_bFakeWHQL "true" do
  Goto exit

do:

  ${LogVerbose} "Turning off WHQL protection..."
  ${CmdExecute} "$\"$INSTDIR\NemuWHQLFake.exe$\" $\"ignore$\"" "true"

exit:

FunctionEnd

Function W2K_WHQLFakeOff

  StrCmp $g_bFakeWHQL "true" do
  Goto exit

do:

  ${LogVerbose} "Turning back on WHQL protection..."
  ${CmdExecute} "$\"$INSTDIR\NemuWHQLFake.exe$\" $\"warn$\"" "true"

exit:

FunctionEnd

!endif

Function W2K_InstallFiles

  ; The Shared Folder IFS goes to the system directory
  FILE /oname=$g_strSystemDir\drivers\NemuSF.sys "$%PATH_OUT%\bin\additions\NemuSF.sys"
  !insertmacro ReplaceDLL "$%PATH_OUT%\bin\additions\NemuMRXNP.dll" "$g_strSystemDir\NemuMRXNP.dll" "$INSTDIR"
  AccessControl::GrantOnFile "$g_strSystemDir\NemuMRXNP.dll" "(BU)" "GenericRead"
  !if $%BUILD_TARGET_ARCH% == "amd64"
    ; Only 64-bit installer: Copy the 32-bit DLL for 32 bit applications.
    !insertmacro ReplaceDLL "$%PATH_OUT%\bin\additions\NemuMRXNP-x86.dll" "$g_strSysWow64\NemuMRXNP.dll" "$INSTDIR"
    AccessControl::GrantOnFile "$g_strSysWow64\NemuMRXNP.dll" "(BU)" "GenericRead"
  !endif

  ; The NemuTray hook DLL also goes to the system directory; it might be locked
  !insertmacro ReplaceDLL "$%PATH_OUT%\bin\additions\NemuHook.dll" "$g_strSystemDir\NemuHook.dll" "$INSTDIR"
  AccessControl::GrantOnFile "$g_strSystemDir\NemuHook.dll" "(BU)" "GenericRead"

  ${LogVerbose} "Installing drivers ..."

  Push $0 ; For fetching results

  SetOutPath "$INSTDIR"

  ${If} $g_bNoGuestDrv == "false"
    ${LogVerbose} "Installing guest driver ..."
    ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" driver install $\"$INSTDIR\NemuGuest.inf$\" $\"$INSTDIR\install_drivers.log$\"" "false"
  ${Else}
    ${LogVerbose} "Guest driver installation skipped!"
  ${EndIf}

  ${If} $g_bNoVideoDrv == "false"
    ${If} $g_bWithWDDM == "true"
  !if $%NEMU_WITH_WDDM_W8% == "1"
      ${If} $g_strWinVersion == "8"
      ${OrIf} $g_strWinVersion == "8_1"
      ${OrIf} $g_strWinVersion == "10"
        ${LogVerbose} "Installing WDDM video driver for Windows 8 or newer..."
        ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" driver install $\"$INSTDIR\NemuVideoW8.inf$\" $\"$INSTDIR\install_drivers.log$\"" "false"
      ${Else}
  !endif
        ${LogVerbose} "Installing WDDM video driver for Windows Vista and 7..."
        ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" driver install $\"$INSTDIR\NemuVideoWddm.inf$\" $\"$INSTDIR\install_drivers.log$\"" "false"
  !if $%NEMU_WITH_WDDM_W8% == "1"
      ${EndIf}
  !endif
    ${Else}
      ${LogVerbose} "Installing video driver ..."
      ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" driver install $\"$INSTDIR\NemuVideo.inf$\" $\"$INSTDIR\install_drivers.log$\"" "false"
    ${EndIf}
  ${Else}
    ${LogVerbose} "Video driver installation skipped!"
  ${EndIf}

  ${If} $g_bNoMouseDrv == "false"
    ${LogVerbose} "Installing mouse driver ..."
    ; The mouse filter does not contain any device IDs but a "DefaultInstall" section;
    ; so this .INF file needs to be installed using "InstallHinfSection" which is implemented
    ; with NemuDrvInst's "driver executeinf" routine
    ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" driver executeinf $\"$INSTDIR\NemuMouse.inf$\"" "false"
  ${Else}
    ${LogVerbose} "Mouse driver installation skipped!"
  ${EndIf}

  ; Create the NemuService service
  ; No need to stop/remove the service here! Do this only on uninstallation!
  ${LogVerbose} "Installing VirtualBox service ..."
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service create $\"NemuService$\" $\"VirtualBox Guest Additions Service$\" 16 2 $\"system32\NemuService.exe$\" $\"Base$\"" "false"

  ; Set service description
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\NemuService" "Description" "Manages VM runtime information, time synchronization, remote sysprep execution and miscellaneous utilities for guest operating systems."

sf:

  ${LogVerbose} "Installing Shared Folders service ..."

  ; Create the Shared Folders service ...
  ; No need to stop/remove the service here! Do this only on uninstallation!
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service create $\"NemuSF$\" $\"VirtualBox Shared Folders$\" 2 1 $\"system32\drivers\NemuSF.sys$\" $\"NetworkProvider$\"" "false"

  ; ... and the link to the network provider
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\NemuSF\NetworkProvider" "DeviceName" "\Device\NemuMiniRdr"
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\NemuSF\NetworkProvider" "Name" "VirtualBox Shared Folders"
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\NemuSF\NetworkProvider" "ProviderPath" "$SYSDIR\NemuMRXNP.dll"

  ; Add default network providers (if not present or corrupted)
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" netprovider add WebClient" "false"
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" netprovider add LanmanWorkstation" "false"
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" netprovider add RDPNP" "false"

  ; Add the shared folders network provider
  ${LogVerbose} "Adding network provider (Order = $g_iSfOrder) ..."
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" netprovider add NemuSF $g_iSfOrder" "false"

!if $%NEMU_WITH_CROGL% == "1"
cropengl:
  ${If} $g_bWithWDDM == "true"
    ; Nothing to do here
  ${Else}
    ${LogVerbose} "Installing 3D OpenGL support ..."
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL" "Version" 2
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL" "DriverVersion" 1
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL" "Flags" 1
    WriteRegStr   HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL" "Dll" "NemuOGL.dll"
!if $%BUILD_TARGET_ARCH% == "amd64"
    SetRegView 32
    ; Write additional keys required for Windows XP, Vista and 7 64-bit (but for 32-bit stuff)
    ${If} $g_strWinVersion   == '10'
    ${OrIf} $g_strWinVersion == '8_1'
    ${OrIf} $g_strWinVersion == '8'
    ${OrIf} $g_strWinVersion == '7'
    ${OrIf} $g_strWinVersion == 'Vista'
    ${OrIf} $g_strWinVersion == '2003' ; Windows XP 64-bit is a renamed Windows 2003 really
      WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL" "Version" 2
      WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL" "DriverVersion" 1
      WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL" "Flags" 1
      WriteRegStr   HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL" "Dll" "NemuOGL.dll"
    ${EndIf}
    SetRegView 64
!endif
  ${Endif}
!endif

  Goto done

done:

  Pop $0

FunctionEnd

Function W2K_Main

  SetOutPath "$INSTDIR"
  SetOverwrite on

  Call W2K_Prepare
  Call W2K_CopyFiles

!ifdef WHQL_FAKE
  Call W2K_WHQLFakeOn
!endif

  Call W2K_InstallFiles

!ifdef WHQL_FAKE
  Call W2K_WHQLFakeOff
!endif

  Call W2K_SetVideoResolution

FunctionEnd

!macro W2K_UninstallInstDir un
Function ${un}W2K_UninstallInstDir

  Delete /REBOOTOK "$INSTDIR\NemuVideo.sys"
  Delete /REBOOTOK "$INSTDIR\NemuVideo.inf"
  Delete /REBOOTOK "$INSTDIR\NemuVideo.cat"
  Delete /REBOOTOK "$INSTDIR\NemuDisp.dll"

  Delete /REBOOTOK "$INSTDIR\NemuMouse.sys"
  Delete /REBOOTOK "$INSTDIR\NemuMouse.inf"
  Delete /REBOOTOK "$INSTDIR\NemuMouse.cat"

  Delete /REBOOTOK "$INSTDIR\NemuTray.exe"

  Delete /REBOOTOK "$INSTDIR\NemuGuest.sys"
  Delete /REBOOTOK "$INSTDIR\NemuGuest.inf"
  Delete /REBOOTOK "$INSTDIR\NemuGuest.cat"

  Delete /REBOOTOK "$INSTDIR\VBCoInst.dll" ; Deprecated, does not get installed anymore
  Delete /REBOOTOK "$INSTDIR\NemuControl.exe"
  Delete /REBOOTOK "$INSTDIR\NemuService.exe" ; Deprecated, does not get installed anymore

!if $%NEMU_WITH_WDDM% == "1"
  Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuVideoWddm.cat"
  Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuVideoWddm.sys"
  Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuVideoWddm.inf"
  !if $%NEMU_WITH_WDDM_W8% == "1"
  Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuVideoW8.cat"
  Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuVideoW8.sys"
  Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuVideoW8.inf"
  !endif
  Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuDispD3D.dll"

    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLarrayspu.dll"
    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLcrutil.dll"
    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLerrorspu.dll"
    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLpackspu.dll"
    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLpassthroughspu.dll"
    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLfeedbackspu.dll"
    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGL.dll"

    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuD3D9wddm.dll"
    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\wined3dwddm.dll"
    ; Try to delete libWine in case it is there from old installation
    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\libWine.dll"

  !if $%BUILD_TARGET_ARCH% == "amd64"
    Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuDispD3D-x86.dll"

      Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLarrayspu-x86.dll"
      Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLcrutil-x86.dll"
      Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLerrorspu-x86.dll"
      Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLpackspu-x86.dll"
      Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLpassthroughspu-x86.dll"
      Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGLfeedbackspu-x86.dll"
      Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuOGL-x86.dll"

      Delete /REBOOTOK "$%PATH_OUT%\bin\additions\NemuD3D9wddm-x86.dll"
      Delete /REBOOTOK "$%PATH_OUT%\bin\additions\wined3dwddm-x86.dll"
  !endif ; $%BUILD_TARGET_ARCH% == "amd64"
!endif ; $%NEMU_WITH_WDDM% == "1"

  ; WHQL fake
!ifdef WHQL_FAKE
  Delete /REBOOTOK "$INSTDIR\NemuWHQLFake.exe"
!endif

  ; Log file
  Delete /REBOOTOK "$INSTDIR\install.log"
  Delete /REBOOTOK "$INSTDIR\install_ui.log"

FunctionEnd
!macroend
!insertmacro W2K_UninstallInstDir ""
!insertmacro W2K_UninstallInstDir "un."

!macro W2K_Uninstall un
Function ${un}W2K_Uninstall

  Push $0

  ; Remove VirtualBox video driver
  ${LogVerbose} "Uninstalling video driver ..."
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" driver uninstall $\"$INSTDIR\NemuVideo.inf$\"" "true"
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuVideo" "true"
  Delete /REBOOTOK "$g_strSystemDir\drivers\NemuVideo.sys"
  Delete /REBOOTOK "$g_strSystemDir\NemuDisp.dll"

  ; Remove video driver
!if $%NEMU_WITH_WDDM% == "1"

  !if $%NEMU_WITH_WDDM_W8% == "1"
  ${LogVerbose} "Uninstalling WDDM video driver for Windows 8..."
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" driver uninstall $\"$INSTDIR\NemuVideoW8.inf$\"" "true"
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuVideoW8" "true"
  ;misha> @todo driver file removal (as well as service removal) should be done as driver package uninstall
  ;       could be done with "NemuDrvInst.exe /u", e.g. by passing additional arg to it denoting that driver package is to be uninstalled
  Delete /REBOOTOK "$g_strSystemDir\drivers\NemuVideoW8.sys"
  !endif ; $%NEMU_WITH_WDDM_W8% == "1"

  ${LogVerbose} "Uninstalling WDDM video driver for Windows Vista and 7..."
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" driver uninstall $\"$INSTDIR\NemuVideoWddm.inf$\"" "true"
  ; Always try to remove both NemuVideoWddm & NemuVideo services no matter what is installed currently
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuVideoWddm" "true"
  ;misha> @todo driver file removal (as well as service removal) should be done as driver package uninstall
  ;       could be done with "NemuDrvInst.exe /u", e.g. by passing additional arg to it denoting that driver package is to be uninstalled
  Delete /REBOOTOK "$g_strSystemDir\drivers\NemuVideoWddm.sys"
  Delete /REBOOTOK "$g_strSystemDir\NemuDispD3D.dll"
!endif ; $%NEMU_WITH_WDDM% == "1"

!if $%NEMU_WITH_CROGL% == "1"

  ${LogVerbose} "Removing Direct3D support ..."

  ; Do file validation before we uninstall
  Call ${un}ValidateD3DFiles
  Pop $0
  ${If} $0 == "1" ; D3D files are invalid
    ${LogVerbose} $(NEMU_UNINST_INVALID_D3D)
    MessageBox MB_ICONSTOP|MB_OK $(NEMU_UNINST_INVALID_D3D) /SD IDOK
    Goto d3d_uninstall_end
  ${EndIf}

  Delete /REBOOTOK "$g_strSystemDir\NemuOGLarrayspu.dll"
  Delete /REBOOTOK "$g_strSystemDir\NemuOGLcrutil.dll"
  Delete /REBOOTOK "$g_strSystemDir\NemuOGLerrorspu.dll"
  Delete /REBOOTOK "$g_strSystemDir\NemuOGLpackspu.dll"
  Delete /REBOOTOK "$g_strSystemDir\NemuOGLpassthroughspu.dll"
  Delete /REBOOTOK "$g_strSystemDir\NemuOGLfeedbackspu.dll"
  Delete /REBOOTOK "$g_strSystemDir\NemuOGL.dll"

  ; Remove D3D stuff
  ; @todo add a feature flag to only remove if installed explicitly
  Delete /REBOOTOK "$g_strSystemDir\libWine.dll"
  Delete /REBOOTOK "$g_strSystemDir\NemuD3D8.dll"
  Delete /REBOOTOK "$g_strSystemDir\NemuD3D9.dll"
  Delete /REBOOTOK "$g_strSystemDir\wined3d.dll"
  ; Update DLL cache
  ${If} ${FileExists} "$g_strSystemDir\dllcache\msd3d8.dll"
    Delete /REBOOTOK "$g_strSystemDir\dllcache\d3d8.dll"
    Rename /REBOOTOK "$g_strSystemDir\dllcache\msd3d8.dll" "$g_strSystemDir\dllcache\d3d8.dll"
  ${EndIf}
  ${If} ${FileExists} "$g_strSystemDir\dllcache\msd3d9.dll"
    Delete /REBOOTOK "$g_strSystemDir\dllcache\d3d9.dll"
    Rename /REBOOTOK "$g_strSystemDir\dllcache\msd3d9.dll" "$g_strSystemDir\dllcache\d3d9.dll"
  ${EndIf}
  ; Restore original DX DLLs
  ${If} ${FileExists} "$g_strSystemDir\msd3d8.dll"
    Delete /REBOOTOK "$g_strSystemDir\d3d8.dll"
    Rename /REBOOTOK "$g_strSystemDir\msd3d8.dll" "$g_strSystemDir\d3d8.dll"
  ${EndIf}
  ${If} ${FileExists} "$g_strSystemDir\msd3d9.dll"
    Delete /REBOOTOK "$g_strSystemDir\d3d9.dll"
    Rename /REBOOTOK "$g_strSystemDir\msd3d9.dll" "$g_strSystemDir\d3d9.dll"
  ${EndIf}

  !if $%BUILD_TARGET_ARCH% == "amd64"
    ; Only 64-bit installer: Also remove 32-bit DLLs on 64-bit target arch in Wow64 node
    Delete /REBOOTOK "$g_strSysWow64\NemuOGLarrayspu.dll"
    Delete /REBOOTOK "$g_strSysWow64\NemuOGLcrutil.dll"
    Delete /REBOOTOK "$g_strSysWow64\NemuOGLerrorspu.dll"
    Delete /REBOOTOK "$g_strSysWow64\NemuOGLpackspu.dll"
    Delete /REBOOTOK "$g_strSysWow64\NemuOGLpassthroughspu.dll"
    Delete /REBOOTOK "$g_strSysWow64\NemuOGLfeedbackspu.dll"
    Delete /REBOOTOK "$g_strSysWow64\NemuOGL.dll"

    ; Remove D3D stuff
    ; @todo add a feature flag to only remove if installed explicitly
    Delete /REBOOTOK "$g_strSysWow64\libWine.dll"
    Delete /REBOOTOK "$g_strSysWow64\NemuD3D8.dll"
    Delete /REBOOTOK "$g_strSysWow64\NemuD3D9.dll"
    Delete /REBOOTOK "$g_strSysWow64\wined3d.dll"
    ; Update DLL cache
    ${If} ${FileExists} "$g_strSysWow64\dllcache\msd3d8.dll"
      Delete /REBOOTOK "$g_strSysWow64\dllcache\d3d8.dll"
      Rename /REBOOTOK "$g_strSysWow64\dllcache\msd3d8.dll" "$g_strSysWow64\dllcache\d3d8.dll"
    ${EndIf}
    ${If} ${FileExists} "$g_strSysWow64\dllcache\msd3d9.dll"
      Delete /REBOOTOK "$g_strSysWow64\dllcache\d3d9.dll"
      Rename /REBOOTOK "$g_strSysWow64\dllcache\msd3d9.dll" "$g_strSysWow64\dllcache\d3d9.dll"
    ${EndIf}
    ; Restore original DX DLLs
    ${If} ${FileExists} "$g_strSysWow64\msd3d8.dll"
      Delete /REBOOTOK "$g_strSysWow64\d3d8.dll"
      Rename /REBOOTOK "$g_strSysWow64\msd3d8.dll" "$g_strSysWow64\d3d8.dll"
    ${EndIf}
    ${If} ${FileExists} "$g_strSysWow64\msd3d9.dll"
      Delete /REBOOTOK "$g_strSysWow64\d3d9.dll"
      Rename /REBOOTOK "$g_strSysWow64\msd3d9.dll" "$g_strSysWow64\d3d9.dll"
    ${EndIf}
    DeleteRegKey HKLM "SOFTWARE\Wow6432Node\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL"
  !endif ; amd64

  DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\NemuOGL"

d3d_uninstall_end:

!endif ; NEMU_WITH_CROGL

  ; Remove mouse driver
  ${LogVerbose} "Removing mouse driver ..."
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuMouse" "true"
  Delete /REBOOTOK "$g_strSystemDir\drivers\NemuMouse.sys"
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" registry delmultisz $\"SYSTEM\CurrentControlSet\Control\Class\{4D36E96F-E325-11CE-BFC1-08002BE10318}$\" $\"UpperFilters$\" $\"NemuMouse$\"" "true"

  ; Delete the NemuService service
  Call ${un}StopNemuService
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuService" "true"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "NemuService"
  Delete /REBOOTOK "$g_strSystemDir\NemuService.exe"

  ; NemuGINA
  Delete /REBOOTOK "$g_strSystemDir\NemuGINA.dll"
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\WinLogon" "GinaDLL"
  ${If} $0 == "NemuGINA.dll"
    ${LogVerbose} "Removing auto-logon support ..."
    DeleteRegValue HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\WinLogon" "GinaDLL"
  ${EndIf}
  DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion\WinLogon\Notify\NemuGINA"

  ; Delete NemuTray
  Call ${un}StopNemuTray
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "NemuTray"

  ; Remove guest driver
  ${LogVerbose} "Removing guest driver ..."
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" driver uninstall $\"$INSTDIR\NemuGuest.inf$\"" "true"

  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuGuest" "true"
  Delete /REBOOTOK "$g_strSystemDir\drivers\NemuGuest.sys"
  Delete /REBOOTOK "$g_strSystemDir\VBCoInst.dll" ; Deprecated, does not get installed anymore
  Delete /REBOOTOK "$g_strSystemDir\NemuTray.exe"
  Delete /REBOOTOK "$g_strSystemDir\NemuHook.dll"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "NemuTray" ; Remove NemuTray autorun
  Delete /REBOOTOK "$g_strSystemDir\NemuControl.exe"

  ; Remove shared folders driver
  ${LogVerbose} "Removing shared folders driver ..."
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" netprovider remove NemuSF" "true"
  ${CmdExecute} "$\"$INSTDIR\NemuDrvInst.exe$\" service delete NemuSF" "true"
  Delete /REBOOTOK "$g_strSystemDir\NemuMRXNP.dll" ; The network provider DLL will be locked
  !if $%BUILD_TARGET_ARCH% == "amd64"
    ; Only 64-bit installer: Also remove 32-bit DLLs on 64-bit target arch in Wow64 node
    Delete /REBOOTOK "$g_strSysWow64\NemuMRXNP.dll"
  !endif ; amd64
  Delete /REBOOTOK "$g_strSystemDir\drivers\NemuSF.sys"

  Pop $0

FunctionEnd
!macroend
!insertmacro W2K_Uninstall ""
!insertmacro W2K_Uninstall "un."

