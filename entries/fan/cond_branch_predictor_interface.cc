/*
  Copyright (C) 2025 Jun Fan

  The EVES value predictor source code is downloaded from https://www.microarch.org/cvp1/code/Seznec.tar.gz.
  Copyright (C) <2018>, INRIA : Institut National de Recherche en Informatique et en Automatique (French National Research Institute for Computer Science and Applied Mathematics)

  Copyright (C) ARM Limited 2008-2025  All rights reserved.

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "lib/sim_common_structs.h"
#include "cbp2016_tage_sc_l.h"
#include "my_cond_branch_predictor.h"
#include <cassert>

#ifdef BALL_DEBUG_PRINT
#ifdef BALL_DEBUG_PRINT_COVERAGE_ACCURACY
extern uint64_t predict_cnt;
extern uint64_t predict_hit_cnt;
extern uint64_t predict_mis_cnt;

extern uint64_t tage_predict_cnt;
extern uint64_t tage_predict_hit_cnt;
extern uint64_t tage_predict_mis_cnt;

extern uint64_t tage_predict_hit_not_use_ball_predict_cnt;
extern uint64_t tage_predict_hit_use_ball_predict_cnt;
extern uint64_t tage_predict_hit_use_ball_predict_hit_cnt;
extern uint64_t tage_predict_hit_use_ball_predict_mis_cnt;

extern uint64_t tage_predict_mis_not_use_ball_predict_cnt;
extern uint64_t tage_predict_mis_use_ball_predict_cnt;
extern uint64_t tage_predict_mis_use_ball_predict_hit_cnt;
extern uint64_t tage_predict_mis_use_ball_predict_mis_cnt;

#endif // BALL_DEBUG_PRINT_COVERAGE_ACCURACY
#endif // BALL_DEBUG_PRINT

extern std::unordered_map<uint64_t/*key*/, ball_pred_time_hist_t/*val*/> ball_pred_time_histories;
extern std::unordered_map<uint64_t/*key*/, load_pred_time_hist_t/*val*/> load_pred_time_histories;

//
// beginCondDirPredictor()
// 
// This function is called by the simulator before the start of simulation.
// It can be used for arbitrary initialization steps for the contestant's code.
//
void beginCondDirPredictor()
{
    cbp2016_tage_sc_l.setup();
    ball_predictor_impl.setup();
}

//
// notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
// 
// This function is called when any instructions(not just branches) gets fetched.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction and fetch_cycle are also provided as inputs
//
void notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
{
    ball_predictor_impl.load_pattern_table_update_fetch_by_load(seq_no, pc, piece);
}

//
// get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle)
// 
// This function is called by the simulator for predicting conditional branches.
// input values are unique identifying ids(seq_no, piece) and PC of the branch.
// return value is the predicted direction. 
//
bool get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle)
{
    const bool tage_sc_l_pred =  cbp2016_tage_sc_l.predict(seq_no, piece, pc);
    const bool ball_pred = ball_predictor_impl.predict(seq_no, piece, pc, tage_sc_l_pred);

    return ball_pred;
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
        cbp2016_tage_sc_l.history_update(seq_no, piece, pc, br_type, ball_pred_time_histories.at(seq_no).tage_sc_l_predict, resolve_dir, next_pc);
    }
    else
    {
        cbp2016_tage_sc_l.TrackOtherInst(pc, br_type, pred_dir, resolve_dir, next_pc);
    }

    // ==========================================================
    // ==========================================================
    // EVES from CVP1 (3/n) (begin)
    // ==========================================================
    // ==========================================================
    const bool cond_br = (inst_class == InstClass::condBranchInstClass);
    const bool uncond_dir_br = (inst_class == InstClass::uncondDirectBranchInstClass);
    const bool uncond_ind_br = (inst_class == InstClass::uncondIndirectBranchInstClass);

    const bool eligible = 0;
    uint8_t prediction_result = 2;

    uint64_t src1 = 0xdeadbeef;
    uint64_t src2 = 0xdeadbeef;
    uint64_t src3 = 0xdeadbeef;
    uint64_t dst  = 0xdeadbeef;

    if (cond_br || uncond_dir_br || uncond_ind_br) {
        ball_predictor_impl.speculativeUpdate (seq_no,	// dynamic micro-instruction # (starts at 0 and increments indefinitely)
	       eligible,	// true: instruction is eligible for value prediction. false: not eligible.
	       prediction_result,	// 0: incorrect, 1: correct, 2: unknown (not revealed)
	       // Note: can assemble local and global branch history using pc, next_pc, and insn.
	       //pc, _exec_info.next_pc, _exec_info.dec_info.insn_class, piece,
	       pc, next_pc, inst_class, piece,
	       // Note: up to 3 logical source register specifiers, up to 1 logical destination register specifier.
	       // 0xdeadbeef means that logical register does not exist.
	       // May use this information to reconstruct architectural register file state (using log. reg. and value at updatePredictor()).
	       src1, src2, src3, dst);
    }
    // ==========================================================
    // ==========================================================
    // EVES from CVP1 (3/n) (end)
    // ==========================================================
    // ==========================================================
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
}

