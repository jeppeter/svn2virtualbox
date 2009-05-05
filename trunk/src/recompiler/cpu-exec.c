/*
 *  i386 emulator main execution loop
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Sun LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Sun elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */
#include "config.h"
#define CPU_NO_GLOBAL_REGS
#include "exec.h"
#include "disas.h"
#include "tcg.h"

#if !defined(CONFIG_SOFTMMU)
#undef EAX
#undef ECX
#undef EDX
#undef EBX
#undef ESP
#undef EBP
#undef ESI
#undef EDI
#undef EIP
#include <signal.h>
#include <sys/ucontext.h>
#endif

#if defined(__sparc__) && !defined(HOST_SOLARIS)
// Work around ugly bugs in glibc that mangle global register contents
#undef env
#define env cpu_single_env
#endif

int tb_invalidated_flag;

//#define DEBUG_EXEC
//#define DEBUG_SIGNAL


void cpu_loop_exit(void)
{
    /* NOTE: the register at this point must be saved by hand because
       longjmp restore them */
    regs_to_env();
    longjmp(env->jmp_env, 1);
}

#if !(defined(TARGET_SPARC) || defined(TARGET_SH4) || defined(TARGET_M68K))
#define reg_T2
#endif

/* exit the current TB from a signal handler. The host registers are
   restored in a state compatible with the CPU emulator
 */
void cpu_resume_from_signal(CPUState *env1, void *puc)
{
#if !defined(CONFIG_SOFTMMU)
    struct ucontext *uc = puc;
#endif

    env = env1;

    /* XXX: restore cpu registers saved in host registers */

#if !defined(CONFIG_SOFTMMU)
    if (puc) {
        /* XXX: use siglongjmp ? */
        sigprocmask(SIG_SETMASK, &uc->uc_sigmask, NULL);
    }
#endif
    longjmp(env->jmp_env, 1);
}

/* Execute the code without caching the generated code. An interpreter
   could be used if available. */
static void cpu_exec_nocache(int max_cycles, TranslationBlock *orig_tb)
{
    unsigned long next_tb;
    TranslationBlock *tb;

    /* Should never happen.
       We only end up here when an existing TB is too long.  */
    if (max_cycles > CF_COUNT_MASK)
        max_cycles = CF_COUNT_MASK;

    tb = tb_gen_code(env, orig_tb->pc, orig_tb->cs_base, orig_tb->flags,
                     max_cycles);
    env->current_tb = tb;
    /* execute the generated code */
#if defined(VBOX) && defined(GCC_WITH_BUGGY_REGPARM)
    tcg_qemu_tb_exec(tb->tc_ptr, next_tb);
#else
    next_tb = tcg_qemu_tb_exec(tb->tc_ptr);
#endif

    if ((next_tb & 3) == 2) {
        /* Restore PC.  This may happen if async event occurs before
           the TB starts executing.  */
        CPU_PC_FROM_TB(env, tb);
    }
    tb_phys_invalidate(tb, -1);
    tb_free(tb);
}

static TranslationBlock *tb_find_slow(target_ulong pc,
                                      target_ulong cs_base,
                                      uint64_t flags)
{
    TranslationBlock *tb, **ptb1;
    unsigned int h;
    target_ulong phys_pc, phys_page1, phys_page2, virt_page2;

    tb_invalidated_flag = 0;

    regs_to_env(); /* XXX: do it just before cpu_gen_code() */

    /* find translated block using physical mappings */
    phys_pc = get_phys_addr_code(env, pc);
    phys_page1 = phys_pc & TARGET_PAGE_MASK;
    phys_page2 = -1;
    h = tb_phys_hash_func(phys_pc);
    ptb1 = &tb_phys_hash[h];
    for(;;) {
        tb = *ptb1;
        if (!tb)
            goto not_found;
        if (tb->pc == pc &&
            tb->page_addr[0] == phys_page1 &&
            tb->cs_base == cs_base &&
            tb->flags == flags) {
            /* check next page if needed */
            if (tb->page_addr[1] != -1) {
                virt_page2 = (pc & TARGET_PAGE_MASK) +
                    TARGET_PAGE_SIZE;
                phys_page2 = get_phys_addr_code(env, virt_page2);
                if (tb->page_addr[1] == phys_page2)
                    goto found;
            } else {
                goto found;
            }
        }
        ptb1 = &tb->phys_hash_next;
    }
 not_found:
   /* if no translated code available, then translate it now */
    tb = tb_gen_code(env, pc, cs_base, flags, 0);

 found:
    /* we add the TB in the virtual pc hash table */
    env->tb_jmp_cache[tb_jmp_cache_hash_func(pc)] = tb;
    return tb;
}

#ifndef VBOX
static inline TranslationBlock *tb_find_fast(void)
#else
DECLINLINE(TranslationBlock *) tb_find_fast(void)
#endif
{
    TranslationBlock *tb;
    target_ulong cs_base, pc;
    uint64_t flags;

    /* we record a subset of the CPU state. It will
       always be the same before a given translated block
       is executed. */
#if defined(TARGET_I386)
    flags = env->hflags;
    flags |= (env->eflags & (IOPL_MASK | TF_MASK | VM_MASK));
    cs_base = env->segs[R_CS].base;
    pc = cs_base + env->eip;
#elif defined(TARGET_ARM)
    flags = env->thumb | (env->vfp.vec_len << 1)
            | (env->vfp.vec_stride << 4);
    if ((env->uncached_cpsr & CPSR_M) != ARM_CPU_MODE_USR)
        flags |= (1 << 6);
    if (env->vfp.xregs[ARM_VFP_FPEXC] & (1 << 30))
        flags |= (1 << 7);
    flags |= (env->condexec_bits << 8);
    cs_base = 0;
    pc = env->regs[15];
#elif defined(TARGET_SPARC)
#ifdef TARGET_SPARC64
    // AM . Combined FPU enable bits . PRIV . DMMU enabled . IMMU enabled
    flags = ((env->pstate & PS_AM) << 2)
        | (((env->pstate & PS_PEF) >> 1) | ((env->fprs & FPRS_FEF) << 2))
        | (env->pstate & PS_PRIV) | ((env->lsu & (DMMU_E | IMMU_E)) >> 2);
#else
    // FPU enable . Supervisor
    flags = (env->psref << 4) | env->psrs;
#endif
    cs_base = env->npc;
    pc = env->pc;
#elif defined(TARGET_PPC)
    flags = env->hflags;
    cs_base = 0;
    pc = env->nip;
#elif defined(TARGET_MIPS)
    flags = env->hflags & (MIPS_HFLAG_TMASK | MIPS_HFLAG_BMASK);
    cs_base = 0;
    pc = env->active_tc.PC;
#elif defined(TARGET_M68K)
    flags = (env->fpcr & M68K_FPCR_PREC)  /* Bit  6 */
            | (env->sr & SR_S)            /* Bit  13 */
            | ((env->macsr >> 4) & 0xf);  /* Bits 0-3 */
    cs_base = 0;
    pc = env->pc;
#elif defined(TARGET_SH4)
    flags = (env->flags & (DELAY_SLOT | DELAY_SLOT_CONDITIONAL
                    | DELAY_SLOT_TRUE | DELAY_SLOT_CLEARME))   /* Bits  0- 3 */
            | (env->fpscr & (FPSCR_FR | FPSCR_SZ | FPSCR_PR))  /* Bits 19-21 */
            | (env->sr & (SR_MD | SR_RB));                     /* Bits 29-30 */
    cs_base = 0;
    pc = env->pc;
#elif defined(TARGET_ALPHA)
    flags = env->ps;
    cs_base = 0;
    pc = env->pc;
#elif defined(TARGET_CRIS)
    flags = env->pregs[PR_CCS] & (S_FLAG | P_FLAG | U_FLAG | X_FLAG);
    flags |= env->dslot;
    cs_base = 0;
    pc = env->pc;
#else
#error unsupported CPU
#endif
    tb = env->tb_jmp_cache[tb_jmp_cache_hash_func(pc)];
    if (unlikely(!tb || tb->pc != pc || tb->cs_base != cs_base ||
                 tb->flags != flags)) {
        tb = tb_find_slow(pc, cs_base, flags);
    }
    return tb;
}

