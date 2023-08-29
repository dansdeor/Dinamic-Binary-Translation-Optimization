#include "pin.H"
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <unordered_map>
#include <vector>

#define RESERVED_SPACE (1024)
#define OUTPUT_FILE_NAME ("count.csv")

using namespace std;

// Structure to store statistics for a routine
typedef struct rtn_stat {
    string image_name;
    ADDRINT image_addr;
    string rtn_name;
    ADDRINT rtn_addr;
    UINT64 ins_count;
    UINT64 rtn_count;
} rtn_stat_t;

// Structure to store basic block information
typedef struct bbl_info {
    string rtn_name;
    ADDRINT rtn_addr;
    ADDRINT bbl_addr;
    bool is_first;
    bool is_last;
    bool has_conditional_jump;
    ADDRINT jump_target_addr;
    string jump_target_name;
} bbl_info_t;

// Global variables
static FILE* file_ptr;
static unordered_map<ADDRINT, rtn_stat_t*> rtn_map;
static vector<rtn_stat_t*> rtn_list;
static unordered_map<ADDRINT, unordered_map<ADDRINT, UINT64>> callSiteCounts;
static unordered_map<ADDRINT, bbl_info_t*> bbl_map;
static vector<bbl_info_t*> bbl_list;

// Function to increment routine execution count
VOID rtn_count(UINT64* counter)
{
    (*counter)++;
}

// Function to increment instruction count
VOID ins_count(UINT64* counter, UINT32 c)
{
    *counter += c;
}

// Instrumentation function for tracing
VOID trace(TRACE trace, VOID* v)
{
    rtn_stat_t* rtn_stat;
    RTN rtn = TRACE_Rtn(trace);
    if (rtn == RTN_Invalid()) {
        return;
    }
    ADDRINT rtn_addr = RTN_Address(rtn);
    IMG img = IMG_FindByAddress(rtn_addr);
    if (img == IMG_Invalid()) {
        return;
    }
    if (!IMG_IsMainExecutable(img)) {
        return;
    }

    ADDRINT callSiteAddr = TRACE_Address(trace);

    auto it = rtn_map.find(rtn_addr);
    if (it == rtn_map.end()) {
        // Create a new entry for this routine in the statistics map
        rtn_stat = new rtn_stat_t({ IMG_Name(img), IMG_LowAddress(img), RTN_Name(rtn), RTN_Address(rtn), 0, 0 });
        rtn_map[rtn_addr] = rtn_stat;
        rtn_list.push_back(rtn_stat);
    } else {
        rtn_stat = it->second;
    }
	
    if (callSiteAddr == rtn_addr) {
        // Increment routine execution count at the routine's address
        TRACE_InsertCall(trace, IPOINT_BEFORE, (AFUNPTR)rtn_count, IARG_PTR, &(rtn_stat->rtn_count), IARG_END);
    } else {
        // Increment call count from this call site to this routine
        callSiteCounts[rtn_addr][callSiteAddr]++;
    }

    // Count instructions in the regular way
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)ins_count, IARG_PTR, &(rtn_stat->ins_count), IARG_UINT32, BBL_NumIns(bbl), IARG_END);
    }
}

VOID mark_basic_blocks(TRACE trace, VOID* v)
{
    RTN rtn = TRACE_Rtn(trace);
    if (rtn == RTN_Invalid()) {
        return;
    }
    ADDRINT rtn_addr = RTN_Address(rtn);
    IMG img = IMG_FindByAddress(rtn_addr);
    if (img == IMG_Invalid()) {
        return;
    }
    if (!IMG_IsMainExecutable(img)) {
        return;
    }
    RTN_Open(rtn);

    // Check if this routine is a target for analysis
    if (rtn_map.find(rtn_addr) == rtn_map.end()) {
        RTN_Close(rtn);
        return;
    }

    bool is_first = true; // Flag to track the first basic block in the routine

    // Iterate through all the basic blocks in the routine
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        ADDRINT bbl_addr = BBL_Address(bbl);
        bool has_conditional_jump = false;
        ADDRINT jump_target_addr = 0;
        string jump_target_name = "";

        // Loop over the instructions in the basic block
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            // Check if the instruction is a conditional jump
            if (INS_IsBranch(ins) && INS_HasFallThrough(ins)) {
                // Get the target address of the jump
                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
                jump_target_addr = INS_DirectBranchOrCallTargetAddress(ins);
                #pragma GCC diagnostic pop
                // Get the name of the jump target (if available)
                RTN jump_target_rtn = RTN_FindByAddress(jump_target_addr);
                if (RTN_Valid(jump_target_rtn)) {
                    jump_target_name = RTN_Name(jump_target_rtn);
                }
                has_conditional_jump = true;
                break; // Stop checking after the first conditional jump is found
            }
        }

        // Create an entry for the BBL in the bbl_map
        bbl_info_t* bbl_info = new bbl_info_t({RTN_Name(rtn), rtn_addr, bbl_addr, is_first, false, has_conditional_jump, jump_target_addr, jump_target_name});
        bbl_map[bbl_addr] = bbl_info;
        bbl_list.push_back(bbl_info);

        is_first = false; // Clear the flag for subsequent basic blocks
    }

    // Mark the last basic block as 'is_last'
    if (!bbl_list.empty()) {
        bbl_list.back()->is_last = true;
    }

    RTN_Close(rtn);
}

// Finalization function
VOID fini(INT32 code, VOID* v)
{
    // Sort routines by instruction count for output
    sort(rtn_list.begin(), rtn_list.end(), [](auto a, auto b) { return a->ins_count > b->ins_count; });
    // Output statistics to the file
    for (auto it = rtn_list.begin(); it != rtn_list.end(); ++it) {
        rtn_stat_t* stat = *it;
        fprintf(file_ptr, "%s, 0x%lx, %s, 0x%lx, %lu, %lu\n", stat->image_name.c_str(), stat->image_addr, stat->rtn_name.c_str(), stat->rtn_addr, stat->ins_count, stat->rtn_count);
    }
    fclose(file_ptr);
}

// Main function for profile collection
int main_collect_profile(int argc, char* argv[])
{
    PIN_InitSymbols();
    rtn_map.reserve(RESERVED_SPACE);
    rtn_list.reserve(RESERVED_SPACE);
    file_ptr = fopen(OUTPUT_FILE_NAME, "w");
    callSiteCounts.clear();

    // Add trace instrumentation and finalization function
    TRACE_AddInstrumentFunction(trace, 0);
	TRACE_AddInstrumentFunction(mark_basic_blocks, 0);
    PIN_AddFiniFunction(fini, 0);

    //PIN_Init(argc, argv);
    // Start profiling
    PIN_StartProgram();

    return 0;
}