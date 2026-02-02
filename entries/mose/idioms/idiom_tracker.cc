#include "idiom_tracker.h"

#include <array>
#include <cassert>
#include <limits>

#include "constants.h"
#include "hash.h"
#include "sat_counter.h"

#ifndef NDEBUG
#include <iostream>
//#define LOG
#endif

idiot_t idiot;

/* tracking for expressed interest in uops */
enum class interest {
    none = 0,
    ifor = 1,
    asciz = 2,
};
interest operator~(interest a) {
    return interest(~unsigned(a));
}
interest operator|(interest a, interest b) {
    return interest(unsigned(a) | unsigned(b));
}
interest &operator|=(interest &a, interest b) {
    a = a | b;
    return a;
}
interest operator&(interest a, interest b) {
    return interest(unsigned(a) & unsigned(b));
}
interest &operator&=(interest &a, interest b) {
    a = a & b;
    return a;
}
static constexpr uint64_t interest_size_in_bits = 2;
static std::array<interest, uop_max> interests;
static std::array<bool, uop_max> predicted;
static std::array<std::optional<bool>, uop_max> prediction;

using confidence_t = sat_counter<unsigned, 0, 31>;
static constexpr uint64_t confidence_t_size_in_bits = 5;
static constexpr confidence_t confidence_init = 24;
static constexpr confidence_t confidence_threshold = 31;
static constexpr confidence_t confidence_add = 1;
static constexpr confidence_t confidence_minus = 24;

using preference_t = sat_counter<signed, -4, 3>;
static constexpr uint64_t preference_t_size_in_bits = 3;
static constexpr preference_t preference_init = 0;
static constexpr preference_t preference_threshold = 2;
static constexpr preference_t preference_ignore = -2;

static constexpr int maximum_idioms_to_track = 128;
static constexpr uint64_t tracked_idiom_size_in_bits = 0
    + interest_size_in_bits
    + uop_id_t_size_in_bits
    + (idiot_for_t_entry_size_in_bits > idiot_asciz_t_entry_size_in_bits
            ? idiot_for_t_entry_size_in_bits : idiot_asciz_t_entry_size_in_bits)
    + confidence_t_size_in_bits
    + preference_t_size_in_bits * 2
    + 2
;
struct tracked_idiom {
    interest type;
    uop_id_t last_seen;
    union {
        idiot_for_t::entry ifor;
        idiot_asciz_t::entry asciz;
    };
    confidence_t confidence;
    preference_t preference_taken;
    preference_t preference_untaken;
    bool seen_taken_correct;
    bool seen_untaken_correct;

    tracked_idiom() : type(interest::none), last_seen(0) { }

    bool matches(uop_id_t id, pc_t pc) const {
        switch (type) {
            case interest::none: return false;
            case interest::ifor: return ifor.matches(id, pc);
            case interest::asciz: return asciz.matches(id, pc);
        }
        assert(false);
        __builtin_unreachable();
    }
    void notify_fetch(uop_id_t id, pc_t pc) {
        switch (type) {
            case interest::none: return;
            case interest::ifor: return ifor.notify_fetch(id, pc);
            case interest::asciz: return asciz.notify_fetch(id, pc);
        }
        assert(false);
        __builtin_unreachable();
    }
    void notify_agen(const uop_id_t id, pc_t pc, uint64_t mem_va, uint64_t mem_sz) {
        switch (type) {
            case interest::none: return;
            case interest::ifor: return ifor.notify_agen(id, pc, mem_va, mem_sz);
            case interest::asciz: return asciz.notify_agen(id, pc, mem_va, mem_sz);
        }
        assert(false);
        __builtin_unreachable();
    }
    void notify_execute(const ExecuteInfo &exe_info, const uop_execute_info_t &uop, pc_t pc) {
        switch (type) {
            case interest::none: return;
            case interest::ifor: return ifor.notify_execute(exe_info, uop, pc);
            case interest::asciz: return asciz.notify_execute(exe_info, uop, pc);
        }
        assert(false);
        __builtin_unreachable();
    }
    std::optional<bool> predict_branch(uop_id_t id, pc_t pc) {
        switch (type) {
            case interest::ifor: return ifor.predict_branch(id, pc);
            case interest::asciz: return asciz.predict_branch(id, pc);
            default:
                assert(false);
                __builtin_unreachable();
                break;
        }
    }
    void update_predictor(uop_id_t id, pc_t pc, bool resolve_dir, bool pred_dir) {
        switch (type) {
            case interest::ifor: return ifor.update_predictor(id, pc, resolve_dir, pred_dir);
            case interest::asciz: return asciz.update_predictor(id, pc, resolve_dir, pred_dir);
            default:
                assert(false);
                __builtin_unreachable();
                break;
        }
    }

