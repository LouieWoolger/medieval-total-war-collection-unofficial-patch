; Optional Huge (120-scalar) economy normalization for the pinned Gold PE32 EXE.
; The table contains section-relative entry offsets in the order used by the
; checked EXE patch sites. All other unit-size settings keep the stock paths.
bits 32
%ifndef BALANCE_CODE_VA
%error BALANCE_CODE_VA must be set by the builder
%endif
org BALANCE_CODE_VA

dd unit_support - $$
dd detail_support - $$
dd info_support - $$
dd time_scalar - $$
dd cost_scalar - $$
dd ai_scalar - $$
dd retrain_discounted - $$
dd retrain_regular - $$
dd overview_support - $$

; 0x550ADB replaces a call that consumes the pushed troop index. Preserve the
; stock troop lookup, then halve the pre-mounted upkeep for scalable Huge units.
unit_support:
    mov edx, [esp + 4]
    push edx
    call 0x4242C0
    push eax
    cmp dword [0xBE65F8], 120
    jne .done
    movzx edx, byte [eax + 0x0B]
    push edx
    call 0x72B2A0
    add esp, 4
    cmp byte [eax + 0x31C], 0
    je .done
    sar esi, 1
.done:
    pop eax
    ret 4

; The detail and information panels have their own pre-mounted totals.
detail_support:
    cmp dword [0xBE65F8], 120
    jne .done
    cmp byte [esi + 0x31C], 0
    je .done
    sar dword [esp + 0x44], 1
.done:
    jmp 0x4242C0

info_support:
    cmp dword [0xBE65F8], 120
    jne .done
    cmp byte [ebx + 0x31C], 0
    je .done
    sar edi, 1
.done:
    jmp 0x4242C0

; Recruitment and AI planning share the 60-scalar reference at Huge.
time_scalar:
    mov ecx, [0xBE65F8]
    cmp ecx, 120
    jne .done
    mov ecx, [0x791448]
.done:
    ret

cost_scalar:
    cmp dword [0xBE65F8], 120
    jne .original
    imul esi, dword [0x791448]
    ret
.original:
    imul esi, dword [0xBE65F8]
    ret

ai_scalar:
    cmp dword [0xBE65F8], 120
    jne .original
    imul ecx, dword [0x791448]
    ret
.original:
    imul ecx, dword [0xBE65F8]
    ret

; Replacement fraction uses the actual formation target at Huge.
retrain_discounted:
    mov edx, [edi + 0x344]
    cmp dword [0xBE65F8], 120
    jne .done
    mov edx, esi
.done:
    ret

retrain_regular:
    mov edi, [edi + 0x344]
    cmp dword [0xBE65F8], 120
    jne .done
    mov edi, esi
.done:
    ret

; The unit overview has a separate non-standard branch at 0x57A807. It must
; continue unchanged; only the regular scalable support calculation is halved.
overview_support:
    cmp byte [esp + 0x3A], 0
    jne .done
    cmp dword [0xBE65F8], 120
    jne .done
    cmp byte [esi + 0x31C], 0
    je .done
    sar eax, 1
.done:
    push eax
    lea ecx, [esp + 0x60]
    jmp 0x57A846
