#ifndef _TAGE_PREDICTOR_H_
#define _TAGE_PREDICTOR_H_

// Original developed by A. Seznec
// Tuned and modified by Yang Man and Lingrui Gou.
// This is the submission code to CBP 2025

#include "lib/sim_common_structs.h"
#include <array>
#include <assert.h>
#include <cstdint>
#include <deque>
#include <inttypes.h>
#include <iostream>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <vector>

#define UINT64 uint64_t

// parameters of the loop predictor
#define LOGL 6
#define WIDTHNBITERLOOP 14 // predict loops up to 16K iterations, not much difference increasing it
#define LOOPTAG 20         // tag width in the loop predictor

#define BORNTICK 1024

// The statistical corrector components

#define PERCWIDTH 7 // Statistical Corrector counter width

// The three BIAS tables in the SC component
// We play with the TAGE confidence here, with the number of the hitting bank
#define LOGBIAS 10 // 2K entries
int8_t Bias[(1 << LOGBIAS)];
int8_t BiasSK[(1 << LOGBIAS)];
int8_t BiasBank[(1 << LOGBIAS)];

// Global History components
// Shared with IMLI
// global branch GEHL
#define LOGGNB 10 // 1K entries
#define GNB 3     // 3 tables
int Gm[GNB]                       = {40, 24, 10};
int8_t GGEHLA[GNB][(1 << LOGGNB)] = {{0}};
int8_t *GGEHL[GNB];

// variation on global branch history
#define LOGPNB 10
#define PNB 3
int Pm[PNB]                       = {25, 16, 9};
int8_t PGEHLA[PNB][(1 << LOGPNB)] = {{0}};

int8_t *PGEHL[PNB];

// Local History components
// per-set local history
#define NPERSET 16      // per-set history entries
#define LOCALSETGRAN 64 // per set local history region size
#define LOGPSNB 11      // 2K table size
#define PSNB 2          // 2 tables
int PSm[PSNB]                        = {14, 5};
int8_t PSGEHLA[PSNB][(1 << LOGPSNB)] = {{0}};
int8_t *PSGEHL[PSNB];

// per-address a.k.a normal local history
#define NLOCAL 32 // local history entries
#define LOGLNB 11 // 2K table size
#define LNB 2     // 2 tables
int Lm[LNB]                       = {11, 5};
int8_t LGEHLA[LNB][(1 << LOGLNB)] = {{0}};
int8_t *LGEHL[LNB];

// IMLI components
// Storage are shared with GHIST components
#define IMLISETGRAN 32   // IMLI region size
#define LOGBRIMLIMAX 10  // Max counter
#define LOGFWDIMLIMAX 10 // Max counter
#define LOGTARIMLIMAX 10 // Max counter

// IMLI-OH history tables
uint32_t SCTARIMOH_Hist[(1 << LOGTARIMLIMAX)] = {0}; // Update at resolve
uint32_t SCBRIMOH_Hist[(1 << LOGBRIMLIMAX)]   = {0}; // Update at resolve
#define IMNB 2
int IMm[IMNB] = {10, 4}; // IMLI-OH history length

// IMLI-OH counter tables
#define LOGTARIMOHNB 10
int8_t SCTARIMOHA[IMNB][(1 << LOGTARIMOHNB)] = {{0}};
int8_t *SCTARIMOH[IMNB];

#define LOGBRIMOHNB 10
int8_t SCBRIMOHA[IMNB][(1 << LOGBRIMOHNB)] = {{0}};
int8_t *SCBRIMOH[IMNB];

// Forward path history
#define LOGFWDNB 10
#define FWDNB 2
int Fm[FWDNB]                         = {11, 4};
int8_t SCFWDA[FWDNB][(1 << LOGFWDNB)] = {{0}};
int8_t *SCFWD[FWDNB];

// IMLI filtered backward path history
#define LOGBKDNB 9
#define BKDNB 3
int Bm[BKDNB]                         = {11, 7, 4};
int8_t SCBKDA[BKDNB][(1 << LOGBKDNB)] = {{0}};
int8_t *SCBKD[BKDNB];

// playing with putting more weights (x2)  on some of the SC components
// playing on using different update thresholds on SC
// update threshold for the statistical corrector
#define WIDTHRES 12
#define WIDTHRESP 8

#define LOGSIZEUP 7
#define LOGSIZEUPS (LOGSIZEUP / 2)
int updatethreshold;
int Pupdatethreshold[(1 << LOGSIZEUP)]; // size is fixed by LOGSIZEUP
#define INDUPD (PC ^ (PC >> 2)) & ((1 << LOGSIZEUP) - 1)
#define INDUPDS ((PC ^ (PC >> 2)) & ((1 << (LOGSIZEUPS)) - 1))
int8_t WG[(1 << LOGSIZEUPS)];
int8_t WL[(1 << LOGSIZEUPS)];
int8_t WP[(1 << LOGSIZEUPS)];
int8_t WbrIMLI[(1 << LOGSIZEUPS)];
int8_t WtarIMLI[(1 << LOGSIZEUPS)];
int8_t WfwdbrIMLI[(1 << LOGSIZEUPS)];
int8_t WIMOH[(1 << LOGSIZEUPS)];
int8_t WIMLIPath[(1 << LOGSIZEUPS)];
int8_t WB[(1 << LOGSIZEUPS)];
#define EWIDTH 6
int LSUM;

// The two counters used to choose between TAGE and SC on Low Conf SC
int8_t FirstH, SecondH;
bool MedConf; // is the TAGE prediction medium confidence

#define CONFWIDTH 8 // for the counters in the choser
#define HISTBUFFERLENGTH                                                                                               \
    4096 // we use a 4K entries history buffer to store the branch history (this allows us to explore using history
         // length up to 4K)

// utility class for index computation
// this is the cyclic shift register for folding
// a long global history into a smaller number of bits; see P. Michaud's PPM-like predictor at CBP-1

class bentry // TAGE bimodal table entry
{
  public:
    int8_t hyst;
    int8_t pred;

    bentry() {
        pred = 0;

        hyst = 1;
    }
};

class gentry // TAGE global table entry
{
  public:
    int8_t ctr;
    uint tag;
    int8_t u;

    gentry() {
        ctr = 0;
        u   = 0;
        tag = 0;
    }
};

#define POWER
// use geometric history length

#define NHIST 36 // twice the number of different histories

#define NBANKLOW 9   // number of banks in the shared bank-interleaved for the low history lengths
#define NBANKHIGH 21 // number of banks in the shared bank-interleaved for the  history lengths

int SizeTable[NHIST + 1];

#define BORN 13 // below BORN in the table for low history lengths, >= BORN in the table for high history lengths,

// we use 2-way associativity for the medium history lengths
#define BORNINFASSOC 9 // 2 -way assoc for those banks 0.4 %
#define BORNSUPASSOC 23

/*in practice 2 bits or 3 bits par branch: around 1200 cond. branchs*/

#define MINHIST 6 // not optimized so far
#define MAXHIST 3000

#define LOGG 11  /* logsize of the  banks in the  tagged TAGE tables */
#define TBITS 11 // minimum width of the tags  (low history lengths), +4 for high history lengths

bool NOSKIP[NHIST + 1]; // to manage the associativity for different history lengths

#define NNN 1       // number of extra entries allocated on a TAGE misprediction (1+NNN)
#define HYSTSHIFT 2 // bimodal hysteresis shared by 4 entries
#define LOGB 17     // log of number of entries in bimodal predictor

