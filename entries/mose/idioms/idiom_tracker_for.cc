#include "idiom_tracker_for.h"

#include <cassert>
#include <map>
#ifndef NDEBUG
#include <iostream>
//#define LOG
#endif

static constexpr int maximum_compares_to_track = 8;
static constexpr uint64_t not_available = uint64_t(-1);
static constexpr int maximum_instances_to_track = 4;
/* we detect we have run past the end for this many steps i.e.
   for (i=0; i<n; i++)
   the value of n-i being negative suggests we are past the end */
static constexpr int past_end_threshold = -256;

static constexpr uint64_t tracked_compare_size_in_bits = 0
    + hashed_pc_size_in_bits /* compare_pc */
    + hashed_pc_size_in_bits /* branch_pc */
    + uop_id_t_size_in_bits /* last_seen */
    + 1 /* uninteresting */
    + (64 + 2 + 2) * maximum_instances_to_track
    + 2 /* direction0 */
    + 1 /* swap_args */
;
struct tracked_compare {
    pc_t compare_pc;
    pc_t branch_pc;
    uop_id_t last_seen;
    bool uninteresting;
    struct instance {
        uint64_t distance = not_available;
        std::optional<bool> direction = std::nullopt;
        std::optional<bool> swap_args = std::nullopt;
    };
    std::map<uop_id_t, instance> instances;
    std::optional<bool> direction0;
    bool swap_args;
};

/* tracker stores up to maximum_compares_to_track pairs of compare and branch
 * pcs and the compare's values. */
static std::array<tracked_compare, maximum_compares_to_track> tracker_;

uint64_t idiot_for_t::calculate_size_in_bits() const {
    return 0
        + tracked_compare_size_in_bits * maximum_compares_to_track /* tracker_ */
        + uop_id_t_size_in_bits /* last_compare */
        + pc_t_size_in_bits /* last_compare_pc */
    ;
}

static void clear_old(uop_id_t id) {
    /* don't track more than uop_max things (for bounded storage reasons) */
    for (auto &compare : tracker_) {
        while (!compare.instances.empty() && compare.instances.begin()->first + uop_max <= id)
            compare.instances.erase(compare.instances.begin());
    }
}

static tracked_compare *lookup_branch(uop_id_t operation, pc_t pc) {
    for (int index = 0; index < maximum_compares_to_track; ++index) {
        auto &compare = tracker_[index];
        if (cam_hash(compare.branch_pc) == cam_hash(pc)) {
            return &compare;
        }
    }
    return nullptr;
}
static tracked_compare *lookup(uop_id_t operation, pc_t pc) {
    for (int index = 0; index < maximum_compares_to_track; ++index) {
        auto &compare = tracker_[index];
        if (cam_hash(compare.compare_pc) == cam_hash(pc)) {
            return &compare;
        }
    }
    return nullptr;
}
static bool lookup_or_alloc(uop_id_t operation, pc_t pc) {
    int alloc_index = 0;
    for (int index = 0; index < maximum_compares_to_track; ++index) {
        auto &compare = tracker_[index];
        if (cam_hash(compare.compare_pc) == cam_hash(pc)) {
            if (!compare.uninteresting)
                if (compare.instances.size() < maximum_instances_to_track)
                    compare.instances[operation] = tracked_compare::instance();
                else
                    return false;
            return true;
        }
        if (tracker_[alloc_index].last_seen > compare.last_seen)
            alloc_index = index;
    }

    auto &compare = tracker_[alloc_index];
    compare.compare_pc = pc;
    compare.branch_pc = null_pc;
    compare.instances.clear();
    compare.instances[operation] = tracked_compare::instance();
    compare.last_seen = operation;
    compare.uninteresting = false;
    compare.direction0 = std::nullopt;
    return true;
}

static bool is_compare(const DecodeInfo &dec_info) {
    return dec_info.insn_class == InstClass::aluInstClass
        && dec_info.dst_reg_info == 64
        && dec_info.src_reg_info.size() == 2
        && dec_info.src_reg_info[0] < 32
        && dec_info.src_reg_info[1] < 32
    ;
}

static bool is_cond_branch(const DecodeInfo &dec_info) {
    return dec_info.insn_class == InstClass::condBranchInstClass
        && dec_info.src_reg_info.size() == 1
        && dec_info.src_reg_info[0] == 64
    ;
}

void idiot_for_t::notify_fetch(uop_id_t id, pc_t pc) {
}

bool idiot_for_t::notify_decode(uop_id_t id, pc_t pc, const DecodeInfo &dec_info) {
    clear_old(id);
    static uop_id_t last_compare = uop_id_t(-1);
    static pc_t last_compare_pc = null_pc;
    if (is_compare(dec_info)) {
        if (lookup_or_alloc(id, pc)) {
            last_compare = id;
            last_compare_pc = pc;
            return true;
        } else
            return false;
    }
    if (dec_info.dst_reg_info == 64)
        last_compare = uop_id_t(-1);
    if (is_cond_branch(dec_info) && last_compare != uop_id_t(-1)) {
        auto compare = lookup(last_compare, last_compare_pc);
        if (compare == nullptr) return false;
        compare->branch_pc = pc;
        return true;
    }
    return false;
}