/* main execution loop */

#ifdef VBOX

int cpu_exec(CPUState *env1)
{
#define DECLARE_HOST_REGS 1
#include "hostregs_helper.h"
    int ret = 0, interrupt_request;
    TranslationBlock *tb;
    uint8_t *tc_ptr;
    unsigned long next_tb;

    cpu_single_env = env1;

    /* first we save global registers */
#define SAVE_HOST_REGS 1
#include "hostregs_helper.h"
    env = env1;

    env_to_regs();
#if defined(TARGET_I386)
    /* put eflags in CPU temporary format */
    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
#elif defined(TARGET_SPARC)
#elif defined(TARGET_M68K)
    env->cc_op = CC_OP_FLAGS;
    env->cc_dest = env->sr & 0xf;
    env->cc_x = (env->sr >> 4) & 1;
#elif defined(TARGET_ALPHA)
#elif defined(TARGET_ARM)
#elif defined(TARGET_PPC)
#elif defined(TARGET_MIPS)
#elif defined(TARGET_SH4)
#elif defined(TARGET_CRIS)
    /* XXXXX */
#else
#error unsupported target CPU
#endif
#ifndef VBOX /* VBOX: We need to raise traps and suchlike from the outside. */
    env->exception_index = -1;
#endif

    /* prepare setjmp context for exception handling */
    for(;;) {
        if (setjmp(env->jmp_env) == 0)
        {
            env->current_tb = NULL;

            /*
             * Check for fatal errors first
             */
            if (env->interrupt_request & CPU_INTERRUPT_RC) {
                env->exception_index = EXCP_RC;
                ASMAtomicAndS32((int32_t volatile *)&env->interrupt_request, ~CPU_INTERRUPT_RC);
                ret = env->exception_index;
                cpu_loop_exit();
            }

            /* if an exception is pending, we execute it here */
            if (env->exception_index >= 0) {
                Assert(!env->user_mode_only);
                if (env->exception_index >= EXCP_INTERRUPT) {
                    /* exit request from the cpu execution loop */
                    ret = env->exception_index;
                    break;
                } else {
                    /* simulate a real cpu exception. On i386, it can
                       trigger new exceptions, but we do not handle
                       double or triple faults yet. */
                    RAWEx_ProfileStart(env, STATS_IRQ_HANDLING);
                    Log(("do_interrupt %d %d %RGv\n", env->exception_index, env->exception_is_int, env->exception_next_eip));
                    do_interrupt(env->exception_index,
                                 env->exception_is_int,
                                 env->error_code,
                                 env->exception_next_eip, 0);
                    /* successfully delivered */
                    env->old_exception = -1;
                    RAWEx_ProfileStop(env, STATS_IRQ_HANDLING);
                }
                env->exception_index = -1;
            }

            next_tb = 0; /* force lookup of first TB */
            for(;;)
            {
                interrupt_request = env->interrupt_request;
#ifndef VBOX
                if (__builtin_expect(interrupt_request, 0))
#else
                if (RT_UNLIKELY(interrupt_request != 0))
#endif
                {
                    /** @todo: reconscille with what QEMU really does */

                    /* Single instruction exec request, we execute it and return (one way or the other).
                       The caller will always reschedule after doing this operation! */
                    if (interrupt_request & CPU_INTERRUPT_SINGLE_INSTR)
                    {
                        /* not in flight are we? (if we are, we trapped) */
                        if (!(env->interrupt_request & CPU_INTERRUPT_SINGLE_INSTR_IN_FLIGHT))
                        {
                            ASMAtomicOrS32((int32_t volatile *)&env->interrupt_request, CPU_INTERRUPT_SINGLE_INSTR_IN_FLIGHT);
                            env->exception_index = EXCP_SINGLE_INSTR;
                            if (emulate_single_instr(env) == -1)
                                AssertMsgFailed(("REM: emulate_single_instr failed for EIP=%RGv!!\n", env->eip));

                            /* When we receive an external interrupt during execution of this single
                               instruction, then we should stay here. We will leave when we're ready
                               for raw-mode or when interrupted by pending EMT requests.  */
                            interrupt_request = env->interrupt_request; /* reload this! */
                            if (   !(interrupt_request & CPU_INTERRUPT_HARD)
                                || !(env->eflags & IF_MASK)
                                ||  (env->hflags & HF_INHIBIT_IRQ_MASK)
                                ||  (env->state & CPU_RAW_HWACC)
                               )
                            {
                                env->exception_index = ret = EXCP_SINGLE_INSTR;
                                cpu_loop_exit();
                            }
                        }
                        /* Clear CPU_INTERRUPT_SINGLE_INSTR and leave CPU_INTERRUPT_SINGLE_INSTR_IN_FLIGHT set. */
                        ASMAtomicAndS32((int32_t volatile *)&env->interrupt_request, ~CPU_INTERRUPT_SINGLE_INSTR);
                    }

                    RAWEx_ProfileStart(env, STATS_IRQ_HANDLING);
                    if ((interrupt_request & CPU_INTERRUPT_SMI) &&
                        !(env->hflags & HF_SMM_MASK)) {
                        env->interrupt_request &= ~CPU_INTERRUPT_SMI;
                        do_smm_enter();
                        next_tb = 0;
                    }
                    else if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                             (env->eflags & IF_MASK) &&
                             !(env->hflags & HF_INHIBIT_IRQ_MASK))
                    {
                        /* if hardware interrupt pending, we execute it */
                        int intno;
                        ASMAtomicAndS32((int32_t volatile *)&env->interrupt_request, ~CPU_INTERRUPT_HARD);
                        intno = cpu_get_pic_interrupt(env);
                        if (intno >= 0)
                        {
                            Log(("do_interrupt %d\n", intno));
                            do_interrupt(intno, 0, 0, 0, 1);
                        }
                        /* ensure that no TB jump will be modified as
                           the program flow was changed */
                        next_tb = 0;
                    }
                    if (env->interrupt_request & CPU_INTERRUPT_EXITTB)
                    {
                        ASMAtomicAndS32((int32_t volatile *)&env->interrupt_request, ~CPU_INTERRUPT_EXITTB);
                        /* ensure that no TB jump will be modified as
                           the program flow was changed */
                        next_tb = 0;
                    }
                    RAWEx_ProfileStop(env, STATS_IRQ_HANDLING);
                    if (interrupt_request & CPU_INTERRUPT_EXIT)
                    {
                        env->exception_index = EXCP_INTERRUPT;
                        ASMAtomicAndS32((int32_t volatile *)&env->interrupt_request, ~CPU_INTERRUPT_EXIT);
                        ret = env->exception_index;
                        cpu_loop_exit();
                    }
                    if (interrupt_request & CPU_INTERRUPT_RC)
                    {
                        env->exception_index = EXCP_RC;
                        ASMAtomicAndS32((int32_t volatile *)&env->interrupt_request, ~CPU_INTERRUPT_RC);
                        ret = env->exception_index;
                        cpu_loop_exit();
                    }
                }

                /*
                 * Check if we the CPU state allows us to execute the code in raw-mode.
                 */
                RAWEx_ProfileStart(env, STATS_RAW_CHECK);
                if (remR3CanExecuteRaw(env,
                                       env->eip + env->segs[R_CS].base,
                                       env->hflags | (env->eflags & (IOPL_MASK | TF_MASK | VM_MASK)),
                                       &env->exception_index))
                {
                    RAWEx_ProfileStop(env, STATS_RAW_CHECK);
                    ret = env->exception_index;
                    cpu_loop_exit();
                }
                RAWEx_ProfileStop(env, STATS_RAW_CHECK);

                RAWEx_ProfileStart(env, STATS_TLB_LOOKUP);
                spin_lock(&tb_lock);
                tb = tb_find_fast();
                 /* Note: we do it here to avoid a gcc bug on Mac OS X when
                   doing it in tb_find_slow */
                if (tb_invalidated_flag) {
                    /* as some TB could have been invalidated because
                       of memory exceptions while generating the code, we
                       must recompute the hash index here */
                    next_tb = 0;
                    tb_invalidated_flag = 0;
                }

                /* see if we can patch the calling TB. When the TB
                   spans two pages, we cannot safely do a direct
                   jump. */
                if (next_tb != 0
                    && !(tb->cflags & CF_RAW_MODE)
                    && tb->page_addr[1] == -1)
                {
                    tb_add_jump((TranslationBlock *)(long)(next_tb & ~3), next_tb & 3, tb);
                }
                spin_unlock(&tb_lock);
                RAWEx_ProfileStop(env, STATS_TLB_LOOKUP);

                env->current_tb = tb;
                while (env->current_tb) {
                    tc_ptr = tb->tc_ptr;
                    /* execute the generated code */
                    RAWEx_ProfileStart(env, STATS_QEMU_RUN_EMULATED_CODE);
#if defined(VBOX) && defined(GCC_WITH_BUGGY_REGPARM)
                    tcg_qemu_tb_exec(tc_ptr, next_tb);
#else
                    next_tb = tcg_qemu_tb_exec(tc_ptr);
#endif
                    RAWEx_ProfileStop(env, STATS_QEMU_RUN_EMULATED_CODE);
                    env->current_tb = NULL;
                     if ((next_tb & 3) == 2) {
                        /* Instruction counter expired.  */
                        int insns_left;
                        tb = (TranslationBlock *)(long)(next_tb & ~3);
                        /* Restore PC.  */
                        CPU_PC_FROM_TB(env, tb);
                        insns_left = env->icount_decr.u32;
                        if (env->icount_extra && insns_left >= 0) {
                            /* Refill decrementer and continue execution.  */
                            env->icount_extra += insns_left;
                            if (env->icount_extra > 0xffff) {
                                insns_left = 0xffff;
                            } else {
                                insns_left = env->icount_extra;
                            }
                            env->icount_extra -= insns_left;
                            env->icount_decr.u16.low = insns_left;
                        } else {
                            if (insns_left > 0) {
                                /* Execute remaining instructions.  */
                                cpu_exec_nocache(insns_left, tb);
                            }
                            env->exception_index = EXCP_INTERRUPT;
                            next_tb = 0;
                            cpu_loop_exit();
                        }
                     }
                }

                /* reset soft MMU for next block (it can currently
                   only be set by a memory fault) */
#if defined(TARGET_I386) && !defined(CONFIG_SOFTMMU)
                if (env->hflags & HF_SOFTMMU_MASK) {
                    env->hflags &= ~HF_SOFTMMU_MASK;
                    /* do not allow linking to another block */
                    next_tb = 0;
                }
#endif
            } /* for(;;) */
        } else {
            env_to_regs();
        }
#ifdef VBOX_HIGH_RES_TIMERS_HACK
        /* NULL the current_tb here so cpu_interrupt() doesn't do
           anything unnecessary (like crashing during emulate single instruction). */
        env->current_tb = NULL;
        /* don't use env1->pVM here, the code wouldn't run with gcc-4.4/amd64
         * anymore, see #3883 */
        TMTimerPoll(env->pVM);
#endif
    } /* for(;;) */