#define PHISTWIDTH 27 // width of the path history used in TAGE
#define UWIDTH 1      // u counter width on TAGE (2 bits not worth the effort for a 512 Kbits predictor 0.2 %)
#define CWIDTH 3      // predictor counter width on the TAGE tagged tables

// the counter(s) to chose between longest match and alternate prediction on TAGE when weak counters
#define LOGSIZEUSEALT 4
bool AltConf; // Confidence on the alternate prediction
#define ALTWIDTH 5
#define SIZEUSEALT (1 << (LOGSIZEUSEALT))
#define INDUSEALT (((((HitBank - 1) / 8) << 1) + AltConf) % (SIZEUSEALT - 1))
int8_t use_alt_on_na[SIZEUSEALT];
// very marginal benefit
int8_t BIM;

int TICK; // for the reset of the u counter

class lentry // loop predictor entry
{
  public:
    uint32_t NbIter;      // 10 bits
    uint8_t confid;       // 4bits
    uint32_t CurrentIter; // 10 bits

    uint32_t TAG; // 10 bits
    uint8_t age;  // 4 bits
    bool dir;     // 1 bit

    // 39 bits per entry
    lentry() {
        confid      = 0;
        CurrentIter = 0;
        NbIter      = 0;
        TAG         = 0;
        age         = 0;
        dir         = false;
    }
};

// For the TAGE predictor
bentry *btable;            // bimodal TAGE table
gentry *gtable[NHIST + 1]; // tagged TAGE tables
int m[NHIST + 1];
int TB[NHIST + 1];
int logg[NHIST + 1];

uint64_t Seed; // for the pseudo-random number generator

class folded_history {
  public:
    unsigned comp;
    int CLENGTH;
    int OLENGTH;
    int OUTPOINT;

    folded_history() {
    }

    void init(int original_length, int compressed_length) {
        comp     = 0;
        OLENGTH  = original_length;
        CLENGTH  = compressed_length;
        OUTPOINT = OLENGTH % CLENGTH;
    }

    void update(std::array<uint8_t, HISTBUFFERLENGTH> &h, int PT) {
        comp = (comp << 1) ^ h[PT & (HISTBUFFERLENGTH - 1)];
        comp ^= h[(PT + OLENGTH) & (HISTBUFFERLENGTH - 1)] << OUTPOINT;
        comp ^= (comp >> CLENGTH);
        comp = (comp) & ((1 << CLENGTH) - 1);
    }
};
using tage_index_t = std::array<folded_history, NHIST + 1>;
using tage_tag_t   = std::array<folded_history, NHIST + 1>;

struct LoadTrackingEntry {
    uint64_t id;
    uint64_t pc;
};

struct cbp_hist_t {
    // Begin Conventional Histories
    uint64_t GHIST;                              // Global history used in SC
    std::array<uint8_t, HISTBUFFERLENGTH> ghist; // global history used in TAGE, no need for recovery
    uint64_t phist;                              // path history used in both TAGE and SC
    int ptghist;                                 // pointer, 12 bits for 4096 history buffer
    tage_index_t ch_i;                           // compressed tage index
    std::array<tage_tag_t, 2> ch_t;              // compressed tage tags

    std::array<uint64_t, NPERSET> per_set_local_history; // per-set local history checkpoint
    std::array<uint64_t, NLOCAL> local_history;          // local history checkpoint
    std::vector<lentry> ltable; // loop predictor table checkpoint, only speculative iteration counter is counted
    int8_t WITHLOOP;            // 7 bit saturation counter

    uint64_t last_back_br;                   // IMLI meta checkpoint
    uint64_t last_forward_br_target;         // IMLI meta checkpoint
    uint64_t last_back_target;               // IMLI meta checkpoint
    uint64_t brIMLI;                         // IMLI counter checkpoint
    uint64_t brIMOH;                         // IMLI-OH history checkpoint
    uint64_t tarIMLI;                        // IMLI counter checkpoint
    uint64_t tarIMOH;                        // IMLI-OH history checkpoint
    uint64_t fwdtarIMLI;                     // IMLI counter checkpoint
    uint64_t forward_path_history;           // path history checkpoint
    uint64_t filtered_backward_path_history; // path history checkpoint

    cbp_hist_t() {
        ltable.resize(1 << (LOGL));
        WITHLOOP = -1;
    }
};

int predictorsize() {
    int STORAGESIZE = 0;
    int inter       = 0;

    STORAGESIZE += NBANKHIGH * (1 << (logg[BORN])) * (CWIDTH + UWIDTH + TB[BORN]);
    STORAGESIZE += NBANKLOW * (1 << (logg[1])) * (CWIDTH + UWIDTH + TB[1]);

    STORAGESIZE += (SIZEUSEALT)*ALTWIDTH;
    STORAGESIZE += (1 << LOGB) + (1 << (LOGB - HYSTSHIFT));
    STORAGESIZE += m[NHIST];
    STORAGESIZE += PHISTWIDTH;
    STORAGESIZE += 10; // the TICK counter

    fprintf(stdout, " (TAGE %d) ", STORAGESIZE);

    // Loop predictor
    inter = (1 << LOGL) * (2 * WIDTHNBITERLOOP + LOOPTAG + 4 + 4 + 1);
    fprintf(stdout, " (LOOP %d) ", inter);
    STORAGESIZE += inter;

    // SC
    inter = 0;
    inter += WIDTHRES;
    inter = WIDTHRESP * ((1 << LOGSIZEUP));  // the update threshold counters
    inter += 9 * EWIDTH * (1 << LOGSIZEUPS); // the extra weight of the partial sums
    inter += (PERCWIDTH)*3 * (1 << (LOGBIAS));

    inter += (GNB) * (1 << (LOGGNB)) * (PERCWIDTH); // GHIST, 3 IMLI components also use these tables
    inter += (PNB) * (1 << (LOGPNB)) * (PERCWIDTH); // PHIST
    // we use phist already counted for these tables

    inter += (PSNB) * (1 << (LOGPSNB)) * (PERCWIDTH); // Per-set Local histoy
    inter += (LNB) * (1 << (LOGLNB)) * (PERCWIDTH);   // Local history

    // IMLI-OH
    inter += (IMNB) * (1 << LOGBRIMOHNB) * (PERCWIDTH);  // brIMLI-OH counter
    inter += (1 << LOGBRIMLIMAX) * IMm[0];               // brIMLI-OH Hist
    inter += (IMNB) * (1 << LOGTARIMOHNB) * (PERCWIDTH); // tarIMLI-OH counter
    inter += (1 << LOGTARIMLIMAX) * IMm[0];              // tarIMLI-OH Hist

    // IMLI filtered path history
    inter += (FWDNB) * (1 << LOGFWDNB) * (PERCWIDTH);
    inter += (BKDNB) * (1 << LOGBKDNB) * (PERCWIDTH);

    inter += 2 * CONFWIDTH; // the 2 counters in the choser
    STORAGESIZE += inter;

    fprintf(stdout, " (SC %d) \n", inter);
    fprintf(stdout, " (TOTAL %d bits %lf KB) \n", STORAGESIZE, (double)STORAGESIZE / 1024 / 8);

    uint64_t low_bank_needed  = 0;
    uint64_t high_bank_needed = 0;
    for (int i = 1; i < BORN; i++) {
        if (NOSKIP[i])
            low_bank_needed++;
    }
    for (int i = BORN; i <= NHIST; i++) {
        if (NOSKIP[i])
            high_bank_needed++;
    }
    printf(" TAGE Low Bank Used: %lu\n", low_bank_needed);
    printf(" TAGE High Bank Used: %lu\n", high_bank_needed);

    return (STORAGESIZE);
}

