; $Id$
;; @file
; BS3Kit - Trap, 16-bit assembly handlers.
;

;
; Copyright (C) 2007-2016 Oracle Corporation
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License (GPL) as published by the Free Software
; Foundation, in version 2 as it comes in the "COPYING" file of the
; VirtualBox OSE distribution. VirtualBox OSE is distributed in the
; hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
;
; The contents of this file may alternatively be used under the terms
; of the Common Development and Distribution License Version 1.0
; (CDDL) only, as it comes in the "COPYING.CDDL" file of the
; VirtualBox OSE distribution, in which case the provisions of the
; CDDL are applicable instead of those of the GPL.
;
; You may elect to license modified versions of this file under the
; terms and conditions of either the GPL or the CDDL or both.
;

;*********************************************************************************************************************************
;*  Header Files                                                                                                                 *
;*********************************************************************************************************************************
%include "bs3kit-template-header.mac"

%ifndef TMPL_16BIT
 %error "16-bit only template"
%endif


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
BS3_EXTERN_DATA16 g_bBs3CurrentMode
BS3_EXTERN_DATA16 g_uBs3TrapEipHint
BS3_EXTERN_SYSTEM16 Bs3Gdt
TMPL_BEGIN_TEXT
BS3_EXTERN_CMN Bs3TrapDefaultHandler
BS3_EXTERN_CMN Bs3RegCtxRestore
TMPL_BEGIN_TEXT


;*********************************************************************************************************************************
;*  Global Variables                                                                                                             *
;*********************************************************************************************************************************
BS3_BEGIN_DATA16
;; Pointer C trap handlers (BS3TEXT16).
BS3_GLOBAL_DATA g_apfnBs3TrapHandlers_c16, 512
        resw 256


TMPL_BEGIN_TEXT

;;
; Generic entry points for IDT handlers, 8 byte spacing.
;
BS3_PROC_BEGIN _Bs3Trap16GenericEntries
BS3_PROC_BEGIN Bs3Trap16GenericEntries
%macro Bs3Trap16GenericEntry 1
        db      06ah, i                 ; push imm8 - note that this is a signextended value.
        jmp     %1
        ALIGNCODE(8)
%assign i i+1
%endmacro

%assign i 0                             ; start counter.
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 0
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 1
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 2
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 3
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 4
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 5
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 6
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 7
        Bs3Trap16GenericEntry bs3Trap16GenericTrapErrCode ; 8
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 9
        Bs3Trap16GenericEntry bs3Trap16GenericTrapErrCode ; a
        Bs3Trap16GenericEntry bs3Trap16GenericTrapErrCode ; b
        Bs3Trap16GenericEntry bs3Trap16GenericTrapErrCode ; c
        Bs3Trap16GenericEntry bs3Trap16GenericTrapErrCode ; d
        Bs3Trap16GenericEntry bs3Trap16GenericTrapErrCode ; e
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; f  (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 10
        Bs3Trap16GenericEntry bs3Trap16GenericTrapErrCode ; 11
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 12
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 13
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 14
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 15 (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 16 (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 17 (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 18 (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 19 (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 1a (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 1b (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 1c (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 1d (reserved)
        Bs3Trap16GenericEntry bs3Trap16GenericTrapErrCode ; 1e
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt   ; 1f (reserved)
%rep 224
        Bs3Trap16GenericEntry bs3Trap16GenericTrapOrInt
%endrep
BS3_PROC_END  Bs3Trap16GenericEntries




;;
; 80386+: Trap or interrupt (no error code).
;
BS3_PROC_BEGIN _bs3Trap16GenericTrapOrInt
BS3_PROC_BEGIN bs3Trap16GenericTrapOrInt
CPU 386
        jmp     near bs3Trap16GenericTrapOrInt80286 ; Bs3Trap16Init adjusts this on 80386+
        push    ebp
        mov     bp, sp
        push    ebx
        pushfd
        cli
        cld

        ; Reserve space for the the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 7) / 8
.more_zeroed_space:
        push    0
        push    0
        push    0
        push    0
        dec     bx
        jnz     .more_zeroed_space
        movzx   ebx, sp

        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], eax
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], edx
        mov     edx, [bp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], edx
        mov     edx, [bp - 4]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], edx

        mov     edx, [bp - 8]
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], edx

        mov     dl, [bp + 4]
        mov     [ss:bx + BS3TRAPFRAME.bXcpt], dl

        add     bp, 4                   ; adjust so it points to the word before the iret frame.
        xor     dx, dx
        jmp     bs3Trap16GenericCommon
BS3_PROC_END   bs3Trap16GenericTrapOrInt


;;
; 80286: Trap or interrupt (no error code)
;
BS3_PROC_BEGIN bs3Trap16GenericTrapOrInt80286
CPU 286
        push    bp
        mov     bp, sp
        push    bx
        pushf
        cli
        cld

        ; Reserve space for the the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 7) / 8
.more_zeroed_space:
        push    0
        push    0
        push    0
        push    0
        dec     bx
        jnz     .more_zeroed_space
        mov     bx, sp

        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], ax
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], dx
        mov     dx, [bp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], dx
        mov     dx, [bp - 2]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], dx

        mov     dl, [bp - 4]
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], dl

        mov     al, byte [bp + 4]
        mov     [ss:bx + BS3TRAPFRAME.bXcpt], al

        add     bp, 4                   ; adjust so it points to the word before the iret frame.
        mov     dx, 1
        jmp     bs3Trap16GenericCommon
