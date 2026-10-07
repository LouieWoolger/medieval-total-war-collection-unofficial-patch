; Direct campaign camera timing for the exact MTW Gold PE32 build pinned by
; tools/build-scroll-exe-patch.py. This replaces the 6-byte pan-function
; prologue, then resumes at 0x627DD6. It never hooks a renderer or caps FPS.
; Build: nasm -f bin scroll_hook.asm -o scroll_hook.bin

BITS 32
ORG 0x00F4C000

%define ORIGINAL_CONTINUE 0x00627DD6
%define IAT_GET_MODULE_HANDLE_A 0x007841F8
%define IAT_GET_PROC_ADDRESS 0x007840A4
%define IAT_QUERY_PERFORMANCE_COUNTER 0x00784270

%define DATA_BASE 0x00F4D000
%define FREQ (DATA_BASE + 0)
%define BUSY (DATA_BASE + 8)
%define SEEN (DATA_BASE + 12)
%define LAST (DATA_BASE + 24)

; Original stack: return, direction, requested double.
; After pushad/sub esp,24: return +56, direction +60, double +64.
start:
    pushad
    sub esp, 24
    mov eax, [esp + 56]
    mov edi, pan_returns
    mov ecx, 12
    repne scasd
    jne .original
    xor esi, esi
    cmp ecx, 4
    jae .path_chosen
    mov esi, 4
.path_chosen:
    mov ebx, [esp + 60]
    cmp ebx, 1
    jbe .direction_chosen
    cmp ebx, 6
    jb .original
    cmp ebx, 7
    ja .original
    sub ebx, 4
.direction_chosen:
    add esi, ebx
    mov eax, [esp + 64]
    or eax, [esp + 68]
    jne .original

    ; Campaign update is normally single-threaded; exclude any nested call
    ; so the eight direction clocks cannot be torn by reentrancy.
    lock bts dword [BUSY], 0
    jc .skip_without_lock

    mov eax, [FREQ]
    or eax, [FREQ + 4]
    jnz .frequency_ready
    push kernel32_name
    call dword [IAT_GET_MODULE_HANDLE_A]
    test eax, eax
    jz .unlock_original
    push qpf_name
    push eax
    call dword [IAT_GET_PROC_ADDRESS]
    test eax, eax
    jz .unlock_original
    push dword FREQ
    call eax
    test eax, eax
    jz .unlock_original
.frequency_ready:
    mov eax, [FREQ]
    or eax, [FREQ + 4]
    jz .unlock_original
    test dword [FREQ + 4], 0x80000000
    jnz .unlock_original
    lea eax, [esp]
    push eax
    call dword [IAT_QUERY_PERFORMANCE_COUNTER]
    test eax, eax
    jz .unlock_original

    mov edi, esi
    shl edi, 3
    add edi, LAST
    cmp byte [SEEN + esi], 0
    je .first_step

    ; Clock discontinuity, or most recent movement in the opposite direction.
    mov eax, [esp + 4]
    cmp eax, [edi + 4]
    jb .first_step
    ja .check_reversal
    mov eax, [esp]
    cmp eax, [edi]
    jb .first_step
.check_reversal:
    mov ebx, esi
    xor ebx, 1
    cmp byte [SEEN + ebx], 0
    je .compute_elapsed
    shl ebx, 3
    add ebx, LAST
    mov eax, [ebx + 4]
    cmp eax, [edi + 4]
    ja .first_step
    jb .compute_elapsed
    mov eax, [ebx]
    cmp eax, [edi]
    ja .first_step

.compute_elapsed:
    mov eax, [esp]
    sub eax, [edi]
    mov [esp + 8], eax
    mov eax, [esp + 4]
    sbb eax, [edi + 4]
    mov [esp + 12], eax
    mov eax, [esp]
    mov [edi], eax
    mov eax, [esp + 4]
    mov [edi + 4], eax
    mov eax, [esp + 8]
    or eax, [esp + 12]
    jz .unlock_skip

    ; distance = 6000 game units/second * elapsed QPC time.
    ; A >100 ms pause contributes one normal reference step on resume.
    fild qword [esp + 8]
    fild qword [FREQ]
    fdivp st1, st0
    fmul qword [scale_6000]
    fcom qword [max_stall_distance]
    fnstsw ax
    sahf
    fstp qword [esp + 16]
    ja .set_reference_distance
    mov eax, [esp + 16]
    mov [esp + 64], eax
    mov eax, [esp + 20]
    mov [esp + 68], eax
    jmp .unlock_continue

.first_step:
    mov eax, [esp]
    mov [edi], eax
    mov eax, [esp + 4]
    mov [edi + 4], eax
    mov byte [SEEN + esi], 1
.set_reference_distance:
    mov dword [esp + 64], 0
    mov dword [esp + 68], 0x40590000 ; 100.0
.unlock_continue:
    lock btr dword [BUSY], 0
.original:
    add esp, 24
    popad
    push ebp
    mov ebp, esp
    sub esp, 8
    jmp ORIGINAL_CONTINUE

.unlock_original:
    lock btr dword [BUSY], 0
    jmp .original
.unlock_skip:
    lock btr dword [BUSY], 0
.skip_without_lock:
    add esp, 24
    popad
    mov eax, 1
    ret

align 4
pan_returns:
    dd 0x006269F7, 0x00626A1A, 0x00626A36, 0x00626A59
    dd 0x00626BC6, 0x00626BDD, 0x00626BF4, 0x00626C0B
    dd 0x0062E9F5, 0x0062EA04, 0x0062EA21, 0x0062EA34
scale_6000: dq 6000.0
max_stall_distance: dq 600.0
kernel32_name: db 'kernel32.dll', 0
qpf_name: db 'QueryPerformanceFrequency', 0
