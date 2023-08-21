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

using std::cerr;
using std::endl;
using std::pair;
using std::sort;
using std::string;
using std::unordered_map;
using std::vector;

typedef struct rtn_stat {
    string image_name;
    ADDRINT image_addr;
    string rtn_name;
    ADDRINT rtn_addr;
    UINT64 ins_count;
    UINT64 rtn_count;
} rtn_stat_t;

static FILE* file_ptr;
static unordered_map<ADDRINT, rtn_stat_t*> rtn_map;
static vector<rtn_stat_t*> rtn_list;

VOID rtn_count(UINT64* counter)
{
    (*counter)++;
}

VOID ins_count(UINT64* counter, UINT32 c)
{
    *counter += c;
}

// A function to inline another function at the given trace
VOID InlineFunction(TRACE trace, RTN targetRtn)
{
    // Get the address of the function to inline
    ADDRINT targetAddr = RTN_Address(targetRtn);

    // Iterate through all BBLs in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        // Get the list of instructions in the BBL
        INS ins = BBL_InsHead(bbl);
        while (INS_Valid(ins)) {
            // Check if the instruction calls the target function
            if (INS_IsCall(ins) && INS_DirectBranchOrCallTargetAddress(ins) == targetAddr) {
                // Get the inlined instructions from the target function
                for (INS targetIns = RTN_InsHead(targetRtn); INS_Valid(targetIns); targetIns = INS_Next(targetIns)) {
                    // Clone the instruction and insert it in the current position
                    INS newIns = INS_Clone(targetIns);
                    BBL_InsertInstrumentCall(bbl, INS_Next(ins), AFUNPTR(INS_InsertDirectJump), IARG_INST_PTR, IARG_BRANCH_TARGET_ADDR, IARG_END);// IARG_RETURN_IP
                }

                // Remove the original call instruction
                INS_Delete(ins);
                break; // No need to process more instructions in this BBL
            }
            ins = INS_Next(ins);
        }
    }
}


/*
VOID RtnCall(UINT64* counter)
{
    (*counter)++;
}

// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID *v)
{
    if (!RTN_IsDynamic(rtn))
    {
        return;
    }
    RTN_Open(rtn);
 
    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)RtnCall, IARG_RETURN_IP, RTN_Name(rtn).c_str(), IARG_END);
 
    RTN_Close(rtn);
}
*/
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
	if (!IMG_IsMainExecutable(img))
		return;
	
    auto it = rtn_map.find(rtn_addr);
    if (it == rtn_map.end()) {
        rtn_stat = new rtn_stat_t({ IMG_Name(img), IMG_LowAddress(img), RTN_Name(rtn), RTN_Address(rtn), 0, 0 });
        rtn_map[rtn_addr] = rtn_stat;
        rtn_list.push_back(rtn_stat);
    } else {
        rtn_stat = it->second;
    }

    if (rtn_addr == TRACE_Address(trace)) {
        TRACE_InsertCall(trace, IPOINT_BEFORE, (AFUNPTR)rtn_count, IARG_PTR, &(rtn_stat->rtn_count), IARG_END);
    }
	
    // Check if the current routine should be inlined
    if (should_inline_routine(rtn)) {
        // Perform function inlining for the current trace
        InlineFunction(trace, rtn);
    } else {
        // Count instructions in the regular way
        for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
            BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)ins_count, IARG_PTR, &(rtn_stat->ins_count), IARG_UINT32, BBL_NumIns(bbl), IARG_END);
        }
}

VOID fini(INT32 code, VOID* v)
{
    sort(rtn_list.begin(), rtn_list.end(), [](auto a, auto b) { return a->ins_count > b->ins_count; });
    for (auto it = rtn_list.begin(); it != rtn_list.end(); ++it) {
        rtn_stat_t* stat = *it;
        fprintf(file_ptr, "%s, 0x%lx, %s, 0x%lx, %lu, %lu\n", stat->image_name.c_str(), stat->image_addr, stat->rtn_name.c_str(), stat->rtn_addr, stat->ins_count, stat->rtn_count);
    }
    fclose(file_ptr);
}
/*
INT32 Usage()
{
    cerr << "This pintool counts the number of times a routine is executed" << endl;
    cerr << "and the number of instructions executed in a routine and save the statistics to " << OUTPUT_FILE_NAME << endl;
    return -1;
}
*/
int collect_profile()
{
    //PIN_InitSymbols();
    rtn_map.reserve(RESERVED_SPACE);
    rtn_list.reserve(RESERVED_SPACE);
    file_ptr = fopen(OUTPUT_FILE_NAME, "w");
/*
    if (PIN_Init(argc, argv)) {
        return Usage();
    }
*/
    // Register Routine to be called to instrument rtn
    //RTN_AddInstrumentFunction(Routine, 0);
    TRACE_AddInstrumentFunction(trace, 0);
    PIN_AddFiniFunction(fini, 0);
    PIN_StartProgram();
    return 0;
}
