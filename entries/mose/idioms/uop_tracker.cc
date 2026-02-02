/* uop_tracker.cc
 *
 * Used to track in-flight (or recently finished) uops */

#include "uop_tracker.h"

#include <cassert>

#include "constants.h"
#include "hash.h"

uop_tracker_t uop_tracker;

static constexpr uint64_t uop_info_t_size_in_bits = 0
    + hashed_pc_size_in_bits
    + phys_reg_id_size_in_bits * maximum_src_regs_per_instr
    + phys_reg_id_size_in_bits
    + 2 + 1;
struct uop_info_t {
    pc_t pc;
    std::vector<unsigned> src_reg;
    std::optional<unsigned> dst_reg;
    std::optional<bool> was_taken;
    bool in_flight;
};

std::array<uop_info_t, uop_max> infos_;
/* this data structure could be encoded MUCH more space efficiently as one bit
 * indicating if the last uop is the same pc as the current one.  I don't for
 * ease of coding, but count space accordingly */
std::vector<std::array<uop_id_t, uop_max>> seq_ids_;
uop_id_t next_ = 0;

static inline uop_id_t &lookup_id(uint64_t seq_no, uint8_t piece) {
    return seq_ids_[piece][seq_no % uop_max];
}
static inline uop_info_t &lookup(uop_id_t id) {
    return infos_[id % uop_max];
}
static inline uop_info_t &lookup(uint64_t seq_no, uint8_t piece) {
    return lookup(lookup_id(seq_no, piece));
}

/* physical register file_ */
struct phys_reg_t {
    uint16_t references;
    uint64_t value;
    bool has_value;
};
static std::vector<phys_reg_t> phys_reg_file_;
static std::vector<unsigned> arch_reg_file_;

uint64_t uop_tracker_t::calculate_size_in_bits() const {
    return
        uop_info_t_size_in_bits * window_size /* infos_ */
        + uop_max /* seq_ids_ */
        + uop_id_t_size_in_bits /* next_ */
        + (uop_id_t_size_in_bits + 64 + 1) * (1 << phys_reg_id_size_in_bits) /* phys_reg_file_ */
        + uop_id_t_size_in_bits * (1 << reg_id_size_in_bits) /* arch_reg_file_ */
    ;
}

static inline unsigned allocate_phys_reg() {
    static unsigned last_alloc = 0;
    unsigned i = 0;
    if (phys_reg_file_.size() > 0) {
        /* ring buffer loop (from last_alloc+1 to end then start to last_alloc) */
        for (
                i = (last_alloc+1 == phys_reg_file_.size() ? 0 : last_alloc+1);
                i != last_alloc;
                i = (i+1 >= phys_reg_file_.size() ? 0 : i+1)
        ) {
            if (phys_reg_file_[i].references == 0)
                break;
        }
    }
    if (i == last_alloc) {
        i = phys_reg_file_.size();
        phys_reg_file_.emplace_back();
    }
    auto &preg = phys_reg_file_[i];
    preg.references++;
    preg.value = 0xdead1dead1;
    preg.has_value = false;
    return last_alloc = i;
}

uop_id_t uop_tracker_t::next_id() {
    return next_;
}

uop_id_t uop_tracker_t::lookup_id(uint64_t seq_no, uint8_t piece) {
    return ::lookup_id(seq_no, piece);
}

uop_id_t uop_tracker_t::allocate(uint64_t seq_no, uint8_t piece, uint64_t pc) {
    while (piece >= seq_ids_.size())
        seq_ids_.push_back({});
    uop_id_t result =  next_++;
    ::lookup_id(seq_no, piece) = result;
    auto &info = lookup(result);
    info.pc = std::make_pair(pc, piece);
    return result;
}

uop_id_t uop_tracker_t::notify_decode(
        uint64_t seq_no, uint8_t piece, const DecodeInfo& _decode_info) {
    auto id = lookup_id(seq_no, piece);
    auto &info = lookup(id);
    assert(!info.in_flight);
    info.in_flight = true;
    info.src_reg.clear();
    info.dst_reg.reset();
    info.was_taken.reset();
    for (uint64_t reg : _decode_info.src_reg_info) {
        while (arch_reg_file_.size() <= reg) {
            arch_reg_file_.emplace_back(allocate_phys_reg());
            phys_reg_file_.at(arch_reg_file_.back()).has_value = true;
        }
        const unsigned preg = arch_reg_file_.at(reg);
        phys_reg_file_.at(preg).references++;
        info.src_reg.push_back(preg);
    }
    if (_decode_info.dst_reg_info) {
        const uint64_t reg = _decode_info.dst_reg_info.value();
        while (arch_reg_file_.size() < reg) {
            arch_reg_file_.emplace_back(allocate_phys_reg());
            phys_reg_file_.at(arch_reg_file_.back()).has_value = true;
        }
        if (arch_reg_file_.size() == reg)
            arch_reg_file_.emplace_back(allocate_phys_reg());
        else {
            auto &r = arch_reg_file_.at(reg);
            phys_reg_file_.at(r).references--;
            r = allocate_phys_reg();
        }
        const unsigned preg = arch_reg_file_.at(reg);
        info.dst_reg = preg;
        auto &p = phys_reg_file_.at(preg);
        p.references++;
        assert(phys_reg_file_.at(preg).has_value == false);
    }
    return id;
}

uop_execute_info_t uop_tracker_t::notify_execute(
        uint64_t seq_no, uint8_t piece, const ExecuteInfo& _exec_info) {
    uop_execute_info_t result;
    result.id = lookup_id(seq_no, piece);
    auto &info = lookup(result.id);
    assert(info.in_flight);
    result.src_reg_values.reserve(info.src_reg.size());
    for (auto reg : info.src_reg) {
        auto &src = phys_reg_file_.at(reg);
        assert(src.has_value);
        result.src_reg_values.push_back(src.value);
    }
    if (info.dst_reg) {
        auto &dst = phys_reg_file_.at(info.dst_reg.value());
        assert(dst.has_value == false);
        dst.has_value = true;
        dst.value = _exec_info.dst_reg_value.value_or(0xFFFFFFFFFFFFFFFF);
        result.dst_reg_value = dst.value;
    }
    result.was_taken = _exec_info.taken;
    return result;
}

uop_execute_info_t uop_tracker_t::notify_commit(
        uint64_t seq_no, uint8_t piece, const ExecuteInfo& _exec_info) {
    uop_execute_info_t result;
    result.id = lookup_id(seq_no, piece);
    auto &info = lookup(result.id);
    assert(info.in_flight);
    result.src_reg_values.reserve(info.src_reg.size());
    for (auto reg : info.src_reg) {
        auto &src = phys_reg_file_.at(reg);
        assert(src.has_value);
        result.src_reg_values.push_back(src.value);
        src.references--;
    }

    if (info.dst_reg) {
        auto &dst = phys_reg_file_.at(info.dst_reg.value());
        result.dst_reg_value = dst.value;
        dst.references--;
    }
    result.was_taken = _exec_info.taken;
    info.in_flight = false;
    return result;
}

unsigned uop_tracker_t::count_instances_after(pc_t pc, uop_id_t id) {
    unsigned count = 0;
    for (++id; id < next_; ++id) {
        auto &uop = lookup(id);
        if (cam_hash(uop.pc) == cam_hash(pc)) ++count;
    }
    return count;
}
