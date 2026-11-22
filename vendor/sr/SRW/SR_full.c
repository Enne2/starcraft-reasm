/**
 *
 *  Copyright (C) 2016-2025 Roman Pauer
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

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "SR_defs.h"
#include "SR_vars.h"
#include "udis86_dep.h"

#undef DISPLAY_DISASSEMBLY

typedef struct _Entry_region_ {
    unsigned int Entry;
    uint_fast32_t begin_ofs;
    int isregion;
} Entry_region;

output_data *SR_disassemble_offset_init_output(unsigned int SecNum, uint_fast32_t offset, unsigned int length)
{
    output_data *output;
    unsigned int index;

    if (offset + length == section[SecNum].size)
    {
        output = section_output_list_FindEntryEqual(SecNum, offset + length);
        if (output != NULL)
        {
            if (NULL == section_alias_list_FindEntryEqual(SecNum, offset + length))
            {
                output->has_label = 0;
                output->align = 0;
            }
        }
    }

    for (index = length - 1; index != 0; index--)
    {
        section_output_list_Delete(SecNum, offset + index);
        section_bound_list_Delete(SecNum, offset + index);
    }

    output = section_output_list_FindEntryEqual(SecNum, offset);
    if (output != NULL)
    {
        if (output->str != NULL)
        {
            free(output->str);
            output->str = NULL;
        }
        output->len = length - 1;
        output->type = OT_INSTRUCTION;

        if (section[SecNum].type != ST_CODE)
        {
            output->type = OT_UNKNOWN;
            if (length & 3)
            {
                fprintf(stderr, "Warning: data replacement causes misalignement - %i - %i (0x%x)\n", SecNum, (unsigned int)offset, (unsigned int)(section[SecNum].start + offset));
            }
        }
    }

    return output;
}

/* ************************************************************************** */

#if (OUTPUT_TYPE != OUT_ORIG && OUTPUT_TYPE != OUT_WINDOWS)
static void SR_disassemble_ignore_instructions(Word_t Index, int Value, void *data)
{
#define DATA ((unsigned int)(uintptr_t) data)

    int index;

    if (Index + Value == section[DATA].size)
    {
        Value++;
    }

    for (index = Value - 1; index >= 0; index--)
    {
        section_output_list_Delete(DATA, Index + index);
        section_bound_list_Delete(DATA, Index + index);
        section_entry_list_Delete(DATA, Index + index);
        section_export_list_Delete(DATA, Index + index);
    }

    if (Index + Value > section[DATA].size)
    {
        output_data *output;

        output = section_output_list_FindEntryEqualOrLower(DATA, section[DATA].size);

        if (output != NULL)
        {
            output = section_output_list_Insert(DATA, output->ofs + output->len + 1);
            if ( output != NULL )
            {
                output->type = OT_NONE;
                output->has_label = 1;
                output->len = (uint_fast32_t) -1;
            }
        }
    }

#undef DATA
}

static void SR_disassemble_replace_instructions(replace_data *item, void *data)
{
    output_data *output;
    bound_data *bound;

#define DATA ((unsigned int)(uintptr_t) data)

    output = SR_disassemble_offset_init_output(DATA, item->ofs, item->length);

    if (output != NULL)
    {
        output->str = strdup(item->instr);

        bound = section_bound_list_Insert(DATA, item->ofs);
        if (bound != NULL) bound->begin = 1;
    }

    if (section[DATA].type == ST_CODE)
    {
        section_entry_list_Insert(DATA, item->ofs + item->length);
        bound = section_bound_list_Insert(DATA, item->ofs + item->length);
        if (bound != NULL) bound->begin = 1;
    }

#undef DATA
}
#endif

#if ((OUTPUT_TYPE == OUT_X86) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
static void SR_remove_nops(unsigned int Entry, uint_fast32_t begin_ofs, uint_fast32_t end_ofs)
{
    output_data *first;

    if (begin_ofs == 0)
    {
        first = section_output_list_FindEntryFirst(Entry);
        begin_ofs = (first != NULL)?first->ofs:end_ofs;
    }

    for (; begin_ofs < end_ofs; begin_ofs++)
    {
        section_output_list_Delete(Entry, begin_ofs);
    }
}

