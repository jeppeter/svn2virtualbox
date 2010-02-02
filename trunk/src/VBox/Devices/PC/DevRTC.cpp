/* $Id$ */
/** @file
 * Motorola MC146818 RTC/CMOS Device.
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * QEMU MC146818 RTC emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_RTC
#include <VBox/pdmdev.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#include "../Builtins.h"

struct RTCState;
typedef struct RTCState RTCState;

#define RTC_CRC_START   0x10
#define RTC_CRC_LAST    0x2d
#define RTC_CRC_HIGH    0x2e
#define RTC_CRC_LOW     0x2f


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
#ifndef VBOX_DEVICE_STRUCT_TESTCASE
RT_C_DECLS_BEGIN
PDMBOTHCBDECL(int) rtcIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t *pu32, unsigned cb);
PDMBOTHCBDECL(int) rtcIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t u32, unsigned cb);
PDMBOTHCBDECL(void) rtcTimerPeriodic(PPDMDEVINS pDevIns, PTMTIMER pTimer, void *pvUser);
PDMBOTHCBDECL(void) rtcTimerSecond(PPDMDEVINS pDevIns, PTMTIMER pTimer, void *pvUser);
PDMBOTHCBDECL(void) rtcTimerSecond2(PPDMDEVINS pDevIns, PTMTIMER pTimer, void *pvUser);
RT_C_DECLS_END
#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/*#define DEBUG_CMOS*/

#define RTC_SECONDS             0
#define RTC_SECONDS_ALARM       1
#define RTC_MINUTES             2
#define RTC_MINUTES_ALARM       3
#define RTC_HOURS               4
#define RTC_HOURS_ALARM         5
#define RTC_ALARM_DONT_CARE    0xC0

#define RTC_DAY_OF_WEEK         6
#define RTC_DAY_OF_MONTH        7
#define RTC_MONTH               8
#define RTC_YEAR                9

#define RTC_REG_A               10
#define RTC_REG_B               11
#define RTC_REG_C               12
#define RTC_REG_D               13

#define REG_A_UIP 0x80

#define REG_B_SET 0x80
#define REG_B_PIE 0x40
#define REG_B_AIE 0x20
#define REG_B_UIE 0x10


/** The saved state version. */
#define RTC_SAVED_STATE_VERSION             2
/** The saved state version used by VirtualBox 3.0 and earlier.
 * This does not include the configuration.  */
#define RTC_SAVED_STATE_VERSION_VBOX_30     1


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/** @todo Replace struct my_tm with RTTIME. */
struct my_tm
{
    int32_t tm_sec;
    int32_t tm_min;
    int32_t tm_hour;
    int32_t tm_mday;
    int32_t tm_mon;
    int32_t tm_year;
    int32_t tm_wday;
    int32_t tm_yday;
};


struct RTCState {
    uint8_t cmos_data[128];
    uint8_t cmos_index;
    uint8_t Alignment0[7];
    struct my_tm current_tm;
    /** The configured IRQ. */
    int32_t irq;
    /** The configured I/O port base. */
    RTIOPORT IOPortBase;
    /** Use UTC or local time initially. */
    bool fUTC;
    /* periodic timer */
    int64_t next_periodic_time;
    /* second update */
    int64_t next_second_time;

    /** Pointer to the device instance - R3 Ptr. */
    PPDMDEVINSR3 pDevInsR3;
    /** The periodic timer (rtcTimerPeriodic) - R3 Ptr. */
    PTMTIMERR3 pPeriodicTimerR3;
    /** The second timer (rtcTimerSecond) - R3 Ptr. */
    PTMTIMERR3 pSecondTimerR3;
    /** The second second timer (rtcTimerSecond2) - R3 Ptr. */
    PTMTIMERR3 pSecondTimer2R3;

    /** Pointer to the device instance - R0 Ptr. */
    PPDMDEVINSR0 pDevInsR0;
    /** The periodic timer (rtcTimerPeriodic) - R0 Ptr. */
    PTMTIMERR0 pPeriodicTimerR0;
    /** The second timer (rtcTimerSecond) - R0 Ptr. */
    PTMTIMERR0 pSecondTimerR0;
    /** The second second timer (rtcTimerSecond2) - R0 Ptr. */
    PTMTIMERR0 pSecondTimer2R0;

    /** Pointer to the device instance - RC Ptr. */
    PPDMDEVINSRC pDevInsRC;
    /** The periodic timer (rtcTimerPeriodic) - RC Ptr. */
    PTMTIMERRC pPeriodicTimerRC;
    /** The second timer (rtcTimerSecond) - RC Ptr. */
    PTMTIMERRC pSecondTimerRC;
    /** The second second timer (rtcTimerSecond2) - RC Ptr. */
    PTMTIMERRC pSecondTimer2RC;

