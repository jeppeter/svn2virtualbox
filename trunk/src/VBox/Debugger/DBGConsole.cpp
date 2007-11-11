/** $Id$ */
/** @file
 * DBGC - Debugger Console.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/** @page pg_dbgc                       DBGC - The Debug Console
 *
 * The debugger console is a first attempt to make some interactive
 * debugging facilities for the VirtualBox backend (i.e. the VM). At a later
 * stage we'll make a fancy gui around this, but for the present a telnet (or
 * serial terminal) will have to suffice.
 *
 * The debugger is only built into the VM with debug builds or when
 * VBOX_WITH_DEBUGGER is defined. There might be need for \#ifdef'ing on this
 * define to enable special debugger hooks, but the general approach is to
 * make generic interfaces. The individual components also can register
 * external commands, and such code must be within \#ifdef.
 *
 *
 * @section sec_dbgc_op                 Operation (intentions)
 *
 * The console will process commands in a manner similar to the OS/2 and
 * windows kernel debuggers. This means ';' is a command separator and
 * that when possible we'll use the same command names as these two uses.
 *
 *
 * @subsection sec_dbg_op_numbers       Numbers
 *
 * Numbers are hexadecimal unless specified with a prefix indicating
 * elsewise. Prefixes:
 *      - '0x' - hexadecimal.
 *      - '0i' - decimal
 *      - '0t' - octal.
 *      - '0y' - binary.
 *
 *
 * @subsection sec_dbg_op_address       Addressing modes
 *
 *      - Default is flat. For compatability '%' also means flat.
 *      - Segmented addresses are specified selector:offset.
 *      - Physical addresses are specified using '%%'.
 *      - The default target for the addressing is the guest context, the '#'
 *        will override this and set it to the host.
 *
 *
 * @subsection sec_dbg_op_evalution     Evaluation
 *
 * As time permits support will be implemented support for a subset of the C
 * binary operators, starting with '+', '-', '*' and '/'. Support for variables
 * are provided thru commands 'set' and 'unset' and the unary operator '$'. The
 * unary '@' operator will indicate function calls. The debugger needs a set of
 * memory read functions, but we might later extend this to allow registration of
 * external functions too.
 *
 * A special command '?' will then be added which evalutates a given expression
 * and prints it in all the different formats.
 *
 *
 * @subsection sec_dbg_op_registers     Registers
 *
 * Registers are addressed using their name. Some registers which have several fields
 * (like gdtr) will have separate names indicating the different fields. The default
 * register set is the guest one. To access the hypervisor register one have to
 * prefix the register names with '.'.
 *
 *
 * @subsection sec_dbg_op_commands      Commands
 *
 * The commands are all lowercase, case sensitive, and starting with a letter. We will
 * later add some special commands ('?' for evaulation) and perhaps command classes ('.', '!')
 *
 *
 * @section sec_dbg_tasks               Tasks
 *
 * To implement DBGT and instrument VMM for basic state inspection and log
 * viewing, the follwing task must be executed:
 *
 *      -# Basic threading layer in RT.
 *      -# Basic tcpip server abstration in RT.
 *      -# Write DBGC.
 *      -# Write DBCTCP.
 *      -# Integrate with VMM and the rest.
 *      -# Start writing DBGF (VMM).
 */




/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGC
#include <VBox/dbg.h>
#include <VBox/dbgf.h>
#include <VBox/vm.h>
#include <VBox/vmm.h>
#include <VBox/mm.h>
#include <VBox/pgm.h>
#include <VBox/selm.h>
#include <VBox/dis.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>

#include <stdlib.h>
#include <stdio.h>

#include "DBGCInternal.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Bitmap where set bits indicates the characters the may start an operator name. */
static uint32_t g_bmOperatorChars[256 / (4*8)];







//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//
//
//
//      V a r i a b l e   M a n i p u l a t i o n
//
//
//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//



/** @todo move me!*/
void dbgcVarSetGCFlat(PDBGCVAR pVar, RTGCPTR GCFlat)
{
    if (pVar)
    {
        pVar->enmType  = DBGCVAR_TYPE_GC_FLAT;
        pVar->u.GCFlat = GCFlat;
        pVar->enmRangeType = DBGCVAR_RANGE_NONE;
        pVar->u64Range  = 0;
    }
}


/** @todo move me!*/
void dbgcVarSetGCFlatByteRange(PDBGCVAR pVar, RTGCPTR GCFlat, uint64_t cb)
{
    if (pVar)
    {
        pVar->enmType  = DBGCVAR_TYPE_GC_FLAT;
        pVar->u.GCFlat = GCFlat;
        pVar->enmRangeType = DBGCVAR_RANGE_BYTES;
        pVar->u64Range  = cb;
    }
}


/** @todo move me!*/
void dbgcVarSetVar(PDBGCVAR pVar, PCDBGCVAR pVar2)
{
    if (pVar)
    {
        if (pVar2)
            *pVar = *pVar2;
        else
        {
            pVar->enmType = DBGCVAR_TYPE_UNKNOWN;
            memset(&pVar->u, 0, sizeof(pVar->u));
            pVar->enmRangeType = DBGCVAR_RANGE_NONE;
            pVar->u64Range  = 0;
        }
    }
}


/** @todo move me!*/
void dbgcVarSetByteRange(PDBGCVAR pVar, uint64_t cb)
{
    if (pVar)
    {
        pVar->enmRangeType = DBGCVAR_RANGE_BYTES;
        pVar->u64Range  = cb;
    }
}


/** @todo move me!*/
void dbgcVarSetNoRange(PDBGCVAR pVar)
{
    if (pVar)
    {
        pVar->enmRangeType = DBGCVAR_RANGE_NONE;
        pVar->u64Range  = 0;
    }
}


/**
 * Converts a DBGC variable to a DBGF address.
 *
 * @returns VBox status code.
 * @param   pDbgc       The DBGC instance.
 * @param   pVar        The variable.
 * @param   pAddress    Where to store the address.
 */
int dbgcVarToDbgfAddr(PDBGC pDbgc, PCDBGCVAR pVar, PDBGFADDRESS pAddress)
{
    AssertReturn(pVar, VERR_INVALID_PARAMETER);
    switch (pVar->enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
            DBGFR3AddrFromFlat(pDbgc->pVM, pAddress, pVar->u.GCFlat);
            return VINF_SUCCESS;

        case DBGCVAR_TYPE_NUMBER:
            DBGFR3AddrFromFlat(pDbgc->pVM, pAddress, (RTGCUINTPTR)pVar->u.u64Number);
            return VINF_SUCCESS;

        case DBGCVAR_TYPE_GC_FAR:
            return DBGFR3AddrFromSelOff(pDbgc->pVM, pAddress, pVar->u.GCFar.sel, pVar->u.GCFar.sel);

        case DBGCVAR_TYPE_STRING:
        case DBGCVAR_TYPE_SYMBOL:
        {
            DBGCVAR Var;
            int rc = pDbgc->CmdHlp.pfnEval(&pDbgc->CmdHlp, &Var, "%%(%DV)", pVar);
            if (VBOX_FAILURE(rc))
                return rc;
            return dbgcVarToDbgfAddr(pDbgc, &Var, pAddress);
        }

        case DBGCVAR_TYPE_GC_PHYS:
        case DBGCVAR_TYPE_HC_FLAT:
        case DBGCVAR_TYPE_HC_FAR:
        case DBGCVAR_TYPE_HC_PHYS:
        default:
            return VERR_PARSE_CONVERSION_FAILED;
    }
}



//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//
//
//
//      B r e a k p o i n t   M a n a g e m e n t
//
//
//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//


/**
 * Adds a breakpoint to the DBGC breakpoint list.
 */
int dbgcBpAdd(PDBGC pDbgc, RTUINT iBp, const char *pszCmd)
{
    /*
     * Check if it already exists.
     */
    PDBGCBP pBp = dbgcBpGet(pDbgc, iBp);
    if (pBp)
        return VERR_DBGC_BP_EXISTS;

    /*
     * Add the breakpoint.
     */
    if (pszCmd)
        pszCmd = RTStrStripL(pszCmd);
    size_t cchCmd = pszCmd ? strlen(pszCmd) : 0;
    pBp = (PDBGCBP)RTMemAlloc(RT_OFFSETOF(DBGCBP, szCmd[cchCmd + 1]));
    if (!pBp)
        return VERR_NO_MEMORY;
    if (cchCmd)
        memcpy(pBp->szCmd, pszCmd, cchCmd + 1);
    else
        pBp->szCmd[0] = '\0';
    pBp->cchCmd = cchCmd;
    pBp->iBp    = iBp;
    pBp->pNext  = pDbgc->pFirstBp;
    pDbgc->pFirstBp = pBp;

    return VINF_SUCCESS;
}

/**
 * Updates the a breakpoint.
 *
 * @returns VBox status code.
 * @param   pDbgc       The DBGC instance.
 * @param   iBp         The breakpoint to update.
 * @param   pszCmd      The new command.
 */