// The interface to the simulator is defined in cond_branch_predictor_interface.cc
// This predictor is a modified version of CBP2016 Tage.
// The CBP Tage predicted and updated the predictor right away.
// The simulator here provides 3 major hooks for the predictor:
// * get_cond_dir_prediction -> lookup the predictor and return the prediction.  This is invoked only for conditional
// branches.
// * spec_update -> This is used for updating the history. It provides the actual direction of the branch. This is
// invoked for all branches.
// * notify_instr_execute_resolve -> This hook is used to update the predictor. This is invoked for all the instructions
// and provides all information available at execute.
//    * Note: The history at update is different than history at predict. To ensure that the predictor is getting
//    trained correctly, at predict, we checkpoint the history in an unordered_map(pred_time_histories) using unique
//    identifying id of the instruction. When updating the predicor, we recover the prediction time history.
// There are a couple of other hooks that aren't used in the current implementation, but are available to exploit:
// * notify_instr_decode
// * notify_instr_commit
class CBP2016_TAGE_SC_L {
  public:
    // state set by predict
    int GI[NHIST + 1];    // indexes to the different tables are computed only once
    uint GTAG[NHIST + 1]; // tags for the different tables are computed only once
    int BI;               // index of the bimodal table

    //
    int THRES;
    //
    // State set in predict and used in update

    bool tage_pred; // TAGE prediction
    bool alttaken;  // alternate  TAGEprediction
    bool LongestMatchPred;
    int HitBank; // longest matching bank
    int AltBank; // alternate matching bank
    bool pred_inter;

    bool LowConf;
    bool HighConf;

    // checkpointed in history
    // Begin LOOPPREDICTOR State
    // int8_t WITHLOOP;    // counter to monitor whether or not loop prediction is beneficial
    bool predloop; // loop predictor prediction
    int LIB;
    int LI;
    int LHIT;    // hitting way in the loop predictor
    int LTAG;    // tag on the loop predictor
    bool LVALID; // validity of the loop predictor prediction
    // End LOOPPREDICTOR State

    cbp_hist_t active_hist; // running history always updated accurately
    // checkpointed history. Can be accesed using the inst-id(seq_no/piece)
    std::unordered_map<uint64_t /*key*/, cbp_hist_t /*val*/> pred_time_histories;

    CBP2016_TAGE_SC_L(void) {
        init_histories(active_hist);
        predictorsize();
    }

    void setup() {
    }

    void terminate() {
    }

    uint64_t get_unique_inst_id(uint64_t seq_no, uint8_t piece) const {
        assert(piece < 16);
        return (seq_no << 4) | (piece & 0x000F);
    }

    void init_histories(cbp_hist_t &current_hist) {
        m[1]         = MINHIST;
        m[NHIST / 2] = MAXHIST;
        for (int i = 2; i <= NHIST / 2; i++) {
            m[i] = (int)(((double)MINHIST *
                          pow((double)(MAXHIST) / (double)MINHIST, (double)(i - 1) / (double)(((NHIST / 2) - 1)))) +
                         0.5);
            //      fprintf(stderr, "(%d %d)", m[i],i);
        }
        for (int i = 1; i <= NHIST; i++) {
            NOSKIP[i] = ((i - 1) & 1) || ((i >= BORNINFASSOC) & (i < BORNSUPASSOC));
        }

        NOSKIP[4]         = 0;
        NOSKIP[NHIST - 2] = 0;
        NOSKIP[8]         = 0;
        NOSKIP[NHIST - 6] = 0;
        // just eliminate some extra tables (very very marginal)

        for (int i = NHIST; i > 1; i--) {
            m[i] = m[(i + 1) / 2];
        }
        for (int i = 1; i <= NHIST; i++) {
            TB[i]   = TBITS + 3 * (i >= BORN);
            logg[i] = LOGG;
        }

        gtable[1]    = new gentry[NBANKLOW * (1 << LOGG)];
        SizeTable[1] = NBANKLOW * (1 << LOGG);

        gtable[BORN]    = new gentry[NBANKHIGH * (1 << LOGG)];
        SizeTable[BORN] = NBANKHIGH * (1 << LOGG);

        for (int i = BORN + 1; i <= NHIST; i++)
            gtable[i] = gtable[BORN];
        for (int i = 2; i <= BORN - 1; i++)
            gtable[i] = gtable[1];
        btable = new bentry[1 << LOGB];

        for (int i = 1; i <= NHIST; i++) {
            current_hist.ch_i[i].init(m[i], (logg[i]));
            current_hist.ch_t[0][i].init(current_hist.ch_i[i].OLENGTH, TB[i]);
            current_hist.ch_t[1][i].init(current_hist.ch_i[i].OLENGTH, TB[i] - 1);
        }

        TICK               = 0;
        current_hist.phist = 0;
        Seed               = 0;

        for (int i = 0; i < HISTBUFFERLENGTH; i++)
            current_hist.ghist[i] = 0;
        current_hist.ptghist = 0;
        updatethreshold      = 35 << 3;

        for (int i = 0; i < (1 << LOGSIZEUP); i++)
            Pupdatethreshold[i] = 0;
        for (int i = 0; i < GNB; i++)
            GGEHL[i] = &GGEHLA[i][0];
        for (int i = 0; i < PSNB; i++)
            PSGEHL[i] = &PSGEHLA[i][0];
        for (int i = 0; i < LNB; i++)
            LGEHL[i] = &LGEHLA[i][0];
        for (int i = 0; i < IMNB; i++)
            SCTARIMOH[i] = &SCTARIMOHA[i][0];
        for (int i = 0; i < IMNB; i++)
            SCBRIMOH[i] = &SCBRIMOHA[i][0];
        for (int i = 0; i < FWDNB; i++)
            SCFWD[i] = &SCFWDA[i][0];
        for (int i = 0; i < BKDNB; i++)
            SCBKD[i] = &SCBKDA[i][0];
        for (int i = 0; i < PNB; i++)
            PGEHL[i] = &PGEHLA[i][0];

        for (int i = 0; i < (1 << LOGB); i++) {
            btable[i].pred = 0;
            btable[i].hyst = 1;
        }

        for (int j = 0; j < (1 << LOGBIAS); j++) {
            switch (j & 3) {
                case 0:
                    BiasSK[j] = -8;
                    break;
                case 1:
                    BiasSK[j] = 7;
                    break;
                case 2:
                    BiasSK[j] = -32;

                    break;
                case 3:
                    BiasSK[j] = 31;
                    break;
            }
        }
        for (int j = 0; j < (1 << LOGBIAS); j++) {
            switch (j & 3) {
                case 0:
                    Bias[j] = -32;

                    break;
                case 1:
                    Bias[j] = 31;
                    break;
                case 2:
                    Bias[j] = -1;
                    break;
                case 3:
                    Bias[j] = 0;
                    break;
            }
        }
        for (int j = 0; j < (1 << LOGBIAS); j++) {
            switch (j & 3) {
                case 0:
                    BiasBank[j] = -32;

                    break;
                case 1:
                    BiasBank[j] = 31;
                    break;
                case 2:
                    BiasBank[j] = -1;
                    break;
                case 3:
                    BiasBank[j] = 0;
                    break;
            }
        }
        for (int i = 0; i < SIZEUSEALT; i++) {
            use_alt_on_na[i] = 0;
        }
        for (int i = 0; i < (1 << LOGSIZEUPS); i++) {
            WG[i]         = 7;
            WL[i]         = 7;
            WP[i]         = 7;
            WbrIMLI[i]    = 7;
            WtarIMLI[i]   = 7;
            WfwdbrIMLI[i] = 7;
            WIMOH[i]      = 7;
            WIMLIPath[i]  = 7;
            WB[i]         = 4;
        }

        LVALID = false;
        Seed   = 0;
        TICK   = 0;

        current_hist.GHIST   = 0;
        current_hist.ptghist = 0;
        current_hist.phist   = 0;
    } // end init_histories

