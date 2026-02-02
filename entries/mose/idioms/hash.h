#ifndef IDIOM_HASH_H
#define IDIOM_HASH_H

#include "uop_tracker.h"

static constexpr uint64_t hashed_pc_size_in_bits = 24;
using hashed_pc_t = uint32_t;
hashed_pc_t cam_hash(pc_t pc);

#endif
