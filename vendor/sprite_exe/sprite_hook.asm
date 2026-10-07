; Sprite-clipping guard for the exact supported Medieval_TW.exe PE32 build.
; Assemble at HOOK_VA=0x00F4C000 (Sprite only) or 0x00F4E000
; (Scroll+Sprite). The original entry at 0x0073D47E is a 5-byte jump.
; No renderer proxy or timing change is involved.

BITS 32
%ifndef HOOK_VA
    %error HOOK_VA must be defined by the exact-identity builder
%endif
ORG HOOK_VA

%define ORIGINAL_BODY 0x00F43389
%define CLIP_WIDTH  0x00F44100
%define CLIP_HEIGHT 0x00F44104

start:
    push eax
    ; On entry: return, sprite, x, y, frame, max_width, max_height.
    ; Pushing EAX moves y to +16 and x to +12.
    mov eax, [esp + 16]
    cmp eax, [CLIP_HEIGHT]
    jge .outside
    mov eax, [esp + 12]
    cmp eax, [CLIP_WIDTH]
    jge .outside
    pop eax
    jmp ORIGINAL_BODY
.outside:
    pop eax
    ret