#if defined(TARGET_I386)
    /* restore flags in standard format */
    env->eflags = env->eflags | cc_table[CC_OP].compute_all() | (DF & DF_MASK);
#else
#error unsupported target CPU
#endif
#include "hostregs_helper.h"
    return ret;
}

#else /* !VBOX */
int cpu_exec(CPUState *env1)
{
#define DECLARE_HOST_REGS 1
#include "hostregs_helper.h"
    int ret, interrupt_request;
    TranslationBlock *tb;
    uint8_t *tc_ptr;
    unsigned long next_tb;

    if (cpu_halted(env1) == EXCP_HALTED)
        return EXCP_HALTED;

    cpu_single_env = env1;

    /* first we save global registers */
#define SAVE_HOST_REGS 1
#include "hostregs_helper.h"
    env = env1;

    env_to_regs();
#if defined(TARGET_I386)
    /* put eflags in CPU temporary format */
    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
#elif defined(TARGET_SPARC)
#elif defined(TARGET_M68K)
    env->cc_op = CC_OP_FLAGS;
    env->cc_dest = env->sr & 0xf;
    env->cc_x = (env->sr >> 4) & 1;
#elif defined(TARGET_ALPHA)
#elif defined(TARGET_ARM)
#elif defined(TARGET_PPC)
#elif defined(TARGET_MIPS)
#elif defined(TARGET_SH4)
#elif defined(TARGET_CRIS)
    /* XXXXX */
#else
#error unsupported target CPU
#endif
    env->exception_index = -1;

    /* prepare setjmp context for exception handling */
    for(;;) {
        if (setjmp(env->jmp_env) == 0) {
            env->current_tb = NULL;
            /* if an exception is pending, we execute it here */
            if (env->exception_index >= 0) {
                if (env->exception_index >= EXCP_INTERRUPT) {
                    /* exit request from the cpu execution loop */
                    ret = env->exception_index;
                    break;
                } else if (env->user_mode_only) {
                    /* if user mode only, we simulate a fake exception
                       which will be handled outside the cpu execution
                       loop */
#if defined(TARGET_I386)
                    do_interrupt_user(env->exception_index,
                                      env->exception_is_int,
                                      env->error_code,
                                      env->exception_next_eip);
                    /* successfully delivered */
                    env->old_exception = -1;
#endif
                    ret = env->exception_index;
                    break;
                } else {
#if defined(TARGET_I386)
                    /* simulate a real cpu exception. On i386, it can
                       trigger new exceptions, but we do not handle
                       double or triple faults yet. */
                    do_interrupt(env->exception_index,
                                 env->exception_is_int,
                                 env->error_code,
                                 env->exception_next_eip, 0);
                    /* successfully delivered */
                    env->old_exception = -1;
#elif defined(TARGET_PPC)
                    do_interrupt(env);
#elif defined(TARGET_MIPS)
                    do_interrupt(env);
#elif defined(TARGET_SPARC)
                    do_interrupt(env);
#elif defined(TARGET_ARM)
                    do_interrupt(env);
#elif defined(TARGET_SH4)
		    do_interrupt(env);
#elif defined(TARGET_ALPHA)
                    do_interrupt(env);
#elif defined(TARGET_CRIS)
                    do_interrupt(env);
#elif defined(TARGET_M68K)
                    do_interrupt(0);
#endif
                }
                env->exception_index = -1;
            }
#ifdef USE_KQEMU
            if (kqemu_is_ok(env) && env->interrupt_request == 0) {
                int ret;
                env->eflags = env->eflags | cc_table[CC_OP].compute_all() | (DF & DF_MASK);
                ret = kqemu_cpu_exec(env);
                /* put eflags in CPU temporary format */
                CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
                DF = 1 - (2 * ((env->eflags >> 10) & 1));
                CC_OP = CC_OP_EFLAGS;
                env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
                if (ret == 1) {
                    /* exception */
                    longjmp(env->jmp_env, 1);
                } else if (ret == 2) {
                    /* softmmu execution needed */
                } else {
                    if (env->interrupt_request != 0) {
                        /* hardware interrupt will be executed just after */
                    } else {
                        /* otherwise, we restart */
                        longjmp(env->jmp_env, 1);
                    }
                }
            }
#endif

            next_tb = 0; /* force lookup of first TB */
            for(;;) {
                interrupt_request = env->interrupt_request;
                if (unlikely(interrupt_request) &&
                    likely(!(env->singlestep_enabled & SSTEP_NOIRQ))) {
                    if (interrupt_request & CPU_INTERRUPT_DEBUG) {
                        env->interrupt_request &= ~CPU_INTERRUPT_DEBUG;
                        env->exception_index = EXCP_DEBUG;
                        cpu_loop_exit();
                    }
#if defined(TARGET_ARM) || defined(TARGET_SPARC) || defined(TARGET_MIPS) || \
    defined(TARGET_PPC) || defined(TARGET_ALPHA) || defined(TARGET_CRIS)
                    if (interrupt_request & CPU_INTERRUPT_HALT) {
                        env->interrupt_request &= ~CPU_INTERRUPT_HALT;
                        env->halted = 1;
                        env->exception_index = EXCP_HLT;
                        cpu_loop_exit();
                    }
#endif
#if defined(TARGET_I386)
                    if (env->hflags2 & HF2_GIF_MASK) {
                        if ((interrupt_request & CPU_INTERRUPT_SMI) &&
                            !(env->hflags & HF_SMM_MASK)) {
                            svm_check_intercept(SVM_EXIT_SMI);
                            env->interrupt_request &= ~CPU_INTERRUPT_SMI;
                            do_smm_enter();
                            next_tb = 0;
                        } else if ((interrupt_request & CPU_INTERRUPT_NMI) &&
                                   !(env->hflags2 & HF2_NMI_MASK)) {
                            env->interrupt_request &= ~CPU_INTERRUPT_NMI;
                            env->hflags2 |= HF2_NMI_MASK;
                            do_interrupt(EXCP02_NMI, 0, 0, 0, 1);
                            next_tb = 0;
                        } else if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                                   (((env->hflags2 & HF2_VINTR_MASK) &&
                                     (env->hflags2 & HF2_HIF_MASK)) ||
                                    (!(env->hflags2 & HF2_VINTR_MASK) &&
                                     (env->eflags & IF_MASK &&
                                      !(env->hflags & HF_INHIBIT_IRQ_MASK))))) {
                            int intno;
                            svm_check_intercept(SVM_EXIT_INTR);
                            env->interrupt_request &= ~(CPU_INTERRUPT_HARD | CPU_INTERRUPT_VIRQ);
                            intno = cpu_get_pic_interrupt(env);
                            if (loglevel & CPU_LOG_TB_IN_ASM) {
                                fprintf(logfile, "Servicing hardware INT=0x%02x\n", intno);
                            }
                            do_interrupt(intno, 0, 0, 0, 1);
                            /* ensure that no TB jump will be modified as
                               the program flow was changed */
                            next_tb = 0;
#if !defined(CONFIG_USER_ONLY)
                        } else if ((interrupt_request & CPU_INTERRUPT_VIRQ) &&
                                   (env->eflags & IF_MASK) &&
                                   !(env->hflags & HF_INHIBIT_IRQ_MASK)) {
                            int intno;
                            /* FIXME: this should respect TPR */
                            svm_check_intercept(SVM_EXIT_VINTR);
                            env->interrupt_request &= ~CPU_INTERRUPT_VIRQ;
                            intno = ldl_phys(env->vm_vmcb + offsetof(struct vmcb, control.int_vector));
                            if (loglevel & CPU_LOG_TB_IN_ASM)
                                fprintf(logfile, "Servicing virtual hardware INT=0x%02x\n", intno);
                            do_interrupt(intno, 0, 0, 0, 1);
                            next_tb = 0;
#endif
                        }
                    }
#elif defined(TARGET_PPC)
#if 0
                    if ((interrupt_request & CPU_INTERRUPT_RESET)) {
                        cpu_ppc_reset(env);
                    }
#endif
                    if (interrupt_request & CPU_INTERRUPT_HARD) {
                        ppc_hw_interrupt(env);
                        if (env->pending_interrupts == 0)
                            env->interrupt_request &= ~CPU_INTERRUPT_HARD;
                        next_tb = 0;
                    }
#elif defined(TARGET_MIPS)
                    if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                        (env->CP0_Status & env->CP0_Cause & CP0Ca_IP_mask) &&
                        (env->CP0_Status & (1 << CP0St_IE)) &&
                        !(env->CP0_Status & (1 << CP0St_EXL)) &&
                        !(env->CP0_Status & (1 << CP0St_ERL)) &&
                        !(env->hflags & MIPS_HFLAG_DM)) {
                        /* Raise it */
                        env->exception_index = EXCP_EXT_INTERRUPT;
                        env->error_code = 0;
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_SPARC)
                    if ((interrupt_request & CPU_INTERRUPT_HARD) &&
			(env->psret != 0)) {
			int pil = env->interrupt_index & 15;
			int type = env->interrupt_index & 0xf0;

			if (((type == TT_EXTINT) &&
			     (pil == 15 || pil > env->psrpil)) ||
			    type != TT_EXTINT) {
			    env->interrupt_request &= ~CPU_INTERRUPT_HARD;
                            env->exception_index = env->interrupt_index;
                            do_interrupt(env);
			    env->interrupt_index = 0;
#if !defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY)
                            cpu_check_irqs(env);
#endif
                        next_tb = 0;
			}
		    } else if (interrupt_request & CPU_INTERRUPT_TIMER) {
			//do_interrupt(0, 0, 0, 0, 0);
			env->interrupt_request &= ~CPU_INTERRUPT_TIMER;
		    }
#elif defined(TARGET_ARM)
                    if (interrupt_request & CPU_INTERRUPT_FIQ
                        && !(env->uncached_cpsr & CPSR_F)) {
                        env->exception_index = EXCP_FIQ;
                        do_interrupt(env);
                        next_tb = 0;
                    }
                    /* ARMv7-M interrupt return works by loading a magic value
                       into the PC.  On real hardware the load causes the
                       return to occur.  The qemu implementation performs the
                       jump normally, then does the exception return when the
                       CPU tries to execute code at the magic address.
                       This will cause the magic PC value to be pushed to
                       the stack if an interrupt occured at the wrong time.
                       We avoid this by disabling interrupts when
                       pc contains a magic address.  */
                    if (interrupt_request & CPU_INTERRUPT_HARD
                        && ((IS_M(env) && env->regs[15] < 0xfffffff0)
                            || !(env->uncached_cpsr & CPSR_I))) {
                        env->exception_index = EXCP_IRQ;
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_SH4)
                    if (interrupt_request & CPU_INTERRUPT_HARD) {
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_ALPHA)
                    if (interrupt_request & CPU_INTERRUPT_HARD) {
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_CRIS)
                    if (interrupt_request & CPU_INTERRUPT_HARD
                        && (env->pregs[PR_CCS] & I_FLAG)) {
                        env->exception_index = EXCP_IRQ;
                        do_interrupt(env);
                        next_tb = 0;
                    }
                    if (interrupt_request & CPU_INTERRUPT_NMI
                        && (env->pregs[PR_CCS] & M_FLAG)) {
                        env->exception_index = EXCP_NMI;
                        do_interrupt(env);
                        next_tb = 0;
                    }
#elif defined(TARGET_M68K)
                    if (interrupt_request & CPU_INTERRUPT_HARD
                        && ((env->sr & SR_I) >> SR_I_SHIFT)
                            < env->pending_level) {
                        /* Real hardware gets the interrupt vector via an
                           IACK cycle at this point.  Current emulated
                           hardware doesn't rely on this, so we
                           provide/save the vector when the interrupt is
                           first signalled.  */
                        env->exception_index = env->pending_vector;
                        do_interrupt(1);
                        next_tb = 0;
                    }
#endif
                   /* Don't use the cached interupt_request value,
                      do_interrupt may have updated the EXITTB flag. */
                    if (env->interrupt_request & CPU_INTERRUPT_EXITTB) {
                        env->interrupt_request &= ~CPU_INTERRUPT_EXITTB;
                        /* ensure that no TB jump will be modified as
                           the program flow was changed */
                        next_tb = 0;
                    }
                    if (interrupt_request & CPU_INTERRUPT_EXIT) {
                        env->interrupt_request &= ~CPU_INTERRUPT_EXIT;
                        env->exception_index = EXCP_INTERRUPT;
                        cpu_loop_exit();
                    }
                }
#ifdef DEBUG_EXEC
                if ((loglevel & CPU_LOG_TB_CPU)) {
                    /* restore flags in standard format */
                    regs_to_env();
#if defined(TARGET_I386)
                    env->eflags = env->eflags | cc_table[CC_OP].compute_all() | (DF & DF_MASK);
                    cpu_dump_state(env, logfile, fprintf, X86_DUMP_CCOP);
                    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
#elif defined(TARGET_ARM)
                    cpu_dump_state(env, logfile, fprintf, 0);
#elif defined(TARGET_SPARC)
                    cpu_dump_state(env, logfile, fprintf, 0);
#elif defined(TARGET_PPC)
                    cpu_dump_state(env, logfile, fprintf, 0);
#elif defined(TARGET_M68K)
                    cpu_m68k_flush_flags(env, env->cc_op);
                    env->cc_op = CC_OP_FLAGS;
                    env->sr = (env->sr & 0xffe0)
                              | env->cc_dest | (env->cc_x << 4);
                    cpu_dump_state(env, logfile, fprintf, 0);
#elif defined(TARGET_MIPS)
                    cpu_dump_state(env, logfile, fprintf, 0);
#elif defined(TARGET_SH4)
		    cpu_dump_state(env, logfile, fprintf, 0);
#elif defined(TARGET_ALPHA)
                    cpu_dump_state(env, logfile, fprintf, 0);
#elif defined(TARGET_CRIS)
                    cpu_dump_state(env, logfile, fprintf, 0);
#else
#error unsupported target CPU
#endif
                }
#endif
                spin_lock(&tb_lock);
                tb = tb_find_fast();
                /* Note: we do it here to avoid a gcc bug on Mac OS X when
                   doing it in tb_find_slow */
                if (tb_invalidated_flag) {
                    /* as some TB could have been invalidated because
                       of memory exceptions while generating the code, we
                       must recompute the hash index here */
                    next_tb = 0;
                    tb_invalidated_flag = 0;
                }
#ifdef DEBUG_EXEC
                if ((loglevel & CPU_LOG_EXEC)) {
                    fprintf(logfile, "Trace 0x%08lx [" TARGET_FMT_lx "] %s\n",
                            (long)tb->tc_ptr, tb->pc,
                            lookup_symbol(tb->pc));
                }
#endif
                /* see if we can patch the calling TB. When the TB
                   spans two pages, we cannot safely do a direct
                   jump. */
                {
                    if (next_tb != 0 &&
#ifdef USE_KQEMU
                        (env->kqemu_enabled != 2) &&
#endif
                        tb->page_addr[1] == -1) {
                    tb_add_jump((TranslationBlock *)(next_tb & ~3), next_tb & 3, tb);
                }
                }
                spin_unlock(&tb_lock);
                env->current_tb = tb;
                while (env->current_tb) {
                    tc_ptr = tb->tc_ptr;
                /* execute the generated code */
#if defined(__sparc__) && !defined(HOST_SOLARIS)
#undef env
                    env = cpu_single_env;
#define env cpu_single_env
#endif
                    next_tb = tcg_qemu_tb_exec(tc_ptr);
                    env->current_tb = NULL;
                    if ((next_tb & 3) == 2) {
                        /* Instruction counter expired.  */
                        int insns_left;
                        tb = (TranslationBlock *)(long)(next_tb & ~3);
                        /* Restore PC.  */
                        CPU_PC_FROM_TB(env, tb);
                        insns_left = env->icount_decr.u32;
                        if (env->icount_extra && insns_left >= 0) {
                            /* Refill decrementer and continue execution.  */
                            env->icount_extra += insns_left;
                            if (env->icount_extra > 0xffff) {
                                insns_left = 0xffff;
                            } else {
                                insns_left = env->icount_extra;
                            }
                            env->icount_extra -= insns_left;
                            env->icount_decr.u16.low = insns_left;
                        } else {
                            if (insns_left > 0) {
                                /* Execute remaining instructions.  */
                                cpu_exec_nocache(insns_left, tb);
                            }
                            env->exception_index = EXCP_INTERRUPT;
                            next_tb = 0;
                            cpu_loop_exit();
                        }
                    }
                }
                /* reset soft MMU for next block (it can currently
                   only be set by a memory fault) */
#if defined(USE_KQEMU)
#define MIN_CYCLE_BEFORE_SWITCH (100 * 1000)
                if (kqemu_is_ok(env) &&
                    (cpu_get_time_fast() - env->last_io_time) >= MIN_CYCLE_BEFORE_SWITCH) {
                    cpu_loop_exit();
                }
#endif
            } /* for(;;) */
        } else {
            env_to_regs();
        }
    } /* for(;;) */


