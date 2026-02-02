#include "idiom_tracker_asciz.h"
#include "hash.h"

#include <cassert>
#include <map>
#ifndef NDEBUG
#include <iostream>
#define LOG
#endif

static constexpr int maximum_load_branches_to_track = 8;
static constexpr int maximum_instances_to_track = 4;

static std::map<uop_id_t, uint64_t> mem_vas;
static constexpr unsigned mem_vas_to_track = 64;

static constexpr uint64_t known_strlen_size_in_bits = 64 + 16 + uop_id_t_size_in_bits;
struct known_strlen {
    uint64_t address;
    uint16_t len_in_bytes;
    unsigned last_used;
};

static constexpr unsigned strlens_to_track = 128;
static unsigned strlen_used = 0;
static std::array<known_strlen, strlens_to_track> strlens;

static constexpr uint64_t tracked_load_branch_size_in_bits = 0
    + hashed_pc_size_in_bits /* load_pc */
    + hashed_pc_size_in_bits /* branch_pc */
    + uop_id_t_size_in_bits /* last_seen */
    + 1 /* uninteresting */
    + 1 /* size is 1 or 2 */
    + (65 + 2 + 2) * maximum_instances_to_track
    + 2 /* direction0 */
;
/* tracks a load feeding directly into a branch */
struct tracked_load_branch {
    pc_t load_pc;
    pc_t branch_pc;
    uop_id_t last_seen;
    bool uninteresting;
    uint8_t size;
    struct instance {
        std::optional<uint64_t> address = std::nullopt;
        std::optional<bool> direction = std::nullopt;
        std::optional<bool> was_zero = std::nullopt;
    };
    std::map<uop_id_t, instance> instances;
    std::optional<bool> direction0;
};

/* tracker stores up to maximum_load_branches_to_track pairs of load and branch
 * pcs and the load's values. */
static std::array<tracked_load_branch, maximum_load_branches_to_track> tracker_;

uint64_t idiot_asciz_t::calculate_size_in_bits() const {
    return 0
        + (64 + uop_id_t_size_in_bits) * mem_vas_to_track /* mem_vas */
        + known_strlen_size_in_bits * strlens_to_track /* known_strlen */
        + tracked_load_branch_size_in_bits * maximum_load_branches_to_track /* known_strlen */
        + 65 * uop_id_t_size_in_bits /* last_load */
        + 65 * pc_t_size_in_bits /* last_load */
    ;
}


static void clear_old(uop_id_t id) {
    /* don't track more than uop_max things (for bounded storage reasons) */
    for (auto &load_branch : tracker_) {
        while (!load_branch.instances.empty() && load_branch.instances.begin()->first + uop_max <= id)
            load_branch.instances.erase(load_branch.instances.begin());
    }
    while (!mem_vas.empty() && mem_vas.begin()->first + uop_max <= id)
        mem_vas.erase(mem_vas.begin());
}

static tracked_load_branch *lookup_branch(uop_id_t operation, pc_t pc) {
    for (int index = 0; index < maximum_load_branches_to_track; ++index) {
        auto &load_branch = tracker_[index];
        if (cam_hash(load_branch.branch_pc) == cam_hash(pc)) {
            return &load_branch;
        }
    }
    return nullptr;
}
static tracked_load_branch *lookup(uop_id_t operation, pc_t pc) {
    for (int index = 0; index < maximum_load_branches_to_track; ++index) {
        auto &load_branch = tracker_[index];
        if (cam_hash(load_branch.load_pc) == cam_hash(pc)) {
            return &load_branch;
        }
    }
    return nullptr;
}
static bool lookup_or_alloc(uop_id_t operation, pc_t pc) {
    int alloc_index = 0;
    for (int index = 0; index < maximum_load_branches_to_track; ++index) {
        auto &load_branch = tracker_[index];
        if (cam_hash(load_branch.load_pc) == cam_hash(pc)) {
            if (!load_branch.uninteresting)
                if (load_branch.instances.size() < maximum_instances_to_track)
                    load_branch.instances[operation] = tracked_load_branch::instance();
                else
                    return false;
            return true;
        }
        if ((!tracker_[alloc_index].uninteresting && load_branch.uninteresting)
                || (tracker_[alloc_index].uninteresting ==
                    load_branch.uninteresting && tracker_[alloc_index].last_seen
                    > load_branch.last_seen))
            alloc_index = index;
    }

    auto &load_branch = tracker_[alloc_index];
    load_branch.load_pc = pc;
    load_branch.branch_pc = null_pc;
    load_branch.instances.clear();
    load_branch.instances[operation] = tracked_load_branch::instance();
    load_branch.last_seen = operation;
    load_branch.uninteresting = false;
    load_branch.direction0 = std::nullopt;
    return true;
}

