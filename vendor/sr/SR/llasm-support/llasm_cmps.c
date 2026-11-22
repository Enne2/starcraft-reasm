//part of static recompiler -- do not edit

/**
 *
 *  Copyright (C) 2021-2026 Roman Pauer
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

#include "llasm_cpu.h"

EXTERNC uint32_t CCALL x86_repe_cmpsb(CPU)
{
    int32_t dir;
    uint32_t srcptr1, srcptr2, counter, srcvalue1, srcvalue2;

    dir = (eflags & DF)?-1:1;

    counter = ecx;
    srcptr1 = esi;
    srcptr2 = edi;

    do {
        srcvalue1 = *((uint8_t *)REG2PTR(srcptr1));
        srcvalue2 = *((uint8_t *)REG2PTR(srcptr2));
        srcptr1 += dir;
        srcptr2 += dir;

        counter--;

        if (srcvalue1 != srcvalue2) break;
    } while (counter != 0);

    ecx = counter;
    esi = srcptr1;
    edi = srcptr2;

    return srcvalue1 | (srcvalue2 << 8);
}

/*
 * Word/dword string compare.  Unlike the byte form, the two compared values
 * do not fit in the 32-bit return value, so these helpers compute the
 * arithmetic flags of the final comparison themselves and store them in the
 * emulated EFLAGS using the llasm flag layout.
 */
static void x86_cmps_set_flags(CPU, uint32_t value1, uint32_t value2, uint32_t size)
{
    uint32_t res, mask, byte, sa, sb;
    int par;

    mask = (size == 32)?0xffffffffu:0xffffu;
    res = (value1 - value2) & mask;

    eflags &= ~(uint32_t)(CF | PF | AF | ZF | SF | OF);

    if ((value1 & mask) < (value2 & mask)) eflags |= CF;
    if (res == 0) eflags |= ZF;
    if (size == 32)
    {
        if (res & 0x80000000u) eflags |= SF;
    }
    else
    {
        if (res & 0x8000u) eflags |= SF;
    }
    if ((value1 ^ value2 ^ res) & 0x10u) eflags |= AF;

    /* parity of the low byte of the result */
    byte = res & 0xff;
    par = 1;
    while (byte)
    {
        par ^= (byte & 1);
        byte >>= 1;
    }
    if (par) eflags |= PF;

    if (size == 32)
    {
        sa = value1 & 0x80000000u;
        sb = value2 & 0x80000000u;
        if ((sa != sb) && ((res & 0x80000000u) != sa)) eflags |= OF;
    }
    else
    {
        sa = value1 & 0x8000u;
        sb = value2 & 0x8000u;
        if ((sa != sb) && ((res & 0x8000u) != sa)) eflags |= OF;
    }
}

static uint32_t x86_cmps_rep(CPU, int word, int stop_on_equal)
{
    int32_t dir;
    uint32_t srcptr1, srcptr2, counter, srcvalue1, srcvalue2;

    dir = (eflags & DF)?-1:1;

    counter = ecx;
    srcptr1 = esi;
    srcptr2 = edi;

    do {
        if (word)
        {
            srcvalue1 = *((uint16_t *)REG2PTR(srcptr1));
            srcvalue2 = *((uint16_t *)REG2PTR(srcptr2));
            srcptr1 += dir * 2;
            srcptr2 += dir * 2;
        }
        else
        {
            srcvalue1 = UNALIGNED_READ_32(srcptr1);
            srcvalue2 = UNALIGNED_READ_32(srcptr2);
            srcptr1 += dir * 4;
            srcptr2 += dir * 4;
        }

        counter--;

        if (stop_on_equal)
        {
            if (srcvalue1 == srcvalue2) break;
        }
        else
        {
            if (srcvalue1 != srcvalue2) break;
        }
    } while (counter != 0);

    ecx = counter;
    esi = srcptr1;
    edi = srcptr2;

    x86_cmps_set_flags(cpu, srcvalue1, srcvalue2, word?16:32);

    return srcvalue1;
}

EXTERNC uint32_t CCALL x86_repe_cmpsw(CPU)
{
    return x86_cmps_rep(cpu, 1, 1);
}

EXTERNC uint32_t CCALL x86_repne_cmpsw(CPU)
{
    return x86_cmps_rep(cpu, 1, 0);
}

EXTERNC uint32_t CCALL x86_repe_cmpsd(CPU)
{
    return x86_cmps_rep(cpu, 0, 1);
}

EXTERNC uint32_t CCALL x86_repne_cmpsd(CPU)
{
    return x86_cmps_rep(cpu, 0, 0);
}

EXTERNC uint32_t CCALL x86_repne_cmpsb(CPU)
{
    int32_t dir;
    uint32_t srcptr1, srcptr2, counter, srcvalue1, srcvalue2;

    dir = (eflags & DF)?-1:1;

    counter = ecx;
    srcptr1 = esi;
    srcptr2 = edi;

    do {
        srcvalue1 = *((uint8_t *)REG2PTR(srcptr1));
        srcvalue2 = *((uint8_t *)REG2PTR(srcptr2));
        srcptr1 += dir;
        srcptr2 += dir;

        counter--;

        if (srcvalue1 == srcvalue2) break;
    } while (counter != 0);

    ecx = counter;
    esi = srcptr1;
    edi = srcptr2;

    return srcvalue1 | (srcvalue2 << 8);
}