    /** The RTC registration structure. */
    PDMRTCREG RtcReg;
    /** The RTC device helpers. */
    R3PTRTYPE(PCPDMRTCHLP) pRtcHlpR3;
    /** Number of release log entries. Used to prevent flooding. */
    uint32_t cRelLogEntries;
    /** The current/previous timer period. Used to prevent flooding changes. */
    int32_t CurPeriod;
};

#ifndef VBOX_DEVICE_STRUCT_TESTCASE
static void rtc_set_time(RTCState *s);
static void rtc_copy_date(RTCState *s);

static void rtc_timer_update(RTCState *s, int64_t current_time)
{
    int period_code, period;
    uint64_t cur_clock, next_irq_clock;
    uint32_t freq;

    period_code = s->cmos_data[RTC_REG_A] & 0x0f;
    if (period_code != 0 &&
        (s->cmos_data[RTC_REG_B] & REG_B_PIE)) {
        if (period_code <= 2)
            period_code += 7;
        /* period in 32 kHz cycles */
        period = 1 << (period_code - 1);
        /* compute 32 kHz clock */
        freq = TMTimerGetFreq(s->CTX_SUFF(pPeriodicTimer));

        cur_clock = ASMMultU64ByU32DivByU32(current_time, 32768, freq);
        next_irq_clock = (cur_clock & ~(uint64_t)(period - 1)) + period;
        s->next_periodic_time = ASMMultU64ByU32DivByU32(next_irq_clock, freq, 32768) + 1;
        TMTimerSet(s->CTX_SUFF(pPeriodicTimer), s->next_periodic_time);

        if (period != s->CurPeriod)
        {
            if (s->cRelLogEntries++ < 64)
                LogRel(("RTC: period=%#x (%d) %u Hz\n", period, period, _32K / period));
            s->CurPeriod = period;
        }
    } else {
        if (TMTimerIsActive(s->CTX_SUFF(pPeriodicTimer)) && s->cRelLogEntries++ < 64)
            LogRel(("RTC: stopped the periodic timer\n"));
        TMTimerStop(s->CTX_SUFF(pPeriodicTimer));
    }
}

static void rtc_periodic_timer(void *opaque)
{
    RTCState *s = (RTCState*)opaque;

    rtc_timer_update(s, s->next_periodic_time);
    s->cmos_data[RTC_REG_C] |= 0xc0;
    PDMDevHlpISASetIrq(s->CTX_SUFF(pDevIns), s->irq, 1);
}

static void cmos_ioport_write(void *opaque, uint32_t addr, uint32_t data)
{
    RTCState *s = (RTCState*)opaque;

    if ((addr & 1) == 0) {
        s->cmos_index = data & 0x7f;
    } else {
        Log(("CMOS: Write idx %#04x: %#04x (old %#04x)\n", s->cmos_index, data, s->cmos_data[s->cmos_index]));
        switch(s->cmos_index) {
        case RTC_SECONDS_ALARM:
        case RTC_MINUTES_ALARM:
        case RTC_HOURS_ALARM:
            s->cmos_data[s->cmos_index] = data;
            break;
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
            s->cmos_data[s->cmos_index] = data;
            /* if in set mode, do not update the time */
            if (!(s->cmos_data[RTC_REG_B] & REG_B_SET)) {
                rtc_set_time(s);
            }
            break;
        case RTC_REG_A:
            /* UIP bit is read only */
            s->cmos_data[RTC_REG_A] = (data & ~REG_A_UIP) |
                (s->cmos_data[RTC_REG_A] & REG_A_UIP);
            rtc_timer_update(s, TMTimerGet(s->CTX_SUFF(pPeriodicTimer)));
            break;
        case RTC_REG_B:
            if (data & REG_B_SET) {
                /* set mode: reset UIP mode */
                s->cmos_data[RTC_REG_A] &= ~REG_A_UIP;
#if 0 /* This is probably wrong as it breaks changing the time/date in OS/2. */
                data &= ~REG_B_UIE;
#endif
            } else {
                /* if disabling set mode, update the time */
                if (s->cmos_data[RTC_REG_B] & REG_B_SET) {
                    rtc_set_time(s);
                }
            }
            s->cmos_data[RTC_REG_B] = data;
            rtc_timer_update(s, TMTimerGet(s->CTX_SUFF(pPeriodicTimer)));
            break;
        case RTC_REG_C:
        case RTC_REG_D:
            /* cannot write to them */
            break;
        default:
            s->cmos_data[s->cmos_index] = data;
            break;
        }
    }
}

