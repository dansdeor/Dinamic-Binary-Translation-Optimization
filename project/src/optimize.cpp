
#include "pin.H"
#include "project.h"
#include <iostream>

using std::cerr;
using std::cout;
using std::dec;
using std::endl;
using std::hex;

extern KNOB<BOOL> KnobVerbose;
extern KNOB<BOOL> KnobDumpTranslatedCode;
extern KNOB<BOOL> KnobDoNotCommitTranslatedCode;
extern xed_state_t dstate;

const static unsigned int max_inst_len = XED_MAX_INSTRUCTION_BYTES;

// Tables of all candidate routines to be translated:
typedef struct {
    ADDRINT rtn_addr;
    USIZE rtn_size;
    int instr_map_entry; // negative instr_map_entry means routine does not have a translation.
    bool isSafeForReplacedProbe;
} translated_rtn_t;

extern translated_rtn_t* translated_rtn;
extern int translated_rtn_num;

int add_new_instr_entry(xed_decoded_inst_t* xedd, ADDRINT pc, unsigned int size);

void verbose_inst(INS ins)
{
    // debug print of orig instruction:
    if (KnobVerbose) {
        cout << "0x" << hex << INS_Address(ins) << ": " << INS_Disassemble(ins) << endl;
        // xed_print_hex_line(reinterpret_cast<UINT8*>(INS_Address (ins)), INS_Size(ins));
    }
}

// This is the optimized version with inline only
void copy_inlined_routine(RTN rtn, prof_rtn_stat* prof_stat)
{
    int rc;
    // Open the RTN.
    RTN_Open(rtn);

    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {

        ADDRINT ins_addr = INS_Address(ins);

        xed_decoded_inst_t xedd;
        xed_error_enum_t xed_code;

        xed_decoded_inst_zero_set_mode(&xedd, &dstate);

        xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(ins_addr), max_inst_len);
        if (xed_code != XED_ERROR_NONE) {
            cerr << "ERROR: xed decode failed for instr at: "
                 << "0x" << hex << ins_addr << endl;
            translated_rtn[translated_rtn_num].instr_map_entry = -1;
            break;
        }

        // if its inlined, copy the routine without the ret or if its not just copy it as usual
        if (!INS_IsRet(ins)) {
            // Add instr into instr map:
            rc = add_new_instr_entry(&xedd, INS_Address(ins), INS_Size(ins));
            if (rc < 0) {
                cerr << "ERROR: failed during instructon translation." << endl;
                translated_rtn[translated_rtn_num].instr_map_entry = -1;
                break;
            }
            // if (prof_stat->rtn_name == "foo")
            verbose_inst(ins);
        }
    } // end for INS...

    // debug print of routine name:
    if (KnobVerbose) {
        cerr << "rtn name: " << RTN_Name(rtn) << " : " << dec << translated_rtn_num << endl;
    }

    // Close the RTN.
    RTN_Close(rtn);

    translated_rtn_num++;
}

void copy_reverted_cond_jump(INS jmp_ins)
{
    xed_decoded_inst_t* xedd = INS_XedDec(jmp_ins);

    xed_category_enum_t category_enum = xed_decoded_inst_get_category(xedd);

    if (category_enum != XED_CATEGORY_COND_BR)
        return;

    xed_iclass_enum_t iclass_enum = xed_decoded_inst_get_iclass(xedd);

    if (iclass_enum == XED_ICLASS_JRCXZ)
        return; // do not revert JRCXZ

    xed_iclass_enum_t retverted_iclass;

    switch (iclass_enum) {

    case XED_ICLASS_JB:
        retverted_iclass = XED_ICLASS_JNB;
        break;

    case XED_ICLASS_JBE:
        retverted_iclass = XED_ICLASS_JNBE;
        break;

    case XED_ICLASS_JL:
        retverted_iclass = XED_ICLASS_JNL;
        break;

    case XED_ICLASS_JLE:
        retverted_iclass = XED_ICLASS_JNLE;
        break;

    case XED_ICLASS_JNB:
        retverted_iclass = XED_ICLASS_JB;
        break;

    case XED_ICLASS_JNBE:
        retverted_iclass = XED_ICLASS_JBE;
        break;

    case XED_ICLASS_JNL:
        retverted_iclass = XED_ICLASS_JL;
        break;

    case XED_ICLASS_JNLE:
        retverted_iclass = XED_ICLASS_JLE;
        break;

    case XED_ICLASS_JNO:
        retverted_iclass = XED_ICLASS_JO;
        break;

    case XED_ICLASS_JNP:
        retverted_iclass = XED_ICLASS_JP;
        break;

    case XED_ICLASS_JNS:
        retverted_iclass = XED_ICLASS_JS;
        break;

    case XED_ICLASS_JNZ:
        retverted_iclass = XED_ICLASS_JZ;
        break;

    case XED_ICLASS_JO:
        retverted_iclass = XED_ICLASS_JNO;
        break;

    case XED_ICLASS_JP:
        retverted_iclass = XED_ICLASS_JNP;
        break;

    case XED_ICLASS_JS:
        retverted_iclass = XED_ICLASS_JNS;
        break;

    case XED_ICLASS_JZ:
        retverted_iclass = XED_ICLASS_JNZ;
        break;

    default:
        return;
        break;
    }

    // Converts the decoder request to a valid encoder request:
    xed_encoder_request_init_from_decode(xedd);

    // set the reverted opcode;
    xed_encoder_request_set_iclass(xedd, retverted_iclass);
    int rc = add_new_instr_entry(xedd, INS_Address(jmp_ins), INS_Size(jmp_ins));
    if (rc < 0) {
        cerr << "ERROR: failed during instructon translation." << endl;
        translated_rtn[translated_rtn_num].instr_map_entry = -1;
    }
}

