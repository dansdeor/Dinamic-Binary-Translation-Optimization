#include "pin.H"
#include "prof_rtn_stat.h"
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <unordered_map>
#include <vector>

#define RESERVED_SPACE (1024)

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::unordered_map;
using std::vector;

#define BRANCH_THRESHOLD 0.8
#define GET_BRANCH_RATIO (X) (((double)(X).branch_taken)/((double)(X).branch_count)))

#define RET_COUNT 1
#define CALL_COUNT 4

enum inline_valid {
    VALID, // Function valid for Inlining
    LAST_INS_NOT_RET, // Last instruction is not ret
    MORE_THAN_ONE_RET, // More than 1 ret instructions
    INDIRECT_JUMPS_CALLS, // Checks for indirect branches in the routine
    OUTSIDE_JUMPS, // Checks for jumps outside the routine
    WRONG_MEMORY_OPERAND_OFFSET, // Check that RSP has no negative displacement and RBP has no positive displacement
    WRONG_STACK_DISP, // ?
    MULTIPLE_CALLS // Multiple calls instruction
};

// Structure to find potential reordering candidates
struct branch_stat {
    ADDRINT branch_addr;
    UINT64 branch_taken; // the number of times we took the jump
    UINT64 branch_count; // how many times we got to that branch

    branch_stat(ADDRINT branch_addr)
        : branch_addr(branch_addr)
        , branch_taken(0)
        , branch_count(0)
    {
    }
};

// Structure to find potential inline candidates
struct call_stat {
    ADDRINT callee_addr; // callee address
    ADDRINT inst_call_addr; // instruction call address
    UINT64 call_count; // how many times we got to the function

    call_stat(ADDRINT callee_addr, ADDRINT inst_call_addr)
        : callee_addr(callee_addr)
        , inst_call_addr(inst_call_addr)
        , call_count(0)
    {
    }
};

// Structure to store statistics for a routine
struct rtn_stat {
    // string image_name;
    // ADDRINT image_addr;
    string rtn_name;
    ADDRINT rtn_addr;
    UINT64 rtn_count; // we will use this to figure out the percentage of calls the potential function
    UINT64 ins_count; // used as a heat score for all of our routines
    bool inline_valid;
    vector<branch_stat*> branches; // vector to record branches behavior per routine
    vector<call_stat*> rtn_calls; // map of call instruction metadata

    rtn_stat(string rtn_name, ADDRINT rtn_addr)
        : rtn_name(rtn_name)
        , rtn_addr(rtn_addr)
        , rtn_count(0)
        , ins_count(0)
        , inline_valid(true)
        , branches()
        , rtn_calls()
    {
    }
};

// Global variables
static FILE* file_ptr;
static unordered_map<ADDRINT, rtn_stat*> rtn_map;
// static vector<rtn_stat*> rtn_list;
// static unordered_map<ADDRINT, unordered_map<ADDRINT, UINT64>> callSiteCounts;

// Function to increment routine execution count
VOID ins_count(UINT64* counter)
{
    (*counter)++;
}

// Function to increment instruction count
VOID bbl_count(UINT64* counter, UINT32 c)
{
    *counter += c;
}

VOID branch_taken_count(branch_stat* branch, BOOL taken)
{
    if (taken) {
        branch->branch_taken++;
    }
    branch->branch_count++;
}

rtn_stat* map_get_rtn_stat(RTN rtn)
{
    rtn_stat* stat;
    if (rtn == RTN_Invalid()) {
        return nullptr;
    }
    ADDRINT rtn_addr = RTN_Address(rtn);
    auto it = rtn_map.find(rtn_addr);
    if (it == rtn_map.end()) {
        IMG img = IMG_FindByAddress(rtn_addr);
        if (img == IMG_Invalid()) {
            return nullptr;
        }
        if (!IMG_IsMainExecutable(img)) {
            return nullptr;
        }
        // Create a new entry for this routine in the statistics map
        // stat = new rtn_stat({ IMG_Name(img), IMG_LowAddress(img), RTN_Name(rtn), RTN_Address(rtn), 0, 0 });
        stat = new rtn_stat(RTN_Name(rtn), RTN_Address(rtn));
        if (stat == nullptr) {
            return nullptr;
        }
        rtn_map[rtn_addr] = stat;
        // rtn_list.push_back(stat);
    } else {
        stat = it->second;
    }
    return stat;
}

branch_stat* set_new_branch_stat(rtn_stat* rtn_stat, ADDRINT branch_addr)
{
    branch_stat* branch = new branch_stat(branch_addr);
    if (branch == nullptr) {
        return nullptr;
    }
    rtn_stat->branches.push_back(branch);
    return branch;
}

