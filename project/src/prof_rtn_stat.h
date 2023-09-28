#ifndef PROF_RTN_STAT
#define PROF_RTN_STAT
#include "pin.H"
#include <string>

#define OUTPUT_FILE_NAME ("profile_stat.csv")

#define OPT_INLINE 0b01
#define OPT_REORDER 0b10
#define OPT_ALL (OPT_INLINE | OPT_REORDER)

struct prof_rtn_stat {
    std::string rtn_name;
    ADDRINT rtn_addr;
    UINT64 heat;
    UINT16 opt_mode;
    UINT32 rtn_branch_offset;
    UINT32 rtn_inline_offset;
    std::string inline_callee_name;
};

#endif