uint64_t detect_for_loop(tracked_compare &compare) {
    if (compare.branch_pc == null_pc) return not_available;

    int swap_args = 0;
    for (const auto &e : compare.instances) {
        const auto &instance = e.second;
        if (instance.swap_args.has_value())
            swap_args += instance.swap_args.value() ? 1 : -1;
    }
    compare.swap_args = swap_args > 0;

    int direction = 0;
    for (const auto &e : compare.instances) {
        const auto &instance = e.second;
        if (instance.distance == not_available) continue;
        if (!instance.direction.has_value()) continue;
        if (instance.direction.value())
            --direction;
        else
            ++direction;
    }
    if (direction > 1 || direction < -1)
        compare.direction0 = direction > 1;

    int old_gap = 0;
    uint64_t old_distance = not_available;
    uint64_t stride = 1;
    for (const auto &e : compare.instances) {
        const auto &instance = e.second;
        old_gap++;
        if (instance.distance == not_available) continue;
        const uint64_t distance = old_distance;
        const int gap = old_gap;
        old_distance = instance.distance;
        old_gap = 0;
        if (distance == not_available)
            continue;
        if (distance <= instance.distance) {
            compare.uninteresting = true;
            break; /* going in the wrong direction. */
        }
        const uint64_t change = distance - instance.distance;
        if (change == gap) {
            /* detected simple for loop! */
            return stride;
        } else if (change % gap == 0) {
            if (stride == 1) {
                stride = change / gap;
                continue;
            } else if (change == gap * stride) {
                /* deteted strided for loop! */
                return stride;
            }
        }
    }
    return not_available;
}

std::optional<idiot_for_t::entry>
idiot_for_t::notify_interest_execute(const uop_execute_info_t &uop, pc_t pc, const ExecuteInfo &exe_info) {
    if (is_cond_branch(exe_info.dec_info)) {
        auto comparep = lookup_branch(uop.id, pc);
        if (comparep) {
            auto &compare = *comparep;
            auto fi = compare.instances.lower_bound(uop.id);
            if (fi != compare.instances.begin()) {
                --fi;
                if (!fi->second.direction.has_value())
                    fi->second.direction = exe_info.taken.value();
            }
        }
        return std::nullopt;
    }
    if (!is_compare(exe_info.dec_info)) return std::nullopt;
    auto comparep = lookup(uop.id, pc);
    if (!comparep) return std::nullopt;
    auto &compare = *comparep;
    if (compare.uninteresting) return std::nullopt;
    const uint64_t distance =
        uop.src_reg_values[0] > uop.src_reg_values[1]
        ? uop.src_reg_values[0] - uop.src_reg_values[1]
        : uop.src_reg_values[1] - uop.src_reg_values[0];
    auto fi = compare.instances.find(uop.id);
    if (fi != compare.instances.end()) {
        if (uop.src_reg_values[0] != uop.src_reg_values[1])
            fi->second.swap_args = uop.src_reg_values[0] > uop.src_reg_values[1];
        fi->second.distance = distance;
    }
    uint64_t stride = detect_for_loop(compare);
    if (stride == not_available) return std::nullopt;
    if (!compare.direction0) return std::nullopt;

#ifdef LOG
    std::cout << "IDIOT_FOR: found! pcs=[0x"
        << std::hex << compare.compare_pc.first
        << ",0x" << compare.branch_pc.first << "] stride " << std::dec
        << stride << " dist " << distance << " dir0 "
        << (compare.direction0.value() ? "T" : "N")
        << " swap_args " << (compare.swap_args ? "T" : "N") << std::endl;
#endif
    unsigned in_flight = uop_tracker.count_instances_after(compare.compare_pc, uop.id);
    return entry{
        .compare_pc = compare.compare_pc,
        .branch_pc = compare.branch_pc,
        .stride = uint16_t(stride),
        .current_distance = distance - in_flight * stride,
        .last_update = uop.id,
        .direction0 = compare.direction0.value(),
        .swap_args = compare.swap_args,
        .goes_negative = std::nullopt,
    };
}

void idiot_for_t::entry::notify_fetch(uop_id_t id, pc_t pc) {
    if (cam_hash(pc) != cam_hash(compare_pc)) return;
    current_distance -= stride;
}

void idiot_for_t::entry::notify_execute(const ExecuteInfo &exe_info, const uop_execute_info_t &uop, pc_t pc) {
    if (last_update > uop.id) return;
    if (cam_hash(pc) != cam_hash(compare_pc)) return;
    unsigned in_flight = uop_tracker.count_instances_after(compare_pc, uop.id);
    if (!is_compare(exe_info.dec_info)) return;
    last_update = uop.id;
    const uint64_t distance =
        swap_args
        ? uop.src_reg_values[0] - uop.src_reg_values[1]
        : uop.src_reg_values[1] - uop.src_reg_values[0];
    current_distance = distance - in_flight * stride;
}

std::optional<bool> idiot_for_t::entry::predict_branch(uop_id_t id, pc_t pc) {
    bool prediction;
    if (goes_negative == true) {
        if (current_distance >= uint64_t(-stride))
            prediction = direction0;
        else if (current_distance > uint64_t(past_end_threshold) * stride)
            return std::nullopt;
        else
            prediction = !direction0;

    } else {
        if (current_distance > uint64_t(past_end_threshold) * stride)
            return std::nullopt;
        prediction = (current_distance == 0 ? direction0 : !direction0);
    }
#ifdef LOG
    std::cout << "IDIOT_FOR: predict pcs=[0x"
        << std::hex << compare_pc.first
        << ",0x" << branch_pc.first << "]"  << std::dec
        << " pdist " << current_distance << " dir " << (prediction ? "T" : "N")
        << " goes_neg " << (!goes_negative.has_value() ? "?" : goes_negative.value() ? "T" : "F") << std::endl;
#endif
    return prediction;
}

void idiot_for_t::entry::update_predictor(uop_id_t id, pc_t pc, bool resolve_dir, bool pred_dir) {
    if (goes_negative.has_value()) return;

    if (pred_dir == direction0)
        goes_negative = resolve_dir != direction0;
}
