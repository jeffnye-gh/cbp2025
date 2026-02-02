#ifndef IDIOM_IDIOM_TRACKER_ASCIZ_H_
#define IDIOM_IDIOM_TRACKER_ASCIZ_H_

#include "../lib/sim_common_structs.h"
#include "constants.h"
#include "hash.h"
#include "uop_tracker.h"

static constexpr uint64_t idiot_asciz_t_entry_size_in_bits = 0
    + hashed_pc_size_in_bits /* load_pc */
    + hashed_pc_size_in_bits /* branch_pc */
    + 1 /* size  - always 1 or 2 */
    + uop_id_t_size_in_bits /* last_update */
    + 65 /* current_address */
    + uop_id_t_size_in_bits /* loads_seen */
    + uop_id_t_size_in_bits /* last_distance_update */
    +  uop_id_t_size_in_bits /* distance_to_end */
    + 1 /* direction0 */
;
/* tracker for NULL terminated ASCII (or UTF-8, or UTF-16) strings */
class idiot_asciz_t {
public:
    struct entry {
        pc_t load_pc;
        pc_t branch_pc;
        uint8_t size;
        uop_id_t last_addr_update;
        std::optional<uint64_t> current_address;
        uint16_t loads_seen;
        uop_id_t last_distance_update;
        std::optional<uint16_t> distance_to_end;
        bool direction0;

        bool matches(uop_id_t id, pc_t pc) const {
            return cam_hash(pc) == cam_hash(load_pc) || cam_hash(pc) == cam_hash(branch_pc);
        }
        void notify_fetch(uop_id_t id, pc_t pc);
        void notify_execute(const ExecuteInfo &exe_info, const uop_execute_info_t &uop, pc_t pc);
        void notify_agen(uop_id_t id, pc_t pc, uint64_t mem_vz, uint64_t mem_sz);
        std::optional<bool> predict_branch(uop_id_t id, pc_t pc);
        void update_predictor(uop_id_t id, pc_t pc, bool resolve_dir, bool pred_dir);
    };

    void notify_fetch(uop_id_t id, pc_t pc);
    bool notify_decode(uop_id_t id, pc_t pc, const DecodeInfo &dec_info);
    void notify_interest_agen(uop_id_t id, pc_t pc, uint64_t mem_vz, uint64_t mem_sz);
    std::optional<entry> notify_interest_execute(
            const uop_execute_info_t &uop, pc_t pc, const ExecuteInfo &exe_info);

    uint64_t calculate_size_in_bits() const;
};

#endif
