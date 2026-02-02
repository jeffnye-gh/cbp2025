/*
  Copyright (C) ARM Limited 2008-2025  All rights reserved.

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// This file provides a sample predictor integration based on the interface provided.
#define FMT_HEADER_ONLY
#include "lib/spdlog/fmt/bundled/format.h"

#include <string>
#include <string_view>
#include "lib/sim_common_structs.h"
#include "cbp2016_tage_sc_l.h"
#include "my_cond_branch_predictor.h"
#include "correlation.h"
#include "lib/parameters.h"
#include <cassert>
#include <unordered_map>
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <numeric>
#include <array>
#include <deque>
#include <unordered_set>
#include <set>
#include "bloom.h"
#include "predictor_defines.h"

using u64 = uint64_t;
using i64 = int64_t;

int temp_counter = 1;
static std::set<uint64_t> seen_pcs;
static std::deque<uint64_t> last_100_indirects;
static std::deque<uint64_t> last_100_longbrs;
static std::deque<uint64_t> last_1K_indirects;
static std::deque<uint64_t> last_1K_longbrs;
static std::deque<u64> last_100_calls;
static std::deque<u64> last_100_forward_brs;
static std::deque<u64> last_100_backward_brs;
static std::deque<u64> last_100_bb_sz;
static std::deque<u64> last_100_bb_count;
static std::deque<u64> last_1K_calls;
static std::deque<u64> last_1K_forward_brs;
static std::deque<u64> last_1K_backward_brs;
static std::deque<u64> last_1K_bb_sz;
static std::deque<u64> last_1K_bb_count;
static constexpr uint64_t uniq(uint64_t const seq_no, uint8_t const piece) __attribute((pure));
static u64 avg(std::deque<u64> const &) __attribute((pure));

static constexpr uint64_t uniq(uint64_t const seq_no, uint8_t const piece)
{
    assert(piece < 16);
    return (seq_no << 4) | (piece & 0x000F);
}

u64 avg(std::deque<u64> const &q)
{
    u64 const count = q.size();
    assert(count > 0 && "must be non-zero range");
    u64 const sum = std::accumulate(q.begin(), q.end(), 0UL);
    return sum / count;
}

static uint64_t preds_from_csc = 0;
static uint64_t preds_from_tage = 0;

// table to keep track of branch latency (for reducing wrong path cycles)

static Correlation correlator;
static std::vector<uint64_t> knobs;
namespace
{
    enum Knobs
    {
        CallDepth,

        GlobalBias,

        IndirectInLast100,
        IndirectInLast1K,

        // long br is more than 4k (page sz)
        LongBrsInLast100,
        LongBrsInLast1K,

        Size
    };
}
static std::vector<std::string> const knob_names = {
    /*[Knobs::CallDepth] = */ "call_depth",
    /*[Knobs::GlobalBias] = */ "global_bias",
    /*[Knobs::IndirectInLast100] = */ "indirect_100",
    /*[Knobs::IndirectInLast1K] = */ "indirect_1K",
    /*[Knobs::LongBrsInLast100] = */ "long_br_100",
    /*[Knobs::LongBrsInLast1K] = */ "long_br_1K",

};

static std::vector<bool> const knob_enable = {
    /*[Knobs::CallDepth] = */ true,
    /*[Knobs::GlobalBias] = */ true,
    /*[Knobs::IndirectInLast100] = */ true,
    /*[Knobs::IndirectInLast1K] = */ true,
    /*[Knobs::LongBrsInLast100] = */ true,
    /*[Knobs::LongBrsInLast1K] = */ true,

};
//
// beginCondDirPredictor()
//
// This function is called by the simulator before the start of simulation.
// It can be used for arbitrary initialization steps for the contestant's code.
//
//
// int bloom_filter_switch_threshold = 13*1024;

#define BLOOM_THRESHOLD 40 * 1024

static int bloom_filter_switch_threshold = BLOOM_THRESHOLD;
static int epoch;
static int num_unique_elements_epoch = 0;

