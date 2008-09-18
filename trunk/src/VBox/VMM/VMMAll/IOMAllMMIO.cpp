/* $Id$ */
/** @file
 * IOM - Input / Output Monitor - Guest Context.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_IOM
#include <VBox/iom.h>
#include <VBox/cpum.h>
#include <VBox/pgm.h>
#include <VBox/selm.h>
#include <VBox/mm.h>
#include <VBox/em.h>
#include <VBox/pgm.h>
#include <VBox/trpm.h>
#include "IOMInternal.h"
#include <VBox/vm.h>

#include <VBox/dis.h>
#include <VBox/disopcode.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/string.h>



/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/

/**
 * Array for fast recode of the operand size (1/2/4/8 bytes) to bit shift value.
 */
static const unsigned g_aSize2Shift[] =
{
    ~0,    /* 0 - invalid */
    0,     /* *1 == 2^0 */
    1,     /* *2 == 2^1 */
    ~0,    /* 3 - invalid */
    2,     /* *4 == 2^2 */
    ~0,    /* 5 - invalid */
    ~0,    /* 6 - invalid */
    ~0,    /* 7 - invalid */
    3      /* *8 == 2^3 */
};

/**
 * Macro for fast recode of the operand size (1/2/4/8 bytes) to bit shift value.
 */
#define SIZE_2_SHIFT(cb)    (g_aSize2Shift[cb])


/**
 * Wrapper which does the write and updates range statistics when such are enabled.
 * @warning VBOX_SUCCESS(rc=VINF_IOM_HC_MMIO_WRITE) is TRUE!
 */
DECLINLINE(int) iomMMIODoWrite(PVM pVM, PIOMMMIORANGE pRange, RTGCPHYS GCPhysFault, const void *pvData, unsigned cb)
{
#ifdef VBOX_WITH_STATISTICS
    PIOMMMIOSTATS pStats = iomMMIOGetStats(&pVM->iom.s, GCPhysFault, pRange);
    Assert(pStats);
#endif

    unsigned idCPU = (pRange->enmCtx == IOMMMIOCTX_GLOBAL) ? 0 : pVM->idCPU;
    Assert(pRange->a[idCPU].CTXALLSUFF(pDevIns)); /** @todo SMP */

    int rc;
    if (RT_LIKELY(pRange->CTXALLSUFF(pfnWriteCallback)))
        rc = pRange->CTXALLSUFF(pfnWriteCallback)(pRange->a[idCPU].CTXALLSUFF(pDevIns), pRange->a[idCPU].CTXALLSUFF(pvUser), GCPhysFault, (void *)pvData, cb); /* @todo fix const!! */
    else
        rc = VINF_SUCCESS;
    if (rc != VINF_IOM_HC_MMIO_WRITE)
        STAM_COUNTER_INC(&pStats->CTXALLSUFF(Write));
    return rc;
}

/**
 * Wrapper which does the read and updates range statistics when such are enabled.
 */
DECLINLINE(int) iomMMIODoRead(PVM pVM, PIOMMMIORANGE pRange, RTGCPHYS GCPhysFault, void *pvData, unsigned cb)
{
#ifdef VBOX_WITH_STATISTICS
    PIOMMMIOSTATS pStats = iomMMIOGetStats(&pVM->iom.s, GCPhysFault, pRange);
    Assert(pStats);
#endif

    unsigned idCPU = (pRange->enmCtx == IOMMMIOCTX_GLOBAL) ? 0 : pVM->idCPU;
    Assert(pRange->a[idCPU].CTXALLSUFF(pDevIns)); /** @todo SMP */

    int rc;
    if (RT_LIKELY(pRange->CTXALLSUFF(pfnReadCallback)))
        rc = pRange->CTXALLSUFF(pfnReadCallback)(pRange->a[idCPU].CTXALLSUFF(pDevIns), pRange->a[idCPU].CTXALLSUFF(pvUser), GCPhysFault, pvData, cb);
    else
    {
        switch (cb)
        {
            case 1: *(uint8_t  *)pvData = 0; break;
            case 2: *(uint16_t *)pvData = 0; break;
            case 4: *(uint32_t *)pvData = 0; break;
            case 8: *(uint64_t *)pvData = 0; break;
            default:
                memset(pvData, 0, cb);
                break;
        }
        rc = VINF_SUCCESS;
    }
    if (rc != VINF_IOM_HC_MMIO_READ)
        STAM_COUNTER_INC(&pStats->CTXALLSUFF(Read));
    return rc;
}

/*
 * Internal - statistics only.
 */
DECLINLINE(void) iomMMIOStatLength(PVM pVM, unsigned cb)
{
#ifdef VBOX_WITH_STATISTICS
    switch (cb)
    {
        case 1:
            STAM_COUNTER_INC(&pVM->iom.s.StatGCMMIO1Byte);
            break;
        case 2:
            STAM_COUNTER_INC(&pVM->iom.s.StatGCMMIO2Bytes);
            break;
        case 4:
            STAM_COUNTER_INC(&pVM->iom.s.StatGCMMIO4Bytes);
            break;
        case 8:
            STAM_COUNTER_INC(&pVM->iom.s.StatGCMMIO8Bytes);
            break;
        default:
            /* No way. */
            AssertMsgFailed(("Invalid data length %d\n", cb));
            break;
    }
#else
    NOREF(pVM); NOREF(cb);
#endif
}


/**
 * MOV      reg, mem         (read)
 * MOVZX    reg, mem         (read)
 * MOVSX    reg, mem         (read)
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Pointer to CPUMCTXCORE guest registers structure.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 */
static int iomInterpretMOVxXRead(PVM pVM, PCPUMCTXCORE pRegFrame, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange, RTGCPHYS GCPhysFault)
{
    Assert(pRange->CTXALLSUFF(pfnReadCallback) || !pRange->pfnReadCallbackR3);

    /*
     * Get the data size from parameter 2,
     * and call the handler function to get the data.
     */
    unsigned cb = DISGetParamSize(pCpu, &pCpu->param2);
    AssertMsg(cb > 0 && cb <= sizeof(uint64_t), ("cb=%d\n", cb));

    uint64_t u64Data = 0;
    int rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &u64Data, cb);
    if (rc == VINF_SUCCESS)
    {
        /*
         * Do sign extension for MOVSX.
         */
        /** @todo checkup MOVSX implementation! */
        if (pCpu->pCurInstr->opcode == OP_MOVSX)
        {
            if (cb == 1)
            {
                /* DWORD <- BYTE */
                int64_t iData = (int8_t)u64Data;
                u64Data = (uint64_t)iData;
            }
            else
            {
                /* DWORD <- WORD */
                int64_t iData = (int16_t)u64Data;
                u64Data = (uint64_t)iData;
            }
        }

        /*
         * Store the result to register (parameter 1).
         */
        bool fRc = iomSaveDataToReg(pCpu, &pCpu->param1, pRegFrame, u64Data);
        AssertMsg(fRc, ("Failed to store register value!\n")); NOREF(fRc);
    }

    if (rc == VINF_SUCCESS)
        iomMMIOStatLength(pVM, cb);
    return rc;
}


/**
 * MOV      mem, reg|imm     (write)
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Pointer to CPUMCTXCORE guest registers structure.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 */
static int iomInterpretMOVxXWrite(PVM pVM, PCPUMCTXCORE pRegFrame, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange, RTGCPHYS GCPhysFault)
{
    Assert(pRange->CTXALLSUFF(pfnWriteCallback) || !pRange->pfnWriteCallbackR3);

    /*
     * Get data to write from second parameter,
     * and call the callback to write it.
     */
    unsigned cb = 0;
    uint64_t u64Data  = 0;
    bool fRc = iomGetRegImmData(pCpu, &pCpu->param2, pRegFrame, &u64Data, &cb);
    AssertMsg(fRc, ("Failed to get reg/imm port number!\n")); NOREF(fRc);

    int rc = iomMMIODoWrite(pVM, pRange, GCPhysFault, &u64Data, cb);
    if (rc == VINF_SUCCESS)
        iomMMIOStatLength(pVM, cb);
    return rc;
}


/** Wrapper for reading virtual memory. */
DECLINLINE(int) iomRamRead(PVM pVM, void *pDest, RTGCPTR GCSrc, uint32_t cb)
{
#ifdef IN_GC
    return MMGCRamReadNoTrapHandler(pDest, (void *)GCSrc, cb);
#else
    return PGMPhysReadGCPtrSafe(pVM, pDest, GCSrc, cb);
#endif
}