    // index function for the bimodal table
    int bindex(uint64_t PC) {
        return (((PC) ^ ((PC) >> LOGB)) & ((1 << (LOGB)) - 1));
    }

    // the index functions for the tagged tables uses path history as in the OGEHL predictor
    // F serves to mix path history: not very important impact
    int F(uint64_t A, int size, int bank) const {
        int A1, A2;
        A  = A & ((1 << size) - 1);
        A1 = (A & ((1 << logg[bank]) - 1));
        A2 = (A >> logg[bank]);

        if (bank < logg[bank])
            A2 = ((A2 << bank) & ((1 << logg[bank]) - 1)) + (A2 >> (logg[bank] - bank));
        A = A1 ^ A2;
        if (bank < logg[bank])
            A = ((A << bank) & ((1 << logg[bank]) - 1)) + (A >> (logg[bank] - bank));
        return (A);
    }

    // gindex computes a full hash of PC, ghist and phist
    // int gindex (unsigned int PC, int bank, uint64_t hist, const folded_history * ch_i) const
    int gindex(unsigned int PC, int bank, uint64_t hist, const tage_index_t &ch_i) const {
        int index;
        int M = (m[bank] > PHISTWIDTH) ? PHISTWIDTH : m[bank];
        index = PC ^ (PC >> (abs(logg[bank] - bank) + 1)) ^ ch_i[bank].comp ^ F(hist, M, bank);

        return (index & ((1 << (logg[bank])) - 1));
    }

    //  tag computation
    uint16_t gtag(unsigned int PC, int bank, const tage_tag_t &tag_0_array, const tage_tag_t &tag_1_array) const {
        int tag = (PC) ^ tag_0_array[bank].comp ^ (tag_1_array[bank].comp << 1);
        return (tag & ((1 << (TB[bank])) - 1));
    }

    // up-down saturating counter
    void ctrupdate(int8_t &ctr, bool taken, int nbits) {
        if (taken) {
            if (ctr < ((1 << (nbits - 1)) - 1))
                ctr++;
        } else {
            if (ctr > -(1 << (nbits - 1)))
                ctr--;
        }
    }

    bool getbim() {
        BIM      = (btable[BI].pred << 1) + (btable[BI >> HYSTSHIFT].hyst);
        HighConf = (BIM == 0) || (BIM == 3);
        LowConf  = !HighConf;
        AltConf  = HighConf;
        MedConf  = false;
        return (btable[BI].pred > 0);
    }

    void baseupdate(bool Taken) {
        int inter = BIM;
        if (Taken) {
            if (inter < 3)
                inter += 1;
        } else if (inter > 0)
            inter--;
        btable[BI].pred              = inter >> 1;
        btable[BI >> HYSTSHIFT].hyst = (inter & 1);
    };

    // just a simple pseudo random number generator: use available information
    //  to allocate entries  in the loop predictor
    int MYRANDOM() {
        Seed++;
        Seed ^= active_hist.phist;
        Seed = (Seed >> 21) + (Seed << 11);
        Seed ^= (int64_t)active_hist.ptghist;
        Seed = (Seed >> 10) + (Seed << 22);
        return (Seed & 0xFFFFFFFF);
    };

    //  TAGE PREDICTION: same code at fetch or retire time but the index and tags must recomputed
    void Tagepred(UINT64 PC, const cbp_hist_t &hist_to_use) {
        HitBank = 0;
        AltBank = 0;
        for (int i = 1; i <= NHIST; i += 2) {
            GI[i]       = gindex(PC, i, hist_to_use.phist, hist_to_use.ch_i);
            GTAG[i]     = gtag(PC, i, hist_to_use.ch_t[0], hist_to_use.ch_t[1]);
            GTAG[i + 1] = GTAG[i];
            GI[i + 1]   = GI[i] ^ (GTAG[i] & ((1 << LOGG) - 1));
        }
        int T = (PC ^ (hist_to_use.phist & ((1ULL << m[BORN]) - 1))) % NBANKHIGH;
        // int T = (PC ^ phist) % NBANKHIGH;
        for (int i = BORN; i <= NHIST; i++)
            if (NOSKIP[i]) {
                GI[i] += (T << LOGG);
                T++;
                T = T % NBANKHIGH;
            }
        T = (PC ^ (hist_to_use.phist & ((1 << m[1]) - 1))) % NBANKLOW;

        for (int i = 1; i <= BORN - 1; i++)
            if (NOSKIP[i]) {
                GI[i] += (T << LOGG);
                T++;
                T = T % NBANKLOW;
            }

        BI = bindex(PC);

        {
            alttaken         = getbim();
            tage_pred        = alttaken;
            LongestMatchPred = alttaken;
        }

        // Look for the bank with longest matching history
        for (int i = NHIST; i > 0; i--) {
            if (NOSKIP[i])
                if (gtable[i][GI[i]].tag == GTAG[i]) {
                    HitBank          = i;
                    LongestMatchPred = (gtable[HitBank][GI[HitBank]].ctr >= 0);
                    break;
                }
        }

        // Look for the alternate bank
        for (int i = HitBank - 1; i > 0; i--) {
            if (NOSKIP[i])
                if (gtable[i][GI[i]].tag == GTAG[i]) {

                    AltBank = i;
                    break;
                }
        }
        // computes the prediction and the alternate prediction

        if (HitBank > 0) {
            if (AltBank > 0) {
                alttaken = (gtable[AltBank][GI[AltBank]].ctr >= 0);
                AltConf  = (abs(2 * gtable[AltBank][GI[AltBank]].ctr + 1) > 1);

            } else
                alttaken = getbim();

            // if the entry is recognized as a newly allocated entry and
            // USE_ALT_ON_NA is positive  use the alternate prediction

            bool Huse_alt_on_na = (use_alt_on_na[INDUSEALT] >= 0);
            if ((!Huse_alt_on_na) || (abs(2 * gtable[HitBank][GI[HitBank]].ctr + 1) > 1))
                tage_pred = LongestMatchPred;
            else
                tage_pred = alttaken;

            HighConf = (abs(2 * gtable[HitBank][GI[HitBank]].ctr + 1) >= (1 << CWIDTH) - 1);
            LowConf  = (abs(2 * gtable[HitBank][GI[HitBank]].ctr + 1) == 1);
            MedConf  = (abs(2 * gtable[HitBank][GI[HitBank]].ctr + 1) == 5);
        }
    }

