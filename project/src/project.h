#ifndef PROJECT_HEADER
#define PROJECT_HEADER
#include "prof_rtn_stat.h"
#include <set>
#include <string>
#include <unordered_map>

/* ============================================================= */
/* Project data structures                                       */
/* ============================================================= */

struct rtn_stat_comp {
    bool operator()(const prof_rtn_stat* a, const prof_rtn_stat* b) const
    {
        return a->heat > b->heat;
    }
};

extern std::unordered_map<std::string, prof_rtn_stat*> rtn_map;
extern std::multiset<prof_rtn_stat*, rtn_stat_comp> rtn_heat_set;

#endif