static void SR_disassemble_create_aligns_code(output_data *item, void *data)
{
#define DATA ((Entry_region *) data)

    switch (DATA->isregion)
    {
    case 0: // data
        if (item->type == OT_INSTRUCTION)
        {
            if (item->align == 0)
            {
                // first instruction after data gets aligned
                item->align = 4;
            }
            DATA->isregion = 1;
        }
        break;
    case 1: // instructions
        if (item->type != OT_INSTRUCTION)
        {
            if (item->type == OT_OFFSET && item->has_label && (item->align == 0 || item->align == 4))
            {
                // jump table after instructions
                DATA->isregion = 3;
            }
            else if (item->type == OT_UNKNOWN && item->len == 0 && item->str == NULL && item->has_label == 0 && item->align == 0 && (section[DATA->Entry].adr[item->ofs] == 0x90 || section[DATA->Entry].adr[item->ofs] == 0xCC))
            {
                DATA->isregion = 2;
                DATA->begin_ofs = item->ofs;
            }
            else
            {
                DATA->isregion = 0;
            }
        }
        break;
    case 2: // nops after instructions
        if (item->type == OT_INSTRUCTION)
        {
            if (item->has_label && item->align == 0 && (item->ofs & 0x03) == 0)
            {
                if ((item->ofs & 0x0f) == 0)
                {
                    item->align = 16;
                }
                else if ((item->ofs & 0x07) == 0)
                {
                    item->align = 8;
                }
                else
                {
                    item->align = 4;
                }

                // remove nops before alignment
                SR_remove_nops(DATA->Entry, DATA->begin_ofs, item->ofs);
            }
            else if (item->align == 0)
            {
                // first instruction after data gets aligned
                item->align = 4;
            }

            DATA->isregion = 1;
        }
        else if (item->type == OT_OFFSET)
        {
            if (item->has_label && (item->align == 0 || item->align == 4))
            {
                // jump table after instructions
                item->align = 4;

                // remove nops before alignment
                SR_remove_nops(DATA->Entry, DATA->begin_ofs, item->ofs);
            }

            DATA->isregion = 3;
        }
        else if (item->type == OT_NONE)
        {
            output_data *last;

            last = section_output_list_FindEntryLast(DATA->Entry);
            if (last != NULL && last->ofs == item->ofs)
            {
                // remove nops before end
                SR_remove_nops(DATA->Entry, DATA->begin_ofs, item->ofs);
            }

            DATA->isregion = 0;
        }
        else if (item->type != OT_UNKNOWN || item->len || item->str || item->has_label || item->align || (section[DATA->Entry].adr[item->ofs] != 0x90 && section[DATA->Entry].adr[item->ofs] != 0xCC))
        {
            DATA->isregion = 0;
        }
        break;
    case 3: // jump table
        if (item->type != OT_OFFSET)
        {
            if (item->type == OT_INSTRUCTION)
            {
                if (item->align == 0)
                {
                    // first instruction after data gets aligned
                    item->align = 4;
                }
                DATA->isregion = 1;
            }
            else if (item->type == OT_UNKNOWN && item->len == 0 && item->str == NULL && item->has_label == 0 && item->align == 0 && (section[DATA->Entry].adr[item->ofs] == 0x90 || section[DATA->Entry].adr[item->ofs] == 0xCC))
            {
                DATA->isregion = 2;
                DATA->begin_ofs = item->ofs;
            }
            else
            {
                DATA->isregion = 0;
            }
        }
        break;
    }

#undef DATA
}

static void SR_disassemble_create_aligns_data(output_data *item, void *data)
{
#define DATA ((Entry_region *) data)

    if (item->type == OT_INSTRUCTION)
    {
        DATA->isregion = 0;
    }
    else
    {
        if (DATA->isregion == 0)
        {
            if (item->align)
            {
                DATA->isregion = -1;
            }
            else
            {
                if (item->has_label)
                {
                    if (item->type != OT_NONE)
                    {
                        item->align = 4;
                        DATA->isregion = -1;
                    }
                }
                else
                {
                    if (item->type != OT_UNKNOWN)
                    {
                        DATA->isregion = -1;
                    }
                }
            }
        }
    }

#undef DATA
}
#endif