//
// notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
// 
// This function is called when any load/store instructions complete agen.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, decode info, mem_va and mem_sz and agen_cycle are also provided as inputs
//
void notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
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
void notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle)
{
    const bool is_branch = is_br(_exec_info.dec_info.insn_class);
    if(is_branch)
    {
        if (is_cond_br(_exec_info.dec_info.insn_class))
        {
            const bool _resolve_dir = _exec_info.taken.value();
            const uint64_t _next_pc = _exec_info.next_pc;

            cbp2016_tage_sc_l.update(seq_no, piece, pc, _resolve_dir, ball_pred_time_histories.at(seq_no).tage_sc_l_predict, _next_pc);

            ball_predictor_impl.cond_br_chain_table_update_resolve(seq_no, pc, _resolve_dir, pred_dir, _exec_info.dec_info.src_reg_info);

            #ifdef BALL_DEBUG_PRINT
            #ifdef BALL_DEBUG_PRINT_MISPREDICT
            if (pc == BALL_DEBUG_PRINT_PC && BALL_DEBUG_PRINT_PC_EN ||
                seq_no == BALL_DEBUG_PRINT_SEQ_NO && BALL_DEBUG_PRINT_SEQ_EN ||
                BALL_DEBUG_PRINT_ALL_EN) {
                fprintf(stdout, "ball_predict_valid: %#x; use_ball_predict: %#x; ball hitpredict: %#x; seq_no: %#zx; pc: %#zx; resolve: %#x; pred: %#x\n", ball_pred_time_histories.at(seq_no).ball_predict_valid, ball_pred_time_histories.at(seq_no).use_ball_predict, (_resolve_dir == ball_pred_time_histories.at(seq_no).ball_predict), seq_no, pc, _resolve_dir, pred_dir);
            }
            #endif // BALL_DEBUG_PRINT_MISPREDICT
            #endif // BALL_DEBUG_PRINT

            #ifdef BALL_DEBUG_PRINT
            #ifdef BALL_DEBUG_PRINT_COVERAGE_ACCURACY
            bool hit = (_resolve_dir == pred_dir);
            bool tage_hit = (_resolve_dir == ball_pred_time_histories.at(seq_no).tage_sc_l_predict);
            bool ball_hit = (_resolve_dir == ball_pred_time_histories.at(seq_no).ball_predict);

            predict_cnt += 1;
            predict_hit_cnt += hit;
            predict_mis_cnt += !hit;

            tage_predict_cnt += 1;
            tage_predict_hit_cnt += tage_hit;
            tage_predict_mis_cnt += !tage_hit;

            tage_predict_mis_not_use_ball_predict_cnt += !tage_hit && !ball_pred_time_histories.at(seq_no).use_ball_predict;
            tage_predict_mis_use_ball_predict_cnt += !tage_hit && ball_pred_time_histories.at(seq_no).use_ball_predict;
            tage_predict_mis_use_ball_predict_hit_cnt += !tage_hit && ball_pred_time_histories.at(seq_no).use_ball_predict && ball_hit;
            tage_predict_mis_use_ball_predict_mis_cnt += !tage_hit && ball_pred_time_histories.at(seq_no).use_ball_predict && !ball_hit;

            tage_predict_hit_not_use_ball_predict_cnt += tage_hit && !ball_pred_time_histories.at(seq_no).use_ball_predict;
            tage_predict_hit_use_ball_predict_cnt += tage_hit && ball_pred_time_histories.at(seq_no).use_ball_predict;
            tage_predict_hit_use_ball_predict_hit_cnt += tage_hit && ball_pred_time_histories.at(seq_no).use_ball_predict && ball_hit;
            tage_predict_hit_use_ball_predict_mis_cnt += tage_hit && ball_pred_time_histories.at(seq_no).use_ball_predict && !ball_hit;

            #endif // BALL_DEBUG_PRINT_COVERAGE_ACCURACY
            #endif // BALL_DEBUG_PRINT

            ball_pred_time_histories.erase(seq_no);

        }
        else
        {
            assert(pred_dir);
        }
    }

    if (is_store(_exec_info.dec_info.insn_class)) {
        ball_predictor_impl.store_resolve_queue_update_resolve(_exec_info.mem_va.value() & BALL_MEM_VA_MASK);
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
    const bool is_load_inst = is_load(_exec_info.dec_info.insn_class);
    const bool is_store_inst = is_store(_exec_info.dec_info.insn_class);
    const bool is_alu_inst = (_exec_info.dec_info.insn_class == InstClass::aluInstClass);
    const bool is_fp_inst = (_exec_info.dec_info.insn_class == InstClass::fpInstClass);
    const bool is_branch = is_br(_exec_info.dec_info.insn_class);

    if (is_load_inst || is_store_inst) {
        ball_predictor_impl.bp_data_cache_update_commit(seq_no, pc, piece, _exec_info.mem_va.value() & BALL_MEM_VA_MASK, _exec_info.mem_sz.value(), _exec_info.dec_info.insn_class,  _exec_info.dec_info.src_reg_info, _exec_info.dst_reg_value);
    } 

    if (is_store_inst) {
        ball_predictor_impl.store_resolve_queue_update_commit(_exec_info.mem_va.value() & BALL_MEM_VA_MASK);
    }

    if (is_load_inst) {
        ball_predictor_impl.load_pattern_table_update_commit_by_load(seq_no, pc, piece, _exec_info.mem_va.value() & BALL_MEM_VA_MASK, _exec_info.mem_sz.value(), _exec_info.dec_info.src_reg_info);
    }

    if (is_alu_inst || is_fp_inst) {
        ball_predictor_impl.alu_pattern_table_update_commit_by_alu(pc, _exec_info.dec_info.src_reg_info, _exec_info.dec_info.dst_reg_info, _exec_info.dst_reg_value);
    }

    // rf_update should be invoked later then alu_pattern_table_update_commit_by_alu,
    // e.g. for r1 = r1 - r2, if invoke rf_update first, the value of source operand r1 will become the result value,
    // but should use the value before committed.
    ball_predictor_impl.rf_update(seq_no, piece, _exec_info.dec_info.dst_reg_info, _exec_info.dst_reg_value);

    if(is_branch)
    {
        if (is_cond_br(_exec_info.dec_info.insn_class))
        {
            const bool _resolve_dir = _exec_info.taken.value();
            const uint64_t _next_pc = _exec_info.next_pc;
            ball_predictor_impl.cond_br_chain_table_update_commit(seq_no, pc, _resolve_dir, pred_dir, _exec_info.dec_info.src_reg_info);
        }
        else
        {
            assert(pred_dir);
        }
    }

    ball_predictor_impl.commit_queue_update(pc, piece, _exec_info.dec_info.insn_class, _exec_info.dec_info.src_reg_info, _exec_info.dec_info.dst_reg_info);

    // ==========================================================
    // ==========================================================
    // EVES from CVP1 (4/n) (begin)
    // ==========================================================
    // ==========================================================
    bool load_may_vtage_predict = (load_pred_time_histories.find(seq_no) != load_pred_time_histories.end());
    bool load_vtage_predict = 0;
    if (load_may_vtage_predict) {
        load_vtage_predict = load_pred_time_histories.at(seq_no).load_pattern_table_hit;
    } 

    // At fetch stage, non-load pc may hit load_pattern_table to make prediction,
    // non-load uop has no mem_va field.
    if (is_load_inst && load_vtage_predict) {
        bool make_prediction = load_pred_time_histories.at(seq_no).make_prediction;
        uint64_t predicted_addr = load_pred_time_histories.at(seq_no).predicted_addr;
        uint64_t actual_addr = _exec_info.mem_va.value() & BALL_MEM_VA_MASK;

        // set to a large value, so vtage will be trained.
        uint64_t actual_latency = 1000000;

        uint8_t prediction_result = make_prediction && (predicted_addr != actual_addr) ? 0 :
                                    make_prediction && (predicted_addr == actual_addr) ? 1 : 2;

        #ifdef BALL_DEBUG_PRINT
        #ifdef BALL_DEBUG_PRINT_VTAGE_PREDICT
        fprintf(stdout, "vtage predict: make_predict: %#x, seq_no: %#zx, pc: %#zx, predicted_addr: %#zx, actual_addr: %#zx, prediction_result: %d\n",
                        make_prediction, seq_no, pc, predicted_addr, actual_addr, prediction_result);
        #endif // BALL_DEBUG_PRINT_VTAGE_PREDICT
        #endif // BALL_DEBUG_PRINT

        bool eligible = 1;

        uint64_t src1 = (_exec_info.dec_info.src_reg_info.size() > 0) ? _exec_info.dec_info.src_reg_info[0] : 0xdeadbeef;
        uint64_t src2 = (_exec_info.dec_info.src_reg_info.size() > 1) ? _exec_info.dec_info.src_reg_info[1] : 0xdeadbeef;
        uint64_t src3 = (_exec_info.dec_info.src_reg_info.size() > 2) ? _exec_info.dec_info.src_reg_info[2] : 0xdeadbeef;
        uint64_t dst  = (_exec_info.dec_info.dst_reg_info.has_value()) ? _exec_info.dec_info.dst_reg_info.value() : 0xdeadbeef;

        ball_predictor_impl.speculativeUpdate (seq_no,	// dynamic micro-instruction # (starts at 0 and increments indefinitely)
	       eligible,	// true: instruction is eligible for value prediction. false: not eligible.
	       prediction_result,	// 0: incorrect, 1: correct, 2: unknown (not revealed)
	       // Note: can assemble local and global branch history using pc, next_pc, and insn.
	       //pc, _exec_info.next_pc, _exec_info.dec_info.insn_class, piece,
	       pc, _exec_info.next_pc, _exec_info.dec_info.insn_class, piece,
	       // Note: up to 3 logical source register specifiers, up to 1 logical destination register specifier.
	       // 0xdeadbeef means that logical register does not exist.
	       // May use this information to reconstruct architectural register file state (using log. reg. and value at updatePredictor()).
	       src1, src2, src3, dst);

        ball_predictor_impl.updatePredictor(seq_no, 0, actual_addr, actual_latency);
    }

    // need explicitly erase here, because non-load pc after hashed may conflict with load pc,
    // if only erase when commit uop is load, then the history will never deallocate
    load_pred_time_histories.erase(seq_no);
    // ==========================================================
    // ==========================================================
    // EVES from CVP1 (4/n) (end)
    // ==========================================================
    // ==========================================================
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
    ball_predictor_impl.terminate();
}
