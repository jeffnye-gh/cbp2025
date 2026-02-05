#include "my_cond_branch_predictor.h"
#include "nhist1.hpp"                 // defines nHIST1::HIST1
#include "trace.h"

// -----------------------------------------------------------------------
void CBP2025_HIST1::setup()
{
    //TR("+setup");
    p_impl = std::make_shared<nHIST1::HIST1>();
    p_impl->setup();
}
// -----------------------------------------------------------------------
bool CBP2025_HIST1::predict(uint64_t seq_no, uint8_t piece, uint64_t PC)
{
    //TR("+predict");
  return p_impl->predict(seq_no, piece, PC);
}

// -----------------------------------------------------------------------
void CBP2025_HIST1::update(uint64_t seq_no, uint8_t piece, uint64_t PC,
                           bool resolveDir, bool predDir, uint64_t nextPC)
{
    //TR("+update");
  p_impl->update(seq_no, piece, PC, resolveDir, predDir, nextPC);
}

// -----------------------------------------------------------------------
void CBP2025_HIST1::history_update(uint64_t seq_no, uint8_t piece, uint64_t PC, int brtype, 
                                   bool pred_dir, bool resolve_dir, uint64_t nextPC)
{
    //TR("+history_update");
  p_impl->history_update(seq_no, piece, PC, brtype, pred_dir, resolve_dir, nextPC);
}

// -----------------------------------------------------------------------
void CBP2025_HIST1::TrackOtherInst(uint64_t PC, int brtype, bool pred_dir, bool resolve_dir,
                                   uint64_t nextPC)
{
    //TR("+TrackOtherInst");
  p_impl->TrackOtherInst(PC, brtype, resolve_dir, pred_dir, nextPC);
}

// -----------------------------------------------------------------------
void CBP2025_HIST1::terminate() { /*TR("terminate");*/  p_impl->terminate(); }
// -----------------------------------------------------------------------
void CBP2025_HIST1::decode_notify(uint64_t seq_no, uint8_t piece, uint64_t dst_reg)
{
    //TR("+decode_notify");
  p_impl->decode_notify(seq_no, piece, dst_reg);
}

// -----------------------------------------------------------------------
void CBP2025_HIST1::execute_notify(uint64_t seq_no, uint8_t piece, uint64_t dst_reg, uint64_t value)
{
    //TR("+execute_notify");
  p_impl->execute_notify(seq_no, piece, dst_reg, value);
}