    uint64_t get_per_set_index(uint64_t PC) const {
        uint64_t regions_idx = PC / LOCALSETGRAN;
        return (regions_idx ^ (regions_idx >> 2)) & (NPERSET - 1);
    }

    uint64_t get_local_index(uint64_t PC) const {
        return (PC ^ (PC >> 4)) & (NLOCAL - 1);
    }

    uint64_t get_bias_index(uint64_t PC) const {
        return (((((PC ^ (PC >> 2)) << 1) ^ (LowConf & (LongestMatchPred != alttaken))) << 1) + pred_inter) &
               ((1 << LOGBIAS) - 1);
    }

    uint64_t get_biassk_index(uint64_t PC) const {
        return (((((PC ^ (PC >> (LOGBIAS - 2))) << 1) ^ (HighConf)) << 1) + pred_inter) & ((1 << LOGBIAS) - 1);
    }

    uint64_t get_biasbank_index(uint64_t PC) const {
        return (pred_inter + (((HitBank + 1) / 4) << 4) + (HighConf << 1) + (LowConf << 2) + ((AltBank != 0) << 3) +
                ((PC ^ (PC >> 2)) << 7)) &
               ((1 << LOGBIAS) - 1);
    }

    std::tuple<bool, bool> predict(uint64_t seq_no, uint8_t piece, UINT64 PC) {
        // checkpoint current hist
        pred_time_histories.emplace(get_unique_inst_id(seq_no, piece), active_hist);
        const bool pred_taken = predict_using_given_hist(seq_no, piece, PC, active_hist, true /*pred_time_predict*/);
        return {pred_taken, HighConf};
    }

    bool predict_using_given_hist(uint64_t seq_no, uint8_t piece, UINT64 PC, const cbp_hist_t &hist_to_use,
                                  const bool pred_time_predict) {
        // computes the TAGE table addresses and the partial tags
        Tagepred(PC, hist_to_use);
        bool pred_taken = tage_pred;

        // Loop predictor
        predloop   = getloop(PC, hist_to_use); // loop prediction
        pred_taken = ((hist_to_use.WITHLOOP >= 0) && (LVALID)) ? predloop : pred_taken;

        pred_inter = pred_taken;

        // Compute the SC prediction

        LSUM = 0;

        // integrate BIAS prediction
        int8_t ctr = Bias[get_bias_index(PC)];

        LSUM += (2 * ctr + 1);
        ctr = BiasSK[get_biassk_index(PC)];
        LSUM += (2 * ctr + 1);
        ctr = BiasBank[get_biasbank_index(PC)];
        LSUM += (2 * ctr + 1);
        LSUM = (1 + (WB[INDUPDS] >= 0)) * LSUM; // Weight

        // Sharing between GHIST and IMLI
        assert(GNB == 3);
        if (hist_to_use.brIMLI == 0) {
            LSUM += Gpredict((PC << 1) + pred_inter, hist_to_use.GHIST, Gm, GGEHL, 1, LOGGNB, WG);
        } else {
            LSUM += Gpredict(PC, hist_to_use.brIMLI, Gm, GGEHL, 1, LOGGNB, WbrIMLI);
        }
        if (hist_to_use.tarIMLI == 0) {
            LSUM += Gpredict((PC << 1) + pred_inter, hist_to_use.GHIST, Gm + 1, GGEHL + 1, 1, LOGGNB, WG);
        } else {
            LSUM += Gpredict(PC, hist_to_use.tarIMLI, Gm + 1, GGEHL + 1, 1, LOGGNB, WtarIMLI);
        }
        if (hist_to_use.fwdtarIMLI == 0) {
            LSUM += Gpredict((PC << 1) + pred_inter, hist_to_use.GHIST, Gm + 2, GGEHL + 2, 1, LOGGNB, WG);
        } else {
            LSUM += Gpredict(PC, hist_to_use.fwdtarIMLI, Gm + 2, GGEHL + 2, 1, LOGGNB, WfwdbrIMLI);
        }

        // integrate the GEHL predictions
        LSUM += Gpredict(PC, hist_to_use.phist, Pm, PGEHL, PNB, LOGPNB, WP);
        LSUM += Gpredict(PC, hist_to_use.per_set_local_history[get_per_set_index(PC)], PSm, PSGEHL, PSNB, LOGPSNB, WL);
        LSUM += Gpredict(PC, hist_to_use.local_history[get_local_index(PC)], Lm, LGEHL, LNB, LOGLNB, WL);
        LSUM += Gpredict(PC, hist_to_use.tarIMOH, IMm, SCTARIMOH, IMNB, LOGTARIMOHNB, WIMOH);
        LSUM += Gpredict(PC, hist_to_use.brIMOH, IMm, SCBRIMOH, IMNB, LOGBRIMOHNB, WIMOH);
        LSUM += Gpredict(PC, hist_to_use.forward_path_history, Fm, SCFWD, FWDNB, LOGFWDNB, WIMLIPath);
        LSUM += Gpredict(PC, hist_to_use.filtered_backward_path_history, Bm, SCBKD, BKDNB, LOGBKDNB, WIMLIPath);

        bool SCPRED = (LSUM >= 0);
        // just  an heuristic if the respective contribution of component groups can be multiplied by 2 or not
        THRES = (updatethreshold >> 3) + Pupdatethreshold[INDUPD];

        // Minimal benefit in trying to avoid accuracy loss on low confidence SC prediction and  high/medium confidence
        // on TAGE
        //  but just uses 2 counters 0.3 % MPKI reduction
        if (pred_inter != SCPRED) {
            // Choser uses TAGE confidence and |LSUM|
            pred_taken = SCPRED;
            if (HighConf) {
                if ((abs(LSUM) < THRES / 4)) {
                    pred_taken = pred_inter;
                }

                else if ((abs(LSUM) < THRES / 2)) {
                    pred_taken = (SecondH < 0) ? SCPRED : pred_inter;
                }
            }

            if (MedConf)
                if ((abs(LSUM) < THRES / 4)) {
                    pred_taken = (FirstH < 0) ? SCPRED : pred_inter;
                }
        }

        return pred_taken;
    }

    void history_update(uint64_t seq_no, uint8_t piece, UINT64 PC, int brtype, bool pred_taken, bool taken,
                        UINT64 nextPC) {
        // HistoryUpdate (PC, brtype, taken, nextPC, active_hist.phist, active_hist.ptghist, active_hist.ch_i,
        // active_hist.ch_t[0], active_hist.ch_t[1]);
        HistoryUpdate(PC, brtype, pred_taken, taken, nextPC);
    }

    void TrackOtherInst(UINT64 PC, int brtype, bool pred_taken, bool taken, UINT64 nextPC) {
        HistoryUpdate(PC, brtype, pred_taken, taken, nextPC);
    }