BS3_PROC_END   bs3Trap16GenericTrapOrInt80286


;;
; Trap with error code.
;
BS3_PROC_BEGIN _bs3Trap16GenericTrapErrCode
BS3_PROC_BEGIN bs3Trap16GenericTrapErrCode
CPU 386
        jmp     near bs3Trap16GenericTrapOrInt80286 ; Bs3Trap16Init adjusts this on 80386+
        push    ebp
        mov     bp, sp
        push    ebx
        pushfd
        cli
        cld

        ; Reserve space for the the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 7) / 8
.more_zeroed_space:
        push    0
        push    0
        push    0
        push    0
        dec     bx
        jnz     .more_zeroed_space
        movzx   ebx, sp

        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], eax
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], edx
        mov     edx, [bp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], edx
        mov     edx, [bp - 4]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], ebx

        mov     edx, [bp - 8]
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], edx

        mov     dl, [bp + 4]
        mov     [ss:bx + BS3TRAPFRAME.bXcpt], dl

        mov     dx, [bp + 6]
;; @todo Do voodoo checks for 'int xx' or misguided hardware interrupts.
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], dx

        add     bp, 6                   ; adjust so it points to the word before the iret frame.
        xor     dx, dx
        jmp     bs3Trap16GenericCommon
BS3_PROC_END   bs3Trap16GenericTrapErrCode

;;
; Trap with error code - 80286 code variant.
;
BS3_PROC_BEGIN bs3Trap16GenericTrapErrCode80286
CPU 286
        push    bp
        mov     bp, sp
        push    bx
        pushf
        cli
        cld

        ; Reserve space for the the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 7) / 8
.more_zeroed_space:
        push    0
        push    0
        push    0
        push    0
        dec     bx
        jnz     .more_zeroed_space
        mov     bx, sp

        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], ax
        mov     [bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], dx
        mov     dx, [bp]
        mov     [bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], dx
        mov     dx, [bp - 2]
        mov     [bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], bx

        mov     dx, [bp - 4]
        mov     [bx + BS3TRAPFRAME.fHandlerRfl], dx

        mov     dl, [bp + 2]
        mov     [bx + BS3TRAPFRAME.bXcpt], dl

        mov     dx, [bp + 4]
;; @todo Do voodoo checks for 'int xx' or misguided hardware interrupts.
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], dx

        add     bp, 4                   ; adjust so it points to the word before the iret frame.
        mov     dl, 1
        jmp     bs3Trap16GenericCommon
BS3_PROC_END   bs3Trap16GenericTrapErrCode80286


;;
; Common context saving code and dispatching.
;
; @param    bx      Pointer to the trap frame, zero filled.  The following members
;                   have been filled in by the previous code:
;                       - bXcpt
;                       - uErrCd
;                       - fHandlerRFL
;                       - Ctx.eax (except upper stuff)
;                       - Ctx.edx (except upper stuff)
;                       - Ctx.ebx (except upper stuff)
;                       - Ctx.ebp (except upper stuff)
;                       - All other bytes are zeroed.
;
; @param    bp      Pointer to the word before the iret frame, i.e. where bp
;                   would be saved if this was a normal near call.
; @param    dx      One (1) if 286, zero (0) if 386+.
;
BS3_PROC_BEGIN bs3Trap16GenericCommon
CPU 286
        ;
        ; Fake EBP frame.
        ;
        mov     ax, [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp]
        mov     [bp], ax

        ;
        ; Save the remaining GPRs and segment registers.
        ;
        test    dx, dx
        jnz     .save_word_grps