int dbgcBpUpdate(PDBGC pDbgc, RTUINT iBp, const char *pszCmd)
{
    /*
     * Find the breakpoint.
     */
    PDBGCBP pBp = dbgcBpGet(pDbgc, iBp);
    if (!pBp)
        return VERR_DBGC_BP_NOT_FOUND;

    /*
     * Do we need to reallocate?
     */
    if (pszCmd)
        pszCmd = RTStrStripL(pszCmd);
    if (!pszCmd || !*pszCmd)
        pBp->szCmd[0] = '\0';
    else
    {
        size_t cchCmd = strlen(pszCmd);
        if (strlen(pBp->szCmd) >= cchCmd)
        {
            memcpy(pBp->szCmd, pszCmd, cchCmd + 1);
            pBp->cchCmd = cchCmd;
        }
        else
        {
            /*
             * Yes, let's do it the simple way...
             */
            int rc = dbgcBpDelete(pDbgc, iBp);
            AssertRC(rc);
            return dbgcBpAdd(pDbgc, iBp, pszCmd);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Deletes a breakpoint.
 *
 * @returns VBox status code.
 * @param   pDbgc       The DBGC instance.
 * @param   iBp         The breakpoint to delete.
 */
int dbgcBpDelete(PDBGC pDbgc, RTUINT iBp)
{
    /*
     * Search thru the list, when found unlink and free it.
     */
    PDBGCBP pBpPrev = NULL;
    PDBGCBP pBp = pDbgc->pFirstBp;
    for (; pBp; pBp = pBp->pNext)
    {
        if (pBp->iBp == iBp)
        {
            if (pBpPrev)
                pBpPrev->pNext = pBp->pNext;
            else
                pDbgc->pFirstBp = pBp->pNext;
            RTMemFree(pBp);
            return VINF_SUCCESS;
        }
        pBpPrev = pBp;
    }

    return VERR_DBGC_BP_NOT_FOUND;
}


/**
 * Get a breakpoint.
 *
 * @returns Pointer to the breakpoint.
 * @returns NULL if the breakpoint wasn't found.
 * @param   pDbgc       The DBGC instance.
 * @param   iBp         The breakpoint to get.
 */
PDBGCBP dbgcBpGet(PDBGC pDbgc, RTUINT iBp)
{
    /*
     * Enumerate the list.
     */
    PDBGCBP pBp = pDbgc->pFirstBp;
    for (; pBp; pBp = pBp->pNext)
        if (pBp->iBp == iBp)
            return pBp;
    return NULL;
}


/**
 * Executes the command of a breakpoint.
 *
 * @returns VINF_DBGC_BP_NO_COMMAND if there is no command associated with the breakpoint.
 * @returns VERR_DBGC_BP_NOT_FOUND if the breakpoint wasn't found.
 * @returns VERR_BUFFER_OVERFLOW if the is not enough space in the scratch buffer for the command.
 * @returns VBox status code from dbgcProcessCommand() other wise.
 * @param   pDbgc       The DBGC instance.
 * @param   iBp         The breakpoint to execute.
 */
int dbgcBpExec(PDBGC pDbgc, RTUINT iBp)
{
    /*
     * Find the breakpoint.
     */
    PDBGCBP pBp = dbgcBpGet(pDbgc, iBp);
    if (!pBp)
        return VERR_DBGC_BP_NOT_FOUND;

    /*
     * Anything to do?
     */
    if (!pBp->cchCmd)
        return VINF_DBGC_BP_NO_COMMAND;

    /*
     * Execute the command.
     * This means copying it to the scratch buffer and process it as if it
     * were user input. We must save and restore the state of the scratch buffer.
     */
    /* Save the scratch state. */
    char       *pszScratch  = pDbgc->pszScratch;
    unsigned    iArg        = pDbgc->iArg;

    /* Copy the command to the scratch buffer. */
    size_t cbScratch = sizeof(pDbgc->achScratch) - (pDbgc->pszScratch - &pDbgc->achScratch[0]);
    if (pBp->cchCmd >= cbScratch)
        return VERR_BUFFER_OVERFLOW;
    memcpy(pDbgc->pszScratch, pBp->szCmd, pBp->cchCmd + 1);

    /* Execute the command. */
    pDbgc->pszScratch = pDbgc->pszScratch + pBp->cchCmd + 1;
    int rc = dbgcProcessCommand(pDbgc, pszScratch, pBp->cchCmd);

    /* Restore the scratch state. */
    pDbgc->iArg         = iArg;
    pDbgc->pszScratch   = pszScratch;

    return rc;
}





//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//
//
//
//      I n p u t ,   p a r s i n g   a n d   l o g g i n g
//
//
//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//



/**
 * Prints any log lines from the log buffer.
 *
 * The caller must not call function this unless pDbgc->fLog is set.
 *
 * @returns VBox status. (output related)
 * @param   pDbgc   Debugger console instance data.
 */
static int dbgcProcessLog(PDBGC pDbgc)
{
    /** @todo */
    NOREF(pDbgc);
    return 0;
}



/**
 * Handle input buffer overflow.
 *
 * Will read any available input looking for a '\n' to reset the buffer on.
 *
 * @returns VBox status.
 * @param   pDbgc   Debugger console instance data.
 */
static int dbgcInputOverflow(PDBGC pDbgc)
{
    /*
     * Assert overflow status and reset the input buffer.
     */
    if (!pDbgc->fInputOverflow)
    {
        pDbgc->fInputOverflow = true;
        pDbgc->iRead = pDbgc->iWrite = 0;
        pDbgc->cInputLines = 0;
        pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "Input overflow!!\n");
    }

    /*
     * Eat input till no more or there is a '\n'.
     * When finding a '\n' we'll continue normal processing.
     */
    while (pDbgc->pBack->pfnInput(pDbgc->pBack, 0))
    {
        size_t cbRead;
        int rc = pDbgc->pBack->pfnRead(pDbgc->pBack, &pDbgc->achInput[0], sizeof(pDbgc->achInput) - 1, &cbRead);
        if (VBOX_FAILURE(rc))
            return rc;
        char *psz = (char *)memchr(&pDbgc->achInput[0], '\n', cbRead);
        if (psz)
        {
            pDbgc->fInputOverflow = false;
            pDbgc->iRead = psz - &pDbgc->achInput[0] + 1;
            pDbgc->iWrite = (unsigned)cbRead;
            pDbgc->cInputLines = 0;
            break;
        }
    }

    return 0;
}



/**
 * Read input and do some preprocessing.
 *
 * @returns VBox status.
 *          In addition to the iWrite and achInput, cInputLines is maintained.
 *          In case of an input overflow the fInputOverflow flag will be set.
 * @param   pDbgc   Debugger console instance data.
 */
static int dbgcInputRead(PDBGC pDbgc)
{
    /*
     * We have ready input.
     * Read it till we don't have any or we have a full input buffer.
     */
    int     rc = 0;
    do
    {
        /*
         * More available buffer space?
         */
        size_t cbLeft;
        if (pDbgc->iWrite > pDbgc->iRead)
            cbLeft = sizeof(pDbgc->achInput) - pDbgc->iWrite - (pDbgc->iRead == 0);
        else
            cbLeft = pDbgc->iRead - pDbgc->iWrite - 1;
        if (!cbLeft)
        {
            /* overflow? */
            if (!pDbgc->cInputLines)
                rc = dbgcInputOverflow(pDbgc);
            break;
        }

        /*
         * Read one char and interpret it.
         */
        char    achRead[128];
        size_t  cbRead;
        rc = pDbgc->pBack->pfnRead(pDbgc->pBack, &achRead[0], RT_MIN(cbLeft, sizeof(achRead)), &cbRead);
        if (VBOX_FAILURE(rc))
            return rc;
        char *psz = &achRead[0];
        while (cbRead-- > 0)
        {
            char ch = *psz++;
            switch (ch)
            {
                /*
                 * Ignore.
                 */
                case '\0':
                case '\r':
                case '\a':
                    break;

                /*
                 * Backspace.
                 */
                case '\b':
                    Log2(("DBGC: backspace\n"));
                    if (pDbgc->iRead != pDbgc->iWrite)
                    {
                        unsigned iWriteUndo = pDbgc->iWrite;
                        if (pDbgc->iWrite)
                            pDbgc->iWrite--;
                        else
                            pDbgc->iWrite = sizeof(pDbgc->achInput) - 1;

                        if (pDbgc->achInput[pDbgc->iWrite] == '\n')
                            pDbgc->iWrite = iWriteUndo;
                    }
                    break;

                /*
                 * Add char to buffer.
                 */
                case '\t':
                case '\n':
                case ';':
                    switch (ch)
                    {
                        case '\t': ch = ' '; break;
                        case '\n': pDbgc->cInputLines++; break;
                    }
                default:
                    Log2(("DBGC: ch=%02x\n", (unsigned char)ch));
                    pDbgc->achInput[pDbgc->iWrite] = ch;
                    if (++pDbgc->iWrite >= sizeof(pDbgc->achInput))
                        pDbgc->iWrite = 0;
                    break;
            }
        }

        /* Terminate it to make it easier to read in the debugger. */
        pDbgc->achInput[pDbgc->iWrite] = '\0';
    } while (pDbgc->pBack->pfnInput(pDbgc->pBack, 0));

    return rc;
}



/**
 * Resolves a symbol (or tries to do so at least).
 *
 * @returns 0 on success.
 * @returns VBox status on failure.
 * @param   pDbgc       The debug console instance.
 * @param   pszSymbol   The symbol name.
 * @param   enmType     The result type.
 * @param   pResult     Where to store the result.
 */
int dbgcSymbolGet(PDBGC pDbgc, const char *pszSymbol, DBGCVARTYPE enmType, PDBGCVAR pResult)
{
    /*
     * Builtin?
     */
    PCDBGCSYM   pSymDesc = dbgcLookupRegisterSymbol(pDbgc, pszSymbol);
    if (pSymDesc)
    {
        if (!pSymDesc->pfnGet)
            return VERR_PARSE_WRITEONLY_SYMBOL;
        return pSymDesc->pfnGet(pSymDesc, &pDbgc->CmdHlp, enmType, pResult);
    }


    /*
     * Ask PDM.
     */
    /** @todo resolve symbols using PDM. */


    /*
     * Ask the debug info manager.
     */
    DBGFSYMBOL Symbol;
    int rc = DBGFR3SymbolByName(pDbgc->pVM, pszSymbol, &Symbol);
    if (VBOX_SUCCESS(rc))
    {
        /*
         * Default return is a flat gc address.
         */
        memset(pResult, 0,  sizeof(*pResult));
        pResult->enmRangeType = Symbol.cb ? DBGCVAR_RANGE_BYTES : DBGCVAR_RANGE_NONE;
        pResult->u64Range     = Symbol.cb;
        pResult->enmType      = DBGCVAR_TYPE_GC_FLAT;
        pResult->u.GCFlat     = Symbol.Value;
        DBGCVAR VarTmp;
        switch (enmType)
        {
            /* nothing to do. */
            case DBGCVAR_TYPE_GC_FLAT:
            case DBGCVAR_TYPE_GC_FAR:
            case DBGCVAR_TYPE_ANY:
                return VINF_SUCCESS;

            /* simply make it numeric. */
            case DBGCVAR_TYPE_NUMBER:
                pResult->enmType = DBGCVAR_TYPE_NUMBER;
                pResult->u.u64Number = Symbol.Value;
                return VINF_SUCCESS;

            /* cast it. */

            case DBGCVAR_TYPE_GC_PHYS:
                VarTmp = *pResult;
                return dbgcOpAddrPhys(pDbgc, &VarTmp, pResult);

            case DBGCVAR_TYPE_HC_FAR:
            case DBGCVAR_TYPE_HC_FLAT:
                VarTmp = *pResult;
                return dbgcOpAddrHost(pDbgc, &VarTmp, pResult);

            case DBGCVAR_TYPE_HC_PHYS:
                VarTmp = *pResult;
                return dbgcOpAddrHostPhys(pDbgc, &VarTmp, pResult);

            default:
                AssertMsgFailed(("Internal error enmType=%d\n", enmType));
                return VERR_INVALID_PARAMETER;
        }
    }

    return VERR_PARSE_NOT_IMPLEMENTED;
}


/**
 * Initalizes g_bmOperatorChars.
 */
static void dbgcInitOpCharBitMap(void)
{
    memset(g_bmOperatorChars, 0, sizeof(g_bmOperatorChars));
    for (unsigned iOp = 0; iOp < g_cOps; iOp++)
        ASMBitSet(&g_bmOperatorChars[0], (uint8_t)g_aOps[iOp].szName[0]);
}


/**
 * Checks whether the character may be the start of an operator.
 *
 * @returns true/false.
 * @param   ch      The character.
 */
DECLINLINE(bool) dbgcIsOpChar(char ch)
{
    return ASMBitTest(&g_bmOperatorChars[0], (uint8_t)ch);
}


static int dbgcEvalSubString(PDBGC pDbgc, char *pszExpr, size_t cchExpr, PDBGCVAR pArg)
{
    Log2(("dbgcEvalSubString: cchExpr=%d pszExpr=%s\n", cchExpr, pszExpr));

    /*
     * Removing any quoting and escapings.
     */
    char ch = *pszExpr;
    if (ch == '"' || ch == '\'' || ch == '`')
    {
        if (pszExpr[--cchExpr] != ch)
            return VERR_PARSE_UNBALANCED_QUOTE;
        cchExpr--;
        pszExpr++;

        /** @todo string unescaping. */
    }
    pszExpr[cchExpr] = '\0';

    /*
     * Make the argument.
     */
    pArg->pDesc         = NULL;
    pArg->pNext         = NULL;
    pArg->enmType       = DBGCVAR_TYPE_STRING;
    pArg->u.pszString   = pszExpr;
    pArg->enmRangeType  = DBGCVAR_RANGE_BYTES;
    pArg->u64Range      = cchExpr;

    NOREF(pDbgc);
    return 0;
}


static int dbgcEvalSubNum(char *pszExpr, unsigned uBase, PDBGCVAR pArg)
{
    Log2(("dbgcEvalSubNum: uBase=%d pszExpr=%s\n", uBase, pszExpr));
    /*
     * Convert to number.
     */
    uint64_t    u64 = 0;
    char        ch;
    while ((ch = *pszExpr) != '\0')
    {
        uint64_t    u64Prev = u64;
        unsigned    u = ch - '0';
        if (u < 10 && u < uBase)
            u64 = u64 * uBase + u;
        else if (ch >= 'a' && (u = ch - ('a' - 10)) < uBase)
            u64 = u64 * uBase + u;
        else if (ch >= 'A' && (u = ch - ('A' - 10)) < uBase)
            u64 = u64 * uBase + u;
        else
            return VERR_PARSE_INVALID_NUMBER;

        /* check for overflow - ARG!!! How to detect overflow correctly!?!?!? */
        if (u64Prev != u64 / uBase)
            return VERR_PARSE_NUMBER_TOO_BIG;

        /* next */
        pszExpr++;
    }

    /*
     * Initialize the argument.
     */
    pArg->pDesc         = NULL;
    pArg->pNext         = NULL;
    pArg->enmType       = DBGCVAR_TYPE_NUMBER;
    pArg->u.u64Number   = u64;
    pArg->enmRangeType  = DBGCVAR_RANGE_NONE;
    pArg->u64Range      = 0;

    return 0;
}


/**
 * Match variable and variable descriptor, promoting the variable if necessary.
 *
 * @returns VBox status code.
 * @param   pDbgc       Debug console instanace.
 * @param   pVar        Variable.
 * @param   pVarDesc    Variable descriptor.
 */
static int dbgcEvalSubMatchVar(PDBGC pDbgc, PDBGCVAR pVar, PCDBGCVARDESC pVarDesc)
{
    /*
     * (If match or promoted to match, return, else break.)
     */
    switch (pVarDesc->enmCategory)
    {
        /*
         * Anything goes
         */
        case DBGCVAR_CAT_ANY:
            return VINF_SUCCESS;

        /*
         * Pointer with and without range.
         * We can try resolve strings and symbols as symbols and
         * promote numbers to flat GC pointers.
         */
        case DBGCVAR_CAT_POINTER_NO_RANGE:
            if (pVar->enmRangeType != DBGCVAR_RANGE_NONE)
                return VERR_PARSE_NO_RANGE_ALLOWED;
            /* fallthru */
        case DBGCVAR_CAT_POINTER:
            switch (pVar->enmType)
            {
                case DBGCVAR_TYPE_GC_FLAT:
                case DBGCVAR_TYPE_GC_FAR:
                case DBGCVAR_TYPE_GC_PHYS:
                case DBGCVAR_TYPE_HC_FLAT:
                case DBGCVAR_TYPE_HC_FAR:
                case DBGCVAR_TYPE_HC_PHYS:
                    return VINF_SUCCESS;

                case DBGCVAR_TYPE_SYMBOL:
                case DBGCVAR_TYPE_STRING:
                {
                    DBGCVAR Var;
                    int rc = dbgcSymbolGet(pDbgc, pVar->u.pszString, DBGCVAR_TYPE_GC_FLAT, &Var);
                    if (VBOX_SUCCESS(rc))
                    {
                        /* deal with range */
                        if (pVar->enmRangeType != DBGCVAR_RANGE_NONE)
                        {
                            Var.enmRangeType = pVar->enmRangeType;
                            Var.u64Range = pVar->u64Range;
                        }
                        else if (pVarDesc->enmCategory == DBGCVAR_CAT_POINTER_NO_RANGE)
                            Var.enmRangeType = DBGCVAR_RANGE_NONE;
                        *pVar = Var;
                        return rc;
                    }
                    break;
                }

                case DBGCVAR_TYPE_NUMBER:
                {
                    RTGCPTR GCPtr = (RTGCPTR)pVar->u.u64Number;
                    pVar->enmType = DBGCVAR_TYPE_GC_FLAT;
                    pVar->u.GCFlat = GCPtr;
                    return VINF_SUCCESS;
                }

                default:
                    break;
            }
            break;

        /*
         * GC pointer with and without range.
         * We can try resolve strings and symbols as symbols and
         * promote numbers to flat GC pointers.
         */
        case DBGCVAR_CAT_GC_POINTER_NO_RANGE:
            if (pVar->enmRangeType != DBGCVAR_RANGE_NONE)
                return VERR_PARSE_NO_RANGE_ALLOWED;
            /* fallthru */
        case DBGCVAR_CAT_GC_POINTER:
            switch (pVar->enmType)
            {
                case DBGCVAR_TYPE_GC_FLAT:
                case DBGCVAR_TYPE_GC_FAR:
                case DBGCVAR_TYPE_GC_PHYS:
                    return VINF_SUCCESS;

                case DBGCVAR_TYPE_HC_FLAT:
                case DBGCVAR_TYPE_HC_FAR:
                case DBGCVAR_TYPE_HC_PHYS:
                    return VERR_PARSE_CONVERSION_FAILED;

                case DBGCVAR_TYPE_SYMBOL:
                case DBGCVAR_TYPE_STRING:
                {
                    DBGCVAR Var;
                    int rc = dbgcSymbolGet(pDbgc, pVar->u.pszString, DBGCVAR_TYPE_GC_FLAT, &Var);
                    if (VBOX_SUCCESS(rc))
                    {
                        /* deal with range */
                        if (pVar->enmRangeType != DBGCVAR_RANGE_NONE)
                        {
                            Var.enmRangeType = pVar->enmRangeType;
                            Var.u64Range = pVar->u64Range;
                        }
                        else if (pVarDesc->enmCategory == DBGCVAR_CAT_POINTER_NO_RANGE)
                            Var.enmRangeType = DBGCVAR_RANGE_NONE;
                        *pVar = Var;
                        return rc;
                    }
                    break;
                }

                case DBGCVAR_TYPE_NUMBER:
                {
                    RTGCPTR GCPtr = (RTGCPTR)pVar->u.u64Number;
                    pVar->enmType = DBGCVAR_TYPE_GC_FLAT;
                    pVar->u.GCFlat = GCPtr;
                    return VINF_SUCCESS;
                }

                default:
                    break;
            }
            break;

        /*
         * Number with or without a range.
         * Numbers can be resolved from symbols, but we cannot demote a pointer
         * to a number.
         */
        case DBGCVAR_CAT_NUMBER_NO_RANGE:
            if (pVar->enmRangeType != DBGCVAR_RANGE_NONE)
                return VERR_PARSE_NO_RANGE_ALLOWED;
            /* fallthru */
        case DBGCVAR_CAT_NUMBER:
            switch (pVar->enmType)
            {
                case DBGCVAR_TYPE_NUMBER:
                    return VINF_SUCCESS;

                case DBGCVAR_TYPE_SYMBOL:
                case DBGCVAR_TYPE_STRING:
                {
                    DBGCVAR Var;
                    int rc = dbgcSymbolGet(pDbgc, pVar->u.pszString, DBGCVAR_TYPE_NUMBER, &Var);
                    if (VBOX_SUCCESS(rc))
                    {
                        *pVar = Var;
                        return rc;
                    }
                    break;
                }
                default:
                    break;
            }
            break;

        /*
         * Strings can easily be made from symbols (and of course strings).
         * We could consider reformatting the addresses and numbers into strings later...
         */
        case DBGCVAR_CAT_STRING:
            switch (pVar->enmType)
            {
                case DBGCVAR_TYPE_SYMBOL:
                    pVar->enmType = DBGCVAR_TYPE_STRING;
                    /* fallthru */
                case DBGCVAR_TYPE_STRING:
                    return VINF_SUCCESS;
                default:
                    break;
            }
            break;

        /*
         * Symol is pretty much the same thing as a string (at least until we actually implement it).
         */
        case DBGCVAR_CAT_SYMBOL:
            switch (pVar->enmType)
            {
                case DBGCVAR_TYPE_STRING:
                    pVar->enmType = DBGCVAR_TYPE_SYMBOL;
                    /* fallthru */
                case DBGCVAR_TYPE_SYMBOL:
                    return VINF_SUCCESS;
                default:
                    break;
            }
            break;

        /*
         * Anything else is illegal.
         */
        default:
            AssertMsgFailed(("enmCategory=%d\n", pVar->enmType));
            break;
    }

    return VERR_PARSE_NO_ARGUMENT_MATCH;
}


/**
 * Matches a set of variables with a description set.
 *
 * This is typically used for routine arguments before a call. The effects in
 * addition to the validation, is that some variables might be propagated to
 * other types in order to match the description. The following transformations
 * are supported:
 *      - String reinterpreted as a symbol and resolved to a number or pointer.
 *      - Number to a pointer.
 *      - Pointer to a number.
 * @returns 0 on success with paVars.
 * @returns VBox error code for match errors.
 */
static int dbgcEvalSubMatchVars(PDBGC pDbgc, unsigned cVarsMin, unsigned cVarsMax,
                                PCDBGCVARDESC paVarDescs, unsigned cVarDescs,
                                PDBGCVAR paVars, unsigned cVars)
{
    /*
     * Just do basic min / max checks first.
     */
    if (cVars < cVarsMin)
        return VERR_PARSE_TOO_FEW_ARGUMENTS;
    if (cVars > cVarsMax)
        return VERR_PARSE_TOO_MANY_ARGUMENTS;

    /*
     * Match the descriptors and actual variables.
     */
    PCDBGCVARDESC   pPrevDesc = NULL;
    unsigned        cCurDesc = 0;
    unsigned        iVar = 0;
    unsigned        iVarDesc = 0;
    while (iVar < cVars)
    {
        /* walk the descriptors */
        if (iVarDesc >= cVarDescs)
            return VERR_PARSE_TOO_MANY_ARGUMENTS;
        if (    (    paVarDescs[iVarDesc].fFlags & DBGCVD_FLAGS_DEP_PREV
                &&  &paVarDescs[iVarDesc - 1] != pPrevDesc)
            ||  cCurDesc >= paVarDescs[iVarDesc].cTimesMax)
        {
            iVarDesc++;
            if (iVarDesc >= cVarDescs)
                return VERR_PARSE_TOO_MANY_ARGUMENTS;
            cCurDesc = 0;
        }

        /*
         * Skip thru optional arguments until we find something which matches
         * or can easily be promoted to what the descriptor want.
         */
        for (;;)
        {
            int rc = dbgcEvalSubMatchVar(pDbgc, &paVars[iVar], &paVarDescs[iVarDesc]);
            if (VBOX_SUCCESS(rc))
            {
                paVars[iVar].pDesc = &paVarDescs[iVarDesc];
                cCurDesc++;
                break;
            }

            /* can we advance? */
            if (paVarDescs[iVarDesc].cTimesMin > cCurDesc)
                return VERR_PARSE_ARGUMENT_TYPE_MISMATCH;
            if (++iVarDesc >= cVarDescs)
                return VERR_PARSE_ARGUMENT_TYPE_MISMATCH;
            cCurDesc = 0;
        }

        /* next var */
        iVar++;
    }

    /*
     * Check that the rest of the descriptors are optional.
     */
    while (iVarDesc < cVarDescs)
    {
        if (paVarDescs[iVarDesc].cTimesMin > cCurDesc)
            return VERR_PARSE_TOO_FEW_ARGUMENTS;
        cCurDesc = 0;

        /* next */
        iVarDesc++;
    }

    return 0;
}


/**
 * Evaluates one argument with respect to unary operators.
 *
 * @returns 0 on success. pResult contains the result.
 * @returns VBox error code on parse or other evaluation error.
 *
 * @param   pDbgc       Debugger console instance data.
 * @param   pszExpr     The expression string.
 * @param   pResult     Where to store the result of the expression evaluation.
 */
static int dbgcEvalSubUnary(PDBGC pDbgc, char *pszExpr, size_t cchExpr, PDBGCVAR pResult)
{
    Log2(("dbgcEvalSubUnary: cchExpr=%d pszExpr=%s\n", cchExpr, pszExpr));

    /*
     * The state of the expression is now such that it will start by zero or more
     * unary operators and being followed by an expression of some kind.
     * The expression is either plain or in parenthesis.
     *
     * Being in a lazy, recursive mode today, the parsing is done as simple as possible. :-)
     * ASSUME: unary operators are all of equal precedence.
     */
    int         rc = 0;
    PCDBGCOP    pOp = dbgcOperatorLookup(pDbgc, pszExpr, false, ' ');
    if (pOp)
    {
        /* binary operators means syntax error. */
        if (pOp->fBinary)
            return VERR_PARSE_UNEXPECTED_OPERATOR;

        /*
         * If the next expression (the one following the unary operator) is in a
         * parenthesis a full eval is needed. If not the unary eval will suffice.
         */
        /* calc and strip next expr. */
        char *pszExpr2 = pszExpr + pOp->cchName;
        while (isblank(*pszExpr2))
            pszExpr2++;

        if (!*pszExpr2)
            rc = VERR_PARSE_EMPTY_ARGUMENT;
        else
        {
            DBGCVAR Arg;
            if (*pszExpr2 == '(')
                rc = dbgcEvalSub(pDbgc, pszExpr2, cchExpr - (pszExpr2 - pszExpr), &Arg);
            else
                rc = dbgcEvalSubUnary(pDbgc, pszExpr2, cchExpr - (pszExpr2 - pszExpr), &Arg);
            if (VBOX_SUCCESS(rc))
                rc = pOp->pfnHandlerUnary(pDbgc, &Arg, pResult);
        }
    }
    else
    {
        /*
         * Didn't find any operators, so it we have to check if this can be an
         * function call before assuming numeric or string expression.
         *
         * (ASSUMPTIONS:)
         * A function name only contains alphanumerical chars and it can not start
         * with a numerical character.
         * Immediately following the name is a parenthesis which must over
         * the remaining part of the expression.
         */
        bool    fExternal = *pszExpr == '.';
        char   *pszFun    = fExternal ? pszExpr + 1 : pszExpr;
        char   *pszFunEnd = NULL;
        if (pszExpr[cchExpr - 1] == ')' && isalpha(*pszFun))
        {
            pszFunEnd = pszExpr + 1;
            while (*pszFunEnd != '(' && isalnum(*pszFunEnd))
                pszFunEnd++;
            if (*pszFunEnd != '(')
                pszFunEnd = NULL;
        }

        if (pszFunEnd)
        {
            /*
             * Ok, it's a function call.
             */
            if (fExternal)
                pszExpr++, cchExpr--;
            PCDBGCCMD pFun = dbgcRoutineLookup(pDbgc, pszExpr, pszFunEnd - pszExpr, fExternal);
            if (!pFun)
                return VERR_PARSE_FUNCTION_NOT_FOUND;
            if (!pFun->pResultDesc)
                return VERR_PARSE_NOT_A_FUNCTION;

            /*
             * Parse the expression in parenthesis.
             */
            cchExpr -= pszFunEnd - pszExpr;
            pszExpr = pszFunEnd;
            /** @todo implement multiple arguments. */
            DBGCVAR     Arg;
            rc = dbgcEvalSub(pDbgc, pszExpr, cchExpr, &Arg);
            if (!rc)
            {
                rc = dbgcEvalSubMatchVars(pDbgc, pFun->cArgsMin, pFun->cArgsMax, pFun->paArgDescs, pFun->cArgDescs, &Arg, 1);
                if (!rc)
                    rc = pFun->pfnHandler(pFun, &pDbgc->CmdHlp, pDbgc->pVM, &Arg, 1, pResult);
            }
            else if (rc == VERR_PARSE_EMPTY_ARGUMENT && pFun->cArgsMin == 0)
                rc = pFun->pfnHandler(pFun, &pDbgc->CmdHlp, pDbgc->pVM, NULL, 0, pResult);
        }
        else
        {
            /*
             * Didn't find any operators, so it must be a plain expression.
             * This might be numeric or a string expression.
             */
            char ch  = pszExpr[0];
            char ch2 = pszExpr[1];
            if (ch == '0' && (ch2 == 'x' || ch2 == 'X'))
                rc = dbgcEvalSubNum(pszExpr + 2, 16, pResult);
            else if (ch == '0' && (ch2 == 'i' || ch2 == 'i'))
                rc = dbgcEvalSubNum(pszExpr + 2, 10, pResult);
            else if (ch == '0' && (ch2 == 't' || ch2 == 'T'))
                rc = dbgcEvalSubNum(pszExpr + 2, 8, pResult);
            /// @todo 0b doesn't work as a binary prefix, we confuse it with 0bf8:0123 and stuff.
            //else if (ch == '0' && (ch2 == 'b' || ch2 == 'b'))
            //    rc = dbgcEvalSubNum(pszExpr + 2, 2, pResult);
            else
            {
                /*
                 * Hexadecimal number or a string?
                 */
                char *psz = pszExpr;
                while (isxdigit(*psz))
                    psz++;
                if (!*psz)
                    rc = dbgcEvalSubNum(pszExpr, 16, pResult);
                else if ((*psz == 'h' || *psz == 'H') && !psz[1])
                {
                    *psz = '\0';
                    rc = dbgcEvalSubNum(pszExpr, 16, pResult);
                }
                else
                    rc = dbgcEvalSubString(pDbgc, pszExpr, cchExpr, pResult);
            }
        }
    }

    return rc;
}


/**
 * Evaluates one argument.
 *
 * @returns 0 on success. pResult contains the result.
 * @returns VBox error code on parse or other evaluation error.
 *
 * @param   pDbgc       Debugger console instance data.
 * @param   pszExpr     The expression string.
 * @param   pResult     Where to store the result of the expression evaluation.
 */
int dbgcEvalSub(PDBGC pDbgc, char *pszExpr, size_t cchExpr, PDBGCVAR pResult)
{
    Log2(("dbgcEvalSub: cchExpr=%d pszExpr=%s\n", cchExpr, pszExpr));
    /*
     * First we need to remove blanks in both ends.
     * ASSUMES: There is no quoting unless the entire expression is a string.
     */

    /* stripping. */
    while (cchExpr > 0 && isblank(pszExpr[cchExpr - 1]))
        pszExpr[--cchExpr] = '\0';
    while (isblank(*pszExpr))
        pszExpr++, cchExpr--;
    if (!*pszExpr)
        return VERR_PARSE_EMPTY_ARGUMENT;

    /* it there is any kind of quoting in the expression, it's string meat. */
    if (strpbrk(pszExpr, "\"'`"))
        return dbgcEvalSubString(pDbgc, pszExpr, cchExpr, pResult);

    /*
     * Check if there are any parenthesis which needs removing.
     */
    if (pszExpr[0] == '(' && pszExpr[cchExpr - 1] == ')')
    {
        do
        {
            unsigned cPar = 1;
            char    *psz = pszExpr + 1;
            char     ch;
            while ((ch = *psz) != '\0')
            {
                if (ch == '(')
                    cPar++;
                else if (ch == ')')
                {
                    if (cPar <= 0)
                        return VERR_PARSE_UNBALANCED_PARENTHESIS;
                    cPar--;
                    if (cPar == 0 && psz[1]) /* If not at end, there's nothing to do. */
                        break;
                }
                /* next */
                psz++;
            }
            if (ch)
                break;

            /* remove the parenthesis. */
            pszExpr++;
            cchExpr -= 2;
            pszExpr[cchExpr] = '\0';

            /* strip blanks. */
            while (cchExpr > 0 && isblank(pszExpr[cchExpr - 1]))
                pszExpr[--cchExpr] = '\0';
            while (isblank(*pszExpr))
                pszExpr++, cchExpr--;
            if (!*pszExpr)
                return VERR_PARSE_EMPTY_ARGUMENT;
        } while (pszExpr[0] == '(' && pszExpr[cchExpr - 1] == ')');
    }

    /* tabs to spaces. */
    char *psz = pszExpr;
    while ((psz = strchr(psz, '\t')) != NULL)
        *psz = ' ';

    /*
     * Now, we need to look for the binary operator with the lowest precedence.
     *
     * If there are no operators we're left with a simple expression which we
     * evaluate with respect to unary operators
     */
    char       *pszOpSplit = NULL;
    PCDBGCOP    pOpSplit = NULL;
    unsigned    cBinaryOps = 0;
    unsigned    cPar = 0;
    char        ch;
    char        chPrev = ' ';
    bool        fBinary = false;
    psz = pszExpr;

    while ((ch = *psz) != '\0')
    {
        //Log2(("ch=%c cPar=%d fBinary=%d\n", ch, cPar, fBinary));
        /*
         * Parenthesis.
         */
        if (ch == '(')
        {
            cPar++;
            fBinary = false;
        }
        else if (ch == ')')
        {
            if (cPar <= 0)
                return VERR_PARSE_UNBALANCED_PARENTHESIS;
            cPar--;
            fBinary = true;
        }
        /*
         * Potential operator.
         */
        else if (cPar == 0 && !isblank(ch))
        {
            PCDBGCOP pOp = dbgcIsOpChar(ch)
                         ? dbgcOperatorLookup(pDbgc, psz, fBinary, chPrev)
                         : NULL;
            if (pOp)
            {
                /* If not the right kind of operator we've got a syntax error. */
                if (pOp->fBinary != fBinary)
                    return VERR_PARSE_UNEXPECTED_OPERATOR;

                /*
                 * Update the parse state and skip the operator.
                 */
                if (!pOpSplit)
                {
                    pOpSplit = pOp;
                    pszOpSplit = psz;
                    cBinaryOps = fBinary;
                }
                else if (fBinary)
                {
                    cBinaryOps++;
                    if (pOp->iPrecedence >= pOpSplit->iPrecedence)
                    {
                        pOpSplit = pOp;
                        pszOpSplit = psz;
                    }
                }

                psz += pOp->cchName - 1;
                fBinary = false;
            }
            else
                fBinary = true;
        }

        /* next */
        psz++;
        chPrev = ch;
    } /* parse loop. */


    /*
     * Either we found an operator to divide the expression by
     * or we didn't find any. In the first case it's divide and
     * conquer. In the latter it's a single expression which
     * needs dealing with its unary operators if any.
     */
    int rc;
    if (    cBinaryOps
        &&  pOpSplit->fBinary)
    {
        /* process 1st sub expression. */
        *pszOpSplit = '\0';
        DBGCVAR     Arg1;
        rc = dbgcEvalSub(pDbgc, pszExpr, pszOpSplit - pszExpr, &Arg1);
        if (VBOX_SUCCESS(rc))
        {
            /* process 2nd sub expression. */
            char       *psz2 = pszOpSplit + pOpSplit->cchName;
            DBGCVAR     Arg2;
            rc = dbgcEvalSub(pDbgc, psz2, cchExpr - (psz2 - pszExpr), &Arg2);
            if (VBOX_SUCCESS(rc))
                /* apply the operator. */
                rc = pOpSplit->pfnHandlerBinary(pDbgc, &Arg1, &Arg2, pResult);
        }
    }
    else if (cBinaryOps)
    {
        /* process sub expression. */
        pszOpSplit += pOpSplit->cchName;
        DBGCVAR     Arg;
        rc = dbgcEvalSub(pDbgc, pszOpSplit, cchExpr - (pszOpSplit - pszExpr), &Arg);
        if (VBOX_SUCCESS(rc))
            /* apply the operator. */
            rc = pOpSplit->pfnHandlerUnary(pDbgc, &Arg, pResult);
    }
    else
        /* plain expression or using unary operators perhaps with paratheses. */
        rc = dbgcEvalSubUnary(pDbgc, pszExpr, cchExpr, pResult);

    return rc;
}


/**
 * Parses the arguments of one command.
 *
 * @returns 0 on success.
 * @returns VBox error code on parse error with *pcArg containing the argument causing trouble.
 * @param   pDbgc       Debugger console instance data.
 * @param   pCmd        Pointer to the command descriptor.
 * @param   pszArg      Pointer to the arguments to parse.
 * @param   paArgs      Where to store the parsed arguments.
 * @param   cArgs       Size of the paArgs array.
 * @param   pcArgs      Where to store the number of arguments.
 *                      In the event of an error this is used to store the index of the offending argument.
 */
static int dbgcProcessArguments(PDBGC pDbgc, PCDBGCCMD pCmd, char *pszArgs, PDBGCVAR paArgs, unsigned cArgs, unsigned *pcArgs)
{
    Log2(("dbgcProcessArguments: pCmd=%s pszArgs='%s'\n", pCmd->pszCmd, pszArgs));
    /*
     * Check if we have any argument and if the command takes any.
     */
    *pcArgs = 0;
    /* strip leading blanks. */
    while (*pszArgs && isblank(*pszArgs))
        pszArgs++;
    if (!*pszArgs)
    {
        if (!pCmd->cArgsMin)
            return 0;
        return VERR_PARSE_TOO_FEW_ARGUMENTS;
    }
    /** @todo fixme - foo() doesn't work. */
    if (!pCmd->cArgsMax)
        return VERR_PARSE_TOO_MANY_ARGUMENTS;

    /*
     * This is a hack, it's "temporary" and should go away "when" the parser is
     * modified to match arguments while parsing.
     */
    if (    pCmd->cArgsMax == 1
        &&  pCmd->cArgsMin == 1
        &&  pCmd->cArgDescs == 1
        &&  pCmd->paArgDescs[0].enmCategory == DBGCVAR_CAT_STRING
        &&  cArgs >= 1)
    {
        *pcArgs = 1;
        RTStrStripR(pszArgs);
        return dbgcEvalSubString(pDbgc, pszArgs, strlen(pszArgs), &paArgs[0]);
    }


    /*
     * The parse loop.
     */
    PDBGCVAR        pArg0 = &paArgs[0];
    PDBGCVAR        pArg = pArg0;
    *pcArgs = 0;
    do
    {
        /*
         * Can we have another argument?
         */
        if (*pcArgs >= pCmd->cArgsMax)
            return VERR_PARSE_TOO_MANY_ARGUMENTS;
        if (pArg >= &paArgs[cArgs])
            return VERR_PARSE_ARGUMENT_OVERFLOW;

        /*
         * Find the end of the argument.
         */
        int     cPar    = 0;
        char    chQuote = '\0';
        char   *pszEnd  = NULL;
        char   *psz     = pszArgs;
        char    ch;
        bool    fBinary = false;
        for (;;)
        {
            /*
             * Check for the end.
             */
            if ((ch = *psz) == '\0')
            {
                if (chQuote)
                    return VERR_PARSE_UNBALANCED_QUOTE;
                if (cPar)
                    return VERR_PARSE_UNBALANCED_PARENTHESIS;
                pszEnd = psz;
                break;
            }
            /*
             * When quoted we ignore everything but the quotation char.
             * We use the REXX way of escaping the quotation char, i.e. double occurence.
             */
            else if (ch == '\'' || ch == '"' || ch == '`')
            {
                if (chQuote)
                {
                    /* end quote? */
                    if (ch == chQuote)
                    {
                        if (psz[1] == ch)
                            psz++;          /* skip the escaped quote char */
                        else
                            chQuote = '\0'; /* end of quoted string. */
                    }
                }
                else
                    chQuote = ch;           /* open new quote */
            }
            /*
             * Parenthesis can of course be nested.
             */
            else if (ch == '(')
            {
                cPar++;
                fBinary = false;
            }
            else if (ch == ')')
            {
                if (!cPar)
                    return VERR_PARSE_UNBALANCED_PARENTHESIS;
                cPar--;
                fBinary = true;
            }
            else if (!chQuote && !cPar)
            {
                /*
                 * Encountering blanks may mean the end of it all. A binary operator
                 * will force continued parsing.
                 */
                if (isblank(*psz))
                {
                    pszEnd = psz++;         /* just in case. */
                    while (isblank(*psz))
                        psz++;
                    PCDBGCOP pOp = dbgcOperatorLookup(pDbgc, psz, fBinary, ' ');
                    if (!pOp || pOp->fBinary != fBinary)
                        break;              /* the end. */
                    psz += pOp->cchName;
                    while (isblank(*psz))   /* skip blanks so we don't get here again */
                        psz++;
                    fBinary = false;
                    continue;
                }

                /*
                 * Look for operators without a space up front.
                 */
                if (dbgcIsOpChar(*psz))
                {
                    PCDBGCOP pOp = dbgcOperatorLookup(pDbgc, psz, fBinary, ' ');
                    if (pOp)
                    {
                        if (pOp->fBinary != fBinary)
                        {
                            pszEnd = psz;
                            /** @todo this is a parsing error really. */
                            break;              /* the end. */
                        }
                        psz += pOp->cchName;
                        while (isblank(*psz))   /* skip blanks so we don't get here again */
                            psz++;
                        fBinary = false;
                        continue;
                    }
                }
                fBinary = true;
            }

            /* next char */
            psz++;
        }
        *pszEnd = '\0';
        /* (psz = next char to process) */

        /*
         * Parse and evaluate the argument.
         */
        int rc = dbgcEvalSub(pDbgc, pszArgs, strlen(pszArgs), pArg);
        if (VBOX_FAILURE(rc))
            return rc;

        /*
         * Next.
         */
        pArg++;
        (*pcArgs)++;
        pszArgs = psz;
        while (*pszArgs && isblank(*pszArgs))
            pszArgs++;
    } while (*pszArgs);

    /*
     * Match the arguments.
     */
    return dbgcEvalSubMatchVars(pDbgc, pCmd->cArgsMin, pCmd->cArgsMax, pCmd->paArgDescs, pCmd->cArgDescs, pArg0, pArg - pArg0);
}


/**
 * Process one command.
 *
 * @returns VBox status code. Any error indicates the termination of the console session.
 * @param   pDbgc   Debugger console instance data.
 * @param   pszCmd  Pointer to the command.
 * @param   cchCmd  Length of the command.
 */
int dbgcProcessCommand(PDBGC pDbgc, char *pszCmd, size_t cchCmd)
{
    char *pszCmdInput = pszCmd;

    /*
     * Skip blanks.
     */
    while (isblank(*pszCmd))
        pszCmd++, cchCmd--;

    /* external command? */
    bool fExternal = *pszCmd == '.';
    if (fExternal)
        pszCmd++, cchCmd--;

    /*
     * Find arguments.
     */
    char *pszArgs = pszCmd;
    while (isalnum(*pszArgs))
        pszArgs++;
    if (*pszArgs && (!isblank(*pszArgs) || pszArgs == pszCmd))
    {
        pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "Syntax error in command '%s'!\n", pszCmdInput);
        return 0;
    }

    /*
     * Find the command.
     */
    PCDBGCCMD pCmd = dbgcRoutineLookup(pDbgc, pszCmd, pszArgs - pszCmd, fExternal);
    if (!pCmd || (pCmd->fFlags & DBGCCMD_FLAGS_FUNCTION))
        return pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "Unknown command '%s'!\n", pszCmdInput);

    /*
     * Parse arguments (if any).
     */
    unsigned    cArgs;
    int rc = dbgcProcessArguments(pDbgc, pCmd, pszArgs, &pDbgc->aArgs[pDbgc->iArg], ELEMENTS(pDbgc->aArgs) - pDbgc->iArg, &cArgs);

    /*
     * Execute the command.
     */
    if (!rc)
    {
        rc = pCmd->pfnHandler(pCmd, &pDbgc->CmdHlp, pDbgc->pVM, &pDbgc->aArgs[0], cArgs, NULL);
    }
    else
    {
        /* report parse / eval error. */
        switch (rc)
        {
            case VERR_PARSE_TOO_FEW_ARGUMENTS:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Syntax error: Too few arguments. Minimum is %d for command '%s'.\n", pCmd->cArgsMin, pCmd->pszCmd);
                break;
            case VERR_PARSE_TOO_MANY_ARGUMENTS:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Syntax error: Too many arguments. Maximum is %d for command '%s'.\n", pCmd->cArgsMax, pCmd->pszCmd);
                break;
            case VERR_PARSE_ARGUMENT_OVERFLOW:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Syntax error: Too many arguments.\n");
                break;
            case VERR_PARSE_UNBALANCED_QUOTE:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Syntax error: Unbalanced quote (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_UNBALANCED_PARENTHESIS:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Syntax error: Unbalanced parenthesis (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_EMPTY_ARGUMENT:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Syntax error: An argument or subargument contains nothing useful (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_UNEXPECTED_OPERATOR:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Syntax error: Invalid operator usage (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_INVALID_NUMBER:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Syntax error: Ivalid numeric value (argument %d). If a string was the intention, then quote it.\n", cArgs);
                break;
            case VERR_PARSE_NUMBER_TOO_BIG:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: Numeric overflow (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_INVALID_OPERATION:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: Invalid operation attempted (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_FUNCTION_NOT_FOUND:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: Function not found (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_NOT_A_FUNCTION:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: The function specified is not a function (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_NO_MEMORY:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: Out memory in the regular heap! Expect odd stuff to happen...\n", cArgs);
                break;
            case VERR_PARSE_INCORRECT_ARG_TYPE:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: Incorrect argument type (argument %d?).\n", cArgs);
                break;
            case VERR_PARSE_VARIABLE_NOT_FOUND:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: An undefined variable was referenced (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_CONVERSION_FAILED:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: A conversion between two types failed (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_NOT_IMPLEMENTED:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: You hit a debugger feature which isn't implemented yet (argument %d).\n", cArgs);
                break;
            case VERR_PARSE_BAD_RESULT_TYPE:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: Couldn't satisfy a request for a specific result type (argument %d). (Usually applies to symbols)\n", cArgs);
                break;
            case VERR_PARSE_WRITEONLY_SYMBOL:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: Cannot get symbol, it's set only (argument %d).\n", cArgs);
                break;

            default:
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                    "Error: Unknown error %d!\n", rc);
                return rc;
        }

        /*
         * Parse errors are non fatal.
         */
        if (rc >= VERR_PARSE_FIRST && rc < VERR_PARSE_LAST)
            rc = 0;
    }

    return rc;
}


/**
 * Process all commands current in the buffer.
 *
 * @returns VBox status code. Any error indicates the termination of the console session.
 * @param   pDbgc   Debugger console instance data.
 */
static int dbgcProcessCommands(PDBGC pDbgc)
{
    int rc = 0;
    while (pDbgc->cInputLines)
    {
        /*
         * Empty the log buffer if we're hooking the log.
         */
        if (pDbgc->fLog)
        {
            rc = dbgcProcessLog(pDbgc);
            if (VBOX_FAILURE(rc))
                break;
        }

        if (pDbgc->iRead == pDbgc->iWrite)
        {
            AssertMsgFailed(("The input buffer is empty while cInputLines=%d!\n", pDbgc->cInputLines));
            pDbgc->cInputLines = 0;
            return 0;
        }

        /*
         * Copy the command to the parse buffer.
         */
        char    ch;
        char   *psz = &pDbgc->achInput[pDbgc->iRead];
        char   *pszTrg = &pDbgc->achScratch[0];
        while ((*pszTrg = ch = *psz++) != ';' && ch != '\n' )
        {
            if (psz == &pDbgc->achInput[sizeof(pDbgc->achInput)])
                psz = &pDbgc->achInput[0];

            if (psz == &pDbgc->achInput[pDbgc->iWrite])
            {
                AssertMsgFailed(("The buffer contains no commands while cInputLines=%d!\n", pDbgc->cInputLines));
                pDbgc->cInputLines = 0;
                return 0;
            }

            pszTrg++;
        }
        *pszTrg = '\0';

        /*
         * Advance the buffer.
         */
        pDbgc->iRead = psz - &pDbgc->achInput[0];
        if (ch == '\n')
            pDbgc->cInputLines--;

        /*
         * Parse and execute this command.
         */
        pDbgc->pszScratch = psz;
        pDbgc->iArg       = 0;
        rc = dbgcProcessCommand(pDbgc, &pDbgc->achScratch[0], psz - &pDbgc->achScratch[0] - 1);
        if (rc)
            break;
    }

    return rc;
}


/**
 * Reads input, parses it and executes commands on '\n'.
 *
 * @returns VBox status.
 * @param   pDbgc   Debugger console instance data.
 */
static int dbgcProcessInput(PDBGC pDbgc)
{
    /*
     * We know there's input ready, so let's read it first.
     */
    int rc = dbgcInputRead(pDbgc);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Now execute any ready commands.
     */
    if (pDbgc->cInputLines)
    {
        /** @todo this fReady stuff is broken. */
        pDbgc->fReady = false;
        rc = dbgcProcessCommands(pDbgc);
        if (VBOX_SUCCESS(rc) && rc != VWRN_DBGC_CMD_PENDING)
            pDbgc->fReady = true;
        if (    VBOX_SUCCESS(rc)
            &&  pDbgc->iRead == pDbgc->iWrite
            &&  pDbgc->fReady)
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "VBoxDbg> ");
    }

    return rc;
}


/**
 * Gets the event context identifier string.
 * @returns Read only string.
 * @param   enmCtx          The context.
 */
static const char *dbgcGetEventCtx(DBGFEVENTCTX enmCtx)
{
    switch (enmCtx)
    {
        case DBGFEVENTCTX_RAW:      return "raw";
        case DBGFEVENTCTX_REM:      return "rem";
        case DBGFEVENTCTX_HWACCL:   return "hwaccl";
        case DBGFEVENTCTX_HYPER:    return "hyper";
        case DBGFEVENTCTX_OTHER:    return "other";

        case DBGFEVENTCTX_INVALID:  return "!Invalid Event Ctx!";
        default:
            AssertMsgFailed(("enmCtx=%d\n", enmCtx));
            return "!Unknown Event Ctx!";
    }
}


/**
 * Processes debugger events.
 *
 * @returns VBox status.
 * @param   pDbgc   DBGC Instance data.
 * @param   pEvent  Pointer to event data.
 */
static int dbgcProcessEvent(PDBGC pDbgc, PCDBGFEVENT pEvent)
{
    /*
     * Flush log first.
     */
    if (pDbgc->fLog)
    {
        int rc = dbgcProcessLog(pDbgc);
        if (VBOX_FAILURE(rc))
            return rc;
    }

    /*
     * Process the event.
     */
    pDbgc->pszScratch = &pDbgc->achInput[0];
    pDbgc->iArg       = 0;
    bool fPrintPrompt = true;
    int rc = VINF_SUCCESS;
    switch (pEvent->enmType)
    {
        /*
         * The first part is events we have initiated with commands.
         */
        case DBGFEVENT_HALT_DONE:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: VM %p is halted! (%s)\n",
                                         pDbgc->pVM, dbgcGetEventCtx(pEvent->enmCtx));
            pDbgc->fRegCtxGuest = true; /* we're always in guest context when halted. */
            if (VBOX_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }


        /*
         * The second part is events which can occur at any time.
         */
        case DBGFEVENT_FATAL_ERROR:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbf event: Fatal error! (%s)\n",
                                         dbgcGetEventCtx(pEvent->enmCtx));
            pDbgc->fRegCtxGuest = false; /* fatal errors are always in hypervisor. */
            if (VBOX_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }

        case DBGFEVENT_BREAKPOINT:
        case DBGFEVENT_BREAKPOINT_HYPER:
        {
            bool fRegCtxGuest = pDbgc->fRegCtxGuest;
            pDbgc->fRegCtxGuest = pEvent->enmType == DBGFEVENT_BREAKPOINT;

            rc = dbgcBpExec(pDbgc, pEvent->u.Bp.iBp);
            switch (rc)
            {
                case VERR_DBGC_BP_NOT_FOUND:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Unknown breakpoint %u! (%s)\n",
                                                 pEvent->u.Bp.iBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                case VINF_DBGC_BP_NO_COMMAND:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Breakpoint %u! (%s)\n",
                                                 pEvent->u.Bp.iBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                case VINF_BUFFER_OVERFLOW:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Breakpoint %u! Command too long to execute! (%s)\n",
                                                 pEvent->u.Bp.iBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                default:
                    break;
            }
            if (VBOX_SUCCESS(rc) && DBGFR3IsHalted(pDbgc->pVM))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            else
                pDbgc->fRegCtxGuest = fRegCtxGuest;
            break;
        }

        case DBGFEVENT_STEPPED:
        case DBGFEVENT_STEPPED_HYPER:
        {
            pDbgc->fRegCtxGuest = pEvent->enmType == DBGFEVENT_STEPPED;

            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event: Single step! (%s)\n", dbgcGetEventCtx(pEvent->enmCtx));
            if (VBOX_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }

        case DBGFEVENT_ASSERTION_HYPER:
        {
            pDbgc->fRegCtxGuest = false;

            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                         "\ndbgf event: Hypervisor Assertion! (%s)\n"
                                         "%s"
                                         "%s"
                                         "\n",
                                         dbgcGetEventCtx(pEvent->enmCtx),
                                         pEvent->u.Assert.pszMsg1,
                                         pEvent->u.Assert.pszMsg2);
            if (VBOX_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }

        case DBGFEVENT_DEV_STOP:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                         "\n"
                                         "dbgf event: DBGFSTOP (%s)\n"
                                         "File:     %s\n"
                                         "Line:     %d\n"
                                         "Function: %s\n",
                                         dbgcGetEventCtx(pEvent->enmCtx),
                                         pEvent->u.Src.pszFile,
                                         pEvent->u.Src.uLine,
                                         pEvent->u.Src.pszFunction);
            if (VBOX_SUCCESS(rc) && pEvent->u.Src.pszMessage && *pEvent->u.Src.pszMessage)
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                         "Message:  %s\n",
                                             pEvent->u.Src.pszMessage);
            if (VBOX_SUCCESS(rc))
                rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r");
            break;
        }


        case DBGFEVENT_INVALID_COMMAND:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf/dbgc error: Invalid command event!\n");
            fPrintPrompt = !pDbgc->fReady;
            break;
        }

        case DBGFEVENT_TERMINATING:
        {
            pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\nVM is terminating!\n");
            rc = VERR_GENERAL_FAILURE;
            break;
        }


        default:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf/dbgc error: Unknown event %d!\n", pEvent->enmType);
            fPrintPrompt = !pDbgc->fReady;
            break;
        }
    }

    /*
     * Prompt, anyone?
     */
    if (fPrintPrompt && VBOX_SUCCESS(rc))
    {
        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "VBoxDbg> ");
    }

    return rc;
}





/**
 * Make a console instance.
 *
 * This will not return until either an 'exit' command is issued or a error code
 * indicating connection loss is encountered.
 *
 * @returns VINF_SUCCESS if console termination caused by the 'exit' command.
 * @returns The VBox status code causing the console termination.
 *
 * @param   pVM         VM Handle.
 * @param   pBack       Pointer to the backend structure. This must contain
 *                      a full set of function pointers to service the console.
 * @param   fFlags      Reserved, must be zero.
 * @remark  A forced termination of the console is easiest done by forcing the
 *          callbacks to return fatal failures.
 */
DBGDECL(int)    DBGCCreate(PVM pVM, PDBGCBACK pBack, unsigned fFlags)
{
    /*
     * Validate input.
     */
    AssertReturn(VALID_PTR(pVM), VERR_INVALID_PARAMETER);
    AssertReturn(VALID_PTR(pBack), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!fFlags, ("%#x", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Allocate and initialize instance data
     */
    PDBGC   pDbgc = (PDBGC)RTMemAllocZ(sizeof(*pDbgc));
    if (!pDbgc)
        return VERR_NO_MEMORY;

    dbgcInitCmdHlp(pDbgc);
    pDbgc->pBack            = pBack;
    pDbgc->pVM              = NULL;
    pDbgc->pszEmulation     = "CodeView/WinDbg";
    pDbgc->paEmulationCmds  = &g_aCmdsCodeView[0];
    pDbgc->cEmulationCmds   = g_cCmdsCodeView;
    //pDbgc->fLog             = false;
    pDbgc->fRegCtxGuest     = true;
    pDbgc->fRegTerse        = true;
    //pDbgc->DisasmPos        = {0};
    //pDbgc->SourcePos        = {0};
    //pDbgc->DumpPos          = {0};
    //pDbgc->cbDumpElement    = 0;
    //pDbgc->cVars            = 0;
    //pDbgc->paVars           = NULL;
    //pDbgc->pFirstBp         = NULL;
    //pDbgc->uInputZero       = 0;
    //pDbgc->iRead            = 0;
    //pDbgc->iWrite           = 0;
    //pDbgc->cInputLines      = 0;
    //pDbgc->fInputOverflow   = false;
    pDbgc->fReady           = true;
    pDbgc->pszScratch       = &pDbgc->achScratch[0];
    //pDbgc->iArg             = 0;
    //pDbgc->rcOutput         = 0;

    dbgcInitOpCharBitMap();

    /*
     * Print welcome message.
     */
    int rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
        "Welcome to the VirtualBox Debugger!\n");
    if (VBOX_FAILURE(rc))
        goto l_failure;

    /*
     * Attach to the VM.
     */
    rc = DBGFR3Attach(pVM);
    if (VBOX_FAILURE(rc))
    {
        rc = pDbgc->CmdHlp.pfnVBoxError(&pDbgc->CmdHlp, rc, "When trying to attach to VM %p\n", pDbgc->pVM);
        goto l_failure;
    }
    pDbgc->pVM = pVM;

    /*
     * Print commandline and auto select result.
     */
    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
        "Current VM is %08x\n" /** @todo get and print the VM name! */
        "VBoxDbg> ",
        pDbgc->pVM);
    if (VBOX_FAILURE(rc))
        goto l_failure;

    /*
     * Main Debugger Loop.
     *
     * This loop will either block on waiting for input or on waiting on
     * debug events. If we're forwarding the log we cannot wait for long
     * before we must flush the log.
     */
    for (rc = 0;;)
    {
        if (pDbgc->pVM && DBGFR3CanWait(pDbgc->pVM))
        {
            /*
             * Wait for a debug event.
             */
            PCDBGFEVENT pEvent;
            rc = DBGFR3EventWait(pDbgc->pVM, pDbgc->fLog ? 1 : 32, &pEvent);
            if (VBOX_SUCCESS(rc))
            {
                rc = dbgcProcessEvent(pDbgc, pEvent);
                if (VBOX_FAILURE(rc))
                    break;
            }
            else if (rc != VERR_TIMEOUT)
                break;

            /*
             * Check for input.
             */
            if (pBack->pfnInput(pDbgc->pBack, 0))
            {
                rc = dbgcProcessInput(pDbgc);
                if (VBOX_FAILURE(rc))
                    break;
            }
        }
        else
        {
            /*
             * Wait for input. If Logging is enabled we'll only wait very briefly.
             */
            if (pBack->pfnInput(pDbgc->pBack, pDbgc->fLog ? 1 : 1000))
            {
                rc = dbgcProcessInput(pDbgc);
                if (VBOX_FAILURE(rc))
                    break;
            }
        }

        /*
         * Forward log output.
         */
        if (pDbgc->fLog)
        {
            rc = dbgcProcessLog(pDbgc);
            if (VBOX_FAILURE(rc))
                break;
        }
    }


l_failure:
    /*
     * Cleanup console debugger session.
     */
    /* Disable log hook. */
    if (pDbgc->fLog)
    {

    }

    /* Detach from the VM. */
    if (pDbgc->pVM)
        DBGFR3Detach(pDbgc->pVM);

    /* finally, free the instance memory. */
    RTMemFree(pDbgc);

    return rc;
}