/** Wrapper for writing virtual memory. */
DECLINLINE(int) iomRamWrite(PVM pVM, RTGCPTR GCDest, void *pSrc, uint32_t cb)
{
#ifdef IN_GC
    return MMGCRamWriteNoTrapHandler((void *)GCDest, pSrc, cb);
#else
    return PGMPhysWriteGCPtrSafe(pVM, GCDest, pSrc, cb);
#endif
}


#ifdef iom_MOVS_SUPPORT
/**
 * [REP] MOVSB
 * [REP] MOVSW
 * [REP] MOVSD
 *
 * Restricted implementation.
 *
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   uErrorCode  CPU Error code.
 * @param   pRegFrame   Trap register frame.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 */
static int iomInterpretMOVS(PVM pVM, RTGCUINT uErrorCode, PCPUMCTXCORE pRegFrame, RTGCPHYS GCPhysFault, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange)
{
    /*
     * We do not support segment prefixes or REPNE.
     */
    if (pCpu->prefix & (PREFIX_SEG | PREFIX_REPNE))
        return VINF_IOM_HC_MMIO_READ_WRITE; /** @todo -> interpret whatever. */


    /*
     * Get bytes/words/dwords count to copy.
     */
    uint32_t cTransfers = 1;
    if (pCpu->prefix & PREFIX_REP)
    {
#ifndef IN_GC
        if (    CPUMIsGuestIn64BitCode(pVM, pRegFrame)
            &&  pRegFrame->rcx >= _4G)
            return VINF_EM_RAW_EMULATE_INSTR;
#endif

        cTransfers = pRegFrame->ecx;
        if (SELMGetCpuModeFromSelector(pVM, pRegFrame->eflags, pRegFrame->cs, &pRegFrame->csHid) == CPUMODE_16BIT)
            cTransfers &= 0xffff;

        if (!cTransfers)
            return VINF_SUCCESS;
    }

    /* Get the current privilege level. */
    uint32_t cpl = CPUMGetGuestCPL(pVM, pRegFrame);

    /*
     * Get data size.
     */
    unsigned cb = DISGetParamSize(pCpu, &pCpu->param1);
    AssertMsg(cb > 0 && cb <= sizeof(uint32_t), ("cb=%d\n", cb));
    int      offIncrement = pRegFrame->eflags.Bits.u1DF ? -(signed)cb : (signed)cb;

#ifdef VBOX_WITH_STATISTICS
    if (pVM->iom.s.cMovsMaxBytes < (cTransfers << SIZE_2_SHIFT(cb)))
        pVM->iom.s.cMovsMaxBytes = cTransfers << SIZE_2_SHIFT(cb);
#endif

/** @todo re-evaluate on page boundraries. */

    RTGCPHYS Phys = GCPhysFault;
    int rc;
    if (uErrorCode & X86_TRAP_PF_RW)
    {
        /*
         * Write operation: [Mem] -> [MMIO]
         * ds:esi (Virt Src) -> es:edi (Phys Dst)
         */
        STAM_PROFILE_START(&pVM->iom.s.StatGCInstMovsToMMIO, a2);

        /* Check callback. */
        if (!pRange->CTXALLSUFF(pfnWriteCallback))
        {
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovsToMMIO, a2);
            return VINF_IOM_HC_MMIO_WRITE;
        }

        /* Convert source address ds:esi. */
        RTGCUINTPTR pu8Virt;
        rc = SELMToFlatEx(pVM, DIS_SELREG_DS, pRegFrame, (RTGCPTR)pRegFrame->rsi,
                          SELMTOFLAT_FLAGS_HYPER | SELMTOFLAT_FLAGS_NO_PL,
                          (PRTGCPTR)&pu8Virt);
        if (VBOX_SUCCESS(rc))
        {

            /* Access verification first; we currently can't recover properly from traps inside this instruction */
            rc = PGMVerifyAccess(pVM, pu8Virt, cTransfers * cb, (cpl == 3) ? X86_PTE_US : 0);
            if (rc != VINF_SUCCESS)
            {
                Log(("MOVS will generate a trap -> recompiler, rc=%d\n", rc));
                STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovsToMMIO, a2);
                return VINF_EM_RAW_EMULATE_INSTR;
            }

#ifdef IN_GC
            MMGCRamRegisterTrapHandler(pVM);
#endif

            /* copy loop. */
            while (cTransfers)
            {
                uint32_t u32Data = 0;
                rc = iomRamRead(pVM, &u32Data, (RTGCPTR)pu8Virt, cb);
                if (rc != VINF_SUCCESS)
                    break;
                rc = iomMMIODoWrite(pVM, pRange, Phys, &u32Data, cb);
                if (rc != VINF_SUCCESS)
                    break;

                pu8Virt        += offIncrement;
                Phys           += offIncrement;
                pRegFrame->rsi += offIncrement;
                pRegFrame->rdi += offIncrement;
                cTransfers--;
            }
#ifdef IN_GC
            MMGCRamDeregisterTrapHandler(pVM);
#endif
            /* Update ecx. */
            if (pCpu->prefix & PREFIX_REP)
                pRegFrame->ecx = cTransfers;
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovsToMMIO, a2);
        }
        else
            rc = VINF_IOM_HC_MMIO_READ_WRITE;
    }
    else
    {
        /*
         * Read operation: [MMIO] -> [mem] or [MMIO] -> [MMIO]
         * ds:[eSI] (Phys Src) -> es:[eDI] (Virt Dst)
         */
        /* Check callback. */
        if (!pRange->pfnReadCallback)
            return VINF_IOM_HC_MMIO_READ;

        /* Convert destination address. */
        RTGCUINTPTR pu8Virt;
        rc = SELMToFlatEx(pVM, DIS_SELREG_ES, pRegFrame, (RTGCPTR)pRegFrame->rdi,
                          SELMTOFLAT_FLAGS_HYPER | SELMTOFLAT_FLAGS_NO_PL,
                          (RTGCPTR *)&pu8Virt);
        if (VBOX_FAILURE(rc))
            return VINF_EM_RAW_GUEST_TRAP;

        /* Check if destination address is MMIO. */
        PIOMMMIORANGE pMMIODst;
        RTGCPHYS PhysDst;
        rc = PGMGstGetPage(pVM, (RTGCPTR)pu8Virt, NULL, &PhysDst);
        PhysDst |= (RTGCUINTPTR)pu8Virt & PAGE_OFFSET_MASK;
        if (    VBOX_SUCCESS(rc)
            &&  (pMMIODst = iomMMIOGetRange(&pVM->iom.s, PhysDst)))
        {
            /*
             * Extra: [MMIO] -> [MMIO]
             */
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstMovsMMIO, d);
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstMovsFromMMIO, c);

            if (!pMMIODst->CTXALLSUFF(pfnWriteCallback) && pMMIODst->pfnWriteCallbackR3)
            {
                STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovsMMIO, d);
                STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovsFromMMIO, c);
                return VINF_IOM_HC_MMIO_READ_WRITE;
            }

            /* copy loop. */
            while (cTransfers)
            {
                uint32_t u32Data;
                rc = iomMMIODoRead(pVM, pRange, Phys, &u32Data, cb);
                if (rc != VINF_SUCCESS)
                    break;
                rc = iomMMIODoWrite(pVM, pMMIODst, PhysDst, &u32Data, cb);
                if (rc != VINF_SUCCESS)
                    break;

                Phys           += offIncrement;
                PhysDst        += offIncrement;
                pRegFrame->rsi += offIncrement;
                pRegFrame->rdi += offIncrement;
                cTransfers--;
            }
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovsMMIO, d);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovsFromMMIO, c);
        }
        else
        {
            /*
             * Normal: [MMIO] -> [Mem]
             */
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstMovsFromMMIO, c);

            /* Access verification first; we currently can't recover properly from traps inside this instruction */
            rc = PGMVerifyAccess(pVM, pu8Virt, cTransfers * cb, X86_PTE_RW | ((cpl == 3) ? X86_PTE_US : 0));
            if (rc != VINF_SUCCESS)
            {
                Log(("MOVS will generate a trap -> recompiler, rc=%d\n", rc));
                STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovsFromMMIO, c);
                return VINF_EM_RAW_EMULATE_INSTR;
            }

            /* copy loop. */
#ifdef IN_GC
            MMGCRamRegisterTrapHandler(pVM);
#endif
            while (cTransfers)
            {
                uint32_t u32Data;
                rc = iomMMIODoRead(pVM, pRange, Phys, &u32Data, cb);
                if (rc != VINF_SUCCESS)
                    break;
                rc = iomRamWrite(pVM, (RTGCPTR)pu8Virt, &u32Data, cb);
                if (rc != VINF_SUCCESS)
                {
                    Log(("iomRamWrite %08X size=%d failed with %d\n", pu8Virt, cb, rc));
                    break;
                }

                pu8Virt        += offIncrement;
                Phys           += offIncrement;
                pRegFrame->rsi += offIncrement;
                pRegFrame->rdi += offIncrement;
                cTransfers--;
            }
