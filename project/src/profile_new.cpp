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

bool should_inline_routine(RTN rtn)
{
    // Count the number of call sites in the routine
    int numCallSites = 0;
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
        if (INS_IsCall(ins)) {
            numCallSites++;
        }
    }

    // If the routine has exactly one call site and that call site is hot, inline it
    return numCallSites == 1;

}

// Callback to insert the inlined instruction at the right position
VOID InsertInlinedInstruction(BBL targetBbl, INS newIns) {
    INS tailIns = BBL_InsTail(targetBbl); // Get the tail instruction of the target basic block
    BBL_InsertCall(targetBbl, IPOINT_BEFORE, (AFUNPTR)INS_Delete, IARG_PTR, tailIns, IARG_END); // Remove the tail instruction
    BBL_InsertIfCall(targetBbl, IPOINT_BEFORE, (AFUNPTR)INS_InsertCall, IARG_PTR, targetBbl,
                     IARG_ADDRINT, IPOINT_AFTER, IARG_PTR, tailIns, IARG_PTR, newIns, IARG_END); // Insert newIns at the tail
}

// A function to inline another function at the given trace
VOID InlineFunction(TRACE trace, RTN targetRtn) {
    // Iterate through all BBLs in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        // Get the list of instructions in the BBL
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            // Check if the instruction is a call instruction
            if (INS_IsCall(ins)) {
                // Iterate through the inlined instructions from the target function
                for (INS targetIns = RTN_InsHead(targetRtn); INS_Valid(targetIns); targetIns = INS_Next(targetIns)) {
                    // Insert the inlined instruction directly into the current BBL
                    BBL_InsertCall(bbl, IPOINT_BEFORE, AFUNPTR(InsertInlinedInstruction), IARG_PTR, targetIns, IARG_END);
                }
                INS_Delete(ins); // Remove the original call instruction
                break; // No need to process more instructions in this BBL
            }
        }
    }
}


// Function to reorder instructions within a basic block
VOID ReorderInstructions(BBL bbl) {
    vector<INS> insVector;

    // Collect the instructions in the original order
    for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
        insVector.push_back(ins);
    }

    // Delete the original instructions
    for (const INS& ins : insVector) {
        INS_Delete(ins);
    }

    // Insert the instructions in the new order
    for (const INS& ins : insVector) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)INS_InsertPredicatedCall,
                       IARG_PTR, ins, IARG_UINT32, IPOINT_AFTER, IARG_END);
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
		
		// Perform code reordering for basic blocks with inlined functions
        for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
            ReorderInstructions(bbl);
        }
    } else {
        // Count instructions in the regular way
        for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
            BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)ins_count, IARG_PTR, &(rtn_stat->ins_count), IARG_UINT32, BBL_NumIns(bbl), IARG_END);
        }
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
int main_collect_profile()
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