static known_strlen *lookup_strlen(uint64_t address) {
    for (auto &s : strlens) {
        if (s.address <= address && s.address + s.len_in_bytes > address) {
            s.last_used = strlen_used++;
            return &s;
        }
    }
    return nullptr;
}

static known_strlen &alloc_strlen(uint64_t address) {
    int alloc_index = 0;
    for (int i = 0; i < strlens.size(); ++i) {
        auto &s = strlens[i];
        if (s.address <= address && s.address + s.len_in_bytes > address) {
            s.last_used = strlen_used++;
            return s;
        }
        if (s.last_used < strlens[alloc_index].last_used)
            alloc_index = i;
    }
    auto &s = strlens[alloc_index];
    s.address = address;
    s.len_in_bytes = 1;
    s.last_used = strlen_used++;
    return s;
}

static bool is_null_compare(const DecodeInfo &dec_info) {
    return dec_info.insn_class == InstClass::aluInstClass
        && dec_info.dst_reg_info == 64
        && dec_info.src_reg_info.size() == 1
        && dec_info.src_reg_info[0] < 32
    ;
}

static bool is_load(const DecodeInfo &dec_info) {
    return dec_info.insn_class == InstClass::loadInstClass
        && dec_info.dst_reg_info.has_value()
        && dec_info.dst_reg_info.value() < 32
    ;
}

static bool is_cond_branch(const DecodeInfo &dec_info) {
    return dec_info.insn_class == InstClass::condBranchInstClass
        && dec_info.src_reg_info.size() == 1
    ;
}

void idiot_asciz_t::notify_fetch(uop_id_t id, pc_t pc) {
}

bool idiot_asciz_t::notify_decode(uop_id_t id, pc_t pc, const DecodeInfo &dec_info) {
    clear_old(id);
    static constexpr uop_id_t never = uop_id_t(-1);
    static std::array<uop_id_t, 65> last_load = {
        never,never,never,never,never,never,never,never,never,never,never,never,never,never,never,never,
        never,never,never,never,never,never,never,never,never,never,never,never,never,never,never,never,
        never,never,never,never,never,never,never,never,never,never,never,never,never,never,never,never,
        never,never,never,never,never,never,never,never,never,never,never,never,never,never,never,never,
        never,
    };
    static std::array<pc_t, 65> last_load_pc = { };
    if (is_load(dec_info)) {
        last_load[dec_info.dst_reg_info.value()] = id;
        last_load_pc[dec_info.dst_reg_info.value()] = pc;
        return true;
    }
    if (is_null_compare(dec_info) && last_load[dec_info.src_reg_info[0]] !=
            never) {
        last_load[dec_info.dst_reg_info.value()] =
            last_load[dec_info.src_reg_info[0]];
        last_load_pc[dec_info.dst_reg_info.value()] =
            last_load_pc[dec_info.src_reg_info[0]];
    }
    if (dec_info.dst_reg_info.has_value())
        if (dec_info.dst_reg_info < last_load.size())
            last_load[dec_info.dst_reg_info.value()] = never;
    if (is_cond_branch(dec_info) && last_load[dec_info.src_reg_info[0]] != never) {
        if (!lookup_or_alloc(last_load[dec_info.src_reg_info[0]], last_load_pc[dec_info.src_reg_info[0]]))
            return false;
        auto load_branch = lookup(last_load[dec_info.src_reg_info[0]], last_load_pc[dec_info.src_reg_info[0]]);
        if (load_branch == nullptr || load_branch->branch_pc != null_pc) return false;
        load_branch->branch_pc = pc;
        return true;
    }
    return false;
}

