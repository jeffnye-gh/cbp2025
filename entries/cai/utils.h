#include "knobs.h"
#include <set>

#ifndef __UTIL_H__
#define __UTIL_H__

struct BranchStats
{
    uint64_t taken;
    uint64_t non_taken;
    uint64_t total_mispred;
    uint64_t total_wrong_path;
    uint64_t pred_with_tage;
    uint64_t pred_with_loop; // aka loop override TAGE
    uint64_t pred_with_sc;   // aka SC override TAGE/loop
    std::pair<uint64_t, uint64_t> loop_disagree_and_override;

    // SC override specific profiling counters
    // all the cases where sc overrides should add up to pred_with_sc
    uint64_t sc_tagel_conflict; // number of times sc disagrees with tage/loop

#ifdef BANK_USAGE
    std::unordered_map<uint16_t, uint64_t> bank_usage; // for TAGE bank distribution
#endif

    // std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> latencies; // [# of cycles, [# of tot occur, # of misp]]

    uint64_t pred_long_is_long;
    uint64_t pred_long_is_short;
    uint64_t pred_long_is_med;
    uint64_t pred_short_is_long;
    uint64_t pred_short_is_short;
    uint64_t pred_short_is_med;

#ifdef RAS_STUDY
    std::unordered_map<RAS1, std::unordered_map<uint64_t, uint64_t>> ras1_wps; // [first address on RAS, [# of cycles, freq]]
    std::unordered_map<RAS2, std::unordered_map<uint64_t, uint64_t>> ras2_wps; // [first and second address on RAS, [# of cycles, freq]]
    std::unordered_map<RAS3, std::unordered_map<uint64_t, uint64_t>> ras3_wps; // [first and second and third address on RAS, [# of cycles, freq]]
#endif

    BranchStats() : taken(0), non_taken(0), total_mispred(0), total_wrong_path(0), pred_with_tage(0), pred_with_loop(0), pred_with_sc(0),
                    sc_tagel_conflict(0), pred_long_is_long(0), pred_long_is_short(0), pred_long_is_med(0), pred_short_is_long(0), pred_short_is_short(0), pred_short_is_med(0) {}
};

struct InstanceStats
{
    uint64_t pc;
    uint64_t taken;
    uint64_t non_taken;
    uint64_t total_mispred;
    uint64_t total_wrong_path;
    // std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> latencies; // [# of cycles, [# of tot occur, # of misp]]

    uint64_t cyc_alloc;
    uint64_t cyc_dealloc;

    uint tag;
    int hist;

    bool placeholder;
    std::set<uint64_t> init_pcs;
    std::set<uint64_t> init_tags;

    InstanceStats() : pc(0), taken(0), non_taken(0), total_mispred(0), total_wrong_path(0), cyc_alloc(0), cyc_dealloc(0), tag(0), hist(-1), placeholder(false) {}
};

namespace std
{
    template <>
    struct hash<std::pair<int, int>>
    {
        std::size_t operator()(const std::pair<int, int> &p) const noexcept
        {
            // Use a common hash-combine trick
            return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
        }
    };
}

#endif