INS get_taken_ins(INS jmp_ins)
{
    ADDRINT taken_addr = INS_DirectControlFlowTargetAddress(jmp_ins);
    INS ins = jmp_ins;
    while (INS_Address(ins) != taken_addr) {
        ins = INS_Next(ins);
    }
    return ins;
}

void create_uncond_jump(ADDRINT jmp_addr, ADDRINT target_addr)
{
    UINT8 enc_buf[max_inst_len];
    unsigned int new_size;
    xed_encoder_instruction_t enc_instr;

    unsigned int size = 5;
    int disp = (int)(target_addr - jmp_addr) - size;

    xed_inst1(&enc_instr, dstate,
        XED_ICLASS_JMP, 64,
        xed_relbr(disp, 32));

    xed_encoder_request_t enc_req;

    xed_encoder_request_zero_set_mode(&enc_req, &dstate);

    xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
    if (!convert_ok) {
        cerr << "conversion to encode request failed" << endl;
        return;
    }

    xed_error_enum_t xed_error = xed_encode(&enc_req, enc_buf, max_inst_len, &new_size);
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
        return;
    }

    xed_decoded_inst_t xedd;

    xed_decoded_inst_zero_set_mode(&xedd, &dstate);
    xed_error = xed_decode(&xedd, enc_buf, new_size);
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ERROR: xed decode failed for instr at: "
             << "0x" << hex << jmp_addr << endl;
        translated_rtn[translated_rtn_num].instr_map_entry = -1;
        return;
    }

    int rc = add_new_instr_entry(&xedd, jmp_addr, new_size);
    if (rc < 0) {
        cerr << "ERROR: failed during instructon translation." << endl;
        translated_rtn[translated_rtn_num].instr_map_entry = -1;
    }
}

void copy_not_taken_block(INS not_taken_ins, prof_rtn_stat* prof_stat, ADDRINT taken_addr, UINT32 inline_offset)
{
    ADDRINT not_taken_addr = INS_Address(not_taken_ins);

    for (INS ins = not_taken_ins; INS_Address(ins) < taken_addr; ins = INS_Next(ins)) {
        ADDRINT ins_addr = INS_Address(ins);

        xed_decoded_inst_t xedd;
        xed_error_enum_t xed_code;

        xed_decoded_inst_zero_set_mode(&xedd, &dstate);

        xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(ins_addr), max_inst_len);
        if (xed_code != XED_ERROR_NONE) {
            cerr << "ERROR: xed decode failed for instr at: "
                 << "0x" << hex << ins_addr << endl;
            translated_rtn[translated_rtn_num].instr_map_entry = -1;
            break;
        }

        if ((UINT32)(ins_addr - not_taken_addr) == inline_offset) {
            prof_rtn_stat* prof_callee_stat = rtn_map[prof_stat->inline_callee_name];
            RTN callee_rtn = RTN_FindByAddress(INS_DirectControlFlowTargetAddress(ins));
            copy_inlined_routine(callee_rtn, prof_callee_stat);
        } else {
            // Add instr into instr map:
            int rc = add_new_instr_entry(&xedd, INS_Address(ins), INS_Size(ins));
            if (rc < 0) {
                cerr << "ERROR: failed during instructon translation." << endl;
                translated_rtn[translated_rtn_num].instr_map_entry = -1;
                break;
            }
            // if (prof_stat->rtn_name == "main")
            verbose_inst(ins);
        }
    }
    create_uncond_jump(taken_addr, not_taken_addr);
}

