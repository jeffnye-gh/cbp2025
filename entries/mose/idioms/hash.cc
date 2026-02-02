#include "hash.h"

/* this is just code off the top of my head, nothing strategic */
hashed_pc_t cam_hash(pc_t pc) {
    hashed_pc_t result = 0;
    uint64_t first = pc.first;
    for (int i = 0; i < 64; i += 24) {
        result ^= first;
        first >>= 24;
    }
    result += pc.second * 0x11111111;
    return result & 0xffffff;
}