#ifdef IN_GC
            MMGCRamDeregisterTrapHandler(pVM);
#endif
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovsFromMMIO, c);
        }

        /* Update ecx on exit. */
        if (pCpu->prefix & PREFIX_REP)
            pRegFrame->ecx = cTransfers;
    }

    /* work statistics. */
    if (rc == VINF_SUCCESS)
    {
        iomMMIOStatLength(pVM, cb);
    }
    return rc;
}
#endif



/**
 * [REP] STOSB
 * [REP] STOSW
 * [REP] STOSD
 *
 * Restricted implementation.
 *
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Trap register frame.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 */
static int iomInterpretSTOS(PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPHYS GCPhysFault, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange)
{
    /*
     * We do not support segment prefixes or REPNE..
     */
    if (pCpu->prefix & (PREFIX_SEG | PREFIX_REPNE))
        return VINF_IOM_HC_MMIO_READ_WRITE; /** @todo -> REM instead of HC */

    /*
     * Get bytes/words/dwords count to copy.
     */
    uint32_t cTransfers = 1;
    if (pCpu->prefix & PREFIX_REP)
    {
#ifndef IN_GC
        if (    CPUMIsGuestIn64BitCode(pVM, pRegFrame)
            &&  pRegFrame->rcx >= _4G)
            return VINF_EM_RAW_EMULATE_INSTR;
#endif

        cTransfers = pRegFrame->ecx;
        if (SELMGetCpuModeFromSelector(pVM, pRegFrame->eflags, pRegFrame->cs, &pRegFrame->csHid) == CPUMODE_16BIT)
            cTransfers &= 0xffff;

        if (!cTransfers)
            return VINF_SUCCESS;
    }

    /*
     * Get data size.
     */
    unsigned cb = DISGetParamSize(pCpu, &pCpu->param1);
    AssertMsg(cb > 0 && cb <= sizeof(uint32_t), ("cb=%d\n", cb));
    int      offIncrement = pRegFrame->eflags.Bits.u1DF ? -(signed)cb : (signed)cb;

#ifdef VBOX_WITH_STATISTICS
    if (pVM->iom.s.cStosMaxBytes < (cTransfers << SIZE_2_SHIFT(cb)))
        pVM->iom.s.cStosMaxBytes = cTransfers << SIZE_2_SHIFT(cb);
#endif


    RTGCPHYS    Phys    = GCPhysFault;
    uint32_t    u32Data = pRegFrame->eax;
    int rc;
    if (pRange->CTXALLSUFF(pfnFillCallback))
    {
        unsigned idCPU = (pRange->enmCtx == IOMMMIOCTX_GLOBAL) ? 0 : pVM->idCPU;
        Assert(pRange->a[idCPU].CTXALLSUFF(pDevIns));  /** @todo SMP */

        /*
         * Use the fill callback.
         */
        /** @todo pfnFillCallback must return number of bytes successfully written!!! */
        if (offIncrement > 0)
        {
            /* addr++ variant. */
            rc = pRange->CTXALLSUFF(pfnFillCallback)(pRange->a[idCPU].CTXALLSUFF(pDevIns), pRange->a[idCPU].CTXALLSUFF(pvUser), Phys, u32Data, cb, cTransfers);
            if (rc == VINF_SUCCESS)
            {
                /* Update registers. */
                pRegFrame->rdi += cTransfers << SIZE_2_SHIFT(cb);
                if (pCpu->prefix & PREFIX_REP)
                    pRegFrame->ecx = 0;
            }
        }
        else
        {
            /* addr-- variant. */
            rc = pRange->CTXALLSUFF(pfnFillCallback)(pRange->a[idCPU].CTXALLSUFF(pDevIns), pRange->a[idCPU].CTXALLSUFF(pvUser), (Phys - (cTransfers - 1)) << SIZE_2_SHIFT(cb), u32Data, cb, cTransfers);
            if (rc == VINF_SUCCESS)
            {
                /* Update registers. */
                pRegFrame->rdi -= cTransfers << SIZE_2_SHIFT(cb);
                if (pCpu->prefix & PREFIX_REP)
                    pRegFrame->ecx = 0;
            }
        }
    }
    else
    {
        /*
         * Use the write callback.
         */
        Assert(pRange->CTXALLSUFF(pfnWriteCallback) || !pRange->pfnWriteCallbackR3);

        /* fill loop. */
        do
        {
            rc = iomMMIODoWrite(pVM, pRange, Phys, &u32Data, cb);
            if (rc != VINF_SUCCESS)
                break;

            Phys           += offIncrement;
            pRegFrame->rdi += offIncrement;
            cTransfers--;
        } while (cTransfers);

        /* Update ecx on exit. */
        if (pCpu->prefix & PREFIX_REP)
            pRegFrame->ecx = cTransfers;
    }

    /*
     * Work statistics and return.
     */
    if (rc == VINF_SUCCESS)
        iomMMIOStatLength(pVM, cb);
    return rc;
}


/**
 * [REP] LODSB
 * [REP] LODSW
 * [REP] LODSD
 *
 * Restricted implementation.
 *
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Trap register frame.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 */
static int iomInterpretLODS(PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPHYS GCPhysFault, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange)
{
    Assert(pRange->CTXALLSUFF(pfnReadCallback) || !pRange->pfnReadCallbackR3);

    /*
     * We do not support segment prefixes or REP*.
     */
    if (pCpu->prefix & (PREFIX_SEG | PREFIX_REP | PREFIX_REPNE))
        return VINF_IOM_HC_MMIO_READ_WRITE; /** @todo -> REM instead of HC */

    /*
     * Get data size.
     */
    unsigned cb = DISGetParamSize(pCpu, &pCpu->param2);
    AssertMsg(cb > 0 && cb <= sizeof(uint64_t), ("cb=%d\n", cb));
    int     offIncrement = pRegFrame->eflags.Bits.u1DF ? -(signed)cb : (signed)cb;

    /*
     * Perform read.
     */
    int rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &pRegFrame->rax, cb);
    if (rc == VINF_SUCCESS)
        pRegFrame->rsi += offIncrement;

    /*
     * Work statistics and return.
     */
    if (rc == VINF_SUCCESS)
        iomMMIOStatLength(pVM, cb);
    return rc;
}


/**
 * CMP [MMIO], reg|imm
 * CMP reg|imm, [MMIO]
 *
 * Restricted implementation.
 *
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Trap register frame.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 */
static int iomInterpretCMP(PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPHYS GCPhysFault, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange)
{
    Assert(pRange->CTXALLSUFF(pfnReadCallback) || !pRange->pfnReadCallbackR3);

    /*
     * Get the operands.
     */
    unsigned cb = 0;
    uint64_t uData1 = 0;
    uint64_t uData2 = 0;
    int rc;
    if (iomGetRegImmData(pCpu, &pCpu->param1, pRegFrame, &uData1, &cb))
        /* cmp reg, [MMIO]. */
        rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &uData2, cb);
    else if (iomGetRegImmData(pCpu, &pCpu->param2, pRegFrame, &uData2, &cb))
        /* cmp [MMIO], reg|imm. */
        rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &uData1, cb);
    else
    {
        AssertMsgFailed(("Disassember CMP problem..\n"));
        rc = VERR_IOM_MMIO_HANDLER_DISASM_ERROR;
    }

    if (rc == VINF_SUCCESS)
    {
        /* Emulate CMP and update guest flags. */
        uint32_t eflags = EMEmulateCmp(uData1, uData2, cb);
        pRegFrame->eflags.u32 = (pRegFrame->eflags.u32 & ~(X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF))
                              | (eflags                &  (X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF));
        iomMMIOStatLength(pVM, cb);
    }

    return rc;
}


/**
 * AND [MMIO], reg|imm
 * AND reg, [MMIO]
 * OR [MMIO], reg|imm
 * OR reg, [MMIO]
 *
 * Restricted implementation.
 *
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Trap register frame.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 * @param   pfnEmulate  Instruction emulation function.
 */