#if ((OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
static int SR_is_jump_mnemonic(enum ud_mnemonic_code mnemonic)
{
    /* jo .. jg are the conditional jumps, plus the loop family and jmp */
    return (((mnemonic >= UD_Ijo) && (mnemonic <= UD_Ijg)) ||
            (mnemonic == UD_Ijmp) ||
            (mnemonic == UD_Ijcxz) || (mnemonic == UD_Ijecxz) || (mnemonic == UD_Ijrcxz) ||
            (mnemonic == UD_Iloop) || (mnemonic == UD_Iloope) || (mnemonic == UD_Iloopne));
}

/*
 * Mark every instruction that is the destination of a conditional or
 * unconditional jump.  The per-region flag scan is linear, so a region that
 * starts on a jump target receives its live flags from the jumping code and
 * not from the instruction that precedes it in address order; this mark lets
 * the region scan distinguish the two situations instead of failing.
 */
static void SR_disassemble_mark_jump_targets(unsigned int Entry)
{
    output_data *output;
    output_data *target;
    ud_t ud;
    int length;
    uint_fast32_t tsec, tofs;
    uint32_t address;

    if (section[Entry].type != ST_CODE) return;

    ud_init(&ud);
    ud_set_mode(&ud, 32);
    ud_set_syntax(&ud, UD_SYN_INTEL);

    output = section_output_list_FindEntryFirst(Entry);
    while (output != NULL)
    {
        if (output->type == OT_INSTRUCTION)
        {
            ud_set_input_buffer(&ud, &(section[Entry].adr[output->ofs]), output->len + 1);
            ud_set_pc(&ud, section[Entry].start + output->ofs);

            length = ud_disassemble(&ud);

            if ((length != 0) && SR_is_jump_mnemonic(ud.mnemonic) &&
                (ud.operand[0].type == UD_OP_JIMM))
            {
                if (ud.operand[0].size == 32)
                {
                    address = ud.operand[0].lval.sdword + (uint32_t) ud.pc;
                }
                else
                {
                    address = (uint32_t) ((int32_t) ud.operand[0].lval.sbyte + (int32_t) (uint32_t) ud.pc);
                }

                if (SR_get_section_reladr(address, &tsec, &tofs) && (tsec == Entry))
                {
                    target = section_output_list_FindEntryEqual(Entry, tofs);
                    if (target != NULL) target->jump_target = 1;
                }
            }
        }

        output = section_output_list_FindEntryHigher(Entry, output->ofs);
    }
}

static void SR_disassemble_create_regions(bound_data *item, void *data)
{
    output_data *output;


#define DATA ((Entry_region *) data)

    if (DATA->isregion && item->begin)
    {
        output = section_output_list_FindEntryLower(DATA->Entry, item->ofs);

        if (output != NULL)
        {
            section_region_list_Insert(DATA->Entry, DATA->begin_ofs, output->ofs);
        }
        DATA->isregion = 0;
    }

    if (item->begin)
    {
        /*
         * A region begins a procedure, and llasm procedures are the unit that
         * code jumps to: mark the first instruction so the previous procedure
         * ends with an explicit call to it.  Without this, a region that no
         * branch points at (a function reachable only through a pointer table,
         * declared as a code entry point) would be emitted in the middle of the
         * preceding procedure.
         */
        output = section_output_list_FindEntryEqual(DATA->Entry, item->ofs);
        if ((output != NULL) && (output->type == OT_INSTRUCTION))
        {
            output->has_label = 1;
        }

        if (item->end)
        {
            section_region_list_Insert(DATA->Entry, item->ofs, item->ofs);
        }
        else
        {
            DATA->isregion = 1;
            DATA->begin_ofs = item->ofs;
        }
    }
    else
    {
        if (DATA->isregion)
        {
            section_region_list_Insert(DATA->Entry, DATA->begin_ofs, item->ofs);

            DATA->isregion = 0;
        }
        else
        {
            section_region_list_Insert(DATA->Entry, item->ofs, item->ofs);
        }
    }


#undef DATA
}
#endif

/* ************************************************************************** */