CPU 386
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], ecx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], edi
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], esi
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], esp ; high word
        mov     ecx, [ss:bx + BS3TRAPFRAME.fHandlerRfl]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], ecx
        jmp     .save_segment_registers
.save_word_grps:
CPU 286
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], cx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], di
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], si
.save_segment_registers:
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], ds
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.es], es
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], fs
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], gs

        ;
        ; Load 16-bit data selector for the DPL we're executing at into DS and ES.
        ; Save the handler SS and CS values first.
        ;
        mov     ax, cs
        mov     [ss:bx + BS3TRAPFRAME.uHandlerCs], ax
        mov     ax, ss
        mov     [ss:bx + BS3TRAPFRAME.uHandlerSs], ax
        and     ax, 3
        mov     cx, ax
        shl     ax, BS3_SEL_RING_SHIFT
        or      ax, cx
        add     ax, BS3_SEL_R0_DS16
        mov     ds, ax
        mov     es, ax

        ;
        ; Copy and update the mode now that we've got a flat DS.
        ;
        mov     al, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], al
        mov     cl, al
        and     cl, ~BS3_MODE_CODE_MASK
        or      cl, BS3_MODE_CODE_16
        mov     [BS3_DATA16_WRT(g_bBs3CurrentMode)], cl

        ;
        ; Copy iret info.
        ;
        mov     cx, [bp + 2]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rip], cx
        mov     cx, [bp + 6]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], cx
        mov     cx, [bp + 4]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cs], cx

        and     al, BS3_MODE_CODE_MASK
        cmp     al, BS3_MODE_CODE_V86
        je      .iret_frame_v8086

        mov     ax, ss
        and     al, 3
        and     cl, 3
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], cl
        cmp     cl, al
        je      .iret_frame_same_cpl

.ret_frame_different_cpl:
        mov     cx, [bp + 10]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        test    dx, dx
        jnz     .ret_frame_different_cpl_286
.ret_frame_different_cpl_386:
CPU 386
        mov     ecx, esp
        mov     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], ecx
        lea     eax, [bp + 12]
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], eax
        jmp     .iret_frame_seed_high_eip_word
.ret_frame_different_cpl_286:
CPU 286
        mov     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], cx
        lea     ax, [bp + 12]
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], ax
        jmp     .iret_frame_done

.iret_frame_same_cpl:
        mov     cx, ss
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        test    dx, dx
        jnz     .iret_frame_same_cpl_286
.iret_frame_same_cpl_386:
CPU 386
        mov     ecx, esp
        lea     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], ecx
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], ecx
        jmp     .iret_frame_seed_high_eip_word
.iret_frame_same_cpl_286:
CPU 286
        lea     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], cx
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], cx
        jmp     .iret_frame_done

.iret_frame_v8086:
CPU 386
        or      dword [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], X86_EFL_VM
        mov     byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], 3
        or      byte [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], BS3_MODE_CODE_V86 ; paranoia ^ 2
        movzx   ecx, word [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], ecx
        mov     cx, [bp + 10]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        mov     cx, [bp + 12]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.es], cx
        mov     cx, [bp + 14]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], cx
        mov     cx, [bp + 16]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], cx
        mov     cx, [bp + 18]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], cx
        lea     eax, [bp + 20]
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], eax
        jmp     .iret_frame_done

        ;
        ; For 386 we do special tricks to supply the high word of EIP when
        ; arriving here from 32-bit code. (ESP was seeded earlier.)
        ;
.iret_frame_seed_high_eip_word:
        lar     eax, [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cs]
        jnz     .iret_frame_done
        test    eax, X86LAR_F_D
        jz      .iret_frame_done
        mov     ax, [g_uBs3TrapEipHint+2]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rip + 2], ax

.iret_frame_done:
        ;
        ; Control registers.
        ;
        str     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.tr]
        sldt    [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ldtr]
        test    dx, dx
        jnz     .save_286_control_registers
