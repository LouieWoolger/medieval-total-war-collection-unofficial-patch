; Modifier-aware held-key tests for the game's original Controls bindings.
; The normal collision-clearing code at 0x7116D2 is left intact. Plain
; Backspace is enabled by the separate one-byte reservation flag transform.
;
; Bindings store VK | Shift:0x10000 | Ctrl:0x20000 | Alt:0x40000.
; The event dispatcher already compares these bits. The three held-key
; consumers did not, so a Ctrl+K binding also reacted to bare K.
bits 32
%ifndef KEY_CODE_VA
%error KEY_CODE_VA must be set by the builder
%endif
org KEY_CODE_VA

; Replacement for 0x4D5D80. Argument [esp+4] is a configurable action
; index. Modified bindings need their exact chord. Plain bindings retain
; the game's original extra-modifier behavior unless that exact chord is
; assigned to another configurable action. The original focus guard stays.
held_entry:
    cmp dword [0xB429B0], 0
    jne .inactive
    push ebx
    push esi
    mov eax, [esp+12]
    mov esi, [0xE78174 + eax*4]
    mov ebx, esi
    and ebx, 0xFFFF
    test ebx, ebx
    jz .false
    push ebx
    call dword [0x7842C0]       ; GetKeyState(base VK)
    test eax, 0x8000
    jz .false
    xor ebx, ebx
    push byte 0x10
    call dword [0x7842C0]       ; VK_SHIFT
    test eax, 0x8000
    jz .no_shift
    or ebx, 0x10000
.no_shift:
    push byte 0x11
    call dword [0x7842C0]       ; VK_CONTROL
    test eax, 0x8000
    jz .no_ctrl
    or ebx, 0x20000
.no_ctrl:
    push byte 0x12
    call dword [0x7842C0]       ; VK_MENU / Alt
    test eax, 0x8000
    jz .no_alt
    or ebx, 0x40000
.no_alt:
    call binding_match
    jmp .done
.false:
    xor eax, eax
.done:
    pop esi
    pop ebx
    ret
.inactive:
    xor eax, eax
    ret

times 0x200 - ($ - $$) db 0x90

; Replacement for 0x4CC040 (Show Target Ghosts). This caller uses the
; inverse result: 0 shows the ghost, 1 hides it. Its [ecx+4] selection
; guard and GetAsyncKeyState input source are preserved.
ghost_entry:
    cmp byte [ecx+4], 0
    je .hidden
    push ebx
    push esi
    mov esi, [0xE781D4]        ; configurable action 24
    mov ebx, esi
    and ebx, 0xFFFF
    test ebx, ebx
    jz .false
    push ebx
    call dword [0x7842BC]       ; GetAsyncKeyState(base VK)
    test eax, 0x8000
    jz .false
    xor ebx, ebx
    push byte 0x10
    call dword [0x7842BC]
    test eax, 0x8000
    jz .no_shift
    or ebx, 0x10000
.no_shift:
    push byte 0x11
    call dword [0x7842BC]
    test eax, 0x8000
    jz .no_ctrl
    or ebx, 0x20000
.no_ctrl:
    push byte 0x12
    call dword [0x7842BC]
    test eax, 0x8000
    jz .no_alt
    or ebx, 0x40000
.no_alt:
    call binding_match
    xor eax, 1
    jmp .done
.false:
    mov eax, 1
.done:
    pop esi
    pop ebx
    ret
.hidden:
    mov eax, 1
    ret

; esi = saved binding, ebx = currently held modifier bits. The base key
; has already passed its GetKeyState/GetAsyncKeyState test. A plain binding
; keeps working with incidental modifiers as in the unpatched game, but a
; matching configurable chord gets priority over the plain held action.
binding_match:
    mov edx, esi
    and edx, 0x70000
    test edx, edx
    jz .plain
    cmp ebx, edx
    sete al
    movzx eax, al
    ret
.plain:
    test ebx, ebx
    jz .yes
    and esi, 0xffff
    or ebx, esi
    xor edx, edx
