#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <unordered_map>
#include <cassert>
#include <fstream>
#include <map>
#include <optional>
#include "idioms/perceptron.h"

// ========================
// Arbiter
// ========================
static constexpr size_t PC_HASH_2(size_t PC) {
    return PC ^ (PC << 5 ) ^ (PC >> 12) ^ (PC >> 19) ^ (PC >> 44);
}


class Arbiter
{
    uint8_t threshold_delta_med = 1;
    uint8_t threshold_delta_low = 1;

    const size_t BUCKETS = 8;

    // These are the threshold tables from the paper
    // There's <BUCKETS> entries in each
    // These, along with the deltas, make up the total state of the Arbiter
    std::vector<uint16_t> threshold_meds;
    std::vector<uint16_t> threshold_lows;

    public:
        Arbiter() {
            threshold_meds.assign(BUCKETS, 35);
            threshold_lows.assign(BUCKETS, 48);
        }

        void recordOutcome(int64_t PC, bool correct, int32_t magnitude, size_t tage_conf){
            if (tage_conf == 2){
                return; // We never revert high conf predictions so don't record
            }

            size_t threshold_idx = PC_HASH_2(PC) % BUCKETS;

            int16_t threshold_low = threshold_lows[threshold_idx];
            int16_t threshold_med = threshold_meds[threshold_idx];

            if (!correct){
                if (tage_conf == 1){ // Medium conf
                    if (threshold_med<=magnitude){
                        threshold_meds[threshold_idx] = magnitude + threshold_delta_med;

                        if (threshold_delta_med<9)
                            threshold_delta_med++;
                    }
                } else { // Low conf
                    assert(tage_conf == 0);
                    if (threshold_low<=magnitude){
                        threshold_lows[threshold_idx] = magnitude + threshold_delta_low;
                        if (threshold_delta_low<10)
                            threshold_delta_low++;
                    }
                    if (threshold_med<=magnitude){ // Med should never be lower than low
                        threshold_meds[threshold_idx] = magnitude + threshold_delta_low;
                    }
                }
            } else {
                if (tage_conf == 1){  // Medium
                    if (threshold_med>magnitude){
                        threshold_meds[threshold_idx]--;
                        if (threshold_delta_med>1)
                            threshold_delta_med--;
                    }
                    if (threshold_low>magnitude){
                        threshold_lows[threshold_idx]--;
                    }
                } else {
                    assert(tage_conf == 0);
                    if (threshold_low>magnitude){
                        threshold_lows[threshold_idx]--;
                        if (threshold_delta_low>1)
                            threshold_delta_low--;
                    }
                }
            }
        }

        bool decide(uint64_t pc, int32_t magnitude, size_t tage_conf) {
            size_t threshold_idx = PC_HASH_2(pc) % BUCKETS;

            int16_t threshold_low = threshold_lows[threshold_idx];
            int16_t threshold_med = threshold_meds[threshold_idx];

            if (tage_conf == 2){
                return false;
            } else if (tage_conf == 1){
                return (threshold_med<=magnitude);
            } else if (tage_conf == 0){
                return (threshold_low<=magnitude);
            }

            assert(false);
        }

        size_t GET_SIZE(){
            return 16*BUCKETS*2 + 2*8;
        }
};

// ========================
// Arbiter, out 😎
// ========================

// Speculative history, hence not accounted for
struct PredInfo {
    bool     tagePred;
    bool     percPred;
    int32_t  percScore;
    size_t   tageConf;
};

class SampleCondPredictor
{
    // We consider this speculative history and so we don't count it
    std::unordered_map<uint64_t,PredInfo> pred_infos;
    std::unique_ptr<MultiperspectivePerceptronPredictor> perceptron;
    std::unique_ptr<Arbiter> arbiter;

    public:

        SampleCondPredictor (void) : perceptron(nullptr), arbiter(nullptr) {}

        void setup()
        {
            arbiter = std::make_unique<Arbiter>();
            perceptron = std::make_unique<MultiperspectivePerceptronPredictor>();  

            std::cout << "Perceptron size: " << (perceptron->GET_SIZE()) << std::endl;
            std::cout << "Arbiter size: " << (arbiter->GET_SIZE()) << std::endl;
        }

        void terminate()
        {
        }

        // sample function to get unique instruction id
        uint64_t get_unique_inst_id(uint64_t seq_no, uint8_t piece) const
        {
            assert(piece < 16);
            return (seq_no << 4) | (piece & 0x000F);
        }

        bool predict (uint64_t seq_no, uint8_t piece, uint64_t PC, CBP2016_TAGE_SC_L::PredictionResult tage_pred)
        {
            int score = perceptron->predict(PC);
            bool percPred = score >= 0;

            pred_infos.emplace(get_unique_inst_id(seq_no, piece), PredInfo{tage_pred.taken,percPred,score, tage_pred.confidence});

            bool arbiter_pred = arbiter->decide(PC, std::abs(score), tage_pred.confidence);
            if (arbiter_pred){
                return percPred;
            } else {
                return tage_pred.taken;
            }
        }

        void history_update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool taken, uint64_t nextPC)
        {
            auto it = pred_infos.find(get_unique_inst_id(seq_no, piece));
            const PredInfo& p = it->second;
            bool tage_ok  = (p.tagePred  == taken);
            bool perc_ok  = (p.percPred  == taken);
            bool isFwd = nextPC > PC;
            
            perceptron->onSpecBranch(PC,taken,isFwd, p.tagePred);
        }

        void update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool resolveDir, bool predDir, uint64_t nextPC)
        {
            const auto pred_hist_key = get_unique_inst_id(seq_no, piece);
            auto it = pred_infos.find(pred_hist_key);
            const PredInfo& p = it->second;

            bool tage_ok  = (p.tagePred  == resolveDir);
            bool perc_ok  = (p.percPred  == resolveDir);
            
            if (tage_ok ^ perc_ok){
                arbiter->recordOutcome(PC, perc_ok, std::abs(p.percScore), p.tageConf);
            }

            pred_infos.erase(pred_hist_key);
            perceptron->onBranchCommit(tage_ok ^ perc_ok, resolveDir);
        }
};
// =================
// Predictor End
// =================

#endif
static SampleCondPredictor cond_predictor_impl;
