// ====== REQUIRED INCLUDES FOR THIS FILE ======
//#include <algorithm>      // ::std::max, ::std::min, ::std::fill
//#include <unordered_map>  // ::std::unordered_map

#include "instrument_history.h"
#include "trace.h"

class HIST1 {
    int NewlyDecay, NewlyUseful = 4;

public:
    cbp_hist_t active_hist; // running history always updated accurately
    // checkpointed history. Can be accesed using the inst-id(seq_no/piece)
    ::std::unordered_map<uint64_t /*key*/, cbp_hist_t /*val*/> pred_time_histories;
    PredRelatedVariables pv;

    HIST1(void)
    {
        init_histories(active_hist);
        print_predictorsize();
    }

    void setup() { }
    void terminate()
    {
        #ifdef INSTRUMENT_HISTORY
            TR("+HIST1::terminate");
            hist_instr::DumpHistoryStats("hist_global.txt");
            hist_instr::DumpPerPcStats("hist_perpc.txt");
        #endif
    }
    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    uint64_t get_unique_inst_id(uint64_t seq_no, uint8_t piece) const
    {
        assert(piece < 16);
        return (seq_no << 4) | (piece & 0x000F);
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    int CalcNextHistLen(int before, int current, double rate)
    {
        int a = current + (current - before + 2); // interval is an arithmetic sequence
        int b = int(current * rate / 2 + 0.5) * 2; // geometric sequence
        return ::std::max(a, b);
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void init_histories(cbp_hist_t& current_hist)
    {
        m[1] = MINHIST;
        for (int i = 2; i <= NHIST; ++i) {
            double rate = ::std::max(HistRate, HistRate + 0.1 * (i - Born2));
            m[i] = CalcNextHistLen(m[i - 2], m[i - 1], rate);
        }
        for (int i = 1; i <= NHIST; i++) {
            TB[i] = TBITS + 4 * (i >= BORN);
            logg[i] = LOGG;
        }

        gtable[1] = new gentry[NBANKLOW * (1 << LOGG)];
        SizeTable[1] = NBANKLOW * (1 << LOGG);

        gtable[BORN] = new gentry[NBANKHIGH * (1 << LOGG)];
        SizeTable[BORN] = NBANKHIGH * (1 << LOGG);

        for (int i = BORN + 1; i <= NHIST; i++)
            gtable[i] = gtable[BORN];
        for (int i = 2; i <= BORN - 1; i++)
            gtable[i] = gtable[1];

        for (int i = 1; i <= NHIST; i++) {
            current_hist.ch_i[i].init(m[i], (logg[i]));
            current_hist.ch_t[0][i].init(current_hist.ch_i[i].OLENGTH, TB[i]);
            current_hist.ch_t[1][i].init(current_hist.ch_i[i].OLENGTH, TB[i] - 1);
        }

        TICK = 0;
        current_hist.phist = 0;
        Seed = 0;

        for (int i = 0; i < HISTBUFFERLENGTH; i++)
            current_hist.ghist[i] = 0;
        updatethreshold = 35;

        for (int i = 0; i < (1 << LOGSIZEUP); i++)
            Pupdatethreshold[i] = 0;

        ::std::fill(bim_pred.begin(), bim_pred.end(), 0);
        ::std::fill(bim_hyst.begin(), bim_hyst.end(), 1);

        for (int i = 0; i < SIZEUSEALT; i++) {
            use_alt_on_na[i] = 0;
        }
        for (int i = 0; i < sLocal1::FeatureSize; i++) {
            current_hist.L_shist[i] = 0;
        }
        for (int i = 0; i < sLocal2::FeatureSize; i++) {
            current_hist.S_slhist[i] = 0;
        }
        for (int i = 0; i < sLocal3::FeatureSize; i++) {
            current_hist.T_slhist[i] = 0;
        }

        current_hist.GHIST = 0;
        current_hist.ptghist = 0;
        current_hist.phist = 0;
    } // end init_histories

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    uint64_t make_reg_digest(size_t reg_num, uint64_t value)
    {
        constexpr size_t W = 12;
        uint64_t hash = 0;
        if (32 <= reg_num && reg_num < 64) { // FP
            if (value >> 16 == 0) {
                hash = value >> (16 - 3);
            } else if (value >> 32 == 0) {
                hash = value >> (32 - 6);
            } else {
                hash = value >> (64 - 9);
            }
        } else if (reg_num == 64) { // Flag
            hash = value << 8 ^ value << 4 ^ value;
        } else { // Int
            int msb_one_pos = 0, msb_zero_pos = 0;
            int lsb_one_pos = 0, lsb_zero_pos = 0;
            for (int i = 0; i < 64; ++i) {
                if (!((value >> i) & 1)) {
                    lsb_one_pos = i;
                    break;
                }
            }
            for (int i = 0; i < 64; ++i) {
                if ((value >> i) & 1) {
                    lsb_zero_pos = i;
                    break;
                }
            }
            for (int i = 63; i >= 0; --i) {
                if (!((value >> i) & 1)) {
                    msb_one_pos = 63 - i;
                    break;
                }
            }
            for (int i = 63; i >= 0; --i) {
                if ((value >> i) & 1) {
                    msb_zero_pos = 63 - i;
                    break;
                }
            }
            hash = ((lsb_one_pos ^ lsb_zero_pos))
                 ^ ((msb_one_pos ^ msb_zero_pos) << 3)
                 ^ (value << 6);
        }
        return hash % (1 << W);
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    uint64_t rbias_index(uint64_t PC, uint64_t reg_value) const
    {
        return (PC ^ PC >> 8 ^ reg_value) % 4096;
    }

    // --------------------------------------------------------------------
    // the index functions for the tagged tables uses path history as in the OGEHL predictor
    // F serves to mix path history: not very important impact
    // --------------------------------------------------------------------
    int F(uint64_t A, int size, int bank) const
    {
        int A1, A2;
        A = A & ((1 << size) - 1);
        A1 = (A & ((1 << logg[bank]) - 1));
        A2 = (A >> logg[bank]);

        if (bank < logg[bank])
            A2 = ((A2 << bank) & ((1 << logg[bank]) - 1)) + (A2 >> (logg[bank] - bank));

        A = A1 ^ A2;

        if (bank < logg[bank])
            A = ((A << bank) & ((1 << logg[bank]) - 1)) + (A >> (logg[bank] - bank));

        return A;
    }

    // --------------------------------------------------------------------
    // gindex computes a full hash of PC, ghist and phist
    // int gindex (unsigned int PC, int bank, uint64_t hist, const folded_history * ch_i) const
    // --------------------------------------------------------------------
    int gindex(unsigned int PC, int bank, uint64_t hist, const tage_index_t& ch_i) const
    {
        int index;
        int M = (m[bank] > PHISTWIDTH) ? PHISTWIDTH : m[bank];
        index = PC ^ (PC >> (abs(logg[bank] - bank) + 1)) ^ ch_i[bank].comp ^ F(hist, M, bank);

        return (index & ((1 << (logg[bank])) - 1));
    }

    // --------------------------------------------------------------------
    //  tag computation
    // --------------------------------------------------------------------
    uint16_t gtag(unsigned int PC, int bank, const tage_tag_t& tag_0_array, const tage_tag_t& tag_1_array) const
    {
        int tag = (PC) ^ tag_0_array[bank].comp ^ (tag_1_array[bank].comp << 1);
        return (tag & ((1 << (TB[bank])) - 1));
    }

    // --------------------------------------------------------------------
    // up-down saturating counter
    // --------------------------------------------------------------------
    void ctrupdate(int& ctr, bool taken, int nbits)
    {
        if (taken) {
            if (ctr < ((1 << (nbits - 1)) - 1))
                ctr++;
        } else {
            if (ctr > -(1 << (nbits - 1)))
                ctr--;
        }
    }
    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void ctrupdate(int8_t& ctr, bool taken, int nbits)
    {
        if (taken) {
            if (ctr < ((1 << (nbits - 1)) - 1))
                ctr++;
        } else {
            if (ctr > -(1 << (nbits - 1)))
                ctr--;
        }
    }

    // --------------------------------------------------------------------
    // just a simple pseudo random number generator: use available information
    //  to allocate entries  in the loop predictor
    // --------------------------------------------------------------------
    int MYRANDOM()
    {
        Seed++;
        Seed ^= active_hist.phist;
        Seed = (Seed >> 21) + (Seed << 11);
        Seed ^= (int64_t)active_hist.ptghist;
        Seed = (Seed >> 10) + (Seed << 22);
        return (Seed & 0xFFFFFFFF);
    }

    bool    gentry_tag_match(int i)       { return gtable[i][pv.GI[i]].tag == pv.GTAG[i]; }
    int8_t& gentry_ctr(int i)             { return gtable[i][pv.GI[i]].ctr; }
    bool    gentry_pred(int i)            { return gentry_ctr(i) >= 0; }
    int     gentry_magnitude(int i)       { return abs(2 * gentry_ctr(i) + 1); }
    bool    gentry_already_trained(int i) { return gentry_magnitude(i) > 1; } // 3or5or7
    bool    gentry_newly_allocated(int i) { return gentry_magnitude(i) == 1; }

    // --------------------------------------------------------------------
    //  TAGE PREDICTION: same code at fetch or retire time but the index and tags must recomputed
    // --------------------------------------------------------------------
    void Prepare_GI_GTAG_BI(UINT64 PC, const cbp_hist_t& hist_to_use)
    {
        // Calculate tag and index
        for (int i = 1; i <= NHIST; ++i) {
            size_t gi = gindex(PC, i, hist_to_use.phist, hist_to_use.ch_i); // phist and ghist folded into Index-width bits
            uint gt = gtag(PC, i, hist_to_use.ch_t[0], hist_to_use.ch_t[1]); // ghist folded into Tag-width bits and Tag-width -1 bits
            pv.GI[i] = gi;
            pv.GTAG[i] = gt;
        }

        // HighBank
        {
            // We can use only m[BORN] bits for bank shuffling entropy
            int T = (PC >> 2 ^ (hist_to_use.phist & ((1ull << m[BORN]) - 1))) % NBANKHIGH;
            for (int i = BORN; i <= NHIST; i++) {
                pv.GI[i] += (T << LOGG);
                T = (T + 1) % NBANKHIGH; // Adjoining bank
            }
        }

        // Low Bank
        {
            int T = (PC >> 2 ^ (hist_to_use.phist & ((1 << m[1]) - 1))) % NBANKLOW;
            for (int i = 1; i <= BORN - 1; i++) {
                pv.GI[i] += (T << LOGG);
                T = (T + 1) % NBANKLOW;
            }
        }

        // Calculate Bim index
        pv.BI = (PC ^ (PC >> 2)) & ((1 << LOGB) - 1);
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void Tagepred(UINT64 PC, const cbp_hist_t& hist_to_use)
    {
        // Calculate tag and index
        Prepare_GI_GTAG_BI(PC, hist_to_use);

        // Search Bim
        const bool bim = bim_pred[pv.BI] > 0;
        pv.BIM = (bim_pred[pv.BI] << 1) | bim_hyst[pv.BI >> HYSTSHIFT]; // Not a shared-split counter form... just a hysteresis-shared 2bc

        // Look for the bank with longest matching history
        pv.HitBank = 0;
        pv.tage_pred = bim;
        pv.LongestMatchPred = bim;
        pv.HighConf = (pv.BIM == 0) || (pv.BIM == 3);
        pv.LowConf = (pv.BIM == 1) || (pv.BIM == 2);
        pv.MedConf = false;
        for (int i = NHIST; i > 0; i--) {
            if (gentry_tag_match(i)) {
                pv.HitBank = i;
                pv.LongestMatchPred = gentry_pred(pv.HitBank);
                pv.HighConf = gentry_magnitude(pv.HitBank) >= (1 << CWIDTH) - 1; // 7
                pv.MedConf = gentry_magnitude(pv.HitBank) == 5;
                pv.LowConf = gentry_magnitude(pv.HitBank) == 1;
                break;
            }
        }

        // Look for the alternate bank
        pv.AltBank = 0;
        pv.alttaken = bim;
        pv.AltConf = (pv.BIM == 0) || (pv.BIM == 3);
        for (int i = pv.HitBank - 1; i > 0; i--) {
            if (gentry_tag_match(i)) {
                pv.AltBank = i;
                pv.alttaken = gentry_pred(pv.AltBank);
                pv.AltConf = gentry_already_trained(pv.AltBank); // 3 or 5 or 7
                break;
            }
        }

        // computes the prediction and the alternate prediction
        if (pv.HitBank > 0) {
            // if the entry is recognized as a newly allocated entry and
            // USE_ALT_ON_NA is positive  use the alternate prediction
            bool Huse_alt_on_na = (use_alt_on_na[INDUSEALT] >= 0);
            if (not gentry_newly_allocated(pv.HitBank)) {
                pv.tage_pred = pv.LongestMatchPred; // Not newly allocated: choose the longest match
            } else if (Huse_alt_on_na) {
                pv.tage_pred = pv.alttaken; // Newly allocated and use_alt_on_na says it's not high conf: choose the second longest match
            } else {
                pv.tage_pred = pv.LongestMatchPred; // Otherwise, choose the longest match
            }
        }
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    bool predict_using_given_hist(uint64_t seq_no, uint8_t piece, UINT64 PC, cbp_hist_t& hist_to_use, const bool pred_time_predict)
    {
        // computes the TAGE table addresses and the partial tags
        Tagepred(PC, hist_to_use);
#ifndef SC
        return pv.tage_pred;
#endif
        pv.pred_inter = pv.tage_pred; // We don't use the Loop Predictor, thus, pred_inter is the same as tage_pred.

        for (int bank = 0; bank < RBiasNBanks; ++bank) {
            hist_to_use.best_reg[bank] = -1;
        }

        int extra_weight = 0;
        pv.LSUM = 0;

        for (int i = 0; i <= 64; ++i) {
            if (hist_to_use.register_values[i] != -1 && WR.get(PC, i) >= 0) {
                if (hist_to_use.best_reg[i % RBiasNBanks] == -1) {
                    hist_to_use.best_reg[i % RBiasNBanks] = i;
                } else if (WR.get(PC, hist_to_use.best_reg[i % RBiasNBanks]) < WR.get(PC, i)) {
                    hist_to_use.best_reg[i % RBiasNBanks] = i;
                }
            }
        }

        for (int bank = 0; bank < RBiasNBanks; ++bank) {
            int i = hist_to_use.best_reg[bank];
            if (i == -1) {
                continue;
            }
            assert(hist_to_use.register_values[i] != -1);
            assert(WR.get(PC, i) >= 0);
            pv.LSUM += RBias.get(rbias_index(PC, hist_to_use.register_values[i]), i) * RBiasScale;
            extra_weight += RBiasScale;
        }

        // Compute the SC prediction
        pv.LSUM += bias_components.get_weighted_value(PC, 0ull, { pv.HitBank, pv.HighConf, pv.LowConf, pv.LongestMatchPred, pv.alttaken, pv.pred_inter });
        pv.LSUM += global_GEHL_components.get_weighted_value((PC << 1) + pv.pred_inter, hist_to_use.GHIST, {});
        pv.LSUM += path_GEHL_components.get_weighted_value(PC, hist_to_use.fphist, {});
        pv.LSUM += local1_GEHL_components.get_weighted_value(PC, hist_to_use.local1_hist(PC), {});
        pv.LSUM += local2_GEHL_components.get_weighted_value(PC, hist_to_use.local2_hist(PC), {});
        pv.LSUM += local3_GEHL_components.get_weighted_value(PC, hist_to_use.local3_hist(PC), {});
        pv.LSUM += call_stack_GEHL_components.get_weighted_value(PC, hist_to_use.call_stack_hist(), {});
        pv.LSUM += IMLI_components.get_weighted_value(PC, hist_to_use.BrIMLI, hist_to_use.TaIMLI);
        pv.SCPRED = (pv.LSUM >= 0);

        // just  an heuristic if the respective contribution of component groups can be multiplied by 2 or not
        const int base_threshold = (updatethreshold >> 1) + Pupdatethreshold[INDUPD];

        extra_weight += 2 * bias_components.get_extra_weight(PC);
        extra_weight += 2 * global_GEHL_components.get_extra_weight(PC);
        extra_weight += 2 * path_GEHL_components.get_extra_weight(PC);
        extra_weight += 2 * local1_GEHL_components.get_extra_weight(PC);
        extra_weight += 2 * local2_GEHL_components.get_extra_weight(PC);
        extra_weight += 2 * local3_GEHL_components.get_extra_weight(PC);
        extra_weight += 2 * call_stack_GEHL_components.get_extra_weight(PC);
        extra_weight += 2 * IMLI_components.get_extra_weight(PC);
        pv.THRES = base_threshold + 6 * extra_weight;

        // Minimal benefit in trying to avoid accuracy loss on low confidence SC prediction and  high/medium confidence on TAGE
        //  but just uses 2 counters 0.3 % MPKI reduction
        const int chooser = pv.chooser();
        switch (chooser) {
        case 0:
            return pv.pred_inter;
        case 1:
            return FirstH < 0 ? pv.SCPRED : pv.pred_inter;
        case 2:
            return SecondH < 0 ? pv.SCPRED : pv.pred_inter;
        case 3:
            return pv.SCPRED;
        default:
            throw nullptr;
        }
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void HistoryUpdate(UINT64 PC, int brtype, bool pred_taken, bool taken, UINT64 nextPC)
    {
        #ifdef INSTRUMENT_HISTORY
            const auto hc = hist_instr::on_historyupdate_begin(PC, brtype, taken ? 1 : 0, nextPC);
        #endif

        auto& X = active_hist.phist;
        auto& Y = active_hist.ptghist;

        auto& H = active_hist.ch_i;
        auto& G = active_hist.ch_t[0];
        auto& J = active_hist.ch_t[1];

        // special treatment for indirect  branchs;
        int maxt = 2;
        if (brtype & 1) // conditional
            maxt = 2;
        else if ((brtype & 2))
            maxt = 3;

        #ifdef INSTRUMENT_HISTORY
            hist_instr::on_historyupdate_steps(hc, maxt);
        #endif

        if (brtype & 1) {
            active_hist.GHIST = (active_hist.GHIST << 1) + (taken & (nextPC < PC)); // backward taken only dir history (low entropy)
            active_hist.local1_hist(PC) =  (active_hist.local1_hist(PC) << 1) | taken;
            active_hist.local2_hist(PC) = ((active_hist.local2_hist(PC) << 1) | taken) ^ (PC & 15);
            active_hist.local3_hist(PC) =  (active_hist.local3_hist(PC) << 1) | taken;
        }

        int T = ((PC ^ (PC >> 2))) ^ taken;
        int PATH = PC ^ (PC >> 2) ^ (PC >> 4);

        for (int t = 0; t < maxt; t++) {
            bool DIR = (T & 1);
            T >>= 1;
            int PATHBIT = (PATH & 127);
            PATH >>= 1;
            // update  history
            Y--; // ptghist
            active_hist.ghist[Y & (HISTBUFFERLENGTH - 1)] = DIR;
            X = (X << 1) ^ PATHBIT; // phist

            // updates to folded histories
            for (int i = 1; i <= NHIST; i++) {
                H[i].update(active_hist.ghist, Y);
                G[i].update(active_hist.ghist, Y);
                J[i].update(active_hist.ghist, Y);
            }
        }
        X = (X & ((1 << PHISTWIDTH) - 1));

        // forward taken path history
        if (nextPC > PC && taken)
            active_hist.fphist = (active_hist.fphist << 3) ^ (nextPC >> 2) ^ (PC >> 1);

        // for call stack history
        active_hist.call_stack_hist() <<= 1;
        active_hist.call_stack_hist() |= taken;
        if (brtype & 4) { // call
            active_hist.CallStackPtr += 1;
            active_hist.CallStackPtr %= sCallStack::FeatureSize;
            active_hist.call_stack_hist() = 0;
        }
        if (brtype & 8) { // return
            active_hist.CallStackPtr += sCallStack::FeatureSize - 1;
            active_hist.CallStackPtr %= sCallStack::FeatureSize;
        }

        // IMLI
        if (taken && nextPC < PC && (brtype & 2) == 0) {
            int prime[18] = { 1, 1, 3, 7, 13, 31, 61, 127, 251, 509, 1021, 2039, 4093, 8191, 16381, 32749, 65521, 131071 }; // not exceeding the power of two
            if (active_hist.last_backward_target / 128 == nextPC / 128) {
                active_hist.TaIMLI = (active_hist.TaIMLI + 1) % prime[sTaIMLI::LogSize];
            } else {
                active_hist.TaIMLI = 0;
            }

            if (active_hist.last_backward_pc / 128 == PC / 128) {
                active_hist.BrIMLI = (active_hist.BrIMLI + 1) % (1ull << sBrIMLI::LogSize);
            } else {
                active_hist.BrIMLI = 0;
            }
            active_hist.last_backward_target = nextPC;
            active_hist.last_backward_pc = PC;

            #ifdef INSTRUMENT_HISTORY
                hist_instr::on_imli_update(active_hist.BrIMLI, active_hist.TaIMLI);
            #endif
        }

    } // END UPDATE  HISTORIES

    // --------------------------------------------------------------------
    // PREDICTOR UPDATE
    // --------------------------------------------------------------------
    void baseupdate(bool Taken)
    {
        int inter = pv.BIM;
        if (Taken) {
            if (inter < 3)
                inter += 1;
        } else if (inter > 0)
            inter--;
        bim_pred[pv.BI] = inter >> 1;
        bim_hyst[pv.BI >> HYSTSHIFT] = (inter & 1);
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void TAGE_do_allocation(bool resolveDir)
    {
        int NumberOfBlocks = 0;
        int NumberOfAllocations = 0;

        const bool AggressiveAllocation = NewlyDecay <= NewlyUseful * 2;
        const bool ModestAllocation = NewlyDecay > NewlyUseful * 4;
        int RestAllocations = AggressiveAllocation ? NNN + 2 : ModestAllocation ? NNN
                                                                                : NNN + 1;

        const int skip = (MYRANDOM() & 127) < 32 ? 1 : 0;
        const int provider_bank_next = pv.HitBank + 1;
        const int start_bank = provider_bank_next + skip;

        int last_alloc_index = -1;

        for (int i = start_bank; i <= NHIST; ++i) {
            if (AggressiveAllocation && gtable[i][pv.GI[i]].is_newly_alloc()) {
                gtable[i][pv.GI[i]].useful_or_newly_alloc = false;
                ++NewlyDecay;
                if (NewlyDecay >= 1 << LogMaxNewlyCounters) {
                    NewlyDecay >>= 1;
                    NewlyUseful >>= 1;
                }
                --RestAllocations;
            } else if (gtable[i][pv.GI[i]].is_useful() == 0) {
                if (abs(2 * gtable[i][pv.GI[i]].ctr + 1) <= 3) {
                    gtable[i][pv.GI[i]].tag = pv.GTAG[i];
                    gtable[i][pv.GI[i]].ctr = (resolveDir) ? 0 : -1;
                    if (gtable[i][pv.GI[i]].is_newly_alloc()) {
                        gtable[i][pv.GI[i]].useful_or_newly_alloc = false;
                        ++NewlyDecay;
                        if (NewlyDecay >= 1 << LogMaxNewlyCounters) {
                            NewlyDecay >>= 1;
                            NewlyUseful >>= 1;
                        }
                    }
                    last_alloc_index = i;
                    ++NumberOfAllocations;
                    RestAllocations -= 1;
                    if (RestAllocations <= 0) {
                        break;
                    }
                    i += i < 3 ? 1 : i < Born2 ? 2
                                               : 0;
                } else {
                    if (gtable[i][pv.GI[i]].ctr > 0)
                        gtable[i][pv.GI[i]].ctr--;
                    else
                        gtable[i][pv.GI[i]].ctr++;
                }
            } else {
                ++NumberOfBlocks;
            }
        }

        if (last_alloc_index != -1)
            gtable[last_alloc_index][pv.GI[last_alloc_index]].useful_or_newly_alloc = true;

        TICK_update(NumberOfBlocks, NumberOfAllocations);
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void SC_update(UINT64 PC, bool resolveDir, const cbp_hist_t& hist_to_use)
    {
        pv.SCPRED = pv.LSUM >= 0;

        // FirstH and SecondH is meta predictors (tagged vs SC)
        // Train FirstH and SecondH
        const int chooser = pv.chooser();
        switch (chooser) {
        case 0:
            break; // do nothing
        case 1:
            ctrupdate(FirstH, (pv.pred_inter == resolveDir), CONFWIDTH);
            break;
        case 2:
            ctrupdate(SecondH, (pv.pred_inter == resolveDir), CONFWIDTH);
            break;
        case 3:
            break; // do nothing
        default:
            throw nullptr;
        }

        bool sum_at_prediction_is_weak = abs(hist_to_use.perceptron_sum_at_prediction) < pv.THRES;
        bool sum_at_train_is_weak = abs(pv.LSUM) < pv.THRES;
        bool mispred_at_prediction = (hist_to_use.perceptron_sum_at_prediction >= 0) != resolveDir;
        bool mispred_at_train = (pv.LSUM >= 0) != resolveDir;

        auto threshold_update = [PC](int up) {
            Pupdatethreshold[INDUPD] = ::std::min((1 << (WIDTHRESP - 1)) - 1, ::std::max(-(1 << (WIDTHRESP - 1)), Pupdatethreshold[INDUPD] + up));
            updatethreshold = ::std::min((1 << (WIDTHRES - 1)) - 1, ::std::max(-(1 << (WIDTHRES - 1)), updatethreshold + up));
        };

        if (mispred_at_prediction and not mispred_at_train) {
            threshold_update(+6); // FTL++-like threshold update. There are strong perturbations.
        }
        if (mispred_at_prediction and mispred_at_train) {
            threshold_update(+1); // Mispredictions; increase the threshold (there are many perturbations)
        }
        if (not mispred_at_prediction and sum_at_prediction_is_weak and sum_at_train_is_weak) {
            threshold_update(-1); // A weak prediction is correct; decrease the threshold (there are small perturbations)
        }
        // Do not decrease the threshold in the cases wehere sum_at_prediction_is_weak but not sum_at_train_is_weak; it may be a loop begining.

        if (mispred_at_train or sum_at_train_is_weak) {
            bias_components.update(pv.LSUM, resolveDir, PC, 0ull, { pv.HitBank, pv.HighConf, pv.LowConf, pv.LongestMatchPred, pv.alttaken, pv.pred_inter });
            global_GEHL_components.update(pv.LSUM, resolveDir, (PC << 1) + pv.pred_inter, hist_to_use.GHIST, {});
            path_GEHL_components.update(pv.LSUM, resolveDir, PC, hist_to_use.fphist, {});
            local1_GEHL_components.update(pv.LSUM, resolveDir, PC, hist_to_use.local1_hist(PC), {});
            local2_GEHL_components.update(pv.LSUM, resolveDir, PC, hist_to_use.local2_hist(PC), {});
            local3_GEHL_components.update(pv.LSUM, resolveDir, PC, hist_to_use.local3_hist(PC), {});
            call_stack_GEHL_components.update(pv.LSUM, resolveDir, PC, hist_to_use.call_stack_hist(), {});
            IMLI_components.update(pv.LSUM, resolveDir, PC, hist_to_use.BrIMLI, hist_to_use.TaIMLI);

            bool bank_used[RBiasNBanks] = {};
            // Train used bank
            for (int bank = 0; bank < RBiasNBanks; ++bank) {
                int i = hist_to_use.best_reg[bank];
                if (i == -1) {
                    continue;
                }
                assert(hist_to_use.register_values[i] != -1);
                assert(WR.get(PC, i) >= 0);
                int c = RBias.get(rbias_index(PC, hist_to_use.register_values[i]), i);
                int XSUM = pv.LSUM - c * RBiasScale * (WR.get(PC, i) >= 0);
                RBias.train(rbias_index(PC, hist_to_use.register_values[i]), i, resolveDir);
                if ((XSUM + RBiasScale * c >= 0) != (XSUM >= 0)) {
                    WR.train(PC, i, (c >= 0) == resolveDir);
                }
                bank_used[i % RBiasNBanks] = true;
            }
            // Explore
            uint64_t start_pos = MYRANDOM() % 65;
            for (int j = 0; j <= 64; ++j) {
                uint64_t i = (j + start_pos) % 65;
                if (hist_to_use.register_values[i] != -1 && not bank_used[i % RBiasNBanks]) {
                    int c = RBias.get(rbias_index(PC, hist_to_use.register_values[i]), i);
                    int XSUM = pv.LSUM - c * RBiasScale * (WR.get(PC, i) >= 0);
                    RBias.train(rbias_index(PC, hist_to_use.register_values[i]), i, resolveDir);
                    if ((XSUM + RBiasScale * c >= 0) != (XSUM >= 0)) {
                        WR.train(PC, i, (c >= 0) == resolveDir);
                    }
                    bank_used[i % RBiasNBanks] = true;
                }
            }
        }
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void TAGE_allocation(UINT64 PC, bool resolveDir, bool pred_taken)
    {
        bool ALLOC = ((pv.tage_pred != resolveDir) & (pv.HitBank < NHIST));

        if (pv.HitBank > 0) {
            const bool PseudoNewAlloc = gentry_newly_allocated(pv.HitBank);
            if (PseudoNewAlloc) {
                // The longest match entry will provide correct prediction if it is trained now.
                // (even if use_alt_on_na or SC cause a misprediction). Thus, do not allocate.
                if (pv.LongestMatchPred == resolveDir)
                    ALLOC = false;

                // Train use_alt_on_na
                if (pv.LongestMatchPred != pv.alttaken) {
                    ctrupdate(use_alt_on_na[INDUSEALT], (pv.alttaken == resolveDir), ALTWIDTH);
                }
            }
        }

        // Do not allocate too often if the overall prediction is correct
        if (pred_taken == resolveDir)
            if ((MYRANDOM() & 31) != 0)
                ALLOC = false;

        if (ALLOC) {
            TAGE_do_allocation(resolveDir);
        }
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void TAGE_train(UINT64 PC, bool resolveDir)
    {
        if (pv.HitBank > 0) {
            // Only when the longest-match entry mispredicts, train the second
            // longest entry. It is not 'useful'.
            if (gentry_magnitude(pv.HitBank) == 1 && pv.LongestMatchPred != resolveDir) {
                if (pv.AltBank > 0) {
                    if (gtable[pv.AltBank][pv.GI[pv.AltBank]].is_newly_alloc())
                        gtable[pv.AltBank][pv.GI[pv.AltBank]].useful_or_newly_alloc = false;
                    ctrupdate(gentry_ctr(pv.AltBank), resolveDir, CWIDTH);
                    if (abs(2 * gtable[pv.AltBank][pv.GI[pv.AltBank]].ctr + 1) == 1)
                        // Ensure u == false when abs(2*ctr+1) == 1 for our novel states
                        gtable[pv.AltBank][pv.GI[pv.AltBank]].useful_or_newly_alloc = false;
                } else {
                    baseupdate(resolveDir);
                }
            }

            if (gtable[pv.HitBank][pv.GI[pv.HitBank]].is_newly_alloc()) {
                if (pv.LongestMatchPred == resolveDir) {
                    ++NewlyUseful;
                }
                gtable[pv.HitBank][pv.GI[pv.HitBank]].useful_or_newly_alloc = false;
                if (NewlyUseful >= 1 << LogMaxNewlyCounters) {
                    NewlyDecay >>= 1;
                    NewlyUseful >>= 1;
                }
            }

            // Train the longest-match entry
            // The counter come 0 or -1 states, remove 'useful'.
            ctrupdate(gentry_ctr(pv.HitBank), resolveDir, CWIDTH);
            if (gentry_magnitude(pv.HitBank) == 1)
                gtable[pv.HitBank][pv.GI[pv.HitBank]].useful_or_newly_alloc = false;

            // The second-longest-match entry has high confidence
            // and provides the same direction and it's correct, remove 'useful'.
            if (pv.alttaken == resolveDir && pv.AltBank > 0 && gentry_magnitude(pv.AltBank) == 7)
                if (pv.LongestMatchPred == resolveDir && gtable[pv.HitBank][pv.GI[pv.HitBank]].is_useful())
                    gtable[pv.HitBank][pv.GI[pv.HitBank]].useful_or_newly_alloc = false;
        } else {
            // The longest match is Bim, train Bim.
            baseupdate(resolveDir);
        }

        // The longest-match entry revokes the second-longest-match entry and it's correct, add 'useful'.
        if (pv.LongestMatchPred != pv.alttaken && pv.LongestMatchPred == resolveDir)
            gtable[pv.HitBank][pv.GI[pv.HitBank]].useful_or_newly_alloc = true;
    }

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void update(UINT64 PC, bool resolveDir, bool pred_taken, UINT64 nextPC,
                const cbp_hist_t& hist_to_use)
    {
        #ifdef SC
        SC_update(PC, resolveDir, hist_to_use);
        #endif
        TAGE_allocation(PC, resolveDir, pred_taken);
        TAGE_train(PC, resolveDir);
    } // END PREDICTOR UPDATE

    // --------------------------------------------------------------------
    // Trampoline functions
    // --------------------------------------------------------------------
    bool predict(uint64_t seq_no, uint8_t piece, UINT64 PC)
    {
        for (int i = 0; i <= 64; ++i) {
            active_hist.register_values.at(i) = RegFileState.at(i).valid ? RegFileState.at(i).payload : -1; // hashed_value
        }

        pred_time_histories.emplace(get_unique_inst_id(seq_no, piece), active_hist); // checkpoint current hist
        const bool pred_taken = predict_using_given_hist(seq_no, piece, PC, active_hist, true /*pred_time_predict*/);
        pred_time_histories.at(get_unique_inst_id(seq_no, piece)).perceptron_sum_at_prediction = pv.LSUM;
        return pred_taken;
    }
    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void history_update(uint64_t seq_no, uint8_t piece, UINT64 PC, int brtype, bool pred_taken, bool taken, UINT64 nextPC)
    {
        HistoryUpdate(PC, brtype, pred_taken, taken, nextPC);
    }
    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void TrackOtherInst(UINT64 PC, int brtype, bool pred_taken, bool taken, UINT64 nextPC)
    {
        HistoryUpdate(PC, brtype, pred_taken, taken, nextPC);
    }
    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void update(uint64_t seq_no, uint8_t piece, UINT64 PC, bool resolveDir, bool predDir, UINT64 nextPC)
    {
        const auto pred_hist_key = get_unique_inst_id(seq_no, piece);
        auto& pred_time_history = pred_time_histories.at(pred_hist_key);
        const bool pred_taken = predict_using_given_hist(seq_no, piece, PC, pred_time_history, false /*pred_time_predict*/);
        update(PC, resolveDir, pred_taken, nextPC, pred_time_history);
        pred_time_histories.erase(pred_hist_key);
    }
    void decode_notify(uint64_t seq_no, uint8_t piece, uint64_t dst_reg)
    {
        for (size_t i = 0; i < RegFileState.size(); ++i) {
            if (not RegFileState.at(i).valid)
                continue;
            if (RegFileState.at(i).ctr == 255) {
                RegFileState.at(i).valid = false; // too old. Invalidate.
                RegFileState.at(i).ctr = 0;
            } else {
                ++RegFileState.at(i).ctr;
            }
        }

        RegFileState.at(dst_reg).valid = false;
        RegFileState.at(dst_reg).payload = (seq_no % 1024 << 4) | piece; // last_write_instr_id
        RegFileState.at(dst_reg).ctr = 0;
    }
    // --------------------------------------------------------------------
    // --------------------------------------------------------------------
    void execute_notify(uint64_t seq_no, uint8_t piece, uint64_t dst_reg, uint64_t value)
    {
        if (RegFileState.at(dst_reg).valid == false && RegFileState.at(dst_reg).payload == ((seq_no % 1024 << 4) | piece)) {
            RegFileState.at(dst_reg).valid = true;
            RegFileState.at(dst_reg).payload = make_reg_digest(dst_reg, value); // hashed_value
        }
    }
};
// =================
// Predictor End
// =================

#undef UINT64