int SR_full_disassembly(void)
{
    unsigned int index;
    int finished, ret;
#if (OUTPUT_TYPE != OUT_ORIG)
    uint_fast32_t offset;
#endif
#if ((OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
    output_data *output;
    region_data *region;
    replace_data *replace;
#endif
#if ((OUTPUT_TYPE == OUT_X86) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
    Entry_region ER;
#endif
    Word_t entry_index;


#if (OUTPUT_TYPE == OUT_ORIG)
    ret = 0;
#endif


#if (OUTPUT_TYPE != OUT_ORIG && OUTPUT_TYPE != OUT_WINDOWS)
    for (index = 0; index < num_sections; index++)
    {
        section_ignored_list_ForEach(index, &SR_disassemble_ignore_instructions, (void *)(uintptr_t) index);
        section_replace_list_ForEach(index, &SR_disassemble_replace_instructions, (void *)(uintptr_t) index);
    }
#endif

#if (OUTPUT_TYPE == OUT_LLASM)
    ret = init_udis86_dep();

    if (ret) return ret;
#endif

    finished = 0;

    while (!finished)
    {
        finished = 1;

        for (index = 0; index < num_sections; index++)
        {
            // process only code segments
            if (section[index].type != ST_CODE) continue;

            while (section_entry_list_FindFirstIndex(index, &entry_index))
            {
                finished = 0;

#if (OUTPUT_TYPE != OUT_ORIG)
                offset = (uint_fast32_t)entry_index;
#endif
                section_entry_list_Delete(index, entry_index);

#if (OUTPUT_TYPE == OUT_WINDOWS)
                ret = SR_disassemble_offset_windows(index, offset);
#endif
#if ((OUTPUT_TYPE == OUT_X86) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
                ret = SR_disassemble_offset_win32(index, offset);
#endif

                if (ret) return ret;
            }
        }
    }

#if ((OUTPUT_TYPE == OUT_X86) || (OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
    // align code and data in code segments
    for (index = 0; index < num_sections; index++)
    {
        // process only code segments
        if (section[index].type != ST_CODE) continue;

        ER.Entry = index;
        ER.begin_ofs = 0;
        ER.isregion = 2;

        section_output_list_ForEach(index, &SR_disassemble_create_aligns_code, (void *) &ER);

        ER.Entry = index;
        ER.isregion = -1;

        section_output_list_ForEach(index, &SR_disassemble_create_aligns_data, (void *) &ER);
    }
#endif

#if ((OUTPUT_TYPE == OUT_LLASM) || (OUTPUT_TYPE == OUT_X64))
    // create regions from bounds
    for (index = 0; index < num_sections; index++)
    {
        // process only code segments
        if (section[index].type != ST_CODE) continue;

        SR_disassemble_mark_jump_targets(index);

        ER.Entry = index;
        ER.isregion = 0;

        section_bound_list_ForEach(index, &SR_disassemble_create_regions, (void *) &ER);
        if (ER.isregion)
        {
            output = section_output_list_FindEntryLast(index);

            if (output != NULL)
            {
                section_region_list_Insert(index, ER.begin_ofs, output->ofs);
            }
        }
    }


    // process regions
    for (index = 0; index < num_sections; index++)
    {
        // process only code segments
        if (section[index].type != ST_CODE) continue;

#if (OUTPUT_TYPE == OUT_LLASM)
        /* Flag liveness flows from later regions to earlier ones. */
        region = section_region_list_FindEntryLast(index);
        while (region != NULL)
        {
            replace = section_replace_list_FindEntryEqual(index, region->begin_ofs);
            if (replace == NULL)
            {
                ret = SR_disassemble_region_llasm(index, region);
                if (ret) return ret;
            }
            region = section_region_list_FindEntryLower(index, region->begin_ofs);
        }
#else
        region = section_region_list_FindEntryFirst(index);
        while (region != NULL)
        {
            replace = section_replace_list_FindEntryEqual(index, region->begin_ofs);
            if (replace == NULL)
            {
                ret = SR_disassemble_region_x64(index, region);
                if (ret) return ret;
            }
            region = section_region_list_FindEntryHigher(index, region->begin_ofs);
        }
#endif
    }
#endif

    return 0;
}