.scan:
    cmp [0xE78174 + edx*4], ebx
    je .no
    inc edx
    cmp edx, 63
    jb .scan
.yes:
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret

times 0x400 - ($ - $$) db 0

; Replacement for the initial fixed-first setup at 0x4C8E56. A newly
; assigned configurable control owns its exact key/chord in battle; the
; fixed slots are skipped for that event. Untouched factory bindings keep
; the game's original fixed-first order, including intentional duplicates.
; Preserve registers and flags that the original two MOVs did not alter.
key_dispatch_entry:
    pushfd
    push eax
    push ecx
    push edx
    ; The Controls Reset command selects one of five native tables through
    ; this same object and pointer array. Keep their layout-specific defaults.
    mov edx, [0x95E7C0]
    test edx, edx
    jz .default
    mov edx, [edx+0x0C]
    cmp edx, 4
    ja .default
    mov edx, [0x8957DC + edx*4]
    test edx, edx
    jz .default
    mov esi, 62
.probe:
    mov eax, [0xE78174 + esi*4]
    mov ecx, eax
    xor ecx, edi
    test ecx, 0x7FFFF
    jnz .next
    xor eax, [edx + esi*4]
    test eax, 0x7FFFF
    jnz .custom
.next:
    dec esi
    jns .probe
.default:
    pop edx
    pop ecx
    pop eax
    popfd
    mov esi, 112
    mov dword [ebp+0x14], 0xE78334
    jmp 0x4C8E62
.custom:
    pop edx
    pop ecx
    pop eax
    popfd
    mov esi, 62
    mov dword [ebp+0x14], 0xE7826C
    jmp 0x4C8E62

times 0x700 - ($ - $$) db 0

; The Controls label builder calls GetKeyNameTextA for ordinary keys, then
; feeds the result to the game's bitmap font. That font discards one-character
; punctuation names, leaving a working assignment visibly blank. Preserve the
; OS/layout-derived character and expand only those characters to words.
; The original stdcall and all non-punctuation names retain their behavior.
key_label_entry:
    push dword [esp+12]
    push dword [esp+12]
    push dword [esp+12]
    call dword [0x7842E0]   ; GetKeyNameTextA
    cmp eax, 1
    jne .return
    push ebx
    push esi
    mov ebx, [esp+16]       ; lpString after two saved registers
    mov dl, [ebx]
    mov esi, key_names
.scan:
    cmp byte [esi], 0
    je .no_match
    cmp dl, [esi]
    je .match
    add esi, 8
    jmp .scan
.match:
    mov esi, [esi+4]
    xor ecx, ecx
.copy:
    mov dl, [esi+ecx]
    mov [ebx+ecx], dl
    inc ecx
    test dl, dl
    jnz .copy
    lea eax, [ecx-1]
.no_match:
    pop esi
    pop ebx
.return:
    ret 12
align 4
key_names:
    db 0x3B,0,0,0
    dd n_semicolon
    db 0x3D,0,0,0
    dd n_equals
    db 0x2C,0,0,0
    dd n_comma
    db 0x2D,0,0,0
    dd n_minus
    db 0x2E,0,0,0
    dd n_period
    db 0x2F,0,0,0
    dd n_slash
    db 0x60,0,0,0
    dd n_grave
    db 0x5B,0,0,0
    dd n_left_bracket
    db 0x5C,0,0,0
    dd n_backslash
    db 0x5D,0,0,0
    dd n_right_bracket
    db 0x27,0,0,0
    dd n_apostrophe
    db 0,0,0,0
    dd 0
n_semicolon: db 'SEMICOLON',0
n_equals: db 'EQUALS',0
n_comma: db 'COMMA',0
n_minus: db 'MINUS',0
n_period: db 'PERIOD',0
n_slash: db 'SLASH',0
n_grave: db 'GRAVE',0
n_left_bracket: db 'LEFT BRACKET',0
n_backslash: db 'BACKSLASH',0
n_right_bracket: db 'RIGHT BRACKET',0
n_apostrophe: db 'APOSTROPHE',0