static inline int to_bcd(RTCState *s, int a)
{
    if (s->cmos_data[RTC_REG_B] & 0x04) {
        return a;
    } else {
        return ((a / 10) << 4) | (a % 10);
    }
}

static inline int from_bcd(RTCState *s, int a)
{
    if (s->cmos_data[RTC_REG_B] & 0x04) {
        return a;
    } else {
        return ((a >> 4) * 10) + (a & 0x0f);
    }
}

static void rtc_set_time(RTCState *s)
{
    struct my_tm *tm = &s->current_tm;

    tm->tm_sec = from_bcd(s, s->cmos_data[RTC_SECONDS]);
    tm->tm_min = from_bcd(s, s->cmos_data[RTC_MINUTES]);
    tm->tm_hour = from_bcd(s, s->cmos_data[RTC_HOURS] & 0x7f);
    if (!(s->cmos_data[RTC_REG_B] & 0x02) &&
        (s->cmos_data[RTC_HOURS] & 0x80)) {
        tm->tm_hour += 12;
    }
    tm->tm_wday = from_bcd(s, s->cmos_data[RTC_DAY_OF_WEEK]);
    tm->tm_mday = from_bcd(s, s->cmos_data[RTC_DAY_OF_MONTH]);
    tm->tm_mon = from_bcd(s, s->cmos_data[RTC_MONTH]) - 1;
    tm->tm_year = from_bcd(s, s->cmos_data[RTC_YEAR]) + 100;
}

static void rtc_copy_date(RTCState *s)
{
    const struct my_tm *tm = &s->current_tm;

    s->cmos_data[RTC_SECONDS] = to_bcd(s, tm->tm_sec);
    s->cmos_data[RTC_MINUTES] = to_bcd(s, tm->tm_min);
    if (s->cmos_data[RTC_REG_B] & 0x02) {
        /* 24 hour format */
        s->cmos_data[RTC_HOURS] = to_bcd(s, tm->tm_hour);
    } else {
        /* 12 hour format */
        s->cmos_data[RTC_HOURS] = to_bcd(s, tm->tm_hour % 12);
        if (tm->tm_hour >= 12)
            s->cmos_data[RTC_HOURS] |= 0x80;
    }
    s->cmos_data[RTC_DAY_OF_WEEK] = to_bcd(s, tm->tm_wday);
    s->cmos_data[RTC_DAY_OF_MONTH] = to_bcd(s, tm->tm_mday);
    s->cmos_data[RTC_MONTH] = to_bcd(s, tm->tm_mon + 1);
    s->cmos_data[RTC_YEAR] = to_bcd(s, tm->tm_year % 100);
}

/* month is between 0 and 11. */
static int get_days_in_month(int month, int year)
{
    static const int days_tab[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int d;
    if ((unsigned )month >= 12)
        return 31;
    d = days_tab[month];
    if (month == 1) {
        if ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0))
            d++;
    }
    return d;
}

/* update 'tm' to the next second */
static void rtc_next_second(struct my_tm *tm)
{
    int days_in_month;

    tm->tm_sec++;
    if ((unsigned)tm->tm_sec >= 60) {
        tm->tm_sec = 0;
        tm->tm_min++;
        if ((unsigned)tm->tm_min >= 60) {
            tm->tm_min = 0;
            tm->tm_hour++;
            if ((unsigned)tm->tm_hour >= 24) {
                tm->tm_hour = 0;
                /* next day */
                tm->tm_wday++;
                if ((unsigned)tm->tm_wday >= 7)
                    tm->tm_wday = 0;
                days_in_month = get_days_in_month(tm->tm_mon,
                                                  tm->tm_year + 1900);
                tm->tm_mday++;
                if (tm->tm_mday < 1) {
                    tm->tm_mday = 1;
                } else if (tm->tm_mday > days_in_month) {
                    tm->tm_mday = 1;
                    tm->tm_mon++;
                    if (tm->tm_mon >= 12) {
                        tm->tm_mon = 0;
                        tm->tm_year++;
                    }
                }
            }
        }
    }
}


static void rtc_update_second(void *opaque)
{
    RTCState *s = (RTCState*)opaque;

    /* if the oscillator is not in normal operation, we do not update */
    if ((s->cmos_data[RTC_REG_A] & 0x70) != 0x20) {
        s->next_second_time += TMTimerGetFreq(s->CTX_SUFF(pSecondTimer));
        TMTimerSet(s->CTX_SUFF(pSecondTimer), s->next_second_time);
    } else {
        rtc_next_second(&s->current_tm);

        if (!(s->cmos_data[RTC_REG_B] & REG_B_SET)) {
            /* update in progress bit */
            Log2(("RTC: UIP %x -> 1\n", !!(s->cmos_data[RTC_REG_A] & REG_A_UIP)));
            s->cmos_data[RTC_REG_A] |= REG_A_UIP;
        }

        /* 244140 ns = 8 / 32768 seconds */
        uint64_t delay = TMTimerFromNano(s->CTX_SUFF(pSecondTimer2), 244140);
        TMTimerSet(s->CTX_SUFF(pSecondTimer2), s->next_second_time + delay);
    }
}