#if defined(TARGET_I386)
    /* restore flags in standard format */
    env->eflags = env->eflags | cc_table[CC_OP].compute_all() | (DF & DF_MASK);
#elif defined(TARGET_ARM)
    /* XXX: Save/restore host fpu exception state?.  */
#elif defined(TARGET_SPARC)
#elif defined(TARGET_PPC)
#elif defined(TARGET_M68K)
    cpu_m68k_flush_flags(env, env->cc_op);
    env->cc_op = CC_OP_FLAGS;
    env->sr = (env->sr & 0xffe0)
              | env->cc_dest | (env->cc_x << 4);
#elif defined(TARGET_MIPS)
#elif defined(TARGET_SH4)
#elif defined(TARGET_ALPHA)
#elif defined(TARGET_CRIS)
    /* XXXXX */
#else
#error unsupported target CPU
#endif

    /* restore global registers */
#include "hostregs_helper.h"

    /* fail safe : never use cpu_single_env outside cpu_exec() */
    cpu_single_env = NULL;
    return ret;
}
#endif /* !VBOX */

/* must only be called from the generated code as an exception can be
   generated */
void tb_invalidate_page_range(target_ulong start, target_ulong end)
{
    /* XXX: cannot enable it yet because it yields to MMU exception
       where NIP != read address on PowerPC */
#if 0
    target_ulong phys_addr;
    phys_addr = get_phys_addr_code(env, start);
    tb_invalidate_phys_page_range(phys_addr, phys_addr + end - start, 0);
#endif
}

