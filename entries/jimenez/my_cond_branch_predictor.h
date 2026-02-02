// Daniel A. Jimenez
// 
// Multiperspective Perceptron Predictor
//
// Most of the useful comments are in mpp/mpp.h and mpp/combine.h

#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
using namespace std;

// include MPP and combining predictor stuff

#include "mpp/branch.h"
#include "mpp/eval.h"
#undef IMLI
#include "mpp/mpp.h"
#include "mpp/combine.h"

// if this is true then we speculatively update perceptron weights tables
// using predictions (not ground truth). it changes to false if there are
// too many low-confidence branches in flight

int speculatively_update_tables = 1;

// true when we want to print the sizes of MPP etc. structures (initially
// false because we call the constructor twice)

bool print_mpp_sizes = 0;

struct SampleHist
{
      uint64_t ghist;
      bool tage_pred;
      //
      SampleHist()
      {
          ghist = 0;
      }
};

// tuned transfer function lookup table that maps 6-bit perceptron weights
// to 8 bit values that follow an inverse sigmoidal curve

static int xfer[] = { -255,-217,-192,-171,-155,-142,-130,-120,-110,-102,-94,-87,-81,-74,-68,-62,-56,-50,-46,-41,-37,-34,-30,-27,-24,-20,-17,-14,-11,-8,-5,2,5,8,11,14,17,20,24,27,30,34,37,41,46,50,56,62,68,74,81,87,94,102,110,120,130,142,155,171,192,217,255, };

// global variable to communicate TAGE-SC-L prediction and confidence to
// the MPP "TAGE" feature

unsigned int global_tage_pred, global_tage_conf;

// global variable to communicate TAGE-SC-L facts to the combining predictor

int global_tage_bits = 0;

// number of weights we can afford in the hardware budget for perceptron
// tables. initialized to -1 and computed later

int nentriestotal = -1;

// tuned MPP features


history_spec tuned_spv[] = {
{ LOCAL, 23, 27, 0, 0, 0, 0, 16 },
{ ACYCLIC, 10, -1, -1, -1, -1, 0, 0 },
{ TAGE, 11, 9, 0, 0, -1, 0, 8 },
{ MODHIST, 5, 17, -1, -1, -1, 0, 8 },
{ ACYCLIC, 9, -1, -1, -1, -1, 0, 0 },
{ LOCAL, 3, 34, 0, 0, 0, 0, 8 },
{ LOCAL, 0, 13, 0, 0, 0, 0, 0 },
{ GHISTPATH, 1, 16, 0, 0, 0, 0, 16 },
{ GHISTMODPATH, 4, 8, 5, -1, -1, 0, 8 },
{ GHISTMODPATH, 5, 5, 2, -1, -1, 0, 16 },
{ RECENCYPOS, 56, 0, -1, -1, -1, 0, 0 },
{ LOCAL, 10, 32, 0, 0, 0, 0, 0 },
{ GHISTPATH, 29, 41, 8, 6, 0, 0, 0 },
{ GHISTPATH, 1, 22, 6, 8, 0, 0, 16 },
{ IMLI, 4, -1, -1, -1, -1, 0, 16 },
{ LOCAL, 0, 9, 0, 0, 0, 0, 0 },
{ GHISTMODPATH, 2, 16, 6, -1, -1, 0, 0 },
{ LOCAL, 0, 20, 0, 0, 0, 0, 8 },
{ GHISTPATH, 0, 9, 3, 0, 0, 0, 8 },
{ GHISTMODPATH, 0, 19, 5, -1, -1, 0, 16 },
{ MODPATH, 1, 20, 1, -1, -1, 0, 8 },
{ MODHIST, 3, 22, -1, -1, -1, 0, 0 },
{ GHISTMODPATH, 1, 7, 1, -1, -1, 0, 8 },
{ LOCAL, 0, 1, 0, 0, 0, 0, 0 },
{ MODPATH, 3, 9, 4, -1, -1, 0, 8 },
{ GHISTMODPATH, 3, 14, 6, -1, -1, 0, 16 },
{ BLURRYPATH, 11, 9, 2, -1, -1, 0, 8 },
{ RECENCY, 10, 1, -1, -1, -1, 0, 8 },
{ LOCAL, 0, 5, 0, 0, 0, 0, 8 },
{ GHISTPATH, 22, 33, 6, 8, 0, 0, 16 },
{ MODPATH, 1, 26, 3, -1, -1, 0, 8 },
{ GHISTMODPATH, 5, 14, 1, -1, -1, 0, 8 },
{ BACKPATH, 22, 6, 0, 0, 0, 0, 8 },
};

// this class wraps the CBP 2025 implementation of TAGE-SC-L in my branch
// predictor class so I can use it in the combiner class

class tage_sc_l_wrapper : public branch_predictor {
public:
	tage_sc_l_wrapper (void) { }

	branch_update *lookup (unsigned int pc, uint64_t) {
		static branch_update u;
		u.address = pc;
		u.confidence = global_tage_bits;
		u.prediction (global_tage_bits & 1);
		return &u;
	}
};

// get an instance of MPP

