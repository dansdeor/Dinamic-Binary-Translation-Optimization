#include "project.h"
#include "pin.H"
#include "prof_rtn_stat.h"
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <string.h>
#include <unordered_map>

using std::cerr;
using std::cout;
using std::endl;
using std::getline;
using std::multiset;
using std::string;
using std::unordered_map;

unordered_map<string, prof_rtn_stat*> rtn_map;
multiset<prof_rtn_stat*, rtn_stat_comp> rtn_heat_set;

KNOB<BOOL> prof_knob(KNOB_MODE_WRITEONCE, "pintool", "prof", "0", "run profiling and save candidates for reordering and inlining optimizations to the file profile_stat.csv");
KNOB<BOOL> opt_knob(KNOB_MODE_WRITEONCE, "pintool", "opt", "0", "run in probe mode and generate the binary code for the optimized binary");
void check_opt_mode(UINT16* opt_mode);

void construct_profile_map(std::ifstream& profiling_file)
{
    if (!profiling_file.is_open()) {
        return;
    }
    string line, rtn_addr, heat, opt_mode, rtn_branch_offset, rtn_inline_offset;
    while (getline(profiling_file, line)) {
        std::stringstream s_stream(line);
        prof_rtn_stat* prof_stat = new prof_rtn_stat();
        if (prof_stat == nullptr) {
            break;
        }
        getline(s_stream, prof_stat->rtn_name, ',');
        getline(s_stream, rtn_addr, ',');
        prof_stat->rtn_addr = std::stoull(rtn_addr, nullptr, 16);
        getline(s_stream, heat, ',');
        prof_stat->heat = std::stoull(heat);
        getline(s_stream, opt_mode, ',');
        prof_stat->opt_mode = std::stoul(opt_mode);
        check_opt_mode(&(prof_stat->opt_mode));
        getline(s_stream, rtn_branch_offset, ',');
        prof_stat->rtn_branch_offset = std::stoul(rtn_branch_offset);
        getline(s_stream, rtn_inline_offset, ',');
        prof_stat->rtn_inline_offset = std::stoul(rtn_inline_offset);
        getline(s_stream, prof_stat->inline_callee_name);
        rtn_map.insert({ prof_stat->rtn_name, prof_stat });
        rtn_heat_set.insert(prof_stat);
    }
    for (auto it = rtn_heat_set.begin(); it != rtn_heat_set.end(); ++it) {
        cout << "name: " << (*it)->rtn_name << " addr: 0x" << std::hex << (*it)->rtn_addr << std::dec
             << " heat: " << (*it)->heat << " opt_mode: " << (*it)->opt_mode << " branch_offset: "
             << (*it)->rtn_branch_offset << " inline_offset: " << (*it)->rtn_inline_offset
             << " inline_callee_name: " << (*it)->inline_callee_name << endl;
    }
}

/*
void get_tc_rtns()
{
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
*/
INT32 Usage()
{
    cerr << "This tool implements final project" << endl;
    cerr << "-prof: run profiling and save candidates for reordering and inlining optimizations to the file profile_stat.csv" << endl;
    cerr << "-opt: run in probe mode and generate the binary code for the optimized binary" << endl;
    return -1;
}

int rtn_translation_main(int argc, char* argv[]);
int collect_profile_main(int argc, char* argv[]);

int main(int argc, char* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv)) {
        return Usage();
    }

    if (prof_knob) {
        collect_profile_main(argc, argv);
    } else if (opt_knob) {
        std::ifstream profiling_file(OUTPUT_FILE_NAME);

        if (!profiling_file.is_open()) {
            cerr << OUTPUT_FILE_NAME << " not found." << endl;
            cerr << "please run -prof before using -opt." << endl;
            return -1;
        }
        construct_profile_map(profiling_file);
        profiling_file.close();
        // IMG_AddInstrumentFunction(mark_executable_rtns, 0);
        rtn_translation_main(argc, argv);
    } else {
        return Usage();
    }
    return 0;
}