#if defined(TARGET_I386) && defined(CONFIG_USER_ONLY)

void cpu_x86_load_seg(CPUX86State *s, int seg_reg, int selector)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = s;
    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
        selector &= 0xffff;
        cpu_x86_load_seg_cache(env, seg_reg, selector,
                               (selector << 4), 0xffff, 0);
    } else {
        load_seg(seg_reg, selector);
    }
    env = saved_env;
}

void cpu_x86_fsave(CPUX86State *s, uint8_t *ptr, int data32)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = s;

    helper_fsave((target_ulong)ptr, data32);

    env = saved_env;
}

void cpu_x86_frstor(CPUX86State *s, uint8_t *ptr, int data32)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = s;

    helper_frstor((target_ulong)ptr, data32);

    env = saved_env;
}

#endif /* TARGET_I386 */

#if !defined(CONFIG_SOFTMMU)

#if defined(TARGET_I386)

/* 'pc' is the host PC at which the exception was raised. 'address' is
   the effective address of the memory exception. 'is_write' is 1 if a
   write caused the exception and otherwise 0'. 'old_set' is the
   signal set which should be restored */
static inline int handle_cpu_signal(unsigned long pc, unsigned long address,
                                    int is_write, sigset_t *old_set,
                                    void *puc)
{
    TranslationBlock *tb;
    int ret;

    if (cpu_single_env)
        env = cpu_single_env; /* XXX: find a correct solution for multithread */
#if defined(DEBUG_SIGNAL)
    qemu_printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n",
                pc, address, is_write, *(unsigned long *)old_set);
