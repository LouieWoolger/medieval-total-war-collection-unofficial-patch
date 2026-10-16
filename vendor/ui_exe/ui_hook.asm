; Character attribute parchment composition for pinned MTW PE32 builds.
; The four entry offsets are stored at the start of this private code section.
; No complete game executable or asset is distributed with the patch.
BITS 32
%ifndef UI_CODE_VA
    %error UI_CODE_VA must be supplied by the generator
%endif
org UI_CODE_VA

dd ui_compose_guard - $$
dd ui_cache_bltfast - $$
dd ui_dest_lock_guard - $$
dd ui_source_lock_guard - $$

; Displaced instruction at 0x58EBD8. Force the game's existing per-pixel
; colour-key composition only for the two campaign character parchment
; instances and only when its destination image contains the entire panel.
ui_compose_guard:
    mov ecx, [esi + 0x12c]
    cmp esi, [0xBE6638]
    je .character
    cmp esi, [0xBE663C]
    jne .native
.character:
    cmp dword [esi + 0x11c], 0
    jne .native
    cmp dword [esi + 0x118], 260
    jne .native
    cmp dword [esi + 0x120], 364
    jne .native
    cmp dword [esi + 0x124], 220
    jne .native
    cmp dword [esi + 0x128], 110
    jne .native
    cmp ecx, 364
    jne .native
    cmp dword [esi + 0xe0], 364
    jl .native
    cmp dword [esi + 0xe4], 370
    jl .native
    jmp 0x58EC33
.native:
    jmp 0x58EBDE

; Replace the rejected cache Blt at 0x591245 with a byte-equivalent
; source/destination BltFast. The existing caller checks EAX (HRESULT).
ui_cache_bltfast:
    push 0x10                   ; DDBLTFAST_WAIT
    push ecx                    ; source RECT (0,260,364,370)
    mov eax, [ebp + 0xdc]      ; source parchment surface
    push eax
    push edi                    ; dest y = 0
    push edi                    ; dest x = 0
    mov eax, [ebp + 0x134]     ; destination cache surface
    mov edx, [eax]
    push eax
    call dword [edx + 0x1c]    ; IDirectDrawSurface::BltFast
    jmp 0x591283

; The original manual fallback ignored Lock HRESULTs. On failure, leave the
; panel dirty so the next draw can retry and avoid dereferencing stale pixels.
ui_dest_lock_guard:
    add esp, 12                 ; caller's diagnostic logger arguments
    cmp dword [0x8DCD08], 0
    jne .failed
    mov [0x8D0248], edx        ; displaced global destination pointer store
    jmp 0x58EC8B
.failed:
    call 0x411FA0               ; original frame handling helper
    jmp 0x58EDBF               ; epilogue, without clearing dirty flag

ui_source_lock_guard:
    mov eax, [ebp - 0x7c]      ; displaced source pointer load
    mov ecx, [0x8D0248]        ; displaced destination pointer load
    cmp dword [0x8DCD08], 0
    jne .failed
    jmp 0x58ECC8
.failed:
    add esp, 12                 ; caller's diagnostic logger arguments
    mov eax, [ebp - 8]
    mov [0x8D0248], eax        ; restore previous global pointer
    mov eax, [ebp - 0xc]
    mov [0x8D0244], eax        ; restore previous global pitch
    mov eax, [esi + 0xdc]      ; only destination Lock succeeded
    mov edx, [eax]
    push ebx                    ; NULL Unlock rectangle; EBX is zero here
    push eax
    call dword [edx + 0x80]    ; IDirectDrawSurface::Unlock
    call 0x411FA0
    jmp 0x58EDBF