    void HistoryUpdate(UINT64 PC, int brtype, bool pred_taken, bool taken, UINT64 nextPC) {

        auto &X = active_hist.phist;
        auto &Y = active_hist.ptghist;

        auto &H = active_hist.ch_i;
        auto &G = active_hist.ch_t[0];
        auto &J = active_hist.ch_t[1];

        // special treatment for indirect  branchs;
        int maxt = 2;
        if (brtype & 1) // conditional
            maxt = 2;
        else if ((brtype & 2))
            maxt = 3;

        // Forward path history
        if (taken && nextPC > PC) {
            active_hist.forward_path_history = active_hist.forward_path_history << 3 ^ (nextPC >> 3) ^ (PC >> 2);
        }

        if (brtype & 1) { // conditional
            active_hist.GHIST = (active_hist.GHIST << 1) + (taken & (nextPC < PC));
            active_hist.per_set_local_history[get_per_set_index(PC)] =
                (active_hist.per_set_local_history[get_per_set_index(PC)] << 1) + (taken);
            active_hist.local_history[get_local_index(PC)] =
                (active_hist.local_history[get_local_index(PC)] << 1) + (taken);

            // only for conditional branch
            if (LVALID) {
                if (pred_taken != predloop)
                    ctrupdate(active_hist.WITHLOOP, (predloop == pred_taken), 7);
            }

            loopupdate(PC, pred_taken, false /*alloc*/, active_hist.ltable);

            if (taken && nextPC < PC) { // IMLI

                if (nextPC / IMLISETGRAN == active_hist.last_back_target) { // same target
                    if (active_hist.tarIMLI < (1 << LOGTARIMLIMAX) - 1)
                        active_hist.tarIMLI++;
                } else {
                    active_hist.filtered_backward_path_history =
                        (active_hist.filtered_backward_path_history << 1) ^ active_hist.last_back_target;
                    active_hist.tarIMLI = 0;
                }
                if (PC / IMLISETGRAN == active_hist.last_back_br) { // same branch PC
                    if (active_hist.brIMLI < (1 << LOGBRIMLIMAX) - 1)
                        active_hist.brIMLI++;
                } else {
                    active_hist.filtered_backward_path_history =
                        (active_hist.filtered_backward_path_history << 1) ^ active_hist.last_back_br;
                    active_hist.brIMLI = 0;
                }
                active_hist.last_back_target = nextPC / IMLISETGRAN;
                active_hist.last_back_br     = PC / IMLISETGRAN;
            } else if (taken && nextPC > PC) { // fwdIMLI
                if (nextPC / IMLISETGRAN == active_hist.last_forward_br_target) {
                    if (active_hist.fwdtarIMLI < (1 << LOGFWDIMLIMAX) - 1)
                        active_hist.fwdtarIMLI++;
                } else {
                    active_hist.fwdtarIMLI = 0;
                }
                active_hist.last_forward_br_target = nextPC / IMLISETGRAN;
            }
        }

        // IMLI-OH history
        // After updating IMLI, read IMLI-OH history from SRAM
        active_hist.brIMOH  = SCBRIMOH_Hist[active_hist.brIMLI];
        active_hist.tarIMOH = SCTARIMOH_Hist[active_hist.tarIMLI];

        int T    = ((PC ^ (PC >> 2))) ^ taken;
        int PATH = PC ^ (PC >> 2) ^ (PC >> 4);
        if ((brtype == 3) & taken) {
            T    = (T ^ (nextPC >> 2));
            PATH = PATH ^ (nextPC >> 2) ^ (nextPC >> 4);
        }

        for (int t = 0; t < maxt; t++) {
            bool DIR = (T & 1);
            T >>= 1;
            int PATHBIT = (PATH & 127);
            PATH >>= 1;
            // update  history
            Y--; // ptghist
            active_hist.ghist[Y & (HISTBUFFERLENGTH - 1)] = DIR;
            X                                             = (X << 1) ^ PATHBIT; // phist

            // updates to folded histories
            for (int i = 1; i <= NHIST; i++) {
                H[i].update(active_hist.ghist, Y);
                G[i].update(active_hist.ghist, Y);
                J[i].update(active_hist.ghist, Y);
            }
        }

        X = (X & ((1 << PHISTWIDTH) - 1));

    } // END UPDATE  HISTORIES

    // PREDICTOR UPDATE

    void update(uint64_t seq_no, uint8_t piece, UINT64 PC, bool resolveDir, bool predDir, UINT64 nextPC) {
        const auto pred_hist_key      = get_unique_inst_id(seq_no, piece);
        const auto &pred_time_history = pred_time_histories.at(pred_hist_key);
        const bool pred_taken =
            predict_using_given_hist(seq_no, piece, PC, pred_time_history, false /*pred_time_predict*/);
        //  remove checkpointed hist
        update(PC, resolveDir, pred_taken, nextPC, pred_time_history);
        pred_time_histories.erase(pred_hist_key);
    }