static void rtc_update_second2(void *opaque)
{
    RTCState *s = (RTCState*)opaque;

    if (!(s->cmos_data[RTC_REG_B] & REG_B_SET)) {
        rtc_copy_date(s);
    }

    /* check alarm */
    if (s->cmos_data[RTC_REG_B] & REG_B_AIE) {
        if (((s->cmos_data[RTC_SECONDS_ALARM] & 0xc0) == 0xc0 ||
             from_bcd(s, s->cmos_data[RTC_SECONDS_ALARM]) == s->current_tm.tm_sec) &&
            ((s->cmos_data[RTC_MINUTES_ALARM] & 0xc0) == 0xc0 ||
             from_bcd(s, s->cmos_data[RTC_MINUTES_ALARM]) == s->current_tm.tm_min) &&
            ((s->cmos_data[RTC_HOURS_ALARM] & 0xc0) == 0xc0 ||
             from_bcd(s, s->cmos_data[RTC_HOURS_ALARM]) == s->current_tm.tm_hour)) {

            s->cmos_data[RTC_REG_C] |= 0xa0;
            PDMDevHlpISASetIrq(s->CTX_SUFF(pDevIns), s->irq, 1);
        }
    }

    /* update ended interrupt */
    if (s->cmos_data[RTC_REG_B] & REG_B_UIE) {
        s->cmos_data[RTC_REG_C] |= 0x90;
        PDMDevHlpISASetIrq(s->CTX_SUFF(pDevIns), s->irq, 1);
    }

    /* clear update in progress bit */
    Log2(("RTC: UIP %x -> 0\n", !!(s->cmos_data[RTC_REG_A] & REG_A_UIP)));
    s->cmos_data[RTC_REG_A] &= ~REG_A_UIP;

    s->next_second_time += TMTimerGetFreq(s->CTX_SUFF(pSecondTimer));
    TMTimerSet(s->CTX_SUFF(pSecondTimer), s->next_second_time);
}

static uint32_t cmos_ioport_read(void *opaque, uint32_t addr)
{
    RTCState *s = (RTCState*)opaque;
    int ret;
    if ((addr & 1) == 0) {
        return 0xff;
    } else {
        switch(s->cmos_index) {
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
            ret = s->cmos_data[s->cmos_index];
            break;
        case RTC_REG_A:
            ret = s->cmos_data[s->cmos_index];
            break;
        case RTC_REG_C:
            ret = s->cmos_data[s->cmos_index];
            PDMDevHlpISASetIrq(s->CTX_SUFF(pDevIns), s->irq, 0);
            s->cmos_data[RTC_REG_C] = 0x00;
            break;
        default:
            ret = s->cmos_data[s->cmos_index];
            break;
        }
        Log(("CMOS: Read idx %#04x: %#04x\n", s->cmos_index, ret));
        return ret;
    }
}

#ifdef IN_RING3
static void rtc_set_memory(RTCState *s, int addr, int val)
{
    if (addr >= 0 && addr <= 127)
        s->cmos_data[addr] = val;
}

static void rtc_set_date(RTCState *s, const struct my_tm *tm)
{
    s->current_tm = *tm;
    rtc_copy_date(s);
}

#endif /* IN_RING3 */

/* -=-=-=-=-=- wrappers / stuff -=-=-=-=-=- */

/**
 * Port I/O Handler for IN operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument - ignored.
 * @param   uPort       Port number used for the IN operation.
 * @param   pu32        Where to store the result.
 * @param   cb          Number of bytes read.
 */
PDMBOTHCBDECL(int) rtcIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t *pu32, unsigned cb)
{
    NOREF(pvUser);
    if (cb == 1)
    {
        *pu32 = cmos_ioport_read(PDMINS_2_DATA(pDevIns, RTCState *), Port);
        return VINF_SUCCESS;
    }
    return VERR_IOM_IOPORT_UNUSED;
}


/**
 * Port I/O Handler for OUT operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument - ignored.
 * @param   uPort       Port number used for the IN operation.
 * @param   u32         The value to output.
 * @param   cb          The value size in bytes.
 */