static int iomInterpretOrXorAnd(PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPHYS GCPhysFault, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange, PFN_EMULATE_PARAM3 pfnEmulate)
{
    unsigned    cb     = 0;
    uint64_t    uData1 = 0;
    uint64_t    uData2 = 0;
    bool        fAndWrite;
    int         rc;

#ifdef LOG_ENABLED
    const char *pszInstr;

    if (pCpu->pCurInstr->opcode == OP_XOR)
        pszInstr = "Xor";
    else if (pCpu->pCurInstr->opcode == OP_OR)
        pszInstr = "Or";
    else if (pCpu->pCurInstr->opcode == OP_AND)
        pszInstr = "And";
    else
        pszInstr = "OrXorAnd??";
#endif

    if (iomGetRegImmData(pCpu, &pCpu->param1, pRegFrame, &uData1, &cb))
    {
        /* and reg, [MMIO]. */
        Assert(pRange->CTXALLSUFF(pfnReadCallback) || !pRange->pfnReadCallbackR3);
        fAndWrite = false;
        rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &uData2, cb);
    }
    else if (iomGetRegImmData(pCpu, &pCpu->param2, pRegFrame, &uData2, &cb))
    {
        /* and [MMIO], reg|imm. */
        fAndWrite = true;
        if (    (pRange->CTXALLSUFF(pfnReadCallback) || !pRange->pfnReadCallbackR3)
            &&  (pRange->CTXALLSUFF(pfnWriteCallback) || !pRange->pfnWriteCallbackR3))
            rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &uData1, cb);
        else
            rc = VINF_IOM_HC_MMIO_READ_WRITE;
    }
    else
    {
        AssertMsgFailed(("Disassember AND problem..\n"));
        return VERR_IOM_MMIO_HANDLER_DISASM_ERROR;
    }

    if (rc == VINF_SUCCESS)
    {
        /* Emulate AND and update guest flags. */
        uint32_t eflags = pfnEmulate((uint32_t *)&uData1, uData2, cb);

        LogFlow(("iomInterpretOrXorAnd %s result %RX64\n", pszInstr, uData1));

        if (fAndWrite)
            /* Store result to MMIO. */
            rc = iomMMIODoWrite(pVM, pRange, GCPhysFault, &uData1, cb);
        else
        {
            /* Store result to register. */
            bool fRc = iomSaveDataToReg(pCpu, &pCpu->param1, pRegFrame, uData1);
            AssertMsg(fRc, ("Failed to store register value!\n")); NOREF(fRc);
        }
        if (rc == VINF_SUCCESS)
        {
            /* Update guest's eflags and finish. */
            pRegFrame->eflags.u32 = (pRegFrame->eflags.u32 & ~(X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF))
                                  | (eflags                &  (X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF));
            iomMMIOStatLength(pVM, cb);
        }
    }

    return rc;
}

/**
 * TEST [MMIO], reg|imm
 * TEST reg, [MMIO]
 *
 * Restricted implementation.
 *
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Trap register frame.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 */
static int iomInterpretTEST(PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPHYS GCPhysFault, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange)
{
    Assert(pRange->CTXALLSUFF(pfnReadCallback) || !pRange->pfnReadCallbackR3);

    unsigned    cb     = 0;
    uint64_t    uData1 = 0;
    uint64_t    uData2 = 0;
    int         rc;

    if (iomGetRegImmData(pCpu, &pCpu->param1, pRegFrame, &uData1, &cb))
    {
        /* and test, [MMIO]. */
        rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &uData2, cb);
    }
    else if (iomGetRegImmData(pCpu, &pCpu->param2, pRegFrame, &uData2, &cb))
    {
        /* test [MMIO], reg|imm. */
        rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &uData1, cb);
    }
    else
    {
        AssertMsgFailed(("Disassember TEST problem..\n"));
        return VERR_IOM_MMIO_HANDLER_DISASM_ERROR;
    }

    if (rc == VINF_SUCCESS)
    {
        /* Emulate TEST (=AND without write back) and update guest EFLAGS. */
        uint32_t eflags = EMEmulateAnd((uint32_t *)&uData1, uData2, cb);
        pRegFrame->eflags.u32 = (pRegFrame->eflags.u32 & ~(X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF))
                              | (eflags                &  (X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF));
        iomMMIOStatLength(pVM, cb);
    }

    return rc;
}

/**
 * BT [MMIO], reg|imm
 *
 * Restricted implementation.
 *
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Trap register frame.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 */
static int iomInterpretBT(PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPHYS GCPhysFault, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange)
{
    Assert(pRange->CTXALLSUFF(pfnReadCallback) || !pRange->pfnReadCallbackR3);

    uint64_t    uBit   = 0;
    uint64_t    uData1 = 0;
    unsigned    cb     = 0;
    int         rc;

    if (iomGetRegImmData(pCpu, &pCpu->param2, pRegFrame, &uBit, &cb))
    {
        /* bt [MMIO], reg|imm. */
        rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &uData1, cb);
    }
    else
    {
        AssertMsgFailed(("Disassember BT problem..\n"));
        return VERR_IOM_MMIO_HANDLER_DISASM_ERROR;
    }

    if (rc == VINF_SUCCESS)
    {
        /* The size of the memory operand only matters here. */
        cb = DISGetParamSize(pCpu, &pCpu->param1);

        /* Find the bit inside the faulting address */
        uBit &= (cb*8 - 1);

        pRegFrame->eflags.Bits.u1CF = (uData1 >> uBit);
        iomMMIOStatLength(pVM, cb);
    }

    return rc;
}

/**
 * XCHG [MMIO], reg
 * XCHG reg, [MMIO]
 *
 * Restricted implementation.
 *
 *
 * @returns VBox status code.
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Trap register frame.
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pCpu        Disassembler CPU state.
 * @param   pRange      Pointer MMIO range.
 */
static int iomInterpretXCHG(PVM pVM, PCPUMCTXCORE pRegFrame, RTGCPHYS GCPhysFault, PDISCPUSTATE pCpu, PIOMMMIORANGE pRange)
{
    /* Check for read & write handlers since IOMMMIOHandler doesn't cover this. */
    if (    (!pRange->CTXALLSUFF(pfnReadCallback) && pRange->pfnReadCallbackR3)
        ||  (!pRange->CTXALLSUFF(pfnWriteCallback) && pRange->pfnWriteCallbackR3))
        return VINF_IOM_HC_MMIO_READ_WRITE;

    int         rc;
    unsigned    cb     = 0;
    uint64_t    uData1 = 0;
    uint64_t    uData2 = 0;
    if (iomGetRegImmData(pCpu, &pCpu->param1, pRegFrame, &uData1, &cb))
    {
        /* xchg reg, [MMIO]. */
        rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &uData2, cb);
        if (rc == VINF_SUCCESS)
        {
            /* Store result to MMIO. */
            rc = iomMMIODoWrite(pVM, pRange, GCPhysFault, &uData1, cb);

            if (rc == VINF_SUCCESS)
            {
                /* Store result to register. */
                bool fRc = iomSaveDataToReg(pCpu, &pCpu->param1, pRegFrame, uData2);
                AssertMsg(fRc, ("Failed to store register value!\n")); NOREF(fRc);
            }
            else
                Assert(rc == VINF_IOM_HC_MMIO_WRITE || rc == VINF_PATM_HC_MMIO_PATCH_WRITE);
        }
        else
            Assert(rc == VINF_IOM_HC_MMIO_READ || rc == VINF_PATM_HC_MMIO_PATCH_READ);
    }
    else if (iomGetRegImmData(pCpu, &pCpu->param2, pRegFrame, &uData2, &cb))
    {
        /* xchg [MMIO], reg. */
        rc = iomMMIODoRead(pVM, pRange, GCPhysFault, &uData1, cb);
        if (rc == VINF_SUCCESS)
        {
            /* Store result to MMIO. */
            rc = iomMMIODoWrite(pVM, pRange, GCPhysFault, &uData2, cb);
            if (rc == VINF_SUCCESS)
            {
                /* Store result to register. */
                bool fRc = iomSaveDataToReg(pCpu, &pCpu->param2, pRegFrame, uData1);
                AssertMsg(fRc, ("Failed to store register value!\n")); NOREF(fRc);
            }
            else
                Assert(rc == VINF_IOM_HC_MMIO_WRITE || rc == VINF_PATM_HC_MMIO_PATCH_WRITE);
        }
        else
            Assert(rc == VINF_IOM_HC_MMIO_READ || rc == VINF_PATM_HC_MMIO_PATCH_READ);
    }
    else
    {
        AssertMsgFailed(("Disassember XCHG problem..\n"));
        rc = VERR_IOM_MMIO_HANDLER_DISASM_ERROR;
    }
    return rc;
}


/**
 * \#PF Handler callback for MMIO ranges.
 * Note: we are on ring0 in Hypervisor and interrupts are disabled.
 *
 * @returns VBox status code (appropriate for GC return).
 * @param   pVM         VM Handle.
 * @param   uErrorCode  CPU Error code.
 * @param   pCtxCore    Trap register frame.
 * @param   pvFault     The fault address (cr2).
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pvUser      Pointer to the MMIO ring-3 range entry.
 */