    void update(UINT64 PC, bool resolveDir, bool pred_taken, UINT64 nextPC, const cbp_hist_t &hist_to_use) {

        if (pred_taken != resolveDir) // incorrect loophhist updates in spec_update
        {
            // fix active hist.ltable and active_hist.WITHLOOP
            active_hist.ltable   = hist_to_use.ltable;
            active_hist.WITHLOOP = hist_to_use.WITHLOOP;
            if (LVALID) {
                if (pred_taken != predloop)
                    ctrupdate(active_hist.WITHLOOP, (predloop == resolveDir), 7);
            }
            loopupdate(PC, resolveDir, (pred_taken != resolveDir), active_hist.ltable);
        }

        // Update IMLI-OH history
        SCBRIMOH_Hist[hist_to_use.brIMLI]   = (SCBRIMOH_Hist[hist_to_use.brIMLI] << 1) + resolveDir;
        SCTARIMOH_Hist[hist_to_use.tarIMLI] = (SCTARIMOH_Hist[hist_to_use.tarIMLI] << 1) + resolveDir;

        bool SCPRED = (LSUM >= 0);
        if (pred_inter != SCPRED) {
            if ((abs(LSUM) < THRES))
                if ((HighConf)) {

                    if ((abs(LSUM) < THRES / 2))
                        if ((abs(LSUM) >= THRES / 4))
                            ctrupdate(SecondH, (pred_inter == resolveDir), CONFWIDTH);
                }
            if ((MedConf))
                if ((abs(LSUM) < THRES / 4)) {
                    ctrupdate(FirstH, (pred_inter == resolveDir), CONFWIDTH);
                }
        }

        if ((SCPRED != resolveDir) || ((abs(LSUM) < THRES))) {
            {
                if (SCPRED != resolveDir) {
                    Pupdatethreshold[INDUPD] += 1;
                    updatethreshold += 1;
                }

                else {
                    Pupdatethreshold[INDUPD] -= 1;
                    updatethreshold -= 1;
                }

                if (Pupdatethreshold[INDUPD] >= (1 << (WIDTHRESP - 1)))
                    Pupdatethreshold[INDUPD] = (1 << (WIDTHRESP - 1)) - 1;
                // Pupdatethreshold[INDUPD] could be negative
                if (Pupdatethreshold[INDUPD] < -(1 << (WIDTHRESP - 1)))
                    Pupdatethreshold[INDUPD] = -(1 << (WIDTHRESP - 1));
                if (updatethreshold >= (1 << (WIDTHRES - 1))) {
                    updatethreshold = (1 << (WIDTHRES - 1)) - 1;
                }
                // updatethreshold could be negative
                if (updatethreshold < -(1 << (WIDTHRES - 1))) {
                    updatethreshold = -(1 << (WIDTHRES - 1));
                }
            }

            {
                int XSUM = LSUM - ((WB[INDUPDS] >= 0) *
                                   ((2 * Bias[get_bias_index(PC)] + 1) + (2 * BiasSK[get_biassk_index(PC)] + 1) +
                                    (2 * BiasBank[get_biasbank_index(PC)] + 1)));
                if ((XSUM + ((2 * Bias[get_bias_index(PC)] + 1) + (2 * BiasSK[get_biassk_index(PC)] + 1) +
                             (2 * BiasBank[get_biasbank_index(PC)] + 1)) >=
                     0) != (XSUM >= 0))
                    ctrupdate(WB[INDUPDS],
                              (((2 * Bias[get_bias_index(PC)] + 1) + (2 * BiasSK[get_biassk_index(PC)] + 1) +
                                    (2 * BiasBank[get_biasbank_index(PC)] + 1) >=
                                0) == resolveDir),
                              EWIDTH);
            }
            ctrupdate(Bias[get_bias_index(PC)], resolveDir, PERCWIDTH);
            ctrupdate(BiasSK[get_biassk_index(PC)], resolveDir, PERCWIDTH);
            ctrupdate(BiasBank[get_biasbank_index(PC)], resolveDir, PERCWIDTH);

            // Share between GHIST and IMLI
            if (hist_to_use.brIMLI == 0) {
                Gupdate((PC << 1) + pred_inter, resolveDir, hist_to_use.GHIST, Gm, GGEHL, 1, LOGGNB, WG);
            } else {
                Gupdate(PC, resolveDir, hist_to_use.brIMLI, Gm, GGEHL, 1, LOGGNB, WbrIMLI);
            }
            if (hist_to_use.tarIMLI == 0) {
                Gupdate((PC << 1) + pred_inter, resolveDir, hist_to_use.GHIST, Gm + 1, GGEHL + 1, 1, LOGGNB, WG);
            } else {
                Gupdate(PC, resolveDir, hist_to_use.tarIMLI, Gm + 1, GGEHL + 1, 1, LOGGNB, WtarIMLI);
            }
            if (hist_to_use.fwdtarIMLI == 0) {
                Gupdate((PC << 1) + pred_inter, resolveDir, hist_to_use.GHIST, Gm + 2, GGEHL + 2, 1, LOGGNB, WG);
            } else {
                Gupdate(PC, resolveDir, hist_to_use.fwdtarIMLI, Gm + 2, GGEHL + 2, 1, LOGGNB, WfwdbrIMLI);
            }

            Gupdate(PC, resolveDir, hist_to_use.phist, Pm, PGEHL, PNB, LOGPNB, WP);
            Gupdate(PC, resolveDir, hist_to_use.per_set_local_history[get_per_set_index(PC)], PSm, PSGEHL, PSNB,
                    LOGPSNB, WL);
            Gupdate(PC, resolveDir, hist_to_use.local_history[get_local_index(PC)], Lm, LGEHL, LNB, LOGLNB, WL);
            Gupdate(PC, resolveDir, hist_to_use.brIMOH, IMm, SCBRIMOH, IMNB, LOGBRIMOHNB, WIMOH);
            Gupdate(PC, resolveDir, hist_to_use.tarIMOH, IMm, SCTARIMOH, IMNB, LOGTARIMOHNB, WIMOH);
            Gupdate(PC, resolveDir, hist_to_use.forward_path_history, Fm, SCFWD, FWDNB, LOGFWDNB, WIMLIPath);
            Gupdate(PC, resolveDir, hist_to_use.filtered_backward_path_history, Bm, SCBKD, BKDNB, LOGBKDNB, WIMLIPath);
        }

        // TAGE UPDATE
        bool ALLOC = ((tage_pred != resolveDir) & (HitBank < NHIST));

        // do not allocate too often if the overall prediction is correct

        if (HitBank > 0) {
            // Manage the selection between longest matching and alternate matching
            // for "pseudo"-newly allocated longest matching entry
            // this is extremely important for TAGE only, not that important when the overall predictor is implemented
            bool PseudoNewAlloc = (abs(2 * gtable[HitBank][GI[HitBank]].ctr + 1) <= 1);
            // an entry is considered as newly allocated if its prediction counter is weak
            if (PseudoNewAlloc) {
                if (LongestMatchPred == resolveDir)
                    ALLOC = false;
                // if it was delivering the correct prediction, no need to allocate a new entry
                // even if the overall prediction was false

                if (LongestMatchPred != alttaken) {
                    ctrupdate(use_alt_on_na[INDUSEALT], (alttaken == resolveDir), ALTWIDTH);
                }
            }
        }

        if (pred_taken == resolveDir)
            if ((MYRANDOM() & 31) != 0)
                ALLOC = false;

        if (ALLOC) {

            int T = NNN;

            int A = 1;
            if ((MYRANDOM() & 127) < 32)
                A = 2;
            int Penalty = 0;
            int NA      = 0;
            int DEP     = ((((HitBank - 1 + 2 * A) & 0xffe)) ^ (MYRANDOM() & 1));
            // just a complex formula to chose between X and X+1, when X is odd: sorry

            for (int I = DEP; I < NHIST; I += 2) {
                int i     = I + 1;
                bool Done = false;
                if (NOSKIP[i]) {
                    if (gtable[i][GI[i]].u == 0)

                    {
#define OPTREMP
                        // the replacement is optimized with a single u bit: 0.2 %
#ifdef OPTREMP
                        if (abs(2 * gtable[i][GI[i]].ctr + 1) <= 3)
#endif
                        {
                            gtable[i][GI[i]].tag = GTAG[i];
                            gtable[i][GI[i]].ctr = (resolveDir) ? 0 : -1;
                            NA++;
                            if (T <= 0) {
                                break;
                            }
                            I += 2;
                            Done = true;
                            T -= 1;
                        }
#ifdef OPTREMP
                        else {
                            if (gtable[i][GI[i]].ctr > 0)
                                gtable[i][GI[i]].ctr--;
                            else
                                gtable[i][GI[i]].ctr++;
                        }

#endif

                    }

                    else {
                        Penalty++;
                    }
                }

                if (!Done) {
                    i = (I ^ 1) + 1;
                    if (NOSKIP[i]) {

                        if (gtable[i][GI[i]].u == 0) {
#ifdef OPTREMP
                            if (abs(2 * gtable[i][GI[i]].ctr + 1) <= 3)
#endif

                            {
                                gtable[i][GI[i]].tag = GTAG[i];
                                gtable[i][GI[i]].ctr = (resolveDir) ? 0 : -1;
                                NA++;
                                if (T <= 0) {
                                    break;
                                }
                                I += 2;
                                T -= 1;
                            }
#ifdef OPTREMP
                            else {
                                if (gtable[i][GI[i]].ctr > 0)
                                    gtable[i][GI[i]].ctr--;
                                else
                                    gtable[i][GI[i]].ctr++;
                            }

#endif

                        } else {
                            Penalty++;
                        }
                    }
                }
            }
            TICK += (Penalty - 2 * NA);

            // just the best formula for the Championship:
            // In practice when one out of two entries are useful
            if (TICK < 0)
                TICK = 0;
            if (TICK >= BORNTICK) {

                for (int i = 1; i <= BORN; i += BORN - 1)
                    for (int j = 0; j < SizeTable[i]; j++)
                        gtable[i][j].u >>= 1;
                TICK = 0;
            }
        }

        // update predictions
        if (HitBank > 0) {
            if (abs(2 * gtable[HitBank][GI[HitBank]].ctr + 1) == 1)
                if (LongestMatchPred != resolveDir)

                { // acts as a protection
                    if (AltBank > 0) {
                        ctrupdate(gtable[AltBank][GI[AltBank]].ctr, resolveDir, CWIDTH);
                    }
                    if (AltBank == 0)
                        baseupdate(resolveDir);
                }
            ctrupdate(gtable[HitBank][GI[HitBank]].ctr, resolveDir, CWIDTH);
            // sign changes: no way it can have been useful
            if (abs(2 * gtable[HitBank][GI[HitBank]].ctr + 1) == 1)
                gtable[HitBank][GI[HitBank]].u = 0;
            if (alttaken == resolveDir)
                if (AltBank > 0)
                    if (abs(2 * gtable[AltBank][GI[AltBank]].ctr + 1) == 7)
                        if (gtable[HitBank][GI[HitBank]].u == 1) {
                            if (LongestMatchPred == resolveDir) {
                                gtable[HitBank][GI[HitBank]].u = 0;
                            }
                        }
        }

        else
            baseupdate(resolveDir);

        if (LongestMatchPred != alttaken)
            if (LongestMatchPred == resolveDir) {
                if (gtable[HitBank][GI[HitBank]].u < (1 << UWIDTH) - 1)
                    gtable[HitBank][GI[HitBank]].u++;
            }
        // END TAGE UPDATE

    } // END PREDICTOR UPDATE

#define GINDEX                                                                                                         \
    (((uint64_t)PC) ^ bhist ^ (bhist >> (8 - i)) ^ (bhist >> (16 - 2 * i)) ^ (bhist >> (24 - 3 * i)) ^                 \
     (bhist >> (32 - 3 * i)) ^ (bhist >> (40 - 4 * i))) &                                                              \
        ((1 << logs) - 1)
    int Gpredict(UINT64 PC, uint64_t BHIST, int *length, int8_t **tab, int NBR, int logs, int8_t *W) {
        int PERCSUM = 0;
        for (int i = 0; i < NBR; i++) {
            uint64_t bhist = BHIST & ((uint64_t)((1ULL << length[i]) - 1));
            uint64_t index = GINDEX;

            int8_t ctr = tab[i][index];

            PERCSUM += (2 * ctr + 1);
        }
        PERCSUM = (1 + (W[INDUPDS] >= 0)) * PERCSUM;
        return ((PERCSUM));
    }
    void Gupdate(UINT64 PC, bool taken, uint64_t BHIST, int *length, int8_t **tab, int NBR, int logs, int8_t *W) {

        int PERCSUM = 0;

        for (int i = 0; i < NBR; i++) {
            uint64_t bhist = BHIST & ((uint64_t)((1ULL << length[i]) - 1));
            uint64_t index = GINDEX;

            PERCSUM += (2 * tab[i][index] + 1);
            ctrupdate(tab[i][index], taken, PERCWIDTH);
        }
        {
            int XSUM = LSUM - ((W[INDUPDS] >= 0)) * PERCSUM;
            if ((XSUM + PERCSUM >= 0) != (XSUM >= 0))
                ctrupdate(W[INDUPDS], ((PERCSUM >= 0) == taken), EWIDTH);
        }
    }