PDMBOTHCBDECL(int) rtcIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT Port, uint32_t u32, unsigned cb)
{
    NOREF(pvUser);
    if (cb == 1)
        cmos_ioport_write(PDMINS_2_DATA(pDevIns, RTCState *), Port, u32);
    return VINF_SUCCESS;
}


/**
 * Device timer callback function, periodic.
 *
 * @param   pDevIns         Device instance of the device which registered the timer.
 * @param   pTimer          The timer handle.
 * @param   pvUser          Pointer to the RTC state.
 */
PDMBOTHCBDECL(void) rtcTimerPeriodic(PPDMDEVINS pDevIns, PTMTIMER pTimer, void *pvUser)
{
    rtc_periodic_timer((RTCState *)pvUser);
}


/**
 * Device timer callback function, second.
 *
 * @param   pDevIns         Device instance of the device which registered the timer.
 * @param   pTimer          The timer handle.
 * @param   pvUser          Pointer to the RTC state.
 */
PDMBOTHCBDECL(void) rtcTimerSecond(PPDMDEVINS pDevIns, PTMTIMER pTimer, void *pvUser)
{
    rtc_update_second((RTCState *)pvUser);
}


/**
 * Device timer callback function, second2.
 *
 * @param   pDevIns         Device instance of the device which registered the timer.
 * @param   pTimer          The timer handle.
 * @param   pvUser          Pointer to the RTC state.
 */
PDMBOTHCBDECL(void) rtcTimerSecond2(PPDMDEVINS pDevIns, PTMTIMER pTimer, void *pvUser)
{
    rtc_update_second2((RTCState *)pvUser);
}

#ifdef IN_RING3

/**
 * @copydoc FNSSMDEVLIVEEXEC
 */
static DECLCALLBACK(int) rtcLiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    RTCState *pThis = PDMINS_2_DATA(pDevIns, RTCState *);

    SSMR3PutU8(    pSSM, pThis->irq);
    SSMR3PutIOPort(pSSM, pThis->IOPortBase);
    SSMR3PutBool(  pSSM, pThis->fUTC);

    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) rtcSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RTCState *pThis = PDMINS_2_DATA(pDevIns, RTCState *);

    /* The config. */
    rtcLiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    /* The state. */
    SSMR3PutMem(pSSM, pThis->cmos_data, 128);
    SSMR3PutU8(pSSM, pThis->cmos_index);

    SSMR3PutS32(pSSM, pThis->current_tm.tm_sec);
    SSMR3PutS32(pSSM, pThis->current_tm.tm_min);
    SSMR3PutS32(pSSM, pThis->current_tm.tm_hour);
    SSMR3PutS32(pSSM, pThis->current_tm.tm_wday);
    SSMR3PutS32(pSSM, pThis->current_tm.tm_mday);
    SSMR3PutS32(pSSM, pThis->current_tm.tm_mon);
    SSMR3PutS32(pSSM, pThis->current_tm.tm_year);

    TMR3TimerSave(pThis->CTX_SUFF(pPeriodicTimer), pSSM);

    SSMR3PutS64(pSSM, pThis->next_periodic_time);

    SSMR3PutS64(pSSM, pThis->next_second_time);
    TMR3TimerSave(pThis->CTX_SUFF(pSecondTimer), pSSM);
    TMR3TimerSave(pThis->CTX_SUFF(pSecondTimer2), pSSM);

    return VINF_SUCCESS;
}