IOMDECL(int) IOMMMIOHandler(PVM pVM, RTGCUINT uErrorCode, PCPUMCTXCORE pCtxCore, RTGCPTR pvFault, RTGCPHYS GCPhysFault, void *pvUser)
{
    STAM_PROFILE_START(&pVM->iom.s.StatGCMMIOHandler, a);
    Log(("IOMMMIOHandler: GCPhys=%RGp uErr=%#x pvFault=%VGv eip=%VGv\n",
          GCPhysFault, (uint32_t)uErrorCode, pvFault, pCtxCore->rip));

    PIOMMMIORANGE pRange = (PIOMMMIORANGE)pvUser;
    Assert(pRange);
    Assert(pRange == iomMMIOGetRange(&pVM->iom.s, GCPhysFault));

#ifdef VBOX_WITH_STATISTICS
    /*
     * Locate the statistics, if > PAGE_SIZE we'll use the first byte for everything.
     */
    PIOMMMIOSTATS pStats = iomMMIOGetStats(&pVM->iom.s, GCPhysFault, pRange);
    if (!pStats)
    {
# ifdef IN_RING3
        return VERR_NO_MEMORY;
# else
        STAM_PROFILE_STOP(&pVM->iom.s.StatGCMMIOHandler, a);
        STAM_COUNTER_INC(&pVM->iom.s.StatGCMMIOFailures);
        return uErrorCode & X86_TRAP_PF_RW ? VINF_IOM_HC_MMIO_WRITE : VINF_IOM_HC_MMIO_READ;
# endif
    }
#endif

#ifndef IN_RING3
    /*
     * Should we defer the request right away?
     */
    if (uErrorCode & X86_TRAP_PF_RW
        ? !pRange->CTXALLSUFF(pfnWriteCallback) && pRange->pfnWriteCallbackR3
        : !pRange->CTXALLSUFF(pfnReadCallback)  && pRange->pfnReadCallbackR3)
    {
# ifdef VBOX_WITH_STATISTICS
        if (uErrorCode & X86_TRAP_PF_RW)
            STAM_COUNTER_INC(&pStats->CTXALLMID(Write,ToR3));
        else
            STAM_COUNTER_INC(&pStats->CTXALLMID(Read,ToR3));
# endif

        STAM_PROFILE_STOP(&pVM->iom.s.StatGCMMIOHandler, a);
        STAM_COUNTER_INC(&pVM->iom.s.StatGCMMIOFailures);
        return uErrorCode & X86_TRAP_PF_RW ? VINF_IOM_HC_MMIO_WRITE : VINF_IOM_HC_MMIO_READ;
    }
#endif /* !IN_RING3 */

    /*
     * Disassemble the instruction and interprete it.
     */
    DISCPUSTATE Cpu;
    unsigned cbOp;
    int rc = EMInterpretDisasOne(pVM, pCtxCore, &Cpu, &cbOp);
    AssertRCReturn(rc, rc);
    switch (Cpu.pCurInstr->opcode)
    {
        case OP_MOV:
        case OP_MOVZX:
        case OP_MOVSX:
        {
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstMov, b);
            if (uErrorCode & X86_TRAP_PF_RW)
                rc = iomInterpretMOVxXWrite(pVM, pCtxCore, &Cpu, pRange, GCPhysFault);
            else
                rc = iomInterpretMOVxXRead(pVM, pCtxCore, &Cpu, pRange, GCPhysFault);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMov, b);
            break;
        }


#ifdef iom_MOVS_SUPPORT
        case OP_MOVSB:
        case OP_MOVSWD:
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstMovs, c);
            rc = iomInterpretMOVS(pVM, uErrorCode, pCtxCore, GCPhysFault, &Cpu, pRange);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstMovs, c);
            break;
#endif

        case OP_STOSB:
        case OP_STOSWD:
            Assert(uErrorCode & X86_TRAP_PF_RW);
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstStos, d);
            rc = iomInterpretSTOS(pVM, pCtxCore, GCPhysFault, &Cpu, pRange);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstStos, d);
            break;

        case OP_LODSB:
        case OP_LODSWD:
            Assert(!(uErrorCode & X86_TRAP_PF_RW));
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstLods, e);
            rc = iomInterpretLODS(pVM, pCtxCore, GCPhysFault, &Cpu, pRange);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstLods, e);
            break;

        case OP_CMP:
            Assert(!(uErrorCode & X86_TRAP_PF_RW));
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstCmp, f);
            rc = iomInterpretCMP(pVM, pCtxCore, GCPhysFault, &Cpu, pRange);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstCmp, f);
            break;

        case OP_AND:
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstAnd, g);
            rc = iomInterpretOrXorAnd(pVM, pCtxCore, GCPhysFault, &Cpu, pRange, EMEmulateAnd);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstAnd, g);
            break;

        case OP_OR:
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstOr, k);
            rc = iomInterpretOrXorAnd(pVM, pCtxCore, GCPhysFault, &Cpu, pRange, EMEmulateOr);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstOr, k);
            break;

        case OP_XOR:
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstXor, m);
            rc = iomInterpretOrXorAnd(pVM, pCtxCore, GCPhysFault, &Cpu, pRange, EMEmulateXor);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstXor, m);
            break;

        case OP_TEST:
            Assert(!(uErrorCode & X86_TRAP_PF_RW));
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstTest, h);
            rc = iomInterpretTEST(pVM, pCtxCore, GCPhysFault, &Cpu, pRange);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstTest, h);
            break;

        case OP_BT:
            Assert(!(uErrorCode & X86_TRAP_PF_RW));
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstBt, l);
            rc = iomInterpretBT(pVM, pCtxCore, GCPhysFault, &Cpu, pRange);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstBt, l);
            break;

        case OP_XCHG:
            STAM_PROFILE_START(&pVM->iom.s.StatGCInstXchg, i);
            rc = iomInterpretXCHG(pVM, pCtxCore, GCPhysFault, &Cpu, pRange);
            STAM_PROFILE_STOP(&pVM->iom.s.StatGCInstXchg, i);
            break;


        /*
         * The instruction isn't supported. Hand it on to ring-3.
         */
        default:
            STAM_COUNTER_INC(&pVM->iom.s.StatGCInstOther);
            rc = (uErrorCode & X86_TRAP_PF_RW) ? VINF_IOM_HC_MMIO_WRITE : VINF_IOM_HC_MMIO_READ;
            break;
    }

    /*
     * On success advance EIP.
     */
    if (rc == VINF_SUCCESS)
        pCtxCore->rip += cbOp;
    else
    {
        STAM_COUNTER_INC(&pVM->iom.s.StatGCMMIOFailures);
#if defined(VBOX_WITH_STATISTICS) && !defined(IN_RING3)
        switch (rc)
        {
            case VINF_IOM_HC_MMIO_READ:
            case VINF_IOM_HC_MMIO_READ_WRITE:
                STAM_COUNTER_INC(&pStats->CTXALLMID(Read,ToR3));
                break;
            case VINF_IOM_HC_MMIO_WRITE:
                STAM_COUNTER_INC(&pStats->CTXALLMID(Write,ToR3));
                break;
        }
#endif
    }

    STAM_PROFILE_STOP(&pVM->iom.s.StatGCMMIOHandler, a);
    return rc;
}


#ifdef IN_RING3
/**
 * \#PF Handler callback for MMIO ranges.
 *
 * @returns VINF_SUCCESS if the handler have carried out the operation.
 * @returns VINF_PGM_HANDLER_DO_DEFAULT if the caller should carry out the access operation.
 * @param   pVM             VM Handle.
 * @param   GCPhys          The physical address the guest is writing to.
 * @param   pvPhys          The HC mapping of that address.
 * @param   pvBuf           What the guest is reading/writing.
 * @param   cbBuf           How much it's reading/writing.
 * @param   enmAccessType   The access type.
 * @param   pvUser          Pointer to the MMIO range entry.
 */
DECLCALLBACK(int) IOMR3MMIOHandler(PVM pVM, RTGCPHYS GCPhysFault, void *pvPhys, void *pvBuf, size_t cbBuf, PGMACCESSTYPE enmAccessType, void *pvUser)
{
    int           rc;
    PIOMMMIORANGE pRange = (PIOMMMIORANGE)pvUser;

    Assert(pRange);
    Assert(pRange == iomMMIOGetRange(&pVM->iom.s, GCPhysFault));

    if (enmAccessType == PGMACCESSTYPE_READ)
        rc = iomMMIODoRead(pVM, pRange, GCPhysFault, pvBuf, cbBuf);
    else
        rc = iomMMIODoWrite(pVM, pRange, GCPhysFault, pvBuf, cbBuf);

    AssertRC(rc);
    return rc;
}
#endif /* IN_RING3 */


