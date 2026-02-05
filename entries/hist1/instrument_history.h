#pragma once
// instrument_history.h
//
// SAFE to include even if the includer is inside a user namespace (e.g., namespace nHIST1),
// because it uses ONLY C headers (no <cstdio>, <cstdint>, etc.), so it does not introduce
// a nested "std" namespace like nHIST1::std.
//
// Enable with -DINSTRUMENT_HISTORY

#ifdef INSTRUMENT_HISTORY

#include <stdint.h>   // uint64_t, uint32_t, uint8_t
#include <stdio.h>    // FILE, fopen, fprintf, fclose
#include <string.h>   // memcpy, memset

namespace hist_instr {

enum HistClass : uint8_t {
    HC_LOOP = 0,
    HC_CALLRET = 1,
    HC_INDIRECT = 2,
    HC_MISC = 3,
    HC_N = 4
};

static inline HistClass classify_hist_event(int brtype, bool taken, uint64_t pc, uint64_t nextpc)
{
    if (brtype & 8) return HC_CALLRET;
    if (brtype & 4) return HC_CALLRET;
    if (brtype & 2) return HC_INDIRECT;
    if ((brtype & 1) && taken && (nextpc < pc)) return HC_LOOP;
    return HC_MISC;
}

struct HistGlobalStats
{
    uint64_t steps_total;
    uint64_t steps_by_class[HC_N];
    uint64_t calls_total;
    uint64_t calls_by_class[HC_N];

    uint64_t imli_triggers;
    uint64_t brimli_low4[16];
    uint64_t taimli_low4[16];

    HistGlobalStats() { memset(this, 0, sizeof(*this)); }

    void dump(FILE* f) const
    {
        fprintf(f, "History calls total: %llu\n", (unsigned long long)calls_total);
        for (int c = 0; c < (int)HC_N; c++) {
            fprintf(f, "  class %d: calls=%llu steps=%llu\n",
                    c,
                    (unsigned long long)calls_by_class[c],
                    (unsigned long long)steps_by_class[c]);
        }
        fprintf(f, "History steps total: %llu\n", (unsigned long long)steps_total);
        fprintf(f, "IMLI triggers: %llu\n", (unsigned long long)imli_triggers);
    }
};

static inline HistGlobalStats g_hist_stats;

struct PerPcEntry
{
    uint64_t pc_tag;
    uint64_t last_steps_by_class[HC_N];
    uint64_t accum_intervening[HC_N];
    uint64_t samples;

    PerPcEntry() { memset(this, 0, sizeof(*this)); }
};

static constexpr uint32_t PERPC_LOG2 = 18;
static constexpr uint32_t PERPC_SIZE = (1u << PERPC_LOG2);
static inline PerPcEntry g_perpc[PERPC_SIZE];

static inline uint32_t perpc_index(uint64_t pc)
{
    uint64_t x = (pc >> 2);
    x ^= (x >> 17);
    x ^= (x >> 9);
    return (uint32_t)x & (PERPC_SIZE - 1);
}

static constexpr uint32_t SAMPLE_MASK = 0x3F; // 1/64 PCs
static inline int do_sample(uint64_t pc)
{
    return (((pc >> 2) & SAMPLE_MASK) == 0);
}

static inline void perpc_on_event(uint64_t pc)
{
    if (!do_sample(pc)) return;

    const uint32_t idx = perpc_index(pc);
    PerPcEntry* e = &g_perpc[idx];

    if (e->pc_tag != pc) {
        e->pc_tag = pc;
        memcpy(e->last_steps_by_class, g_hist_stats.steps_by_class, sizeof(e->last_steps_by_class));
        memset(e->accum_intervening, 0, sizeof(e->accum_intervening));
        e->samples = 0;
        return;
    }

    for (int c = 0; c < (int)HC_N; c++) {
        const uint64_t cur = g_hist_stats.steps_by_class[c];
        const uint64_t d   = cur - e->last_steps_by_class[c];
        e->accum_intervening[c] += d;
        e->last_steps_by_class[c] = cur;
    }
    e->samples++;
}

static inline HistClass on_historyupdate_begin(uint64_t pc, int brtype, int taken, uint64_t nextpc)
{
    const HistClass hc = classify_hist_event(brtype, (taken != 0), pc, nextpc);

    g_hist_stats.calls_total++;
    g_hist_stats.calls_by_class[hc]++;

    perpc_on_event(pc);

    return hc;
}

static inline void on_historyupdate_steps(HistClass hc, int maxt)
{
    const uint64_t steps = (uint64_t)maxt;
    g_hist_stats.steps_total += steps;
    g_hist_stats.steps_by_class[hc] += steps;
}

static inline void on_imli_update(uint64_t brimli, uint64_t taimli)
{
    g_hist_stats.imli_triggers++;
    g_hist_stats.brimli_low4[brimli & 0xFULL]++;
    g_hist_stats.taimli_low4[taimli & 0xFULL]++;
}

static inline void DumpHistoryStats(const char* path)
{
    FILE* f = fopen(path, "w");
    if (!f) return;
    g_hist_stats.dump(f);
    fclose(f);
}

static inline void DumpPerPcStats(const char* path)
{
    FILE* f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "PC samples loop_steps callret_steps indirect_steps misc_steps\n");

    for (uint32_t i = 0; i < PERPC_SIZE; i++) {
        const PerPcEntry* e = &g_perpc[i];
        if (e->samples == 0) continue;

        fprintf(f, "%llx %llu %llu %llu %llu %llu\n",
                (unsigned long long)e->pc_tag,
                (unsigned long long)e->samples,
                (unsigned long long)e->accum_intervening[HC_LOOP],
                (unsigned long long)e->accum_intervening[HC_CALLRET],
                (unsigned long long)e->accum_intervening[HC_INDIRECT],
                (unsigned long long)e->accum_intervening[HC_MISC]);
    }

    fclose(f);
}

} // namespace hist_instr

#else  // INSTRUMENT_HISTORY not defined

namespace hist_instr {
    enum HistClass : uint8_t { HC_LOOP=0, HC_CALLRET=1, HC_INDIRECT=2, HC_MISC=3, HC_N=4 };
    static inline HistClass on_historyupdate_begin(uint64_t, int, int, uint64_t) { return HC_MISC; }
    static inline void on_historyupdate_steps(HistClass, int) {}
    static inline void on_imli_update(uint64_t, uint64_t) {}
    static inline void DumpHistoryStats(const char*) {}
    static inline void DumpPerPcStats(const char*) {}
}

#endif