/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) rtcLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    RTCState   *pThis = PDMINS_2_DATA(pDevIns, RTCState *);
    int         rc;

    if (    uVersion != RTC_SAVED_STATE_VERSION
        &&  uVersion != RTC_SAVED_STATE_VERSION_VBOX_30)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* The config. */
    if (uVersion > RTC_SAVED_STATE_VERSION_VBOX_30)
    {
        uint8_t u8Irq;
        rc = SSMR3GetU8(pSSM, &u8Irq);          AssertRCReturn(rc, rc);
        if (u8Irq != pThis->irq)
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - u8Irq: saved=%#x config=%#x"), u8Irq, pThis->irq);

        RTIOPORT IOPortBase;
        rc = SSMR3GetIOPort(pSSM, &IOPortBase); AssertRCReturn(rc, rc);
        if (IOPortBase != pThis->IOPortBase)
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - IOPortBase: saved=%RTiop config=%RTiop"), IOPortBase, pThis->IOPortBase);

        bool fUTC;
        rc = SSMR3GetBool(pSSM, &fUTC);         AssertRCReturn(rc, rc);
        if (fUTC != pThis->fUTC)
            LogRel(("RTC: Config mismatch - fUTC: saved=%RTbool config=%RTbool\n", fUTC, pThis->fUTC));
    }

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /* The state. */
    SSMR3GetMem(pSSM, pThis->cmos_data, 128);
    SSMR3GetU8(pSSM, &pThis->cmos_index);

    SSMR3GetS32(pSSM, &pThis->current_tm.tm_sec);
    SSMR3GetS32(pSSM, &pThis->current_tm.tm_min);
    SSMR3GetS32(pSSM, &pThis->current_tm.tm_hour);
    SSMR3GetS32(pSSM, &pThis->current_tm.tm_wday);
    SSMR3GetS32(pSSM, &pThis->current_tm.tm_mday);
    SSMR3GetS32(pSSM, &pThis->current_tm.tm_mon);
    SSMR3GetS32(pSSM, &pThis->current_tm.tm_year);

    TMR3TimerLoad(pThis->CTX_SUFF(pPeriodicTimer), pSSM);

    SSMR3GetS64(pSSM, &pThis->next_periodic_time);

    SSMR3GetS64(pSSM, &pThis->next_second_time);
    TMR3TimerLoad(pThis->CTX_SUFF(pSecondTimer), pSSM);
    TMR3TimerLoad(pThis->CTX_SUFF(pSecondTimer2), pSSM);

    int period_code = pThis->cmos_data[RTC_REG_A] & 0x0f;
    if (    period_code != 0
        &&  (pThis->cmos_data[RTC_REG_B] & REG_B_PIE)) {
        if (period_code <= 2)
            period_code += 7;
        int period = 1 << (period_code - 1);
        LogRel(("RTC: period=%#x (%d) %u Hz (restore)\n", period, period, _32K / period));
        pThis->CurPeriod = period;
    } else {
        LogRel(("RTC: stopped the periodic timer (restore)\n"));
        pThis->CurPeriod = 0;
    }
    pThis->cRelLogEntries = 0;
    return VINF_SUCCESS;
}


/* -=-=-=-=-=- PDM Interface provided by the RTC device  -=-=-=-=-=- */

/**
 * Calculate and update the standard CMOS checksum.
 *
 * @param   pThis       Pointer to the RTC state data.
 */
static void rtcCalcCRC(RTCState *pThis)
{
    uint16_t u16;
    unsigned i;

    for (i = RTC_CRC_START, u16 = 0; i <= RTC_CRC_LAST; i++)
        u16 += pThis->cmos_data[i];
    pThis->cmos_data[RTC_CRC_LOW] = u16 & 0xff;
    pThis->cmos_data[RTC_CRC_HIGH] = (u16 >> 8) & 0xff;
}


/**
 * Write to a CMOS register and update the checksum if necessary.
 *
 * @returns VBox status code.
 * @param   pDevIns     Device instance of the RTC.
 * @param   iReg        The CMOS register index.
 * @param   u8Value     The CMOS register value.
 */
static DECLCALLBACK(int) rtcCMOSWrite(PPDMDEVINS pDevIns, unsigned iReg, uint8_t u8Value)
{
    RTCState *pThis = PDMINS_2_DATA(pDevIns, RTCState *);
    if (iReg < RT_ELEMENTS(pThis->cmos_data))
    {
        pThis->cmos_data[iReg] = u8Value;

        /* does it require checksum update? */
        if (    iReg >= RTC_CRC_START
            &&  iReg <= RTC_CRC_LAST)
            rtcCalcCRC(pThis);

        return VINF_SUCCESS;
    }
    AssertMsgFailed(("iReg=%d\n", iReg));
    return VERR_INVALID_PARAMETER;
}


/**
 * Read a CMOS register.
 *
 * @returns VBox status code.
 * @param   pDevIns     Device instance of the RTC.
 * @param   iReg        The CMOS register index.
 * @param   pu8Value    Where to store the CMOS register value.
 */
static DECLCALLBACK(int) rtcCMOSRead(PPDMDEVINS pDevIns, unsigned iReg, uint8_t *pu8Value)
{
    RTCState   *pThis = PDMINS_2_DATA(pDevIns, RTCState *);
    if (iReg < RT_ELEMENTS(pThis->cmos_data))
    {
        *pu8Value = pThis->cmos_data[iReg];
        return VINF_SUCCESS;
    }
    AssertMsgFailed(("iReg=%d\n", iReg));
    return VERR_INVALID_PARAMETER;
}


/* -=-=-=-=-=- based on bits from pc.c -=-=-=-=-=- */