.save_386_control_registers:
CPU 386
        mov     eax, cr0
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0], eax
        mov     eax, cr2
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr2], eax
        mov     eax, cr3
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr3], eax
        mov     eax, cr4
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr4], eax
        jmp     .dispatch_to_handler
CPU 286
.save_286_control_registers:
        smsw    [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0]

        ;
        ; Dispatch it to C code.
        ;
.dispatch_to_handler:                   ; The double fault code joins us here.
        mov     di, bx
        mov     bl, byte [ss:bx + BS3TRAPFRAME.bXcpt]
        mov     bh, 0
        shl     bx, 1
        mov     bx, [bx + BS3_DATA16_WRT(_g_apfnBs3TrapHandlers_c16)]
        or      bx, bx
        jnz     .call_handler
        mov     bx, Bs3TrapDefaultHandler
.call_handler:
        push    ss
        push    di
        call    bx

        ;
        ; Resume execution using trap frame.
        ;
        push    0
        push    ss
        add     di, BS3TRAPFRAME.Ctx
        push    di
        call    Bs3RegCtxRestore
.panic:
        hlt
        jmp     .panic
BS3_PROC_END   bs3Trap16GenericCommon


;;
; Helper.
;
; @retruns  Flat address in es:di.
; @param    di
; @uses     eax
;
bs3Trap16TssInDiToFar1616InEsDi:
CPU 286
        push    ax

        ; ASSUME Bs3Gdt is being used.
        push    BS3_SEL_SYSTEM16
        pop     es
        and     di, 0fff8h
        add     di, Bs3Gdt wrt BS3SYSTEM16

        ; Load the TSS base into ax:di (di is low, ax high)
        mov     al, [es:di + (X86DESCGENERIC_BIT_OFF_BASE_HIGH1 / 8)]
        mov     ah, [es:di + (X86DESCGENERIC_BIT_OFF_BASE_HIGH2 / 8)]
        mov     di, [es:di + (X86DESCGENERIC_BIT_OFF_BASE_LOW / 8)]

        ; Convert ax to tiled selector, if not within the tiling area we read
        ; random BS3SYSTEM16 bits as that's preferable to #GP'ing.
        shl     ax, X86_SEL_SHIFT
        cmp     ax, BS3_SEL_TILED_LAST - BS3_SEL_TILED
%ifdef BS3_STRICT
        jbe     .tiled
        int3
%endif
        ja      .return                 ; don't crash again.
.tiled:
        add     ax, BS3_SEL_TILED
        mov     es, ax
.return:
        pop     ax
        ret


;;
; Double fault handler.
;
; We don't have to load any selectors or clear anything in EFLAGS because the
; TSS specified sane values which got loaded during the task switch.
;
; @param    dx      Zero (0) for indicating 386+ to the common code.
;
BS3_PROC_BEGIN _Bs3Trap16DoubleFaultHandler80386
BS3_PROC_BEGIN Bs3Trap16DoubleFaultHandler80386
CPU 386
        push    0                       ; We'll copy the rip from the other TSS here later to create a more sensible call chain.
        push    ebp
        mov     bp, sp
        pushfd                          ; Handler flags.

        ; Reserve space for the the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 15) / 16
.more_zeroed_space:
        push    dword 0
        push    dword 0
        push    dword 0
        push    dword 0
        dec     bx
        jz      .more_zeroed_space
        mov     bx, sp

        ;
        ; Fill in the high GRP register words before we mess them up.
        ;
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], eax
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], ebx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], ecx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], edx
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], esi
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], edi
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], ebp
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], esp

        ;
        ; FS and GS are not part of the 16-bit TSS because they are 386+ specfic.
        ;
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.fs], fs
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.gs], gs

        ;
        ; Fill in the non-context trap frame bits.
        ;
        mov     ecx, [bp - 4]
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], ecx
        mov     byte [ss:bx + BS3TRAPFRAME.bXcpt], X86_XCPT_DF
        mov     [ss:bx + BS3TRAPFRAME.uHandlerCs], cs
        mov     [ss:bx + BS3TRAPFRAME.uHandlerSs], ss
        mov     ecx, esp
        lea     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], ecx
        mov     cx, [bp + 6]
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], cx

        ;
        ; Copy 80386+ control registers.
        ;
        mov     ecx, cr0
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0], ecx
        mov     ecx, cr2
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr2], ecx
        mov     ecx, cr3
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr3], ecx
        mov     ecx, cr4
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr4], ecx

        ;
        ; Copy the register state from the previous task segment.
        ; The 80286 code with join us here.
        ;