    unsigned score() const {
        return
            (confidence >= confidence_threshold ? 5 : 0) +
            (seen_taken_correct && seen_untaken_correct ? 20 : 0) +
            (preference_taken >= preference_threshold ? 1000 : 0) +
            (preference_untaken >= preference_threshold ? 1000 : 0) +
            (preference_taken >= preference_ignore ? 20 : 0) +
            (preference_untaken >= preference_ignore ? 20 : 0) +
            unsigned(last_seen >> 12);
    }
    /* used by replacement policy */
    bool operator>(const tracked_idiom &other) {
        unsigned score0 = score();
        unsigned score1 = other.score();
        return score0 > score1;
    }
};
static std::array<tracked_idiom, maximum_idioms_to_track> tracker_;
static tracked_idiom &allocate_track() {
    int alloc_index = 0;
    for (int index = 0; index < maximum_idioms_to_track; ++index) {
        auto &idiom = tracker_[index];
        if (tracker_[alloc_index] > idiom)
            alloc_index = index;
    }
    tracker_[alloc_index].type = interest::none;
    tracker_[alloc_index].confidence = confidence_init;
    /* start true to avoid initial mispredict */
    tracker_[alloc_index].preference_taken = preference_init;
    tracker_[alloc_index].preference_untaken = preference_init;
    tracker_[alloc_index].seen_taken_correct = true;
    tracker_[alloc_index].seen_untaken_correct = true;
    return tracker_[alloc_index];
}

uint64_t idiot_t::calculate_size_in_bits() const {
    return 0
        + interest_size_in_bits * uop_max /* interests */
        + uop_max /* predicted */
        + 2 * uop_max /* prediction */
        + tracked_idiom_size_in_bits * maximum_idioms_to_track /* tracker_ */
        + idiot_for.calculate_size_in_bits()
        + idiot_asciz.calculate_size_in_bits()
    ;
}

void idiot_t::notify_fetch(uop_id_t id, pc_t pc) {
    predicted[id % uop_max] = false;
    prediction[id % uop_max] = std::nullopt;
    idiot_for.notify_fetch(id, pc);
    idiot_asciz.notify_fetch(id, pc);
    for (auto &track : tracker_) {
        if (track.matches(id, pc)) {
            track.notify_fetch(id, pc);
        }
    }
}

void idiot_t::notify_decode(uop_id_t id, pc_t pc, const DecodeInfo &dec_info) {
    interest i = interest::none;
    interest mask = interest::none;
    for (auto &track : tracker_) {
        if (track.matches(id, pc)) {
            mask |= track.type;
        }
    }
    if (!unsigned(mask & interest::ifor))
        if (idiot_for.notify_decode(id, pc, dec_info))
            i |= interest::ifor;
    if (!unsigned(mask & interest::asciz))
        if (idiot_asciz.notify_decode(id, pc, dec_info))
            i |= interest::asciz;
    interests[id % uop_max] = i;
}

void idiot_t::notify_agen(uop_id_t id, pc_t pc, uint64_t mem_va, uint64_t mem_sz) {
    for (auto &track : tracker_) {
        if (track.matches(id, pc)) {
            track.notify_agen(id, pc, mem_va, mem_sz);
        }
    }
    if (unsigned(interests[id % uop_max] & interest::ifor)) {
        idiot_for.notify_interest_agen(id, pc, mem_va, mem_sz);
    }
    if (unsigned(interests[id % uop_max] & interest::asciz)) {
        idiot_asciz.notify_interest_agen(id, pc, mem_va, mem_sz);
    }
}