/// this is num of bits
// BloomFilter1<BLOOM_SIZE> bloom1 = BloomFilter1<BLOOM_SIZE>(2);
// BloomFilter2<BLOOM_SIZE> bloom2 = BloomFilter2<BLOOM_SIZE>(2);
static BloomFilter1<BLS> bloom1 = BloomFilter1<BLS>(BLB);
// static BloomFilter2<BLS> bloom2 = BloomFilter2<BLS>(BLB);
void beginCondDirPredictor()
{
    knobs.clear();
    knobs.reserve(Knobs::Size);
    knobs.insert(knobs.end(), Knobs::Size, 0);
    // setup sample_predictor
    cbp2016_tage_sc_l.setup();
    cond_predictor_impl.setup();

    assert(knob_enable.size() == knob_names.size());
    assert(knob_names.size() == Knobs::Size);
    correlator.init_pred(knob_names, knob_enable);
    epoch = 0;
    num_unique_elements_epoch = 0;
}

//
// notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
//
// This function is called when any instructions(not just branches) gets fetched.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction and fetch_cycle are also provided as inputs
//
void notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
{
}

// set of PCs that should be predicted using tage instead of the cold_sc
static std::unordered_set<uint64_t> pc_2_tage;

//
// get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle)
//
// This function is called by the simulator for predicting conditional branches.
// input values are unique identifying ids(seq_no, piece) and PC of the branch.
// return value is the predicted direction.
//
bool get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle)
{
    knobs[IndirectInLast100] = last_100_indirects.size();
    knobs[IndirectInLast1K] = last_1K_indirects.size();
    knobs[LongBrsInLast100] = last_100_longbrs.size();
    knobs[LongBrsInLast1K] = last_1K_longbrs.size();

    const bool cold_sc_pred = correlator.pred(knobs, uniq(seq_no, piece));

    const bool tage_sc_l_pred = cbp2016_tage_sc_l.predict(seq_no, piece, pc);
    bool my_prediction = cond_predictor_impl.predict(seq_no, piece, pc, tage_sc_l_pred);

    if (true && (!bloom1.possiblyContains(pc)))
    {
        my_prediction = cold_sc_pred;
        preds_from_csc++;
    }
    else
    {
        preds_from_tage++;
    }

    return my_prediction;
}