/**
 * Reads a MMIO register.
 *
 * @returns VBox status code.
 *
 * @param   pVM         VM handle.
 * @param   GCPhys      The physical address to read.
 * @param   pu32Value   Where to store the value read.
 * @param   cbValue     The size of the register to read in bytes. 1, 2 or 4 bytes.
 */
IOMDECL(int) IOMMMIORead(PVM pVM, RTGCPHYS GCPhys, uint32_t *pu32Value, size_t cbValue)
{
    /*
     * Lookup the current context range node and statistics.
     */
    PIOMMMIORANGE pRange = iomMMIOGetRange(&pVM->iom.s, GCPhys);
    AssertMsgReturn(pRange,
                    ("Handlers and page tables are out of sync or something! GCPhys=%VGp cbValue=%d\n", GCPhys, cbValue),
                    VERR_INTERNAL_ERROR);
#ifdef VBOX_WITH_STATISTICS
    PIOMMMIOSTATS pStats = iomMMIOGetStats(&pVM->iom.s, GCPhys, pRange);
    if (!pStats)
# ifdef IN_RING3
        return VERR_NO_MEMORY;
# else
        return VINF_IOM_HC_MMIO_READ;
# endif
#endif /* VBOX_WITH_STATISTICS */
    if (pRange->CTXALLSUFF(pfnReadCallback))
    {
        /*
         * Perform the read and deal with the result.
         */
        unsigned idCPU = (pRange->enmCtx == IOMMMIOCTX_GLOBAL) ? 0 : pVM->idCPU;
        Assert(pRange->a[idCPU].CTXALLSUFF(pDevIns)); /** @todo SMP */

#ifdef VBOX_WITH_STATISTICS
        if (pStats)
            STAM_PROFILE_ADV_START(&pStats->CTXALLSUFF(ProfRead), a);
#endif
        int rc = pRange->CTXALLSUFF(pfnReadCallback)(pRange->a[idCPU].CTXALLSUFF(pDevIns), pRange->a[idCPU].CTXALLSUFF(pvUser), GCPhys, pu32Value, cbValue);
#ifdef VBOX_WITH_STATISTICS
        if (pStats)
            STAM_PROFILE_ADV_STOP(&pStats->CTXALLSUFF(ProfRead), a);
        if (pStats && rc != VINF_IOM_HC_MMIO_READ)
            STAM_COUNTER_INC(&pStats->CTXALLSUFF(Read));
#endif
        switch (rc)
        {
            case VINF_SUCCESS:
            default:
                Log4(("IOMMMIORead: GCPhys=%RGp *pu32=%08RX32 cb=%d rc=%Vrc\n", GCPhys, *pu32Value, cbValue, rc));
                return rc;

            case VINF_IOM_MMIO_UNUSED_00:
                switch (cbValue)
                {
                    case 1: *(uint8_t *)pu32Value  = UINT8_C(0x00); break;
                    case 2: *(uint16_t *)pu32Value = UINT16_C(0x0000); break;
                    case 4: *(uint32_t *)pu32Value = UINT32_C(0x00000000); break;
                    case 8: *(uint64_t *)pu32Value = UINT64_C(0x0000000000000000); break;
                    default: AssertReleaseMsgFailed(("cbValue=%d GCPhys=%VGp\n", cbValue, GCPhys)); break;
                }
                Log4(("IOMMMIORead: GCPhys=%RGp *pu32=%08RX32 cb=%d rc=%Vrc\n", GCPhys, *pu32Value, cbValue, rc));
                return VINF_SUCCESS;

            case VINF_IOM_MMIO_UNUSED_FF:
                switch (cbValue)
                {
                    case 1: *(uint8_t *)pu32Value  = UINT8_C(0xff); break;
                    case 2: *(uint16_t *)pu32Value = UINT16_C(0xffff); break;
                    case 4: *(uint32_t *)pu32Value = UINT32_C(0xffffffff); break;
                    case 8: *(uint64_t *)pu32Value = UINT64_C(0xffffffffffffffff); break;
                    default: AssertReleaseMsgFailed(("cbValue=%d GCPhys=%VGp\n", cbValue, GCPhys)); break;
                }
                Log4(("IOMMMIORead: GCPhys=%RGp *pu32=%08RX32 cb=%d rc=%Vrc\n", GCPhys, *pu32Value, cbValue, rc));
                return VINF_SUCCESS;
        }
    }
#ifndef IN_RING3
    if (pRange->pfnReadCallbackR3)
    {
        STAM_COUNTER_INC(&pStats->CTXALLMID(Read,ToR3));
        return VINF_IOM_HC_MMIO_READ;
    }
#endif

    /*
     * Lookup the ring-3 range.
     */
#ifdef VBOX_WITH_STATISTICS
    if (pStats)
        STAM_COUNTER_INC(&pStats->CTXALLSUFF(Read));
#endif
    /* Unassigned memory; this is actually not supposed to happen. */
    switch (cbValue)
    {
        case 1: *(uint8_t *)pu32Value  = UINT8_C(0xff); break;
        case 2: *(uint16_t *)pu32Value = UINT16_C(0xffff); break;
        case 4: *(uint32_t *)pu32Value = UINT32_C(0xffffffff); break;
        case 8: *(uint64_t *)pu32Value = UINT64_C(0xffffffffffffffff); break;
        default: AssertReleaseMsgFailed(("cbValue=%d GCPhys=%VGp\n", cbValue, GCPhys)); break;
    }
    Log4(("IOMMMIORead: GCPhys=%RGp *pu32=%08RX32 cb=%d rc=VINF_SUCCESS\n", GCPhys, *pu32Value, cbValue));
    return VINF_SUCCESS;
}


/**
 * Writes to a MMIO register.
 *
 * @returns VBox status code.
 *
 * @param   pVM         VM handle.
 * @param   GCPhys      The physical address to write to.
 * @param   u32Value    The value to write.
 * @param   cbValue     The size of the register to read in bytes. 1, 2 or 4 bytes.
 */
IOMDECL(int) IOMMMIOWrite(PVM pVM, RTGCPHYS GCPhys, uint32_t u32Value, size_t cbValue)
{
    /*
     * Lookup the current context range node.
     */
    PIOMMMIORANGE pRange = iomMMIOGetRange(&pVM->iom.s, GCPhys);
    AssertMsgReturn(pRange,
                    ("Handlers and page tables are out of sync or something! GCPhys=%VGp cbValue=%d\n", GCPhys, cbValue),
                    VERR_INTERNAL_ERROR);
#ifdef VBOX_WITH_STATISTICS
    PIOMMMIOSTATS pStats = iomMMIOGetStats(&pVM->iom.s, GCPhys, pRange);
    if (!pStats)
# ifdef IN_RING3
        return VERR_NO_MEMORY;
# else
        return VINF_IOM_HC_MMIO_WRITE;
# endif
#endif /* VBOX_WITH_STATISTICS */

    /*
     * Perform the write if there's a write handler. R0/GC may have
     * to defer it to ring-3.
     */
    if (pRange->CTXALLSUFF(pfnWriteCallback))
    {
        unsigned idCPU = (pRange->enmCtx == IOMMMIOCTX_GLOBAL) ? 0 : pVM->idCPU;
        Assert(pRange->a[idCPU].CTXALLSUFF(pDevIns)); /** @todo SMP */

#ifdef VBOX_WITH_STATISTICS
        if (pStats)
            STAM_PROFILE_ADV_START(&pStats->CTXALLSUFF(ProfWrite), a);
#endif
        int rc = pRange->CTXALLSUFF(pfnWriteCallback)(pRange->a[idCPU].CTXALLSUFF(pDevIns), pRange->a[idCPU].CTXALLSUFF(pvUser), GCPhys, &u32Value, cbValue);
#ifdef VBOX_WITH_STATISTICS
        if (pStats)
            STAM_PROFILE_ADV_STOP(&pStats->CTXALLSUFF(ProfWrite), a);
        if (pStats && rc != VINF_IOM_HC_MMIO_WRITE)
            STAM_COUNTER_INC(&pStats->CTXALLSUFF(Write));
#endif
        Log4(("IOMMMIOWrite: GCPhys=%RGp u32=%08RX32 cb=%d rc=%Vrc\n", GCPhys, u32Value, cbValue, rc));
        return rc;
    }
#ifndef IN_RING3
    if (pRange->pfnWriteCallbackR3)
    {
        STAM_COUNTER_INC(&pStats->CTXALLMID(Write,ToR3));
        return VINF_IOM_HC_MMIO_WRITE;
    }
#endif

    /*
     * No write handler, nothing to do.
     */
#ifdef VBOX_WITH_STATISTICS
    if (pStats)
        STAM_COUNTER_INC(&pStats->CTXALLSUFF(Write));
#endif
    Log4(("IOMMMIOWrite: GCPhys=%RGp u32=%08RX32 cb=%d rc=%Vrc\n", GCPhys, u32Value, cbValue, VINF_SUCCESS));
    return VINF_SUCCESS;
}


