#ifndef IDIOM_UOP_TRACKER_H_
#define IDIOM_UOP_TRACKER_H_

#include <array>
#include <vector>

#include "../lib/sim_common_structs.h"

using uop_id_t = uint64_t;
using pc_t = std::pair<uint64_t, uint8_t>;
static constexpr pc_t null_pc = std::make_pair(uint64_t(0), uint8_t(0));

static constexpr uint64_t uop_id_t_size_in_bits = 11;
/* power of 2 sized maximum in-flight amount of uops we allow */
static constexpr uop_id_t uop_max = 1 << uop_id_t_size_in_bits;

struct uop_execute_info_t {
    uop_id_t id; /* sequential uop id (NOT seq_no) */
    std::vector<uint64_t> src_reg_values;
    std::optional<uint64_t> dst_reg_value;
    std::optional<bool> was_taken;
};

class uop_tracker_t {
public:
    uop_id_t next_id();
    uop_id_t begin() { return next_id() > uop_max ? next_id() - uop_max : 0; }
    uop_id_t end() { return next_id(); }
    uop_id_t lookup_id(uint64_t seq_no, uint8_t piece);
    uop_id_t allocate(uint64_t seq_no, uint8_t piece, uint64_t pc);
    uop_id_t notify_decode(
            uint64_t seq_no, uint8_t piece, const DecodeInfo& _decode_info);
    uop_execute_info_t notify_execute(
            uint64_t seq_no, uint8_t piece, const ExecuteInfo &_exec_info);
    uop_execute_info_t notify_commit(
            uint64_t seq_no, uint8_t piece, const ExecuteInfo &_exec_info);
    unsigned count_instances_after(pc_t pc, uop_id_t id);

    uint64_t calculate_size_in_bits() const;
};

extern uop_tracker_t uop_tracker;

#endif