#endif
    /* XXX: locking issue */
    if (is_write && page_unprotect(h2g(address), pc, puc)) {
        return 1;
    }

    /* see if it is an MMU fault */
    ret = cpu_x86_handle_mmu_fault(env, address, is_write,
                                   ((env->hflags & HF_CPL_MASK) == 3), 0);
    if (ret < 0)
        return 0; /* not an MMU fault */
    if (ret == 0)
        return 1; /* the MMU fault was handled without causing real CPU fault */
    /* now we have a real cpu fault */
    tb = tb_find_pc(pc);
    if (tb) {
        /* the PC is inside the translated code. It means that we have
           a virtual CPU fault */
        cpu_restore_state(tb, env, pc, puc);
    }
    if (ret == 1) {
#if 0
        printf("PF exception: EIP=0x%RGv CR2=0x%RGv error=0x%x\n",
               env->eip, env->cr[2], env->error_code);
#endif
        /* we restore the process signal mask as the sigreturn should
           do it (XXX: use sigsetjmp) */
        sigprocmask(SIG_SETMASK, old_set, NULL);
        raise_exception_err(env->exception_index, env->error_code);
    } else {
        /* activate soft MMU for this block */
        env->hflags |= HF_SOFTMMU_MASK;
        cpu_resume_from_signal(env, puc);
    }
    /* never comes here */
    return 1;
}

#elif defined(TARGET_ARM)
static inline int handle_cpu_signal(unsigned long pc, unsigned long address,
                                    int is_write, sigset_t *old_set,
                                    void *puc)
{
    TranslationBlock *tb;
    int ret;

    if (cpu_single_env)
        env = cpu_single_env; /* XXX: find a correct solution for multithread */
#if defined(DEBUG_SIGNAL)
    printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n",
           pc, address, is_write, *(unsigned long *)old_set);
#endif
    /* XXX: locking issue */
    if (is_write && page_unprotect(h2g(address), pc, puc)) {
        return 1;
    }
    /* see if it is an MMU fault */
    ret = cpu_arm_handle_mmu_fault(env, address, is_write, 1, 0);
    if (ret < 0)
        return 0; /* not an MMU fault */
    if (ret == 0)
        return 1; /* the MMU fault was handled without causing real CPU fault */
    /* now we have a real cpu fault */
    tb = tb_find_pc(pc);
    if (tb) {
        /* the PC is inside the translated code. It means that we have
           a virtual CPU fault */
        cpu_restore_state(tb, env, pc, puc);
    }
    /* we restore the process signal mask as the sigreturn should
       do it (XXX: use sigsetjmp) */
    sigprocmask(SIG_SETMASK, old_set, NULL);
    cpu_loop_exit();
}
#elif defined(TARGET_SPARC)
static inline int handle_cpu_signal(unsigned long pc, unsigned long address,
                                    int is_write, sigset_t *old_set,
                                    void *puc)
{
    TranslationBlock *tb;
    int ret;

    if (cpu_single_env)
        env = cpu_single_env; /* XXX: find a correct solution for multithread */
#if defined(DEBUG_SIGNAL)
    printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n",
           pc, address, is_write, *(unsigned long *)old_set);
#endif
    /* XXX: locking issue */
    if (is_write && page_unprotect(h2g(address), pc, puc)) {
        return 1;
    }
    /* see if it is an MMU fault */
    ret = cpu_sparc_handle_mmu_fault(env, address, is_write, 1, 0);
    if (ret < 0)
        return 0; /* not an MMU fault */
    if (ret == 0)
        return 1; /* the MMU fault was handled without causing real CPU fault */
    /* now we have a real cpu fault */
    tb = tb_find_pc(pc);
    if (tb) {
        /* the PC is inside the translated code. It means that we have
           a virtual CPU fault */
        cpu_restore_state(tb, env, pc, puc);
    }
    /* we restore the process signal mask as the sigreturn should
       do it (XXX: use sigsetjmp) */
    sigprocmask(SIG_SETMASK, old_set, NULL);
    cpu_loop_exit();
}
#elif defined (TARGET_PPC)
static inline int handle_cpu_signal(unsigned long pc, unsigned long address,
                                    int is_write, sigset_t *old_set,
                                    void *puc)
{
    TranslationBlock *tb;
    int ret;

    if (cpu_single_env)
        env = cpu_single_env; /* XXX: find a correct solution for multithread */
#if defined(DEBUG_SIGNAL)
    printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n",
           pc, address, is_write, *(unsigned long *)old_set);