call_stat* set_new_call_stat(rtn_stat* rtn_stat, ADDRINT callee_addr, ADDRINT inst_call_addr)
{
    call_stat* call = new call_stat(callee_addr, inst_call_addr);
    if (call == nullptr) {
        return nullptr;
    }
    rtn_stat->rtn_calls.push_back(call);
    return call;
}

// Function to check for multiple return instructions
bool more_than_one_ret(INS ins, unsigned int& ret_count)
{
    if (INS_IsRet(ins)) {
        ret_count++;
    }
    return (ret_count > RET_COUNT);
}

// Function to check for multiple call instructions
bool has_multiple_calls(INS ins, unsigned int& call_count)
{
    if (INS_IsCall(ins)) {
        call_count++;
    }
    return (call_count > CALL_COUNT);
}

// Function to check that instructions like 'call' or 'jmp' are indirect
bool is_indirect_control_flow(INS ins)
{
    return (INS_IsIndirectControlFlow(ins) && !INS_IsRet(ins));
}

// Function to check for instructions like 'call' or 'jmp' outside the routine
bool contains_outside_control_flow(INS ins, ADDRINT start_addr, ADDRINT end_addr)
{
    if (INS_IsDirectBranch(ins)) {
        ADDRINT targetAddr = INS_DirectControlFlowTargetAddress(ins);
        if (targetAddr < start_addr || targetAddr > end_addr) {
            return true;
        }
    }
    return false;
}

// Function to check for problematic memory operand offsets
bool has_invalid_memory_operand_offset(INS ins)
{
    UINT32 operand_count = INS_MemoryOperandCount(ins);
    for (UINT32 operand_index = 0; operand_index < operand_count; operand_index++) {
        if (INS_MemoryOperandIsRead(ins, operand_index) || INS_MemoryOperandIsWritten(ins, operand_index)) {
            REG base_reg = INS_OperandMemoryBaseReg(ins, operand_index);
            ADDRDELTA displacement = INS_OperandMemoryDisplacement(ins, operand_index);
            if ((base_reg == REG_RSP && displacement < 0) || (base_reg == REG_RBP && displacement > 0)) {
                return true;
            }
        }
    }
    return false;
}

inline_valid routine_inline_valid_result(RTN rtn)
{
    unsigned int num_of_rets = 0;
    unsigned int num_of_calls = 0;
    ADDRINT start_addr = RTN_Address(rtn);
    INS ins_tail = RTN_InsTail(rtn);
    if (!INS_IsRet(ins_tail)) {
        cerr << RTN_Name(rtn) << ": LAST_INS_NOT_RET" << endl;
        return LAST_INS_NOT_RET;
    }
    ADDRINT end_addr = INS_Address(ins_tail);

    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
        if (more_than_one_ret(ins, num_of_rets)) {
            cerr << RTN_Name(rtn) << ": MORE_THAN_ONE_RET" << endl;
            return MORE_THAN_ONE_RET;
        }
        if (is_indirect_control_flow(ins)) {
            cerr << RTN_Name(rtn) << ": INDIRECT_JUMPS_CALLS" << endl;
            // cerr << std::hex << (INS_Address(ins) - start_addr) << endl;
            return INDIRECT_JUMPS_CALLS;
        }
        if (contains_outside_control_flow(ins, start_addr, end_addr)) {
            cerr << RTN_Name(rtn) << ": OUTSIDE_JUMPS" << endl;
            return OUTSIDE_JUMPS;
        }
        if (has_invalid_memory_operand_offset(ins)) {
            cerr << RTN_Name(rtn) << ": WRONG_MEMORY_OPERAND_OFFSET" << endl;
            return WRONG_MEMORY_OPERAND_OFFSET;
        }
        if (has_multiple_calls(ins, num_of_calls)) {
            cerr << RTN_Name(rtn) << ": MULTIPLE_CALLS" << endl;
            return MULTIPLE_CALLS;
        }
    }
    return VALID;
}

VOID routine(RTN rtn, VOID* v)
{
    rtn_stat* stat = map_get_rtn_stat(rtn);
    if (stat == nullptr) {
        return;
    }
    RTN_Open(rtn);
    stat->inline_valid = (routine_inline_valid_result(rtn) == VALID);
    // Increment routine execution count at the routine's address
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)ins_count, IARG_PTR, &(stat->rtn_count), IARG_END);
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
        xed_category_enum_t ins_category = (xed_category_enum_t)INS_Category(ins);

        if (ins_category == XED_CATEGORY_COND_BR) {
            branch_stat* branch = set_new_branch_stat(stat, INS_Address(ins));
            if (branch != nullptr) {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)branch_taken_count, IARG_PTR, branch, IARG_BRANCH_TAKEN, IARG_END);
            }
        }
        if (ins_category == XED_CATEGORY_CALL && INS_IsDirectControlFlow(ins)) {
            call_stat* call = set_new_call_stat(stat, INS_DirectControlFlowTargetAddress(ins), INS_Address(ins));
            if (call != nullptr) {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)ins_count, IARG_PTR, &(call->call_count), IARG_END);
            }
        }
    }
    RTN_Close(rtn);
}