/**
 * [REP*] INSB/INSW/INSD
 * ES:EDI,DX[,ECX]
 *
 * @remark Assumes caller checked the access privileges (IOMInterpretCheckPortIOAccess)
 *
 * @returns Strict VBox status code. Informational status codes other than the one documented
 *          here are to be treated as internal failure. Use IOM_SUCCESS() to check for success.
 * @retval  VINF_SUCCESS                Success.
 * @retval  VINF_EM_FIRST-VINF_EM_LAST  Success with some exceptions (see IOM_SUCCESS()), the
 *                                      status code must be passed on to EM.
 * @retval  VINF_IOM_HC_IOPORT_READ     Defer the read to ring-3. (R0/GC only)
 * @retval  VINF_EM_RAW_EMULATE_INSTR   Defer the read to the REM.
 * @retval  VINF_EM_RAW_GUEST_TRAP      The exception was left pending. (TRPMRaiseXcptErr)
 * @retval  VINF_TRPM_XCPT_DISPATCHED   The exception was raised and dispatched for raw-mode execution. (TRPMRaiseXcptErr)
 * @retval  VINF_EM_RESCHEDULE_REM      The exception was dispatched and cannot be executed in raw-mode. (TRPMRaiseXcptErr)
 *
 * @param   pVM             The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame       Pointer to CPUMCTXCORE guest registers structure.
 * @param   uPort           IO Port
 * @param   uPrefix         IO instruction prefix
 * @param   cbTransfer      Size of transfer unit
 */
IOMDECL(int) IOMInterpretINSEx(PVM pVM, PCPUMCTXCORE pRegFrame, uint32_t uPort, uint32_t uPrefix, uint32_t cbTransfer)
{
#ifdef VBOX_WITH_STATISTICS
    STAM_COUNTER_INC(&pVM->iom.s.StatGCInstIns);
#endif

    /*
     * We do not support REPNE or decrementing destination
     * pointer. Segment prefixes are deliberately ignored, as per the instruction specification.
     */
    if (   (uPrefix & PREFIX_REPNE)
        || pRegFrame->eflags.Bits.u1DF)
        return VINF_EM_RAW_EMULATE_INSTR;

    /*
     * Get bytes/words/dwords count to transfer.
     */
    RTGCUINTREG cTransfers = 1;
    if (uPrefix & PREFIX_REP)
    {
#ifndef IN_GC
        if (    CPUMIsGuestIn64BitCode(pVM, pRegFrame)
            &&  pRegFrame->rcx >= _4G)
            return VINF_EM_RAW_EMULATE_INSTR;
#endif
        cTransfers = pRegFrame->ecx;

        if (SELMGetCpuModeFromSelector(pVM, pRegFrame->eflags, pRegFrame->cs, &pRegFrame->csHid) == CPUMODE_16BIT)
            cTransfers &= 0xffff;

        if (!cTransfers)
            return VINF_SUCCESS;
    }

    /* Convert destination address es:edi. */
    RTGCPTR GCPtrDst;
    int rc = SELMToFlatEx(pVM, DIS_SELREG_ES, pRegFrame, (RTGCPTR)pRegFrame->rdi,
                          SELMTOFLAT_FLAGS_HYPER | SELMTOFLAT_FLAGS_NO_PL,
                          &GCPtrDst);
    if (VBOX_FAILURE(rc))
    {
        Log(("INS destination address conversion failed -> fallback, rc=%d\n", rc));
        return VINF_EM_RAW_EMULATE_INSTR;
    }

    /* Access verification first; we can't recover from traps inside this instruction, as the port read cannot be repeated. */
    uint32_t cpl = CPUMGetGuestCPL(pVM, pRegFrame);

    rc = PGMVerifyAccess(pVM, (RTGCUINTPTR)GCPtrDst, cTransfers * cbTransfer,
                         X86_PTE_RW | ((cpl == 3) ? X86_PTE_US : 0));
    if (rc != VINF_SUCCESS)
    {
        Log(("INS will generate a trap -> fallback, rc=%d\n", rc));
        return VINF_EM_RAW_EMULATE_INSTR;
    }

    Log(("IOM: rep ins%d port %#x count %d\n", cbTransfer * 8, uPort, cTransfers));
    if (cTransfers > 1)
    {
        /* If the device supports string transfers, ask it to do as
         * much as it wants. The rest is done with single-word transfers. */
        const RTGCUINTREG cTransfersOrg = cTransfers;
        rc = IOMIOPortReadString(pVM, uPort, &GCPtrDst, &cTransfers, cbTransfer);
        AssertRC(rc); Assert(cTransfers <= cTransfersOrg);
        pRegFrame->rdi += (cTransfersOrg - cTransfers) * cbTransfer;
    }

#ifdef IN_GC
    MMGCRamRegisterTrapHandler(pVM);
#endif

    while (cTransfers && rc == VINF_SUCCESS)
    {
        uint32_t u32Value;
        rc = IOMIOPortRead(pVM, uPort, &u32Value, cbTransfer);
        if (!IOM_SUCCESS(rc))
            break;
        int rc2 = iomRamWrite(pVM, GCPtrDst, &u32Value, cbTransfer);
        Assert(rc2 == VINF_SUCCESS); NOREF(rc2);
        GCPtrDst = (RTGCPTR)((RTGCUINTPTR)GCPtrDst + cbTransfer);
        pRegFrame->rdi += cbTransfer;
        cTransfers--;
    }
#ifdef IN_GC
    MMGCRamDeregisterTrapHandler(pVM);
#endif

    /* Update ecx on exit. */
    if (uPrefix & PREFIX_REP)
        pRegFrame->ecx = cTransfers;

    AssertMsg(rc == VINF_SUCCESS || rc == VINF_IOM_HC_IOPORT_READ || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST) || VBOX_FAILURE(rc), ("%Vrc\n", rc));
    return rc;
}


/**
 * [REP*] INSB/INSW/INSD
 * ES:EDI,DX[,ECX]
 *
 * @returns Strict VBox status code. Informational status codes other than the one documented
 *          here are to be treated as internal failure. Use IOM_SUCCESS() to check for success.
 * @retval  VINF_SUCCESS                Success.
 * @retval  VINF_EM_FIRST-VINF_EM_LAST  Success with some exceptions (see IOM_SUCCESS()), the
 *                                      status code must be passed on to EM.
 * @retval  VINF_IOM_HC_IOPORT_READ     Defer the read to ring-3. (R0/GC only)
 * @retval  VINF_EM_RAW_EMULATE_INSTR   Defer the read to the REM.
 * @retval  VINF_EM_RAW_GUEST_TRAP      The exception was left pending. (TRPMRaiseXcptErr)
 * @retval  VINF_TRPM_XCPT_DISPATCHED   The exception was raised and dispatched for raw-mode execution. (TRPMRaiseXcptErr)
 * @retval  VINF_EM_RESCHEDULE_REM      The exception was dispatched and cannot be executed in raw-mode. (TRPMRaiseXcptErr)
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Pointer to CPUMCTXCORE guest registers structure.
 * @param   pCpu        Disassembler CPU state.
 */
IOMDECL(int) IOMInterpretINS(PVM pVM, PCPUMCTXCORE pRegFrame, PDISCPUSTATE pCpu)
{
    /*
     * Get port number directly from the register (no need to bother the
     * disassembler). And get the I/O register size from the opcode / prefix.
     */
    RTIOPORT    Port = pRegFrame->edx & 0xffff;
    unsigned    cb = 0;
    if (pCpu->pCurInstr->opcode == OP_INSB)
        cb = 1;
    else
        cb = (pCpu->opmode == CPUMODE_16BIT) ? 2 : 4;       /* dword in both 32 & 64 bits mode */

    int rc = IOMInterpretCheckPortIOAccess(pVM, pRegFrame, Port, cb);
    if (RT_UNLIKELY(rc != VINF_SUCCESS))
    {
        AssertMsg(rc == VINF_EM_RAW_GUEST_TRAP || rc == VINF_TRPM_XCPT_DISPATCHED || rc == VINF_TRPM_XCPT_DISPATCHED || VBOX_FAILURE(rc), ("%Vrc\n", rc));
        return rc;
    }

    return IOMInterpretINSEx(pVM, pRegFrame, Port, pCpu->prefix, cb);
}