#endif
    /* XXX: locking issue */
    if (is_write && page_unprotect(h2g(address), pc, puc)) {
        return 1;
    }

    /* see if it is an MMU fault */
    ret = cpu_ppc_handle_mmu_fault(env, address, is_write, msr_pr, 0);
    if (ret < 0)
        return 0; /* not an MMU fault */
    if (ret == 0)
        return 1; /* the MMU fault was handled without causing real CPU fault */

    /* now we have a real cpu fault */
    tb = tb_find_pc(pc);
    if (tb) {
        /* the PC is inside the translated code. It means that we have
           a virtual CPU fault */
        cpu_restore_state(tb, env, pc, puc);
    }
    if (ret == 1) {
#if 0
        printf("PF exception: NIP=0x%08x error=0x%x %p\n",
               env->nip, env->error_code, tb);
#endif
    /* we restore the process signal mask as the sigreturn should
       do it (XXX: use sigsetjmp) */
        sigprocmask(SIG_SETMASK, old_set, NULL);
        do_raise_exception_err(env->exception_index, env->error_code);
    } else {
        /* activate soft MMU for this block */
        cpu_resume_from_signal(env, puc);
    }
    /* never comes here */
    return 1;
}

#elif defined(TARGET_M68K)
static inline int handle_cpu_signal(unsigned long pc, unsigned long address,
                                    int is_write, sigset_t *old_set,
                                    void *puc)
{
    TranslationBlock *tb;
    int ret;

    if (cpu_single_env)
        env = cpu_single_env; /* XXX: find a correct solution for multithread */
#if defined(DEBUG_SIGNAL)
    printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n",
           pc, address, is_write, *(unsigned long *)old_set);
#endif
    /* XXX: locking issue */
    if (is_write && page_unprotect(address, pc, puc)) {
        return 1;
    }
    /* see if it is an MMU fault */
    ret = cpu_m68k_handle_mmu_fault(env, address, is_write, 1, 0);
    if (ret < 0)
        return 0; /* not an MMU fault */
    if (ret == 0)
        return 1; /* the MMU fault was handled without causing real CPU fault */
    /* now we have a real cpu fault */
    tb = tb_find_pc(pc);
    if (tb) {
        /* the PC is inside the translated code. It means that we have
           a virtual CPU fault */
        cpu_restore_state(tb, env, pc, puc);
    }
    /* we restore the process signal mask as the sigreturn should
       do it (XXX: use sigsetjmp) */
    sigprocmask(SIG_SETMASK, old_set, NULL);
    cpu_loop_exit();
    /* never comes here */
    return 1;
}

#elif defined (TARGET_MIPS)
static inline int handle_cpu_signal(unsigned long pc, unsigned long address,
                                    int is_write, sigset_t *old_set,
                                    void *puc)
{
    TranslationBlock *tb;
    int ret;

    if (cpu_single_env)
        env = cpu_single_env; /* XXX: find a correct solution for multithread */
#if defined(DEBUG_SIGNAL)
    printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n",
           pc, address, is_write, *(unsigned long *)old_set);
#endif
    /* XXX: locking issue */
    if (is_write && page_unprotect(h2g(address), pc, puc)) {
        return 1;
    }

    /* see if it is an MMU fault */
    ret = cpu_mips_handle_mmu_fault(env, address, is_write, 1, 0);
    if (ret < 0)
        return 0; /* not an MMU fault */
    if (ret == 0)
        return 1; /* the MMU fault was handled without causing real CPU fault */

    /* now we have a real cpu fault */
    tb = tb_find_pc(pc);
    if (tb) {
        /* the PC is inside the translated code. It means that we have
           a virtual CPU fault */
        cpu_restore_state(tb, env, pc, puc);
    }
    if (ret == 1) {
#if 0
        printf("PF exception: NIP=0x%08x error=0x%x %p\n",
               env->nip, env->error_code, tb);
#endif
    /* we restore the process signal mask as the sigreturn should
       do it (XXX: use sigsetjmp) */
        sigprocmask(SIG_SETMASK, old_set, NULL);
        do_raise_exception_err(env->exception_index, env->error_code);
    } else {
        /* activate soft MMU for this block */
        cpu_resume_from_signal(env, puc);
    }
    /* never comes here */
    return 1;
}

#elif defined (TARGET_SH4)
static inline int handle_cpu_signal(unsigned long pc, unsigned long address,
                                    int is_write, sigset_t *old_set,
                                    void *puc)
{
    TranslationBlock *tb;
    int ret;

    if (cpu_single_env)
        env = cpu_single_env; /* XXX: find a correct solution for multithread */
#if defined(DEBUG_SIGNAL)
    printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n",
           pc, address, is_write, *(unsigned long *)old_set);
#endif
    /* XXX: locking issue */
    if (is_write && page_unprotect(h2g(address), pc, puc)) {
        return 1;
    }

    /* see if it is an MMU fault */
    ret = cpu_sh4_handle_mmu_fault(env, address, is_write, 1, 0);
    if (ret < 0)
        return 0; /* not an MMU fault */
    if (ret == 0)
        return 1; /* the MMU fault was handled without causing real CPU fault */

    /* now we have a real cpu fault */
    tb = tb_find_pc(pc);
    if (tb) {
        /* the PC is inside the translated code. It means that we have
           a virtual CPU fault */
        cpu_restore_state(tb, env, pc, puc);
    }
#if 0
        printf("PF exception: NIP=0x%08x error=0x%x %p\n",
               env->nip, env->error_code, tb);
#endif
    /* we restore the process signal mask as the sigreturn should
       do it (XXX: use sigsetjmp) */
    sigprocmask(SIG_SETMASK, old_set, NULL);
    cpu_loop_exit();
    /* never comes here */
    return 1;
}
#else
#error unsupported target CPU
#endif

#if defined(__i386__)

#if defined(__APPLE__)
# include <sys/ucontext.h>

# define EIP_sig(context)  (*((unsigned long*)&(context)->uc_mcontext->ss.eip))
# define TRAP_sig(context)    ((context)->uc_mcontext->es.trapno)
# define ERROR_sig(context)   ((context)->uc_mcontext->es.err)
#else
# define EIP_sig(context)     ((context)->uc_mcontext.gregs[REG_EIP])
# define TRAP_sig(context)    ((context)->uc_mcontext.gregs[REG_TRAPNO])
# define ERROR_sig(context)   ((context)->uc_mcontext.gregs[REG_ERR])
#endif

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    struct ucontext *uc = puc;
    unsigned long pc;
    int trapno;

#ifndef REG_EIP
/* for glibc 2.1 */
#define REG_EIP    EIP
#define REG_ERR    ERR
#define REG_TRAPNO TRAPNO
#endif
    pc = uc->uc_mcontext.gregs[REG_EIP];
    trapno = uc->uc_mcontext.gregs[REG_TRAPNO];
#if defined(TARGET_I386) && defined(USE_CODE_COPY)
    if (trapno == 0x00 || trapno == 0x05) {
        /* send division by zero or bound exception */
        cpu_send_trap(pc, trapno, uc);
        return 1;
    } else
#endif
        return handle_cpu_signal(pc, (unsigned long)info->si_addr,
                                 trapno == 0xe ?
                                 (uc->uc_mcontext.gregs[REG_ERR] >> 1) & 1 : 0,
                                 &uc->uc_sigmask, puc);
}

#elif defined(__x86_64__)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    struct ucontext *uc = puc;
    unsigned long pc;

    pc = uc->uc_mcontext.gregs[REG_RIP];
    return handle_cpu_signal(pc, (unsigned long)info->si_addr,
                             uc->uc_mcontext.gregs[REG_TRAPNO] == 0xe ?
                             (uc->uc_mcontext.gregs[REG_ERR] >> 1) & 1 : 0,
                             &uc->uc_sigmask, puc);
}

#elif defined(__powerpc__)

/***********************************************************************
 * signal context platform-specific definitions
 * From Wine
 */
