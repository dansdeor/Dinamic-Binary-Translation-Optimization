#include "pin.H"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <malloc.h>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

#define TC_RTN_COUNT 10
#define CSV_LINE_ELEMENTS_NUM 9
#define RTN_NAME 5
#define RTN_INS_COUNT 7
#define PROFILING_FILE_NAME "loop-count.csv"

typedef struct rtnStat {
    string rtn_name;
    uint64_t count;
    bool in_executable;
} rtnStat_t;

bool rtnStat_comp(rtnStat_t* a, rtnStat_t* b)
{
    if (a->in_executable != b->in_executable) {
        return a->in_executable;
    }
    return a->count >= b->count;
}

map<string, rtnStat_t*> rtnMap;
vector<rtnStat_t*> rtn_vec;

set<string> tc_rtn_names;

KNOB<BOOL> prof_knob(KNOB_MODE_WRITEONCE, "pintool", "prof", "0", "run profile and print out routines information into the file count.csv");
KNOB<BOOL> opt_knob(KNOB_MODE_WRITEONCE, "pintool", "opt", "0", "run in probe mode");

int collect_profile();

int rtn_translation_inst();

void construct_profile_map(std::ifstream& profiling_file)
{
    string line;
    while (getline(profiling_file, line)) {
        size_t pos = 0;
        string rtn_name, rtn_count_str;
        uint64_t rtn_count = 0;
        for (size_t i = 0; i < CSV_LINE_ELEMENTS_NUM; i++) {
            size_t next_pos = line.find(",", pos) + 2;
            switch (i) {
            case RTN_NAME:
                rtn_name = line.substr(pos, next_pos - pos - 2);
                break;
            case RTN_INS_COUNT:
                rtn_count_str = line.substr(pos, next_pos - pos - 2);
                rtn_count = strtoull(rtn_count_str.c_str(), nullptr, 10);
                break;
            default:
                break;
            }
            pos = next_pos;
        }
        // cout << "rtn_name:" << rtn_name << ",rtn_count:" << rtn_count << endl;
        if (rtnMap.find(rtn_name) == rtnMap.end()) {
            rtnStat_t* rtnStat = new rtnStat_t();
            rtnStat->rtn_name = rtn_name;
            rtnStat->count = rtn_count;
            rtnStat->in_executable = false;
            rtnMap[rtn_name] = rtnStat;
            rtn_vec.push_back(rtnStat);
        }
    }
    profiling_file.close();
}

void get_tc_rtns()
{
    /*
    for (auto it = rtn_vec.begin(); it != rtn_vec.end();) {
        if (!(*it)->in_executable) {
            // delete (*it);
            it = rtn_vec.erase(it);
        } else {
            ++it;
        }
    }
    */
    sort(rtn_vec.begin(), rtn_vec.end(), rtnStat_comp);
    if (rtn_vec.size() < TC_RTN_COUNT) {
        return;
    }
    size_t i = 0;
    for (auto it = rtn_vec.begin(); it != rtn_vec.end() && i < TC_RTN_COUNT; ++it) {
        tc_rtn_names.insert((*it)->rtn_name);
        cout << "rtn_name: " << (*it)->rtn_name << " rtn_count: " << (*it)->count << " in_executable: " << (*it)->in_executable << endl;
        i++;
    }
}

VOID mark_executable_rtns(IMG img, VOID* v)
{
    if (!IMG_IsMainExecutable(img))
        return;
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
            const string& rtn_name = RTN_Name(rtn);
            if (rtnMap.find(rtn_name) != rtnMap.end()) {
                rtnMap[rtn_name]->in_executable = true;
            }
        }
    }
    get_tc_rtns();
}

INT32 Usage()
{
    cerr << "This tool implements homework 3" << endl;
    cerr << "-prof: run exercise 2 and print out loop trip count information into the file loop-count.csv" << endl;
    cerr << "-inst: run in probe mode and generate the binary code of the top 10 routines" << endl;
    return -1;
}

int main(int argc, char* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv)) {
        return Usage();
    }

    if (prof_knob) {
        main_collect_profile();
    } else if (opt_knob) {
        std::ifstream profiling_file(PROFILING_FILE_NAME);

        if (!profiling_file.is_open()) {
            cerr << PROFILING_FILE_NAME << " not found." << endl;
            cerr << "please run -prof before using -opt." << endl;
            return -1;
        }
        construct_profile_map(profiling_file);
        IMG_AddInstrumentFunction(mark_executable_rtns, 0);
        rtn_translation_inst();
    } else {
        return Usage();
    }
    return 0;
}
