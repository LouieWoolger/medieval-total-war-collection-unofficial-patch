!ifndef UNOFFICIAL_MEDIEVAL_PATCH_UI_NSH
!define UNOFFICIAL_MEDIEVAL_PATCH_UI_NSH

!macro SET_TAHOMA CONTROL FONT_HANDLE
    SendMessage ${CONTROL} ${WM_SETFONT} ${FONT_HANDLE} 1
!macroend

!macro CHECK_FINISH_BADGE_HOVER HANDLE KEY
    System::Call "*(i 0, i 0, i 0, i 0) p.r2"
    System::Call "user32::GetWindowRect(p${HANDLE}, p r2)i.r3"
    ${If} $3 <> 0
        System::Call "*$2(i.r3, i.r4, i.r5, i.r6)"
        ${If} $0 >= $3
        ${AndIf} $0 <= $5
        ${AndIf} $1 >= $4
        ${AndIf} $1 <= $6
            StrCpy $R0 "${KEY}"
        ${EndIf}
    ${EndIf}
    System::Free $2
!macroend

!endif