// This is the optimized version with inline only
void optimize_translated_routine(RTN rtn, prof_rtn_stat* prof_stat)
{
    int rc;
    INS not_taken_ins;
    ADDRINT taken_addr = 0, not_taken_addr = 0;

    // Open the RTN.
    RTN_Open(rtn);

    ADDRINT rtn_addr = RTN_Address(rtn);
    UINT32 branch_offset = prof_stat->rtn_branch_offset;
    UINT32 inline_offset = prof_stat->rtn_inline_offset;
    bool inline_callee_inside_taken_block = false;

    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {

        ADDRINT ins_addr = INS_Address(ins);

        xed_decoded_inst_t xedd;
        xed_error_enum_t xed_code;

        xed_decoded_inst_zero_set_mode(&xedd, &dstate);

        xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(ins_addr), max_inst_len);
        if (xed_code != XED_ERROR_NONE) {
            cerr << "ERROR: xed decode failed for instr at: "
                 << "0x" << hex << ins_addr << endl;
            translated_rtn[translated_rtn_num].instr_map_entry = -1;
            break;
        }

        if ((prof_stat->opt_mode & OPT_REORDER) && (UINT32)(ins_addr - rtn_addr) == branch_offset) {
            INS jmp_ins = ins;
            not_taken_ins = INS_Next(ins);

            taken_addr = INS_DirectControlFlowTargetAddress(jmp_ins);
            not_taken_addr = INS_Address(not_taken_ins);

            copy_reverted_cond_jump(jmp_ins);
            verbose_inst(jmp_ins);
            // check if the inline callee needs to be inside the not taken block
            if ((UINT32)(not_taken_addr - rtn_addr) <= inline_offset && inline_offset <= (UINT32)(taken_addr - rtn_addr)) {
                // We will fix the inling later when we will copy the not taken block
                inline_callee_inside_taken_block = true;
                prof_stat->opt_mode ^= OPT_INLINE;

                inline_offset = inline_offset - (UINT32)(not_taken_addr - rtn_addr);
            }
            // We start coping the taken block from now on (We call INS_prev because of INS_next after the iteration begins)
            ins = INS_Prev(get_taken_ins(jmp_ins));
        }

        // Start Copying the inline callee
        else if ((prof_stat->opt_mode & OPT_INLINE) && (UINT32)(ins_addr - rtn_addr) == inline_offset) {
            prof_rtn_stat* prof_callee_stat = rtn_map[prof_stat->inline_callee_name];
            RTN callee_rtn = RTN_FindByAddress(INS_DirectControlFlowTargetAddress(ins));
            RTN_Close(rtn);
            copy_inlined_routine(callee_rtn, prof_callee_stat);
            RTN_Open(rtn);
        }
        // just copy it as usual
        else {
            // Add instr into instr map:
            rc = add_new_instr_entry(&xedd, INS_Address(ins), INS_Size(ins));
            if (rc < 0) {
                cerr << "ERROR: failed during instructon translation." << endl;
                translated_rtn[translated_rtn_num].instr_map_entry = -1;
                break;
            }
            // if (prof_stat->rtn_name == "main")
            verbose_inst(ins);
        }
    } // end for INS...

    if (prof_stat->opt_mode & OPT_REORDER) {
        if (!inline_callee_inside_taken_block) {
            inline_offset = UINT32_MAX;
        }
        RTN_Close(rtn);
        copy_not_taken_block(not_taken_ins, prof_stat, taken_addr, inline_offset);
        RTN_Open(rtn);
    }

    // debug print of routine name:
    if (KnobVerbose) {
        cerr << "rtn name: " << RTN_Name(rtn) << " : " << dec << translated_rtn_num << endl;
    }
    // Close the RTN.
    RTN_Close(rtn);
    translated_rtn_num++;
}

/*
 * // Example of adding a jump to a following additional nop instruction
 * // into the TC:
 * if (INS_IsNop(ins)) {
 *   // Create a temporary NOP instruction as a placeholder and then modify
 *   // it to a jump instruction.
 *   rc = add_new_instr_entry(&xedd, INS_Address(ins), INS_Size(ins));
 *   if (rc < 0) {
 *     cerr << "ERROR: failed during instructon translation." << endl;
 *     translated_rtn[translated_rtn_num].instr_map_entry = -1;
 *     break;
 *   }
 *
 *   // Create an unconditional jump instruction:
 *   xed_encoder_instruction_t  enc_instr;
 *   xed_inst1(&enc_instr, dstate,
 *             XED_ICLASS_JMP, 64,
 *             xed_relbr (0, 32));
 *
 *   xed_encoder_request_t enc_req;
 *   xed_encoder_request_zero_set_mode(&enc_req, &dstate);
 *   xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
 *   if (!convert_ok) {
 *   	cerr << "conversion to encode request failed" << endl;
 *   	return -1;
 *   }
 *
 *   unsigned int ilen = XED_MAX_INSTRUCTION_BYTES;
 *   unsigned int olen = 0;
 *   xed_error_enum_t xed_error = xed_encode(&enc_req,
 *             reinterpret_cast<UINT8*>(instr_map[num_of_instr_map_entries-1].encoded_ins), ilen, &olen);
 *   if (xed_error != XED_ERROR_NONE) {
 *   	cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
 *     return -1;
 *   }
 *   instr_map[num_of_instr_map_entries-1].orig_targ_addr = INS_Address(ins) + olen;
 *
 *   // Create another NOP instruction.
 *   rc = add_new_instr_entry(&xedd, INS_Address(ins) + olen, INS_Size(ins));
 *   if (rc < 0) {
 *     cerr << "ERROR: failed during instructon translation." << endl;
 *     translated_rtn[translated_rtn_num].instr_map_entry = -1;
 *     break;
 *   }
 * }
 */
void check_opt_mode(UINT16* opt_mode)
{
    if (*opt_mode & OPT_REORDER)
        *opt_mode ^= OPT_REORDER;
    if (*opt_mode & OPT_INLINE)
        *opt_mode ^= OPT_INLINE;
}