.common:
CPU 286
        ; Find our TSS.
        str     di
        call    bs3Trap16TssInDiToFar1616InEsDi

        ; Find the previous TSS.
        mov     di, [es:di + X86TSS32.selPrev]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.tr], ax
        call    bs3Trap16TssInDiToFar1616InEsDi

        ; Do the copying.
        mov     cx, [es:di + X86TSS16.ax]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rax], cx
        mov     cx, [es:di + X86TSS16.cx]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rcx], cx
        mov     cx, [es:di + X86TSS16.dx]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdx], cx
        mov     cx, [es:di + X86TSS16.bx]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbx], cx
        mov     cx, [es:di + X86TSS16.sp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsp], cx
        mov     cx, [es:di + X86TSS16.bp]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rbp], cx
        mov     [bp], cx                ; For better call stacks.
        mov     cx, [es:di + X86TSS16.si]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], cx
        mov     cx, [es:di + X86TSS16.di]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rdi], cx
        mov     cx, [es:di + X86TSS16.si]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rsi], cx
        mov     cx, [es:di + X86TSS16.flags]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rflags], cx
        mov     cx, [es:di + X86TSS16.ip]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.rip], cx
        mov     [bp + 2], cx            ; For better call stacks.
        mov     cx, [eax + X86TSS16.cs]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cs], cx
        mov     cx, [eax + X86TSS16.ds]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ds], cx
        mov     cx, [eax + X86TSS16.es]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.es], cx
        mov     cx, [eax + X86TSS16.ss]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss], cx
        mov     cx, [eax + X86TSS16.selLdt]             ; Note! This isn't necessarily the ldtr at the time of the fault.
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ldtr], cx

        ;
        ; Set CPL; copy and update mode.
        ;
        mov     cl, [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.ss]
        and     cl, 3
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bCpl], cl

        mov     cl, [BS3_DATA16_WRT(g_bBs3CurrentMode)]
        mov     [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.bMode], cl
        and     cl, ~BS3_MODE_CODE_MASK
        or      cl, BS3_MODE_CODE_32
        mov     [BS3_DATA16_WRT(g_bBs3CurrentMode)], cl

        ;
        ; Join code paths with the generic handler code.
        ;
        jmp     bs3Trap16GenericCommon.dispatch_to_handler
BS3_PROC_END   Bs3Trap16DoubleFaultHandler


;;
; Double fault handler.
;
; We don't have to load any selectors or clear anything in EFLAGS because the
; TSS specified sane values which got loaded during the task switch.
;
; @param    dx      One (1) for indicating 386+ to the common code.
;
BS3_PROC_BEGIN _Bs3Trap16DoubleFaultHandler80286
BS3_PROC_BEGIN Bs3Trap16DoubleFaultHandler80286
CPU 286
        push    0                       ; We'll copy the rip from the other TSS here later to create a more sensible call chain.
        push    bp
        mov     bp, sp
        pushf                           ; Handler flags.

        ; Reserve space for the the register and trap frame.
        mov     bx, (BS3TRAPFRAME_size + 7) / 8
.more_zeroed_space:
        push    0
        push    0
        push    0
        push    0
        dec     bx
        jz      .more_zeroed_space
        mov     bx, sp

        ;
        ; Fill in the non-context trap frame bits.
        ;
        mov     cx, [bp - 2]
        mov     [ss:bx + BS3TRAPFRAME.fHandlerRfl], cx
        mov     byte [ss:bx + BS3TRAPFRAME.bXcpt], X86_XCPT_DF
        mov     [ss:bx + BS3TRAPFRAME.uHandlerCs], cs
        mov     [ss:bx + BS3TRAPFRAME.uHandlerSs], ss
        lea     cx, [bp + 8]
        mov     [ss:bx + BS3TRAPFRAME.uHandlerRsp], cx
        mov     cx, [bp + 6]
        mov     [ss:bx + BS3TRAPFRAME.uErrCd], cx

        ;
        ; Copy 80286 specific control register.
        ;
        smsw    [ss:bx + BS3TRAPFRAME.Ctx + BS3REGCTX.cr0]

        jmp     Bs3Trap16DoubleFaultHandler80386.common
BS3_PROC_END   Bs3Trap16DoubleFaultHandler80286


