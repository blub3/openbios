\   Xbox 360 specific initialization code
\
\   Copyright (C) 2005 Stefan Reinauer
\   Copyright (C) 2026 John Davis
\
\   This program is free software; you can redistribute it and/or
\   modify it under the terms of the GNU General Public License
\   as published by the Free Software Foundation
\


\ -------------------------------------------------------------------------
\ initialization
\ -------------------------------------------------------------------------

: make-openable ( path )
  find-dev if
    begin ?dup while
      \ install trivial open and close methods
      dup active-package! is-open
      parent
    repeat
  then
;

: preopen ( chosen-str node-path )
  2dup make-openable

  " /chosen" find-device
  open-dev ?dup if
    encode-int 2swap property
  else
    2drop
  then
;

\ preopen device nodes (and store the ihandles under /chosen)
:noname
  " rtc" " rtc" preopen
  " memory" " /memory" preopen
; SYSTEM-initializer


\ use the tty interface if available
: activate-tty-interface
  " /packages/terminal-emulator" find-dev if drop
  then
;

variable keyboard-phandle 0 keyboard-phandle !

: (find-keyboard-device) ( phandle -- )
  recursive
  keyboard-phandle @ 0= if  \ Return first match
    >dn.child @
    begin ?dup while
      dup dup " device_type" rot get-package-property 0= if
        drop dup cstrlen
        " keyboard" strcmp 0= if
          dup to keyboard-phandle
        then
      then
      (find-keyboard-device)
      >dn.peer @
    repeat
  else
    drop
  then
;

\ create the keyboard devalias
:noname
  device-tree @ (find-keyboard-device)
  keyboard-phandle @ if
    active-package
    " /aliases" find-device
    keyboard-phandle @ get-package-path 2dup
    encode-string " kbd" property
    encode-string " keyboard" property
    active-package!
  then
; SYSTEM-initializer

\ -------------------------------------------------------------------------
\ pre-booting
\ -------------------------------------------------------------------------

: update-chosen
  " /chosen" find-device
  stdin @ encode-int " stdin" property
  stdout @ encode-int " stdout" property
  device-end
;

\ Boot the tested SSD directly, without relying on HFS blessing metadata.
\ Keep the boot stick and SSD in their tested ports (SSD alias ud1).
: mac-boot
  \ The experimental HDMI transmitter setup runs before both manual and automatic SSD boot.
  s" xenon-hdmi-audio" evaluate
  0 state-valid !
  0 !load-size
  s" boot ud1:,\System\Library\CoreServices\BootX" evaluate
;

: xbox360-autoboot
  cr ." Mac OS X SSD autoboot v1: press any key within 5 seconds to cancel." cr
  d# 50 0 do
    stdin @ if
      key? if
        key drop
        ." Automatic boot cancelled. Type mac-boot to boot the SSD." cr
        unloop exit
      then
    then
    \ Bound by arch_of_init; use the Xenon timebase, not dummy Forth ticks.
    s" xbox360-boot-wait" evaluate
  loop
  ['] mac-boot catch ?dup if
    ." SSD boot exception: " . cr
  then
  ." SSD boot returned. Check the SSD and Xbox360.mkext boot stick." cr
;

\ Run after console setup, before initialize-of evaluates boot-command.
\ Xbox NVRAM is RAM-only, so install these defaults on every startup.
:noname
  s" xbox360-autoboot" s" boot-command" $setenv
  s" true" s" auto-boot?" $setenv
; DIAG-initializer

:noname
  set-defaults
; PREPOST-initializer

\ -------------------------------------------------------------------------
\ Mac OF specific words
\ -------------------------------------------------------------------------

: parse-1hex 1 parse-nhex ;
: parse-2hex 2 parse-nhex ;
: parse-3hex 3 parse-nhex ;

\ -------------------------------------------------------------------------
\ copyright property handling
\ -------------------------------------------------------------------------

: insert-copyright-property
  \ As required for MacOS 9 and below
  " Pbclevtug 1983-2001 Nccyr Pbzchgre, Vap. GUVF ZRFFNTR SBE PBZCNGVOVYVGL BAYL"
  rot13-str encode-string " copyright"
  " /" find-package if
    " set-property" $find if
      execute
    else
      3drop drop
    then
  then
;

: delete-copyright-property
  \ Remove copyright property created above
  active-package
  " /" find-package if
      active-package!
      " copyright" delete-property
  then
  active-package!
;

: (exit)
  \ Clean up before returning to the interpreter
  delete-copyright-property
;

\ -------------------------------------------------------------------------
\ Adler-32 wrapper
\ -------------------------------------------------------------------------

: adler32 ( adler buf len -- checksum )
  " (adler32)" $find if
    execute
  else
    ." Can't find " ( adler32-name ) type cr
    3drop 0
  then
;

[IFDEF] CONFIG_DRIVER_XENOS
  -1 value xenos-driver-fcode
  " MSFT,xenos.bin" $encode-file to xenos-driver-fcode
[THEN]
