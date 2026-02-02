#ifndef IDIOM_IDIOM_TRACKER_FOR_H_
#define IDIOM_IDIOM_TRACKER_FOR_H_

#include "../lib/sim_common_structs.h"
#include "constants.h"
#include "hash.h"
#include "uop_tracker.h"

static constexpr uint64_t idiot_for_t_entry_size_in_bits = 0
    + hashed_pc_size_in_bits /* compare_pc */
    + hashed_pc_size_in_bits /* branch_pc */
    + 16 /* stride */
    + 64 /* current_distance */
    + uop_id_t_size_in_bits /* last_update */
    + 1 /* direction0 */
    + 1 /* swap_args */
    + 2 /* goes_negative */
;
/* tracker for simple
 *  for (i = B; i != E; i += S)
 * branches */
class idiot_for_t {
public:
    struct entry {
        pc_t compare_pc;
        pc_t branch_pc;
        uint16_t stride;
        uint64_t current_distance;
        uop_id_t last_update;
        bool direction0;
        bool swap_args; /* which way round to compare */
        std::optional<bool> goes_negative; /* catch for(i=B; i<=E; i+=S) vs for(i=B; i<E; i+=S) */

        bool matches(uop_id_t id, pc_t pc) const {
            return cam_hash(pc) == cam_hash(compare_pc) || cam_hash(pc) == cam_hash(branch_pc);
        }
        void notify_fetch(uop_id_t id, pc_t pc);
        void notify_agen(uop_id_t id, pc_t pc, uint64_t mem_vz, uint64_t mem_sz) {}
        void notify_execute(const ExecuteInfo &exe_info, const uop_execute_info_t &uop, pc_t pc);
        std::optional<bool> predict_branch(uop_id_t id, pc_t pc);
        void update_predictor(uop_id_t id, pc_t pc, bool resolve_dir, bool pred_dir);
    };

    void notify_fetch(uop_id_t id, pc_t pc);
    bool notify_decode(uop_id_t id, pc_t pc, const DecodeInfo &dec_info);
    void notify_interest_agen(uop_id_t id, pc_t pc, uint64_t mem_vz, uint64_t mem_sz) {}
    std::optional<entry> notify_interest_execute(
            const uop_execute_info_t &uop, pc_t pc, const ExecuteInfo &exe_info);

    uint64_t calculate_size_in_bits() const;
};

#endif