//
// spec_update(uint64_t seq_no, uint8_t piece, uint64_t pc, InstClass inst_class, const bool resolve_dir, const bool pred_dir, const uint64_t next_pc)
//
// This function is called by the simulator for updating the history vectors and any state that needs to be updated speculatively.
// The function is called for all the branches (not just conditional branches). To faciliate accurate history updates, spec_update is called right
// after a prediction is made.
// input values are unique identifying ids(seq_no, piece), PC of the instruction, instruction class, predicted/resolve direction and the next_pc
//
void spec_update(uint64_t seq_no, uint8_t piece, uint64_t pc, InstClass inst_class, const bool resolve_dir, const bool pred_dir, const uint64_t next_pc)
{

    assert(is_br(inst_class));
    assert(knobs[CallDepth] != UINT64_MAX);
    int br_type = 0;

    if (piece == 0)
    {

        u64 const bb_start_seq_no = last_100_bb_count.empty() ? 0 : last_100_bb_count.back();
        last_100_bb_count.push_back(seq_no);
        last_1K_bb_count.push_back(seq_no);

        u64 const bb_sz = seq_no - bb_start_seq_no;
        last_100_bb_sz.push_back(bb_sz);
        last_1K_bb_sz.push_back(bb_sz);
    }

    i64 const delta = next_pc - pc;
    if (delta > 0)
    {
        // forward br
        last_100_forward_brs.push_back(seq_no);
        last_1K_forward_brs.push_back(seq_no);
    }

    if (delta < 0)
    {
        // backward br
        last_100_backward_brs.push_back(seq_no);
        last_1K_backward_brs.push_back(seq_no);
    }

    if (piece == 0 && abs(delta) > 4096)
    {
        last_100_longbrs.push_back(seq_no);
        last_1K_longbrs.push_back(seq_no);
    }

    switch (inst_class)
    {
    case InstClass::condBranchInstClass:
        if (resolve_dir)
        {
            if (knobs[Knobs::GlobalBias] != 7)
                ++knobs[GlobalBias];
        }
        else
        {
            if (knobs[GlobalBias] != 0)
                --knobs[GlobalBias];
        }
        br_type = 1;
        break;
    case InstClass::uncondDirectBranchInstClass:
        br_type = 0;
        break;
    case InstClass::uncondIndirectBranchInstClass:
        br_type = 2;

        last_100_indirects.push_back(seq_no);
        last_1K_indirects.push_back(seq_no);

        break;
    case InstClass::callDirectInstClass:
        ++knobs[CallDepth];

        last_100_calls.push_back(seq_no);
        last_1K_calls.push_back(seq_no);

        br_type = 0;
        break;
    case InstClass::callIndirectInstClass:
        ++knobs[CallDepth];

        last_100_calls.push_back(seq_no);
        last_1K_calls.push_back(seq_no);

        br_type = 2;
        break;
    case InstClass::ReturnInstClass:
        br_type = 2;
        if (knobs[CallDepth] != 0)
            --knobs[CallDepth];
        break;
    default:
        assert(false);
    }

    if (inst_class == InstClass::condBranchInstClass)
    {
        cbp2016_tage_sc_l.history_update(seq_no, piece, pc, br_type, pred_dir, resolve_dir, next_pc);
        cond_predictor_impl.history_update(seq_no, piece, pc, resolve_dir, next_pc);
    }
    else
    {
        cbp2016_tage_sc_l.TrackOtherInst(pc, br_type, pred_dir, resolve_dir, next_pc);
    }

    int pop_count = 0;

    while (!last_100_indirects.empty() && last_100_indirects.front() < (seq_no - 100))
    {
        last_100_indirects.pop_front();
        pop_count++;
    }
    // assert(pop_count <= 1);
    pop_count = 0;

    while (!last_100_longbrs.empty() && last_100_longbrs.front() < (seq_no - 100))
    {
        last_100_longbrs.pop_front();
        pop_count++;
    }
    // assert(pop_count <= 1);
    pop_count = 0;

    while (!last_100_calls.empty() && last_100_calls.front() < (seq_no - 100))
    {
        last_100_calls.pop_front();
    }

    // XXX: this is sus
    while (!last_100_bb_count.empty() && last_100_bb_count.front() < (seq_no - 100))
    {
        assert(last_100_bb_sz.size() == last_100_bb_count.size());
        last_100_bb_count.pop_front();
        last_100_bb_sz.pop_front();
    }

    while (!last_100_backward_brs.empty() && last_100_backward_brs.front() < (seq_no - 100))
    {
        last_100_backward_brs.pop_front();
    }

    while (!last_100_forward_brs.empty() && last_100_forward_brs.front() < (seq_no - 100))
    {
        last_100_forward_brs.pop_front();
    }

    while (!last_1K_indirects.empty() && last_1K_indirects.front() < (seq_no - 1000))
    {
        last_1K_indirects.pop_front();
        pop_count++;
    }
    // assert(pop_count <= 1);
    pop_count = 0;

    while (!last_1K_longbrs.empty() && last_1K_longbrs.front() < (seq_no - 1000))
    {
        last_1K_longbrs.pop_front();
        pop_count++;
    }
    // assert(pop_count <= 1);
    pop_count = 0;

    while (!last_1K_calls.empty() && last_1K_calls.front() < (seq_no - 1000))
    {
        last_1K_calls.pop_front();
    }

    // XXX: this is also sus
    while (!last_1K_bb_count.empty() && last_1K_bb_count.front() < (seq_no - 1000))
    {
        assert(last_1K_bb_count.size() == last_1K_bb_sz.size());
        last_1K_bb_count.pop_front();
        last_1K_bb_sz.pop_front();
    }

    while (!last_1K_backward_brs.empty() && last_1K_backward_brs.front() < (seq_no - 1000))
    {
        last_1K_backward_brs.pop_front();
    }

    while (!last_1K_forward_brs.empty() && last_1K_forward_brs.front() < (seq_no - 1000))
    {
        last_1K_forward_brs.pop_front();
    }
}