/**
 * [REP*] OUTSB/OUTSW/OUTSD
 * DS:ESI,DX[,ECX]
 *
 * @remark  Assumes caller checked the access privileges (IOMInterpretCheckPortIOAccess)
 *
 * @returns Strict VBox status code. Informational status codes other than the one documented
 *          here are to be treated as internal failure. Use IOM_SUCCESS() to check for success.
 * @retval  VINF_SUCCESS                Success.
 * @retval  VINF_EM_FIRST-VINF_EM_LAST  Success with some exceptions (see IOM_SUCCESS()), the
 *                                      status code must be passed on to EM.
 * @retval  VINF_IOM_HC_IOPORT_WRITE    Defer the write to ring-3. (R0/GC only)
 * @retval  VINF_EM_RAW_GUEST_TRAP      The exception was left pending. (TRPMRaiseXcptErr)
 * @retval  VINF_TRPM_XCPT_DISPATCHED   The exception was raised and dispatched for raw-mode execution. (TRPMRaiseXcptErr)
 * @retval  VINF_EM_RESCHEDULE_REM      The exception was dispatched and cannot be executed in raw-mode. (TRPMRaiseXcptErr)
 *
 * @param   pVM             The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame       Pointer to CPUMCTXCORE guest registers structure.
 * @param   uPort           IO Port
 * @param   uPrefix         IO instruction prefix
 * @param   cbTransfer      Size of transfer unit
 */
IOMDECL(int) IOMInterpretOUTSEx(PVM pVM, PCPUMCTXCORE pRegFrame, uint32_t uPort, uint32_t uPrefix, uint32_t cbTransfer)
{
#ifdef VBOX_WITH_STATISTICS
    STAM_COUNTER_INC(&pVM->iom.s.StatGCInstOuts);
#endif

    /*
     * We do not support segment prefixes, REPNE or
     * decrementing source pointer.
     */
    if (   (uPrefix & (PREFIX_SEG | PREFIX_REPNE))
        || pRegFrame->eflags.Bits.u1DF)
        return VINF_EM_RAW_EMULATE_INSTR;

    /*
     * Get bytes/words/dwords count to transfer.
     */
    RTGCUINTREG cTransfers = 1;
    if (uPrefix & PREFIX_REP)
    {
#ifndef IN_GC
        if (    CPUMIsGuestIn64BitCode(pVM, pRegFrame)
            &&  pRegFrame->rcx >= _4G)
            return VINF_EM_RAW_EMULATE_INSTR;
#endif
        cTransfers = pRegFrame->ecx;
        if (SELMGetCpuModeFromSelector(pVM, pRegFrame->eflags, pRegFrame->cs, &pRegFrame->csHid) == CPUMODE_16BIT)
            cTransfers &= 0xffff;

        if (!cTransfers)
            return VINF_SUCCESS;
    }

    /* Convert source address ds:esi. */
    RTGCPTR GCPtrSrc;
    int rc = SELMToFlatEx(pVM, DIS_SELREG_DS, pRegFrame, (RTGCPTR)pRegFrame->rsi,
                          SELMTOFLAT_FLAGS_HYPER | SELMTOFLAT_FLAGS_NO_PL,
                          &GCPtrSrc);
    if (VBOX_FAILURE(rc))
    {
        Log(("OUTS source address conversion failed -> fallback, rc=%Vrc\n", rc));
        return VINF_EM_RAW_EMULATE_INSTR;
    }

    /* Access verification first; we currently can't recover properly from traps inside this instruction */
    uint32_t cpl = CPUMGetGuestCPL(pVM, pRegFrame);
    rc = PGMVerifyAccess(pVM, (RTGCUINTPTR)GCPtrSrc, cTransfers * cbTransfer,
                         (cpl == 3) ? X86_PTE_US : 0);
    if (rc != VINF_SUCCESS)
    {
        Log(("OUTS will generate a trap -> fallback, rc=%Vrc\n", rc));
        return VINF_EM_RAW_EMULATE_INSTR;
    }

    Log(("IOM: rep outs%d port %#x count %d\n", cbTransfer * 8, uPort, cTransfers));
    if (cTransfers > 1)
    {
        /*
         * If the device supports string transfers, ask it to do as
         * much as it wants. The rest is done with single-word transfers.
         */
        const RTGCUINTREG cTransfersOrg = cTransfers;
        rc = IOMIOPortWriteString(pVM, uPort, &GCPtrSrc, &cTransfers, cbTransfer);
        AssertRC(rc); Assert(cTransfers <= cTransfersOrg);
        pRegFrame->rsi += (cTransfersOrg - cTransfers) * cbTransfer;
    }

#ifdef IN_GC
    MMGCRamRegisterTrapHandler(pVM);
#endif

    while (cTransfers && rc == VINF_SUCCESS)
    {
        uint32_t u32Value;
        rc = iomRamRead(pVM, &u32Value, GCPtrSrc, cbTransfer);
        if (rc != VINF_SUCCESS)
            break;
        rc = IOMIOPortWrite(pVM, uPort, u32Value, cbTransfer);
        if (!IOM_SUCCESS(rc))
            break;
        GCPtrSrc = (RTGCPTR)((RTUINTPTR)GCPtrSrc + cbTransfer);
        pRegFrame->rsi += cbTransfer;
        cTransfers--;
    }

#ifdef IN_GC
    MMGCRamDeregisterTrapHandler(pVM);
#endif

    /* Update ecx on exit. */
    if (uPrefix & PREFIX_REP)
        pRegFrame->ecx = cTransfers;

    AssertMsg(rc == VINF_SUCCESS || rc == VINF_IOM_HC_IOPORT_WRITE || (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST) || VBOX_FAILURE(rc), ("%Vrc\n", rc));
    return rc;
}


/**
 * [REP*] OUTSB/OUTSW/OUTSD
 * DS:ESI,DX[,ECX]
 *
 * @returns Strict VBox status code. Informational status codes other than the one documented
 *          here are to be treated as internal failure. Use IOM_SUCCESS() to check for success.
 * @retval  VINF_SUCCESS                Success.
 * @retval  VINF_EM_FIRST-VINF_EM_LAST  Success with some exceptions (see IOM_SUCCESS()), the
 *                                      status code must be passed on to EM.
 * @retval  VINF_IOM_HC_IOPORT_WRITE    Defer the write to ring-3. (R0/GC only)
 * @retval  VINF_EM_RAW_EMULATE_INSTR   Defer the write to the REM.
 * @retval  VINF_EM_RAW_GUEST_TRAP      The exception was left pending. (TRPMRaiseXcptErr)
 * @retval  VINF_TRPM_XCPT_DISPATCHED   The exception was raised and dispatched for raw-mode execution. (TRPMRaiseXcptErr)
 * @retval  VINF_EM_RESCHEDULE_REM      The exception was dispatched and cannot be executed in raw-mode. (TRPMRaiseXcptErr)
 *
 * @param   pVM         The virtual machine (GC pointer ofcourse).
 * @param   pRegFrame   Pointer to CPUMCTXCORE guest registers structure.
 * @param   pCpu        Disassembler CPU state.
 */
IOMDECL(int) IOMInterpretOUTS(PVM pVM, PCPUMCTXCORE pRegFrame, PDISCPUSTATE pCpu)
{
    /*
     * Get port number from the first parameter.
     * And get the I/O register size from the opcode / prefix.
     */
    uint64_t    Port = 0;
    unsigned    cb = 0;
    bool fRc = iomGetRegImmData(pCpu, &pCpu->param1, pRegFrame, &Port, &cb);
    AssertMsg(fRc, ("Failed to get reg/imm port number!\n")); NOREF(fRc);
    if (pCpu->pCurInstr->opcode == OP_OUTSB)
        cb = 1;
    else
        cb = (pCpu->opmode == CPUMODE_16BIT) ? 2 : 4;       /* dword in both 32 & 64 bits mode */

    int rc = IOMInterpretCheckPortIOAccess(pVM, pRegFrame, Port, cb);
    if (RT_UNLIKELY(rc != VINF_SUCCESS))
    {
        AssertMsg(rc == VINF_EM_RAW_GUEST_TRAP || rc == VINF_TRPM_XCPT_DISPATCHED || rc == VINF_TRPM_XCPT_DISPATCHED || VBOX_FAILURE(rc), ("%Vrc\n", rc));
        return rc;
    }

    return IOMInterpretOUTSEx(pVM, pRegFrame, Port, pCpu->prefix, cb);
}