#ifdef linux
/* All Registers access - only for local access */
# define REG_sig(reg_name, context)		((context)->uc_mcontext.regs->reg_name)
/* Gpr Registers access  */
# define GPR_sig(reg_num, context)		REG_sig(gpr[reg_num], context)
# define IAR_sig(context)			REG_sig(nip, context)	/* Program counter */
# define MSR_sig(context)			REG_sig(msr, context)   /* Machine State Register (Supervisor) */
# define CTR_sig(context)			REG_sig(ctr, context)   /* Count register */
# define XER_sig(context)			REG_sig(xer, context) /* User's integer exception register */
# define LR_sig(context)			REG_sig(link, context) /* Link register */
# define CR_sig(context)			REG_sig(ccr, context) /* Condition register */
/* Float Registers access  */
# define FLOAT_sig(reg_num, context)		(((double*)((char*)((context)->uc_mcontext.regs+48*4)))[reg_num])
# define FPSCR_sig(context)			(*(int*)((char*)((context)->uc_mcontext.regs+(48+32*2)*4)))
/* Exception Registers access */
# define DAR_sig(context)			REG_sig(dar, context)
# define DSISR_sig(context)			REG_sig(dsisr, context)
# define TRAP_sig(context)			REG_sig(trap, context)
#endif /* linux */

#ifdef __APPLE__
# include <sys/ucontext.h>
typedef struct ucontext SIGCONTEXT;
/* All Registers access - only for local access */
# define REG_sig(reg_name, context)		((context)->uc_mcontext->ss.reg_name)
# define FLOATREG_sig(reg_name, context)	((context)->uc_mcontext->fs.reg_name)
# define EXCEPREG_sig(reg_name, context)	((context)->uc_mcontext->es.reg_name)
# define VECREG_sig(reg_name, context)		((context)->uc_mcontext->vs.reg_name)
/* Gpr Registers access */
# define GPR_sig(reg_num, context)		REG_sig(r##reg_num, context)
# define IAR_sig(context)			REG_sig(srr0, context)	/* Program counter */
# define MSR_sig(context)			REG_sig(srr1, context)  /* Machine State Register (Supervisor) */
# define CTR_sig(context)			REG_sig(ctr, context)
# define XER_sig(context)			REG_sig(xer, context) /* Link register */
# define LR_sig(context)			REG_sig(lr, context)  /* User's integer exception register */
# define CR_sig(context)			REG_sig(cr, context)  /* Condition register */
/* Float Registers access */
# define FLOAT_sig(reg_num, context)		FLOATREG_sig(fpregs[reg_num], context)
# define FPSCR_sig(context)			((double)FLOATREG_sig(fpscr, context))
/* Exception Registers access */
# define DAR_sig(context)			EXCEPREG_sig(dar, context)     /* Fault registers for coredump */
# define DSISR_sig(context)			EXCEPREG_sig(dsisr, context)
# define TRAP_sig(context)			EXCEPREG_sig(exception, context) /* number of powerpc exception taken */
#endif /* __APPLE__ */

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    struct ucontext *uc = puc;
    unsigned long pc;
    int is_write;

    pc = IAR_sig(uc);
    is_write = 0;
#if 0
    /* ppc 4xx case */
    if (DSISR_sig(uc) & 0x00800000)
        is_write = 1;
#else
    if (TRAP_sig(uc) != 0x400 && (DSISR_sig(uc) & 0x02000000))
        is_write = 1;
#endif
    return handle_cpu_signal(pc, (unsigned long)info->si_addr,
                             is_write, &uc->uc_sigmask, puc);
}

#elif defined(__alpha__)

int cpu_signal_handler(int host_signum, void *pinfo,
                           void *puc)
{
    siginfo_t *info = pinfo;
    struct ucontext *uc = puc;
    uint32_t *pc = uc->uc_mcontext.sc_pc;
    uint32_t insn = *pc;
    int is_write = 0;

    /* XXX: need kernel patch to get write flag faster */
    switch (insn >> 26) {
    case 0x0d: // stw
    case 0x0e: // stb
    case 0x0f: // stq_u
    case 0x24: // stf
    case 0x25: // stg
    case 0x26: // sts
    case 0x27: // stt
    case 0x2c: // stl
    case 0x2d: // stq
    case 0x2e: // stl_c
    case 0x2f: // stq_c
	is_write = 1;
    }

    return handle_cpu_signal(pc, (unsigned long)info->si_addr,
                             is_write, &uc->uc_sigmask, puc);
}
#elif defined(__sparc__)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    uint32_t *regs = (uint32_t *)(info + 1);
    void *sigmask = (regs + 20);
    unsigned long pc;
    int is_write;
    uint32_t insn;

    /* XXX: is there a standard glibc define ? */
    pc = regs[1];
    /* XXX: need kernel patch to get write flag faster */
    is_write = 0;
    insn = *(uint32_t *)pc;
    if ((insn >> 30) == 3) {
      switch((insn >> 19) & 0x3f) {
      case 0x05: // stb
      case 0x06: // sth
      case 0x04: // st
      case 0x07: // std
      case 0x24: // stf
      case 0x27: // stdf
      case 0x25: // stfsr
	is_write = 1;
	break;
      }
    }
    return handle_cpu_signal(pc, (unsigned long)info->si_addr,
                             is_write, sigmask, NULL);
}

#elif defined(__arm__)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    struct ucontext *uc = puc;
    unsigned long pc;
    int is_write;

    pc = uc->uc_mcontext.gregs[R15];
    /* XXX: compute is_write */
    is_write = 0;
    return handle_cpu_signal(pc, (unsigned long)info->si_addr,
                             is_write,
                             &uc->uc_sigmask, puc);
}

#elif defined(__mc68000)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    struct ucontext *uc = puc;
    unsigned long pc;
    int is_write;

    pc = uc->uc_mcontext.gregs[16];
    /* XXX: compute is_write */
    is_write = 0;
    return handle_cpu_signal(pc, (unsigned long)info->si_addr,
                             is_write,
                             &uc->uc_sigmask, puc);
}

#elif defined(__ia64)

#ifndef __ISR_VALID
  /* This ought to be in <bits/siginfo.h>... */
# define __ISR_VALID	1
#endif

int cpu_signal_handler(int host_signum, void *pinfo, void *puc)
{
    siginfo_t *info = pinfo;
    struct ucontext *uc = puc;
    unsigned long ip;
    int is_write = 0;

    ip = uc->uc_mcontext.sc_ip;
    switch (host_signum) {
      case SIGILL:
      case SIGFPE:
      case SIGSEGV:
      case SIGBUS:
      case SIGTRAP:
	  if (info->si_code && (info->si_segvflags & __ISR_VALID))
	      /* ISR.W (write-access) is bit 33:  */
	      is_write = (info->si_isr >> 33) & 1;
	  break;

      default:
	  break;
    }
    return handle_cpu_signal(ip, (unsigned long)info->si_addr,
                             is_write,
                             &uc->uc_sigmask, puc);
}

#elif defined(__s390__)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    struct ucontext *uc = puc;
    unsigned long pc;
    int is_write;

    pc = uc->uc_mcontext.psw.addr;
    /* XXX: compute is_write */
    is_write = 0;
    return handle_cpu_signal(pc, (unsigned long)info->si_addr,
                             is_write,
                             &uc->uc_sigmask, puc);
}

#else

#error host CPU specific signal handler needed

#endif

#endif /* !defined(CONFIG_SOFTMMU) */