// Instrumentation function for tracing
VOID trace(TRACE trace, VOID* v)
{
    RTN rtn = TRACE_Rtn(trace);
    rtn_stat* stat = map_get_rtn_stat(rtn);
    if (stat == nullptr) {
        return;
    }
    // Count instructions in the regular way
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)bbl_count, IARG_PTR, &(stat->ins_count), IARG_UINT32, BBL_NumIns(bbl), IARG_END);
    }
}

UINT32 get_reorder_offset(rtn_stat* stat)
{
    UINT64 max_count = 0;
    ADDRINT reorder_branch_addr = 0;

    for (auto it = stat->branches.begin(); it != stat->branches.end(); ++it) {
        if ((*it)->branch_count == 0) {
            continue;
        }
        if (((double)(*it)->branch_taken) / (*it)->branch_count < BRANCH_THRESHOLD) {
            continue;
        }
        if (max_count < (*it)->branch_count) {
            reorder_branch_addr = (*it)->branch_addr;
            max_count = (*it)->branch_count;
        }
    }
    if (reorder_branch_addr == 0) {
        return 0;
    }
    return reorder_branch_addr - stat->rtn_addr;
}

UINT32 get_inline_offset(rtn_stat* stat, string* callee_name)
{
    UINT64 max_count = 0;
    ADDRINT callee_addr = 0;
    ADDRINT inline_call_addr = 0;

    for (auto it = stat->rtn_calls.begin(); it != stat->rtn_calls.end(); ++it) {
        auto callee_it = rtn_map.find((*it)->callee_addr);
        if (callee_it == rtn_map.end()) {
            continue;
        }
        rtn_stat* callee_stat = callee_it->second;
        if (!callee_stat->inline_valid) {
            continue;
        }
        if (max_count < (*it)->call_count) {
            inline_call_addr = (*it)->inst_call_addr;
            callee_addr = (*it)->callee_addr;
            max_count = (*it)->call_count;
        }
    }
    if (inline_call_addr == 0) {
        return 0;
    }
    if (callee_name) {
        *callee_name = rtn_map[callee_addr]->rtn_name;
    }
    return inline_call_addr - stat->rtn_addr;
}

// Finalization function
VOID fini(INT32 code, VOID* v)
{
    file_ptr = fopen(OUTPUT_FILE_NAME, "w");
    if (file_ptr == NULL) {
        cerr << "Error: opening a file" << endl;
        return;
    }
    // Output statistics to the file
    for (auto it = rtn_map.begin(); it != rtn_map.end(); ++it) {
        rtn_stat* stat = it->second;
        UINT16 opt_mode = 0;
        UINT32 rtn_inline_offset = 0;
        UINT32 rtn_branch_offset = 0;
        string inline_candidate_name = "";

        rtn_inline_offset = get_inline_offset(stat, &inline_candidate_name);
        rtn_branch_offset = get_reorder_offset(stat);

        if (rtn_inline_offset) {
            opt_mode |= OPT_INLINE;
        }
        if (rtn_branch_offset) {
            opt_mode |= OPT_REORDER;
        }
        // Please check prof_rtn_stat struct
        fprintf(file_ptr, "%s,0x%lx,%lu,%hhu,%u,%u,%s\n",
            stat->rtn_name.c_str(),
            stat->rtn_addr,
            stat->ins_count,
            opt_mode,
            rtn_branch_offset,
            rtn_inline_offset,
            inline_candidate_name.c_str());
    }
    fclose(file_ptr);
}

// Main function
int collect_profile_main(int argc, char* argv[])
{
    // PIN_InitSymbols();
    rtn_map.reserve(RESERVED_SPACE);
    // rtn_list.reserve(RESERVED_SPACE);
    // Add trace instrumentation and finalization function
    TRACE_AddInstrumentFunction(trace, 0);
    RTN_AddInstrumentFunction(routine, 0);
    PIN_AddFiniFunction(fini, 0);
    // Initialize PIN
    // PIN_Init(argc, argv);
    // Start the program
    PIN_StartProgram();
    return 0;
}
