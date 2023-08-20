#include "pin.H"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <iostream>
#include <iomanip>
#include <set>
#include <algorithm>
#include <map>
#include <unistd.h>
#include <fstream>
#include <string>
#include <vector>

using namespace std;

/*Global variables:*/
// Struct with constructor
struct LOOP_INFO {
    UINT64 CountSeen;
    ADDRINT targetAddress;
    ADDRINT RTN_Address;
    string RTN_Name;
    UINT64 CountLoopInvoked;
    UINT64 DiffCount;
    UINT64 PREVIOUS_iterations_counter;
    UINT64 CURRENT_iterations_counter;
    bool previously_taken;

    LOOP_INFO(UINT64 count_seen, ADDRINT target_address, ADDRINT rtn_address,
               const string& rtn_name, UINT64 count_loop_invoked, UINT64 diff_count,
               UINT64 prev_iterations_counter, UINT64 curr_iterations_counter,
               bool prev_taken)
        : CountSeen(count_seen), targetAddress(target_address),
          RTN_Address(rtn_address), RTN_Name(rtn_name),
          CountLoopInvoked(count_loop_invoked), DiffCount(diff_count),
          PREVIOUS_iterations_counter(prev_iterations_counter),
          CURRENT_iterations_counter(curr_iterations_counter),
          previously_taken(prev_taken) {}

    LOOP_INFO() : LOOP_INFO(0, 0, 0, "", 0, 0, 0, 0, false) {}
};

map<ADDRINT,UINT64> RTN_MAP; //Key is RTN_Address and Value is RTN_COUNTER
map<ADDRINT, UINT64> RTN_COUNT_MAP; //Key is RTN_Address and Value is RTN_COUNTER

map<ADDRINT,LOOP_INFO> LOOP_MAP;;//KEY is ins address, VAL=LOOP_INFO[KEY]
map<ADDRINT,UINT64> MAP_FOR_SORTING;

/*Inserts a new loop to the map and updates it*/

void inc_loop(INT32 taken, ADDRINT ins_addr) {

    auto& loop_info = LOOP_MAP[ins_addr];

    loop_info.CountSeen++;
    MAP_FOR_SORTING[ins_addr]++; 

    if (taken) {
        loop_info.CURRENT_iterations_counter++;
        loop_info.previously_taken = true;
    } else {
        loop_info.CountLoopInvoked++;
        loop_info.previously_taken = false;

        if (loop_info.CountLoopInvoked > 0 &&
            (loop_info.PREVIOUS_iterations_counter != loop_info.CURRENT_iterations_counter)) {
            loop_info.DiffCount++;
        }

        loop_info.PREVIOUS_iterations_counter = loop_info.CURRENT_iterations_counter;
        loop_info.CURRENT_iterations_counter = 0;
    }
}

// Function to count the number of instructions executed
VOID docount(ADDRINT rtn_addr) {
    RTN_MAP[rtn_addr]++;
}

// Function to count the number of times a routine is called
VOID docallcount(ADDRINT rtn_addr) {
    RTN_COUNT_MAP[rtn_addr]++;
}

VOID Trace (TRACE trace, void *v) {
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        INS ins = BBL_InsTail(bbl);
        if (!INS_Valid(ins))
            continue;
        ADDRINT ins_addr = INS_Address(ins);
        RTN ins_rtn = RTN_FindByAddress(ins_addr);
        if (!RTN_Valid(ins_rtn))
            continue;
        ADDRINT rtn_addr = RTN_Address(ins_rtn);
        // RTN_COUNT_MAP[rtn_addr]++; // Counts the number of routines 
        string rtn_name = RTN_FindNameByAddress(ins_addr);
        if (INS_IsDirectBranch(ins) && INS_IsControlFlow(ins)) { 
            ADDRINT speculative_target_address = INS_DirectControlFlowTargetAddress(ins);
            if (speculative_target_address < ins_addr) {
                auto iter = LOOP_MAP.find(ins_addr);
				if (iter == LOOP_MAP.end()) {
					LOOP_MAP[ins_addr] = LOOP_INFO(0, speculative_target_address, rtn_addr, rtn_name, 0, 0, 0, 0, false);
				}
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) inc_loop, IARG_BRANCH_TAKEN, IARG_ADDRINT, ins_addr,
                               IARG_END);
            }
		}
    }
}

VOID Instruction (INS ins, void *v)
{
    RTN ins_rtn = INS_Rtn(ins);
    ADDRINT addr = INS_Address(ins);//DEBUG
    if (ins_rtn == RTN_Invalid())
        return;
    ADDRINT rtn_addr = RTN_Address(ins_rtn);
    if (RTN_MAP.find(rtn_addr) == RTN_MAP.end()) {
        RTN_MAP[rtn_addr] = 0;
    }
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_ADDRINT, rtn_addr, IARG_END);
    if (addr == rtn_addr) { //DEBUG
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docallcount, IARG_ADDRINT, rtn_addr, IARG_END); //DEBUG
	} //DEBUG 

}


VOID Fini(int n, void *v) {
    /*sorting the map by Countseen*/
    multimap <UINT64, LOOP_INFO> OUT_MAP;
    for (map<ADDRINT, LOOP_INFO>::iterator it = LOOP_MAP.begin(); it != LOOP_MAP.end(); ++it) { 
            OUT_MAP.insert(pair<UINT64, LOOP_INFO>((it->second).CountSeen, (it->second)));
        }
        /*opens a file:*/
	std::ofstream file_pointer("loop-count.csv");
	if (!file_pointer) {
		std::cerr << "Can't open data file!" << std::endl;
	}
	double get_mean = 0;
	multimap<UINT64, LOOP_INFO>::iterator it = OUT_MAP.end();
	it--;
	for ( ; it != OUT_MAP.begin(); --it) {
		//ADDRINT iterator_key =it->first;
		LOOP_INFO iterator_value = it->second; 
		if (iterator_value.CountSeen == 0) {
			continue;
		}
		if (iterator_value.CountLoopInvoked > 0) {
	    get_mean = (double) (iterator_value.CountSeen) /( iterator_value.CountLoopInvoked);
		}
		iterator_value.DiffCount = iterator_value.DiffCount - 1;
		// Printing!!
		file_pointer << "0x" << hex << iterator_value.targetAddress << ", "
				<< dec << iterator_value.CountSeen<< ", "
				<< iterator_value.CountLoopInvoked<< ", "
				<< get_mean << ", "<< dec <<iterator_value.DiffCount<< ", "<<iterator_value.RTN_Name<< ", "<< "0x" << hex << iterator_value.RTN_Address << ", "
				<< dec <<RTN_MAP[iterator_value.RTN_Address]<< ", " << dec << RTN_COUNT_MAP[iterator_value.RTN_Address] << endl;
	}
    file_pointer.close();
}

int ex2_main_prof()
{
    TRACE_AddInstrumentFunction(Trace, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();
    return 0;
}