// how frequently to decrement the h2p counters in # of instructions.
static constexpr uint64_t dec_freq = 1000;

//
// notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const
// DecodeInfo& _decode_info, const uint64_t decode_cycle)
//
// This function is called when any instructions(not just branches) gets
// decoded. Along with the unique identifying ids(seq_no, piece), PC of the
// instruction, decode info and cycle are also provided as inputs
//
// For the sample predictor implementation, we do not leverage decode
// information
void notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo &_decode_info, const uint64_t decode_cycle)
{
}

//
// notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
//
// This function is called when any load/store instructions complete agen.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, decode info, mem_va and mem_sz and agen_cycle are also provided as inputs
//
void notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo &_decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
{
}

//
// notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle)
//
// This function is called when any instructions(not just branches) gets executed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
// For conditional branches, we use this information to update the predictor.
// At the moment, we do not consider updating any other structure, but the contestants are allowed to  update any other predictor state.
void notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo &_exec_info, const uint64_t execute_cycle)
{
    const bool is_branch = is_br(_exec_info.dec_info.insn_class);
    if (is_branch)
    {
        if (is_cond_br(_exec_info.dec_info.insn_class))
        {
            const bool _resolve_dir = _exec_info.taken.value();
            const uint64_t _next_pc = _exec_info.next_pc;

            // misp should fall back to tage. alloc in tage, and alla dat
            if (_resolve_dir != pred_dir)
            {
                pc_2_tage.insert(pc); // oracle implementation
                                      //
                // if(!bloom1.possiblyContains(pc) || !bloom2.possiblyContains(pc)){
                if (!bloom1.possiblyContains(pc) && epoch % 2 == 0)
                {
                    num_unique_elements_epoch++;
                    bloom1.insert(pc);
                }
            }

            bool taken = _resolve_dir;
            bool misp = _resolve_dir != pred_dir;
            correlator.update(taken, pred_dir, uniq(seq_no, piece));

            if (bloom1.possiblyContains(pc) /* || bloom2.possiblyContains(pc))*/ /* || cbp2016_tage_sc_l.HitBank > 0*/)
            {
                cbp2016_tage_sc_l.update(seq_no, piece, pc, _resolve_dir, pred_dir, _next_pc);
            }
            else
            {
                cbp2016_tage_sc_l.kill_checkpoint(seq_no, piece);
            }

            cond_predictor_impl.update(seq_no, piece, pc, _resolve_dir, pred_dir, _next_pc);
            pred_used.erase(seq_no);
            ldao.erase(seq_no);
        }
        else
        {
            assert(pred_dir);
        }
    }
}

//
// notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t commit_cycle)
//
// This function is called when any instructions(not just branches) gets committed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
// For the sample predictor implementation, we do not leverage commit information

void notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo &_exec_info, const uint64_t commit_cycle)
{
    const bool is_branch = is_br(_exec_info.dec_info.insn_class);
    if (is_branch)
    {
        if (is_cond_br(_exec_info.dec_info.insn_class))
        {
            const bool _resolve_dir = _exec_info.taken.value();
            const uint64_t _next_pc = _exec_info.next_pc;
        }
        else
        {
            assert(pred_dir);
        }
    }
}

//
// endCondDirPredictor()
//
// This function is called by the simulator at the end of simulation.
// It can be used by the contestant to print out other contestant-specific measurements.
//
void endCondDirPredictor()
{
    cbp2016_tage_sc_l.terminate();
    cond_predictor_impl.terminate();
}
