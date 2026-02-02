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

#include "lib/sim_common_structs.h"
#include "cbp2016_tage_sc_l.h"
#include "my_cond_branch_predictor.h"
#include <cassert>

#include "idioms/debug_op_printer.h"
#include "idioms/idiom_tracker.h"
#include "idioms/uop_tracker.h"

#ifndef NO_IDIOT
    #define IDIOT
    #define PRINT_SIZE
#endif

//
// beginCondDirPredictor()
// 
// This function is called by the simulator before the start of simulation.
// It can be used for arbitrary initialization steps for the contestant's code.
//
void beginCondDirPredictor()
{
    // setup sample_predictor
    std::cout << "TAGE_SIZE:" << predictorsize() << std::endl;
    cbp2016_tage_sc_l.setup();
    cond_predictor_impl.setup();
#ifdef IDIOT
#ifdef PRINT_SIZE
    std::cerr << "IDIOT size: "
        << idiot.calculate_size_in_bits() + uop_max /* cond_predictions_ */
        << " bits" << std::endl;
    std::cerr << "  of which: IDIOT_FOR size: "
        << idiot_for_t().calculate_size_in_bits()
        << " bits" << std::endl;
    std::cerr << "            IDIOT_ASCIZ size: "
        << idiot_asciz_t().calculate_size_in_bits()
        << " bits" << std::endl;
    std::cerr << "uop_tracker size: "
        << uop_tracker.calculate_size_in_bits()
        << " bits" << std::endl;
#endif
#endif
}

//
// notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
// 
// This function is called when any instructions(not just branches) gets fetched.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction and fetch_cycle are also provided as inputs
//
void notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
{
    uop_tracker.allocate(seq_no, piece, pc);
#ifdef IDIOT
    idiot.notify_fetch(uop_tracker.lookup_id(seq_no, piece), std::make_pair(pc, piece));
#endif
}

#ifdef IDIOT
/* keep track of what the base predictor did as well as IDIOT */
static std::array<bool, uop_max> cond_predictions_;
#endif

//
// get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle)
// 
// This function is called by the simulator for predicting conditional branches.
// input values are unique identifying ids(seq_no, piece) and PC of the branch.
// return value is the predicted direction. 
//
bool get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle)
{
#ifdef IDIOT
    const auto idiot_pred = idiot.predict_branch(uop_tracker.lookup_id(seq_no, piece), std::make_pair(pc, piece));
#endif
    CBP2016_TAGE_SC_L::PredictionResult tage_sc_l_pred =  cbp2016_tage_sc_l.predict(seq_no, piece, pc);
    const bool my_prediction = cond_predictor_impl.predict(seq_no, piece, pc, tage_sc_l_pred);
#ifdef IDIOT
    cond_predictions_[uop_tracker.lookup_id(seq_no, piece) % uop_max] = my_prediction;
    if (idiot_pred.has_value())
        return idiot_pred.value();
#endif
    return my_prediction;
}

#ifdef DEBUG_MISPRED
std::unordered_map<uint64_t, uint64_t> mispred_freq; // Number of mispredicts. Used for debug printing only.
std::unordered_map<uint64_t, uint64_t> condbr_freq; // Number of occurrences of this conditional br. Used for debug printing only.
#endif

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
    bool my_prediction = pred_dir;
#ifdef IDIOT
    my_prediction = cond_predictions_[uop_tracker.lookup_id(seq_no, piece) % uop_max];
    idiot.spec_update(uop_tracker.lookup_id(seq_no, piece), std::make_pair(pc, piece), resolve_dir, pred_dir, my_prediction);
    /* overwrite pred_dir ignoring IDIOT's prediciton */
