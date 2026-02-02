////////////////////////////////////////////////////////////////////////
//
//  Code submitted for the 6th Championship Branch Prediction (CBP2025)
//
//  Author: Alberto Ros (aros@ditec.um.es)
//
//  Paper #2: A Deep Dive Into TAGE-SC-L
//
//  Code: Interface to the conditional branch predictor
//
//  Note: This code is derived from the TAGE-SC-L code by André Seznec
//    at the 5th Championship Branch Prediction and provided by the  
//    CBP2025 organizers as baseline.
//
////////////////////////////////////////////////////////////////////////

#include "lib/sim_common_structs.h"
#include "tage_sc_l.h"
#include <cassert>

void beginCondDirPredictor() {
  tage_sc_l.setup();
}

void notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle) { }

bool get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle) {
  return tage_sc_l.predict(seq_no, piece, pc);
}

void spec_update(uint64_t seq_no, uint8_t piece, uint64_t pc, InstClass inst_class, const bool resolve_dir, const bool pred_dir, const uint64_t next_pc) {
  assert(is_br(inst_class));
  int br_type = 0;
  switch(inst_class) {
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

  tage_sc_l.historyUpdate(pc, br_type, resolve_dir, pred_dir, next_pc);    

}

void notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t decode_cycle) { }

void notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle) { }

void notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle) {
  const bool is_branch = is_br(_exec_info.dec_info.insn_class);
  if(is_branch) {
    if (is_cond_br(_exec_info.dec_info.insn_class)) {
      const bool _resolve_dir = _exec_info.taken.value();
      const uint64_t _next_pc = _exec_info.next_pc;
      tage_sc_l.update(seq_no, piece, pc, _resolve_dir, pred_dir, _next_pc);
    } else {
      assert(pred_dir);
    }
  }
}

void notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t commit_cycle) { }

void endCondDirPredictor () { }