uint64_t detect_asciz_loop(tracked_load_branch &load_branch) {
    if (load_branch.branch_pc == null_pc) return false;

    int direction = 0;
    for (const auto &e : load_branch.instances) {
        const auto &instance = e.second;
        if (!instance.direction.has_value()) continue;
        if (instance.direction.value() == !(instance.was_zero == true))
            --direction;
        else
            ++direction;
    }

    if (direction >= 1 || direction <= -1)
        load_branch.direction0 = direction >= 1;

    int old_gap = 0;
    std::optional<uint64_t> old_address;
    for (const auto &e : load_branch.instances) {
        const auto &instance = e.second;
        old_gap++;
        if (!instance.address.has_value()) continue;
        const std::optional<uint64_t> address = old_address;
        const int gap = old_gap;
        old_address = instance.address;
        old_gap = 0;
        if (!address.has_value()) continue;
        if (address.value() >= instance.address.value()) {
            load_branch.uninteresting = true;
            break; /* going in the wrong direction. */
        }
        const uint64_t change = instance.address.value() - address.value();
        if (change == gap * load_branch.size) {
            return true;
        } else
            return false;
    }
    return false;
}

void idiot_asciz_t::notify_interest_agen(uop_id_t id, pc_t pc, uint64_t mem_va, uint64_t mem_sz) {
    auto loadp = lookup(id, pc);
    if (!loadp) return;
    mem_vas[id] = mem_va;
    if (mem_vas.size() > mem_vas_to_track)
        mem_vas.erase(mem_vas.begin());
    auto &load_branch = *loadp;
    /* can't be ASCII or Unicode if it's too big! */
    if (mem_sz > 2) load_branch.uninteresting = true;
    if (load_branch.uninteresting) return;
    load_branch.size = mem_sz;
    auto fi = load_branch.instances.find(id);
    if (fi != load_branch.instances.end()) {
        fi->second.address = mem_va;
    }
}

static bool might_be_ascii(uint64_t v) {
    if (v < ' ' && v != '\0' && v != '\t' && v != '\r' && v != '\n')
        return false;
    return true;
}

std::optional<idiot_asciz_t::entry>
idiot_asciz_t::notify_interest_execute(const uop_execute_info_t &uop, pc_t pc, const ExecuteInfo &exe_info) {
    if (is_cond_branch(exe_info.dec_info)) {
        auto loadp = lookup_branch(uop.id, pc);
        if (loadp) {
            auto &load_branch = *loadp;
            auto fi = load_branch.instances.lower_bound(uop.id);
            if (fi != load_branch.instances.begin()) {
                --fi;
                if (!fi->second.direction.has_value())
                    fi->second.direction = exe_info.taken.value();
            }
        }
        return std::nullopt;
    }
    if (!is_load(exe_info.dec_info)) return std::nullopt;
    auto loadp = lookup(uop.id, pc);
    if (!loadp) return std::nullopt;
    auto &load_branch = *loadp;
    if (!might_be_ascii(exe_info.dst_reg_value.value()))
        load_branch.uninteresting = true;
    if (load_branch.uninteresting) return std::nullopt;
    auto fi = load_branch.instances.find(uop.id);
    if (fi != load_branch.instances.end()) {
        fi->second.was_zero = exe_info.dst_reg_value == 0;
    }
    if (!detect_asciz_loop(load_branch)) return std::nullopt;
    if (!load_branch.direction0) return std::nullopt;

#ifdef LOG
    std::cout << "IDIOT_ASCIZ: found! pcs=[0x"
        << std::hex << load_branch.load_pc.first
        << ",0x" << load_branch.branch_pc.first << "] size " << std::dec
        << int(load_branch.size) << " dir0 "
        << (load_branch.direction0.value() ? "T" : "N")
        << std::endl;
#endif
    return entry{
        .load_pc = load_branch.load_pc,
        .branch_pc = load_branch.branch_pc,
        .size = load_branch.size,
        .last_addr_update = load_branch.instances.begin()->first,
        .current_address = load_branch.instances.begin()->second.address,
        .loads_seen =
            uint16_t(uop_tracker.count_instances_after(load_branch.load_pc,
                        uop.id)),
        .last_distance_update = 0,
        .distance_to_end = std::nullopt,
        .direction0 = load_branch.direction0.value(),
    };
}

void idiot_asciz_t::entry::notify_fetch(uop_id_t id, pc_t pc) {
    if (cam_hash(pc) != cam_hash(load_pc)) return;
    loads_seen++;
    if (distance_to_end)
        if (distance_to_end.value() > 0)
            distance_to_end = distance_to_end.value() - 1;
        else
            distance_to_end.reset();
}