/** @copydoc FNPDMDEVINITCOMPLETE */
static DECLCALLBACK(int)  rtcInitComplete(PPDMDEVINS pDevIns)
{
    /** @todo this should be (re)done at power on if we didn't load a state... */
    RTCState   *pThis = PDMINS_2_DATA(pDevIns, RTCState *);

    /*
     * Set the CMOS date/time.
     */
    RTTIMESPEC  Now;
    PDMDevHlpTMUtcNow(pDevIns, &Now);
    RTTIME Time;
    if (pThis->fUTC)
        RTTimeExplode(&Time, &Now);
    else
        RTTimeLocalExplode(&Time, &Now);

    struct my_tm Tm;
    memset(&Tm, 0, sizeof(Tm));
    Tm.tm_year = Time.i32Year - 1900;
    Tm.tm_mon  = Time.u8Month - 1;
    Tm.tm_mday = Time.u8MonthDay;
    Tm.tm_wday = (Time.u8WeekDay + 1 + 7) % 7; /* 0 = monday -> sunday */
    Tm.tm_yday = Time.u16YearDay - 1;
    Tm.tm_hour = Time.u8Hour;
    Tm.tm_min  = Time.u8Minute;
    Tm.tm_sec  = Time.u8Second;

    rtc_set_date(pThis, &Tm);

    int iYear = to_bcd(pThis, (Tm.tm_year / 100) + 19); /* tm_year is 1900 based */
    rtc_set_memory(pThis, 0x32, iYear);                                     /* 32h - Century Byte (BCD value for the century */
    rtc_set_memory(pThis, 0x37, iYear);                                     /* 37h - (IBM PS/2) Date Century Byte */

    /*
     * Recalculate the checksum just in case.
     */
    rtcCalcCRC(pThis);

    Log(("CMOS: \n%16.128Rhxd\n", pThis->cmos_data));
    return VINF_SUCCESS;
}


/* -=-=-=-=-=- real code -=-=-=-=-=- */

/**
 * @copydoc
 */