    int lindex(UINT64 PC) {
        return (((PC ^ (PC >> 2)) & ((1 << (LOGL - 2)) - 1)) << 2);
    }

    // loop prediction: only used if high confidence
    // skewed associative 4-way
    // At fetch time: speculative
#define CONFLOOP 15
    bool getloop(UINT64 PC, const cbp_hist_t &hist_to_use) {
        const auto &ltable = hist_to_use.ltable;
        LHIT               = -1;

        LI   = lindex(PC);
        LIB  = ((PC >> (LOGL - 2)) & ((1ULL << (LOGL - 2)) - 1));
        LTAG = (PC >> (LOGL - 2)) & ((1ULL << 2 * LOOPTAG) - 1);
        LTAG ^= (LTAG >> LOOPTAG);
        LTAG = (LTAG & ((1ULL << LOOPTAG) - 1));

        for (int i = 0; i < 4; i++) {
            int index = (LI ^ ((LIB >> i) << 2)) + i;

            if (ltable[index].TAG == LTAG) {
                LHIT   = i;
                LVALID = ((ltable[index].confid == CONFLOOP) || (ltable[index].confid * ltable[index].NbIter > 128));

                if (ltable[index].CurrentIter + 1 == ltable[index].NbIter)
                    return (!(ltable[index].dir));
                return ((ltable[index].dir));
            }
        }

        LVALID = false;
        return (false);
    }

    void loopupdate(UINT64 PC, bool Taken, bool ALLOC, std::vector<lentry> &ltable) {
        if (LHIT >= 0) {
            int index = (LI ^ ((LIB >> LHIT) << 2)) + LHIT;
            // already a hit
            if (LVALID) {
                if (Taken != predloop) {
                    // free the entry
                    ltable[index].NbIter      = 0;
                    ltable[index].age         = 0;
                    ltable[index].confid      = 0;
                    ltable[index].CurrentIter = 0;
                    return;

                } else if ((predloop != tage_pred) || ((MYRANDOM() & 7) == 0))
                    if (ltable[index].age < CONFLOOP)
                        ltable[index].age++;
            }

            ltable[index].CurrentIter++;
            ltable[index].CurrentIter &= ((1 << WIDTHNBITERLOOP) - 1);
            // loop with more than 2** WIDTHNBITERLOOP iterations are not treated correctly; but who cares :-)
            if (ltable[index].CurrentIter > ltable[index].NbIter) {
                ltable[index].confid = 0;
                ltable[index].NbIter = 0;
                // treat like the 1st encounter of the loop
            }
            if (Taken != ltable[index].dir) {
                if (ltable[index].CurrentIter == ltable[index].NbIter) {
                    if (ltable[index].confid < CONFLOOP)
                        ltable[index].confid++;
                    if (ltable[index].NbIter < 3)
                    // just do not predict when the loop count is 1 or 2
                    {
                        // free the entry
                        ltable[index].dir    = Taken;
                        ltable[index].NbIter = 0;
                        ltable[index].age    = 0;
                        ltable[index].confid = 0;
                    }
                } else {
                    if (ltable[index].NbIter == 0) {
                        // first complete nest;
                        ltable[index].confid = 0;
                        ltable[index].NbIter = ltable[index].CurrentIter;
                    } else {
                        // not the same number of iterations as last time: free the entry
                        ltable[index].NbIter = 0;
                        ltable[index].confid = 0;
                    }
                }
                ltable[index].CurrentIter = 0;
            }

        } else if (ALLOC) {
            UINT64 X = MYRANDOM() & 3;

            if ((MYRANDOM() & 3) == 0)
                for (int i = 0; i < 4; i++) {
                    int loop_hit_way_loc = (X + i) & 3;
                    int index            = (LI ^ ((LIB >> loop_hit_way_loc) << 2)) + loop_hit_way_loc;
                    if (ltable[index].age == 0) {
                        ltable[index].dir = !Taken;
                        // most of mispredictions are on last iterations
                        ltable[index].TAG         = LTAG;
                        ltable[index].NbIter      = 0;
                        ltable[index].age         = 7;
                        ltable[index].confid      = 0;
                        ltable[index].CurrentIter = 0;
                        break;

                    } else
                        ltable[index].age--;
                    break;
                }
        }
    }
};
// =================
// Predictor End
// =================

#undef UINT64

#endif
static CBP2016_TAGE_SC_L cbp2016_tage_sc_l;