void idiot_asciz_t::entry::notify_agen(uop_id_t id, pc_t pc, uint64_t mem_va, uint64_t mem_sz) {
    if (cam_hash(pc) != cam_hash(load_pc)) return;
    mem_vas[id] = mem_va;
    if (mem_vas.size() > mem_vas_to_track)
        mem_vas.erase(mem_vas.begin());
    if ((!distance_to_end || last_distance_update < id)) {
        auto sp = lookup_strlen(mem_va);
        if (sp) {
            auto &s = *sp;
            auto dist = mem_va - s.address;
            auto outstanding = uop_tracker.count_instances_after(load_pc, id);
            if (s.len_in_bytes >= dist + outstanding * size)
                distance_to_end = (s.len_in_bytes - dist) / size - outstanding;
            else
                distance_to_end.reset();
#ifdef LOG
            std::cout << "IDIOT_ASCIZ: known strlen!!! pcs=[0x"
            << std::hex << load_pc.first
            << ",0x" << branch_pc.first << "] str 0x"
            << s.address << std::dec
            << " len "
            << s.len_in_bytes
            << " current 0x" << std::hex << mem_va
            << std::dec
            << " out " << outstanding
            << " dtoe " << int16_t(distance_to_end.value_or(uint16_t(-1)))
            << std::endl;
#endif
            last_distance_update = id;
        } else {
            distance_to_end.reset();
            last_distance_update = id;
        }
    }
}

void idiot_asciz_t::entry::notify_execute(const ExecuteInfo &exe_info, const uop_execute_info_t &uop, pc_t pc) {
    if (cam_hash(pc) != cam_hash(load_pc)) return;
    if (!is_load(exe_info.dec_info)) return;
    auto fi = mem_vas.find(uop.id);
    if (fi == mem_vas.end()) return;
    uint64_t mem_va = fi->second;
#ifdef LOG
            std::cout << "IDIOT: notify_execute pc 0x"
        << std::hex << load_pc.first <<  " mem_va 0x" << std::hex << mem_va
                << " val 0x" << uop.dst_reg_value.value_or(uint64_t(-1)) <<
                std::dec << std::endl;
#endif
    if (!current_address || last_addr_update > uop.id) {
        last_addr_update = uop.id;
        current_address = mem_va;
        loads_seen = 1 + uop_tracker.count_instances_after(load_pc, uop.id);
    }
    if (current_address) {
        if (mem_va < current_address.value() || mem_va > size * loads_seen +
                current_address.value()) {
            last_addr_update = uop.id;
            current_address = mem_va;
            loads_seen = 1 + uop_tracker.count_instances_after(load_pc, uop.id);
        }
    }
    if (uop.dst_reg_value == 0 && current_address && mem_va - current_address.value() < 8192 && mem_va - current_address.value() > 1) {
        auto &s = alloc_strlen(current_address.value());
        s.len_in_bytes = mem_va - current_address.value();
#ifdef LOG
    std::cout << "IDIOT_ASCIZ: learned strlen pcs=[0x"
        << std::hex << load_pc.first
        << ",0x" << branch_pc.first << "] str 0x"
        << s.address << std::dec
        << " len "
        << s.len_in_bytes
        << std::endl;
#endif
    }
    if (uop.dst_reg_value == 0 || !might_be_ascii(uop.dst_reg_value.value())) {
        last_addr_update = uop_id_t(-1);
        current_address.reset();
    }
}

std::optional<bool> idiot_asciz_t::entry::predict_branch(uop_id_t id, pc_t pc) {
    if (!distance_to_end) return std::nullopt;
    if (distance_to_end.value())
        return !direction0;
    else
        return direction0;
}

void idiot_asciz_t::entry::update_predictor(uop_id_t id, pc_t pc, bool resolve_dir, bool pred_dir) {
#ifdef LOG
    std::cout << "IDIOT_ASCIZ: pc 0x" << std::hex <<
        pc.first << std::dec << " " << (resolve_dir == pred_dir ? "correct" :
            "mispredict") << " " << (resolve_dir ? "taken" : "untaken")
        << std::endl;
#endif
    return;
}