mpp *get_mpp (void) {
	mpp *p = new mpp (
		tuned_spv,
		sizeof (tuned_spv) / sizeof (history_spec),
		11,     // initial value of training threshold theta
		0.3, 	// alpha, a learning rate for threshold training
		xfer,	// transfer function lookup table
		3,      // pcbit
		2,      // hash taken bit
		30,     // block size
		true,   // hash taken outcome with PC bit
		191,    // mask for recording filtered branches
		208 /* X_IND_PC | X_CALL_PC | X_CALL_TARGET */, // xflag
		3              // xn (number of bits in non-conditional branches to shift)
	);
	return p;
}

// get the combined MPP/TAGE-SC-L

combine *get_combine (void) {
	// this is a little hacky. combine() needs to know the number of history bits so it can
	// figure out how many entries MPP can use, but only MPP knows how many history bits it
	// uses. so we make an MPP with the wrong number of entries, it can report its number of
	// history bits to combine, and then we can delete it and make a new MPP with the right
	// number of entries

	// get an MPP instance with the same parameters to figure out number of history bits
	nentriestotal = -1;
	print_mpp_sizes = false;
	mpp *v = get_mpp ();
	combine *p = new combine (v->history_bits());

	// blow away this predictor and make a new one with the right number of bits
	delete v;
	print_mpp_sizes = true;

	// put TAGE-SC-L and MPP together in the combining predictor

	p->attach (new tage_sc_l_wrapper (), get_mpp ());
	return p;
}

class SampleCondPredictor
{
        SampleHist active_hist;
        std::unordered_map<uint64_t/*key*/, SampleHist/*val*/> pred_time_histories;
    public:
	combine *p;

        SampleCondPredictor (void)
        {
		p = get_combine ();
        }

        void setup()
        {
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

        bool predict (uint64_t seq_no, uint8_t piece, uint64_t PC, const bool tage_pred) {
		active_hist.tage_pred = tage_pred;
		// checkpoint current hist
		pred_time_histories.emplace(get_unique_inst_id(seq_no, piece), active_hist);
		const bool pred_taken = predict_using_given_hist(seq_no, piece, PC, active_hist, true/*pred_time_predict*/);
		combine_update *u = (combine_update *) p->lookup (PC, get_unique_inst_id (seq_no, piece));

		// djimenez; remember the prediction

		prediction_hack = u->prediction();
		return u->prediction();
	}

        bool predict_using_given_hist (uint64_t seq_no, uint8_t piece, uint64_t PC, const SampleHist& hist_to_use, const bool pred_time_predict)
        {
            return hist_to_use.tage_pred;
        }

	// djimenez - keep this prediction around to speculatively update
	// tables with the prediction rather than the actual taken/not taken
	// outcome

	bool prediction_hack;

        void history_update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool taken, uint64_t nextPC) {
		active_hist.ghist = active_hist.ghist << 1;
		if (taken) {
			active_hist.ghist |= 1;
		}
		p->spec_update (NULL, nextPC, taken, prediction_hack, OPTYPE_JMP_DIRECT_COND, false, get_unique_inst_id (seq_no, piece));
	}

	// record nonconditional branch outcome for MPP

        void nonconditional_history_update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool taken, uint64_t nextPC, InstClass inst_class) {
		int mpp_instruction_type = 0;

		switch(inst_class) {
			case InstClass::condBranchInstClass:
			assert (0); // we should never reach here
			mpp_instruction_type = OPTYPE_JMP_DIRECT_COND;
			break;
		case InstClass::uncondDirectBranchInstClass:
			mpp_instruction_type = OPTYPE_JMP_DIRECT_UNCOND;
			break;
		case InstClass::uncondIndirectBranchInstClass:
			mpp_instruction_type = OPTYPE_JMP_INDIRECT_UNCOND;
			break;
		case InstClass::callDirectInstClass:
			mpp_instruction_type = OPTYPE_CALL_DIRECT_UNCOND;
			break;
		case InstClass::callIndirectInstClass:
			mpp_instruction_type = OPTYPE_CALL_INDIRECT_UNCOND;
			break;
		case InstClass::ReturnInstClass:
			mpp_instruction_type = OPTYPE_RET_UNCOND;
			break;
		default: 
			assert(false);
		}
		p->nonconditional_branch (PC, nextPC, mpp_instruction_type);
	}

        void update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool resolveDir, bool predDir, uint64_t nextPC)
        {
		// djimenez
            const auto pred_hist_key = get_unique_inst_id(seq_no, piece);
            const auto& pred_time_history = pred_time_histories.at(pred_hist_key);
            update(PC, resolveDir, predDir, nextPC, pred_time_history);
	
            pred_time_histories.erase(pred_hist_key);
		// djimenez
		p->update (NULL, nextPC, resolveDir, OPTYPE_JMP_DIRECT_COND, get_unique_inst_id (seq_no, piece));
        }

        void update (uint64_t PC, bool resolveDir, bool pred_taken, uint64_t nextPC, const SampleHist& hist_to_use)
        {
        }
};
// =================
// Predictor End
// =================

#endif
static SampleCondPredictor cond_predictor_impl;