void idiot_t::notify_execute(
        uop_id_t id, pc_t pc, const uop_execute_info_t &uop,
        const ExecuteInfo &exe_info) {
    for (auto &track : tracker_) {
        if (track.matches(id, pc)) {
            track.notify_execute(exe_info, uop, pc);
        }
    }
    if (unsigned(interests[id % uop_max] & interest::ifor)) {
        auto result = idiot_for.notify_interest_execute(uop, pc, exe_info);
        if (result) {
            auto &track = allocate_track();
            track.type = interest::ifor;
            track.last_seen = id;
            track.ifor = result.value();

            /*
            for (uop_id_t uid = uop_tracker.begin(); uid != uop_tracker.end(); ++uid) {
                if (!track.matches(uid, ???)) continue;
                interests[uid % uop_max] &= ~interest::ifor;
                track.last_seen = id;
            }*/
        }
    }
    if (unsigned(interests[id % uop_max] & interest::asciz)) {
        auto result = idiot_asciz.notify_interest_execute(uop, pc, exe_info);
        if (result) {
            auto &track = allocate_track();
            track.type = interest::asciz;
            track.last_seen = id;
            track.asciz = result.value();

            /*
            for (uop_id_t uid = uop_tracker.begin(); uid != uop_tracker.end(); ++uid) {
                if (!track.matches(uid, ???)) continue;
                interests[uid % uop_max] &= ~interest::asciz;
                track.last_seen = id;
            }*/
        }
    }
}

std::optional<bool> idiot_t::predict_branch(uop_id_t id, pc_t pc) {
    for (auto &track : tracker_) {
        if (track.matches(id, pc)) {
            std::optional<bool> r = track.predict_branch(id, pc);
            if (!r.has_value())
                break;
            const bool p = r.value();
            prediction[id % uop_max] = p;
            preference_t preference =
                p ? track.preference_taken : track.preference_untaken;
            /* use prediction if we're confident, or else if we seem to be doing
             * better than the alternative predictor */
            const bool use_pred = (preference > preference_ignore) && (
                (track.confidence >= confidence_threshold
                && track.seen_taken_correct && track.seen_untaken_correct)
                || (preference >= preference_threshold)
            );
#ifdef LOG
            std::cout << "IDIOT: predict pc=["
                << std::hex << pc.first
                << "]"  << std::dec
                << " dir " << (p ? "T" : "N") << " confidence "
                << unsigned(track.confidence)
                << " preference " << signed(preference)
                << " use " << (use_pred ? "T" : "F") << std::endl;
#endif
            if (use_pred) {
                predicted[id % uop_max] = true;
                return p;
            }
            break;
        }
    }
    return std::nullopt;
}

bool idiot_t::spec_update(uop_id_t id, pc_t pc, bool resolve_dir, bool pred_dir, bool alt_pred_dir) {
    if (prediction[id % uop_max].has_value()) {
        const bool track_pred_dir = prediction[id % uop_max].value();
        for (auto &track : tracker_) {
            if (track.matches(id, pc)) {
#ifdef TAGE_LOG
                if (track_pred_dir != alt_pred_dir) {
                    if (track_pred_dir == resolve_dir)
                        std::cout << "IDIOT: beat TAGE";
                    else
                        std::cout << "IDIOT: lost to TAGE";
                    std::cout << " used " << predicted[id % uop_max];
                    std::cout << " predictor " << unsigned(track.type)
                        << " pc 0x" << std::hex <<
                        pc.first << std::dec
                        << std::endl;
                }
#endif
                track.update_predictor(id, pc, resolve_dir, track_pred_dir);
                if (alt_pred_dir != track_pred_dir) {
                    if (resolve_dir == track_pred_dir)
                        if (track_pred_dir)
                            ++track.preference_taken;
                        else
                            ++track.preference_untaken;
                    else
                        if (track_pred_dir)
                            --track.preference_taken;
                        else
                            --track.preference_untaken;
                }
                if (resolve_dir == track_pred_dir) {
                    if (resolve_dir)
                        track.seen_taken_correct = true;
                    else
                        track.seen_untaken_correct = true;
                    if (track.seen_taken_correct && track.seen_untaken_correct
                            || track.confidence < confidence_init) {
                        track.confidence += confidence_add;
                    }
                } else {
                    track.confidence -= confidence_minus;
                    track.seen_taken_correct = false;
                    track.seen_untaken_correct = false;
                }
                break;
            }
        }
    }
    if (predicted[id % uop_max]) {
        if (resolve_dir != pred_dir) {
#ifdef LOG
            std::cout << "IDIOT: mispredict " << (resolve_dir ? "N should be T" : "T should be N") << std::endl;
#endif
        }
        return true;
    }
    return false;
}
bool idiot_t::update(uop_id_t id, pc_t pc, bool resolve_dir, bool pred_dir) {
    return predicted[id % uop_max];
}
