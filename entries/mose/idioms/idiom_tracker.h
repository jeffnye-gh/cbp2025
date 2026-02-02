#ifndef IDIOM_IDIOM_TRACKER_H_
#define IDIOM_IDIOM_TRACKER_H_

#include "../lib/sim_common_structs.h"
#include "idiom_tracker_asciz.h"
#include "idiom_tracker_for.h"
#include "uop_tracker.h"

/* IdioT is the Idiom Tracker used to make accurate branch predicion based on
 * detected programmer idioms. This class coordinates the various specific idiom
 * predictors. */
class idiot_t {
    idiot_for_t idiot_for;
    idiot_asciz_t idiot_asciz;
public:
    void notify_fetch(uop_id_t id, pc_t pc);
    void notify_decode(uop_id_t id, pc_t pc, const DecodeInfo &dec_info);
    void notify_agen(uop_id_t id, pc_t pc, uint64_t mem_vz, uint64_t mem_sz);
    void notify_execute(
            uop_id_t id, pc_t pc, const uop_execute_info_t &uop,
            const ExecuteInfo &exe_info);

    /* prediction interface */
    std::optional<bool> predict_branch(uop_id_t id, pc_t pc);
    bool spec_update(uop_id_t id, pc_t pc, bool resolve_dir, bool pred_dir, bool alt_pred_dir);
    bool update(uop_id_t id, pc_t pc, bool resolve_dir, bool pred_dir);

    uint64_t calculate_size_in_bits() const;
};

extern idiot_t idiot;

#endif
