; example2.nsi
;
; This script is based on example1.nsi, but it remember the directory, 
; has uninstall support and (optionally) installs start menu shortcuts.
;
; It will install example2.nsi into a directory that the user selects,

;--------------------------------

; The name of the installer
Name "gcn64ctl"

; The file to write
OutFile "installers/gcn64ctl-install-${VERSION}.exe"

; The default installation directory
InstallDir $PROGRAMFILES\gcn64ctl

; Registry key to check for directory (so if you install again, it will 
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\gcn64ctl" "Install_Dir"

LicenseData ../LICENSE

; Request application privileges for Windows Vista
RequestExecutionLevel admin

;--------------------------------
!include "LogicLib.nsh"
!include "x64.nsh"

; Pages

Page license
Page components
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------

; The stuff to install
Section "gcn64ctl (required)"

  SectionIn RO
  
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Put file there
  File /r "release_windows\*.*"
  
  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\gcn64ctl "Install_Dir" "$INSTDIR"
  
  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\gcn64ctl" "DisplayName" "gcn64ctl"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\gcn64ctl" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\gcn64ctl" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\gcn64ctl" "NoRepair" 1
  WriteUninstaller "uninstall.exe"
  
SectionEnd

Section "Install driver for updating adapter firmware (dfu-programmer)"
	${If} ${RunningX64}
		ExecWait '"$INSTDIR\dfu-prog-usb-1.2.2\dpinst_amd64.exe" /PATH "$INSTDIR\dfu-prog-usb-1.2.2"'
	${Else}
		EXecWait '"$INSTDIR\dfu-prog-usb-1.2.2\dpinst_x86.exe" /PATH "$INSTDIR\dfu-prog-usb-1.2.2"'
	${EndIf}	
SectionEnd

; Optional section (can be disabled by the user)
Section "Start Menu Shortcuts"

  CreateDirectory "$SMPROGRAMS\raphnet-tech GC-N64 adapter manager"
  CreateShortCut "$SMPROGRAMS\raphnet-tech GC-N64 adapter manager\Uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
  CreateShortCut "$SMPROGRAMS\raphnet-tech GC-N64 adapter manager\Command-line tools.lnk" "$SYSDIR\cmd.exe" '/K "cd /d $INSTDIR"' "$SYSDIR\cmd.exe" 0
  CreateShortCut "$SMPROGRAMS\raphnet-tech GC-N64 adapter manager\GC/N64 adapter manager.lnk" "$INSTDIR\gcn64ctl_gui.exe" "" "$INSTDIR\gcn64ctl_gui.exe" 0
  
SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
  
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\gcn64ctl"
  DeleteRegKey HKLM SOFTWARE\gcn64ctl

  ; Remove files and uninstaller
  Delete "$INSTDIR\*.*"

  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\raphnet-tech GC-N64 adapter manager\*.lnk"

  ; Remove directories used
  RMDir "$SMPROGRAMS\raphnet-tech GC-N64 adapter manager"
  RMDir "$INSTDIR"

SectionEnd