static DECLCALLBACK(void) rtcRelocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    RTCState *pThis = PDMINS_2_DATA(pDevIns, RTCState *);

    pThis->pDevInsRC        = PDMDEVINS_2_RCPTR(pDevIns);
    pThis->pPeriodicTimerRC = TMTimerRCPtr(pThis->pPeriodicTimerR3);
    pThis->pSecondTimerRC   = TMTimerRCPtr(pThis->pSecondTimerR3);
    pThis->pSecondTimer2RC  = TMTimerRCPtr(pThis->pSecondTimer2R3);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)  rtcConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfgHandle)
{
    RTCState   *pThis = PDMINS_2_DATA(pDevIns, RTCState *);
    int         rc;
    Assert(iInstance == 0);

    /*
     * Validate configuration.
     */
    if (!CFGMR3AreValuesValid(pCfgHandle,
                              "Irq\0"
                              "Base\0"
                              "UseUTC\0"
                              "GCEnabled\0"
                              "R0Enabled\0"))
        return VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES;

    /*
     * Init the data.
     */
    uint8_t u8Irq;
    rc = CFGMR3QueryU8Def(pCfgHandle, "Irq", &u8Irq, 8);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"Irq\" as a uint8_t failed"));
    pThis->irq = u8Irq;

    rc = CFGMR3QueryPortDef(pCfgHandle, "Base", &pThis->IOPortBase, 0x70);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"Base\" as a RTIOPORT failed"));

    rc = CFGMR3QueryBoolDef(pCfgHandle, "UseUTC", &pThis->fUTC, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"UseUTC\" as a bool failed"));

    bool fGCEnabled;
    rc = CFGMR3QueryBoolDef(pCfgHandle, "GCEnabled", &fGCEnabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: failed to read GCEnabled as boolean"));

    bool fR0Enabled;
    rc = CFGMR3QueryBoolDef(pCfgHandle, "R0Enabled", &fR0Enabled, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: failed to read R0Enabled as boolean"));

    Log(("RTC: Irq=%#x Base=%#x fGCEnabled=%RTbool fR0Enabled=%RTbool\n",
         u8Irq, pThis->IOPortBase, fGCEnabled, fR0Enabled));


    pThis->pDevInsR3            = pDevIns;
    pThis->pDevInsR0            = PDMDEVINS_2_R0PTR(pDevIns);
    pThis->pDevInsRC            = PDMDEVINS_2_RCPTR(pDevIns);
    pThis->cmos_data[RTC_REG_A] = 0x26;
    pThis->cmos_data[RTC_REG_B] = 0x02;
    pThis->cmos_data[RTC_REG_C] = 0x00;
    pThis->cmos_data[RTC_REG_D] = 0x80;
    pThis->RtcReg.u32Version    = PDM_RTCREG_VERSION;
    pThis->RtcReg.pfnRead       = rtcCMOSRead;
    pThis->RtcReg.pfnWrite      = rtcCMOSWrite;

    /*
     * Create timers, arm them, register I/O Ports and save state.
     */
    rc = PDMDevHlpTMTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, rtcTimerPeriodic, pThis,
                                TMTIMER_FLAGS_DEFAULT_CRIT_SECT, "MC146818 RTC/CMOS - Periodic",
                                &pThis->pPeriodicTimerR3);
    if (RT_FAILURE(rc))
        return rc;
    pThis->pPeriodicTimerR0 = TMTimerR0Ptr(pThis->pPeriodicTimerR3);
    pThis->pPeriodicTimerRC = TMTimerRCPtr(pThis->pPeriodicTimerR3);

    rc = PDMDevHlpTMTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, rtcTimerSecond, pThis,
                                TMTIMER_FLAGS_DEFAULT_CRIT_SECT, "MC146818 RTC/CMOS - Second",
                                &pThis->pSecondTimerR3);
    if (RT_FAILURE(rc))
        return rc;
    pThis->pSecondTimerR0 = TMTimerR0Ptr(pThis->pSecondTimerR3);
    pThis->pSecondTimerRC = TMTimerRCPtr(pThis->pSecondTimerR3);

    rc = PDMDevHlpTMTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, rtcTimerSecond2, pThis,
                                TMTIMER_FLAGS_DEFAULT_CRIT_SECT, "MC146818 RTC/CMOS - Second2",
                                &pThis->pSecondTimer2R3);
    if (RT_FAILURE(rc))
        return rc;
    pThis->pSecondTimer2R0  = TMTimerR0Ptr(pThis->pSecondTimer2R3);
    pThis->pSecondTimer2RC  = TMTimerRCPtr(pThis->pSecondTimer2R3);
    pThis->next_second_time = TMTimerGet(pThis->CTX_SUFF(pSecondTimer2))
                            + (TMTimerGetFreq(pThis->CTX_SUFF(pSecondTimer2)) * 99) / 100;
    rc = TMTimerSet(pThis->CTX_SUFF(pSecondTimer2), pThis->next_second_time);
    if (RT_FAILURE(rc))
        return rc;

    rc = PDMDevHlpIOPortRegister(pDevIns, pThis->IOPortBase, 2, NULL,
                                 rtcIOPortWrite, rtcIOPortRead, NULL, NULL, "MC146818 RTC/CMOS");
    if (RT_FAILURE(rc))
        return rc;
    if (fGCEnabled)
    {
        rc = PDMDevHlpIOPortRegisterRC(pDevIns, pThis->IOPortBase, 2, 0,
                                       "rtcIOPortWrite", "rtcIOPortRead", NULL, NULL, "MC146818 RTC/CMOS");
        if (RT_FAILURE(rc))
            return rc;
    }
    if (fR0Enabled)
    {
        rc = PDMDevHlpIOPortRegisterR0(pDevIns, pThis->IOPortBase, 2, 0,
                                       "rtcIOPortWrite", "rtcIOPortRead", NULL, NULL, "MC146818 RTC/CMOS");
        if (RT_FAILURE(rc))
            return rc;
    }

    rc = PDMDevHlpSSMRegister3(pDevIns, RTC_SAVED_STATE_VERSION, sizeof(*pThis), rtcLiveExec, rtcSaveExec, rtcLoadExec);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register ourselves as the RTC/CMOS with PDM.
     */
    rc = PDMDevHlpRTCRegister(pDevIns, &pThis->RtcReg, &pThis->pRtcHlpR3);
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}


/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceMC146818 =
{
    /* u32Version */
    PDM_DEVREG_VERSION,
    /* szName */
    "mc146818",
    /* szRCMod */
    "VBoxDDGC.gc",
    /* szR0Mod */
    "VBoxDDR0.r0",
    /* pszDescription */
    "Motorola MC146818 RTC/CMOS Device.",
    /* fFlags */
    PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DEVREG_FLAGS_GUEST_BITS_32_64 | PDM_DEVREG_FLAGS_PAE36 | PDM_DEVREG_FLAGS_RC | PDM_DEVREG_FLAGS_R0,
    /* fClass */
    PDM_DEVREG_CLASS_RTC,
    /* cMaxInstances */
    1,
    /* cbInstance */
    sizeof(RTCState),
    /* pfnConstruct */
    rtcConstruct,
    /* pfnDestruct */
    NULL,
    /* pfnRelocate */
    rtcRelocate,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnQueryInterface */
    NULL,
    /* pfnInitComplete */
    rtcInitComplete,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32VersionEnd */
    PDM_DEVREG_VERSION
};

#endif /* IN_RING3 */
#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