#endif
    assert(is_br(inst_class));
    int br_type = 0;
    switch(inst_class)
    {
        case InstClass::condBranchInstClass:
            br_type = 1;
            break;
        case InstClass::uncondDirectBranchInstClass:
            br_type = 0; 
            break;
        case InstClass::uncondIndirectBranchInstClass:
            br_type = 2;
            break;
        case InstClass::callDirectInstClass:
            br_type = 0;
            break;
        case InstClass::callIndirectInstClass:
            br_type = 2; 
            break;
        case InstClass::ReturnInstClass:
            br_type = 2;
            break;
        default:
            assert(false);
    }

    if(inst_class == InstClass::condBranchInstClass)
    {
        cbp2016_tage_sc_l.history_update(seq_no, piece, pc, br_type, my_prediction, resolve_dir, next_pc);
        cond_predictor_impl.history_update(seq_no, piece, pc, resolve_dir, next_pc);
    }
    else
    {
        cbp2016_tage_sc_l.TrackOtherInst(pc, br_type, my_prediction, resolve_dir, next_pc);
    }

    #ifdef DEBUG_MISPRED
    condbr_freq[pc]++;
    if (pred_dir != resolve_dir) {
        mispred_freq[pc]++;
        float miss_rate = ((float)mispred_freq[pc]) / ((float)condbr_freq[pc]);
        int miss_pki = (int)(miss_rate*1000);
        std::cout << "MISPRED " << std::hex << seq_no << "." << ((int)piece) << " PC" << pc << " "
                << (resolve_dir ? "T" : "N") << (pred_dir ? "t" : "n")
                << std::dec << " misses:" << mispred_freq[pc] << "/" << condbr_freq[pc] << " "
                << (miss_pki/10) << "." << (miss_pki%10) << "%" << std::endl;
    }
    #endif
}

//
// notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t decode_cycle)
// 
// This function is called when any instructions(not just branches) gets decoded.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, decode info and cycle are also provided as inputs
//
// For the sample predictor implementation, we do not leverage decode information
void notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t decode_cycle)
{
    auto id = uop_tracker.notify_decode(seq_no, piece, _decode_info);
#ifdef IDIOT
    idiot.notify_decode(id, std::make_pair(pc, piece), _decode_info);
#endif
}

//
// notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
// 
// This function is called when any load/store instructions complete agen.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, decode info, mem_va and mem_sz and agen_cycle are also provided as inputs
//
void notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
{
#ifdef IDIOT
    idiot.notify_agen(uop_tracker.lookup_id(seq_no, piece), std::make_pair(pc, piece), mem_va, mem_sz);
#endif
}

//
// notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle)
// 
// This function is called when any instructions(not just branches) gets executed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
// For conditional branches, we use this information to update the predictor.
// At the moment, we do not consider updating any other structure, but the contestants are allowed to  update any other predictor state.
void notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle)
{
    auto uop = uop_tracker.notify_execute(seq_no, piece, _exec_info);
#ifdef IDIOT
    idiot.notify_execute(uop_tracker.lookup_id(seq_no, piece), std::make_pair(pc, piece), uop, _exec_info);
#endif
    const bool is_branch = is_br(_exec_info.dec_info.insn_class);

    if(is_branch)
    {
        if (is_cond_br(_exec_info.dec_info.insn_class))
        {
            const bool _resolve_dir = _exec_info.taken.value();
            bool my_prediction = pred_dir;
#ifdef IDIOT
            my_prediction = cond_predictions_[uop.id % uop_max];
            idiot.update(uop_tracker.lookup_id(seq_no, piece), std::make_pair(pc, piece), _resolve_dir, pred_dir);
#endif
            const uint64_t _next_pc = _exec_info.next_pc;
            cbp2016_tage_sc_l.update(seq_no, piece, pc, _resolve_dir, my_prediction, _next_pc);
            cond_predictor_impl.update(seq_no, piece, pc, _resolve_dir, my_prediction, _next_pc);
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
void notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t commit_cycle)
{
    const auto info = uop_tracker.notify_commit(seq_no, piece, _exec_info);
    debug_print_execute(seq_no, piece, pc, _exec_info, info);
}

//
// endCondDirPredictor()
//
// This function is called by the simulator at the end of simulation.
// It can be used by the contestant to print out other contestant-specific measurements.
//
void endCondDirPredictor ()
{
    cbp2016_tage_sc_l.terminate();
    cond_predictor_impl.terminate();
}
