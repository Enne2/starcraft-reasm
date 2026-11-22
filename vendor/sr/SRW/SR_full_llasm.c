/**
 *
 *  Copyright (C) 2019-2021 Roman Pauer
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

int SR_disassemble_region_llasm(unsigned int Entry, region_data *region)
{
    output_data *output;
    uint_fast32_t cur_ofs, flags_to_write, flags_write, flags_read;
    int finished, orig_ebp_dword_aligned, orig_esp_dword_aligned, last_instruction;
    unsigned int ret;
    char *newstr;
    bound_data *iflags;
    region_data *prev_region;
    int *ua_ebp_area_value, *ua_esp_area_value;
    Word_t ua_ebp_area_index, ua_esp_area_index;

    cur_ofs = region->end_ofs;
    finished = 0;
    flags_to_write = region->flags_to_write;
    last_instruction = 2;

    orig_ebp_dword_aligned = ebp_dword_aligned;
    orig_esp_dword_aligned = esp_dword_aligned;

    ua_ebp_area_index = 0;
    ua_esp_area_index = 0;

    if (orig_ebp_dword_aligned)
    {
        ua_ebp_area_value = section_ua_ebp_list_FindEntryEqualOrLowerIndex(Entry, cur_ofs, &ua_ebp_area_index);
        if (ua_ebp_area_value != NULL)
        {
            if (ua_ebp_area_index + *ua_ebp_area_value <= region->begin_ofs)
            {
                ua_ebp_area_value = NULL;
            }
        }

    }
    else
    {
        ua_ebp_area_value = NULL;
    }

    if (orig_esp_dword_aligned)
    {
        ua_esp_area_value = section_ua_esp_list_FindEntryEqualOrLowerIndex(Entry, cur_ofs, &ua_esp_area_index);
        if (ua_esp_area_value != NULL)
        {
            if (ua_esp_area_index + *ua_esp_area_value <= region->begin_ofs)
            {
                ua_esp_area_value = NULL;
            }
        }
    }
    else
    {
        ua_esp_area_value = NULL;
    }

    output = section_output_list_FindEntryEqual(Entry, cur_ofs);
    while (!finished)
    {
        if (output == NULL)
        {
            fprintf(stderr, "Error: output not found - %i - %i (0x%x)\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs));

            return 1;
        }

        cur_ofs = output->ofs;

        if (cur_ofs < region->begin_ofs)
        {
            fprintf(stderr, "Error: output out of bounds - %i - %i (0x%x)\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs));

            return 2;
        }

        if (output->type == OT_NONE) break;

        /*
         * A data hole inside a code region: skip it and keep scanning the
         * preceding instructions.  The emitter closes the procedure of the
         * instruction before the hole, because control cannot fall through.
         */
        if ((output->str == NULL) || (output->type != OT_INSTRUCTION))
        {
            if (cur_ofs == region->begin_ofs)
            {
                finished = 1;
                break;
            }

            cur_ofs--;
            output = section_output_list_FindEntryEqualOrLower(Entry, cur_ofs);
            continue;
        }

        if (output->str[0] == ';' && strchr(output->str, '\n') == NULL)
        {
            // instruction is commented out
            newstr = (char *) malloc(strlen(output->str) + 2);
            if (newstr == NULL)
            {
                fprintf(stderr, "Error: not enough memory - %i - %i (0x%x)\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs));

                return 3;
            }

            newstr[0] = ';';
            strcpy(&(newstr[1]), output->str);
            free(output->str);
            output->str = newstr;
        }
        else
        {
            ud_set_input_buffer(&ud_obj, &(section[Entry].adr[cur_ofs]), output->len + 1);
            ud_set_pc(&ud_obj, section[Entry].start + cur_ofs);

            ret = ud_disassemble(&ud_obj);


            if (ret == 0 || ret != output->len + 1)
            {
                fprintf(stderr, "Error: wrong disassembly - %i - %i (0x%x) - %i\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs), ret);

                finished = 3;
                break;
            }

            flags_read = flags_needed[ud_obj.mnemonic];
            flags_write = flags_modified[ud_obj.mnemonic];
            if (flags_read == FL_UNKNOWN ||
                flags_write == FL_UNKNOWN)
            {
                fprintf(stderr, "Error: unknown flags - %i - %i (0x%x) - %s\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs), output->str);

                return 5;
            }

            iflags = section_iflags_list_FindEntryEqual(Entry, cur_ofs);
            if (iflags)
            {
                if (flags_to_write & FL_INST_MASK)
                {
                    fprintf(stderr, "Error: mixing flags and instruction flags - %i - %i (0x%x) - %s\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs), output->str);

                    return 5;
                }

                flags_to_write |= (uint32_t) iflags->begin;
            }


            if (orig_ebp_dword_aligned)
            {
                if (ua_ebp_area_value != NULL)
                {
                    if (ua_ebp_area_index > cur_ofs)
                    {
                        ua_ebp_area_value = section_ua_ebp_list_FindEntryEqualOrLowerIndex(Entry, cur_ofs, &ua_ebp_area_index);
                    }

                    if (ua_ebp_area_value != NULL)
                    {
                        if (ua_ebp_area_index + *ua_ebp_area_value > cur_ofs)
                        {
                            ebp_dword_aligned = 0;
                        }
                    }
                }
            }

            if (orig_esp_dword_aligned)
            {
                if (ua_esp_area_value != NULL)
                {
                    if (ua_esp_area_index > cur_ofs)
                    {
                        ua_esp_area_value = section_ua_esp_list_FindEntryEqualOrLowerIndex(Entry, cur_ofs, &ua_esp_area_index);
                    }

                    if (ua_esp_area_value != NULL)
                    {
                        if (ua_esp_area_index + *ua_esp_area_value > cur_ofs)
                        {
                            esp_dword_aligned = 0;
                        }
                    }
                }
            }

            /*
             * Flags that are live at the destination of a jump have to be
             * materialised before the jump itself: the instructions that
             * follow the jump in address order may overwrite them, so the
             * linear backward scan alone cannot see that requirement.
             */
            if ((((ud_obj.mnemonic >= UD_Ijo) && (ud_obj.mnemonic <= UD_Ijg)) ||
                 (ud_obj.mnemonic == UD_Ijmp) ||
                 (ud_obj.mnemonic == UD_Ijcxz) || (ud_obj.mnemonic == UD_Ijecxz) || (ud_obj.mnemonic == UD_Ijrcxz) ||
                 (ud_obj.mnemonic == UD_Iloop) || (ud_obj.mnemonic == UD_Iloope) || (ud_obj.mnemonic == UD_Iloopne)) &&
                (ud_obj.operand[0].type == UD_OP_JIMM))
            {
                uint32_t target_adr;
                uint_fast32_t tsec, tofs;
                region_data *target_region;

                if (ud_obj.operand[0].size == 32)
                {
                    target_adr = ud_obj.operand[0].lval.sdword + (uint32_t) ud_obj.pc;
                }
                else
                {
                    target_adr = (uint32_t) ((int32_t) ud_obj.operand[0].lval.sbyte + (int32_t) (uint32_t) ud_obj.pc);
                }

                if (SR_get_section_reladr(target_adr, &tsec, &tofs) && (tsec == Entry))
                {
                    target_region = section_region_list_FindEntryEqual(tsec, tofs);
                    if ((target_region != NULL) && (target_region->flags_required & FL_ALL))
                    {
                        flags_to_write |= (target_region->flags_required & FL_ALL);
                        fprintf(stderr, "[flags-jump-require] section=%u at=0x%x target=0x%x mask=0x%x\n",
                                Entry, (unsigned int)(section[Entry].start + cur_ofs),
                                (unsigned int)target_adr, (unsigned int)(target_region->flags_required & FL_ALL));
                    }
                }
            }

            ret = SR_disassemble_llasm_instruction(Entry, output, flags_to_write, &flags_write, &flags_read, &last_instruction);

            if (orig_ebp_dword_aligned)
            {
                if (ua_ebp_area_value != NULL)
                {
                    ebp_dword_aligned = 1;
                }
            }

            if (orig_esp_dword_aligned)
            {
                if (ua_esp_area_value != NULL)
                {
                    esp_dword_aligned = 1;
                }
            }


            if (ret) return ret;

            if (flags_read == FL_SPECIFIC ||
                flags_write == FL_SPECIFIC)
            {
                fprintf(stderr, "Error: unknown specific flags - %i - %i (0x%x) - %s\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs), output->str);

                return 5;
            }

            if ((last_instruction) && (cur_ofs != region->end_ofs))
            {
                fprintf(stderr, "Error: instruction ends in the middle of region - %i - %i (0x%x) - %s\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs), output->str);

                return 5;
            }

            last_instruction = 0;

            if ((flags_to_write & FL_INST_MASK) && (flags_read || (flags_write & ~FL_INST_MASK)))
            {
                fprintf(stderr, "Error: unprocessed instruction flags - %i - %i (0x%x) - %s\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs), output->str);

                return 5;
            }

            flags_to_write &= ~flags_write;
            if (flags_to_write == FL_WEAK) flags_to_write = FL_NONE;
            flags_to_write |= flags_read;

            if (iflags)
            {
                flags_to_write &= ~((uint32_t) iflags->end);
            }

        }


        if (cur_ofs == region->begin_ofs)
        {
            finished = 1;
        }
        else
        {
            cur_ofs--;
            output = section_output_list_FindEntryEqualOrLower(Entry, cur_ofs);
        }
    }

    /* remember the live flags of the region entry for the code that jumps here */
    region->flags_required = flags_to_write & (uint_fast32_t) FL_ALL;

    if (flags_to_write)
    {
        if (flags_to_write & FL_WEAK)
        {
            fprintf(stderr, "Warning: unknown weak flags on start of region - %i - %i (0x%x) - 0x%x\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs), (unsigned int)flags_to_write);
        }
        else
        {
            /*
             * Carry live flag requirements into the preceding contiguous code
             * region. Regions are processed in reverse address order, so the
             * predecessor has not yet been emitted and can consume this seed.
             * Do not propagate across data, holes, or a non-adjacent boundary:
             * those remain hard errors below.
             */
            prev_region = section_region_list_FindEntryLower(Entry, region->begin_ofs);
            if (prev_region != NULL)
            {
                output = section_output_list_FindEntryEqual(Entry, prev_region->end_ofs);
                if (output != NULL && output->type == OT_INSTRUCTION &&
                    prev_region->end_ofs + output->len + 1 == region->begin_ofs)
                {
                    prev_region->flags_to_write |= flags_to_write;
                    fprintf(stderr, "[flags-region-propagate] section=%u from=0x%x to=0x%x mask=0x%x\n",
                            Entry, (unsigned int)(section[Entry].start + region->begin_ofs),
                            (unsigned int)(section[Entry].start + prev_region->begin_ofs),
                            (unsigned int)flags_to_write);
                    flags_to_write = 0;
                }
            }

            if (flags_to_write)
            {
                /*
                 * A region that starts on a jump target receives those flags
                 * from the jumping instruction, which was given the same
                 * requirement when it was emitted (forward jumps) or will be
                 * handled by an explicit, IDA-backed instruction_flags.sci
                 * entry (backward jumps).  Record the requirement for the
                 * jumpers and continue instead of aborting.
                 */
                output = section_output_list_FindEntryEqual(Entry, region->begin_ofs);
                if (output != NULL && output->jump_target)
                {
                    fprintf(stderr, "[flags-jump-target] section=%u at=0x%x mask=0x%x\n",
                            Entry, (unsigned int)(section[Entry].start + region->begin_ofs),
                            (unsigned int)flags_to_write);
                }
                else
                {
                    fprintf(stderr, "Error: unknown flags on start of region - %i - %i (0x%x) - 0x%x\n", Entry, (unsigned int)cur_ofs, (unsigned int)(section[Entry].start + cur_ofs), (unsigned int)flags_to_write);

                    return 6;
                }
            }
        }
    }

    return 0;
}

