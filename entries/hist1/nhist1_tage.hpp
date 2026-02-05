class gentry // TAGE global table entry
{
public:
    int8_t ctr;
    uint tag;
    // useful_or_newly_alloc means...
    // ctr == 3,5,7 : useful (u)
    // ctr == 1 : newly allocated (novel state)
    bool useful_or_newly_alloc;

    bool is_useful() const { return abs(2 * ctr + 1) != 1 & useful_or_newly_alloc; }
    bool is_newly_alloc() const { return abs(2 * ctr + 1) == 1 & useful_or_newly_alloc; }

    gentry()
    {
        ctr = 0;
        useful_or_newly_alloc = false;
        tag = 0;
    }
};

#define NBANKLOW 9 // number of banks in the shared bank-interleaved for the low history lengths
#define NBANKHIGH 25 // number of banks in the shared bank-interleaved for the  history lengths

int SizeTable[NHIST + 1];

#define BORN 5 // below BORN in the table for low history lengths, >= BORN in the table for high history lengths,

#define LOGG 11 /* logsize of the  banks in the  tagged TAGE tables */
#define TBITS 9 // minimum width of the tags  (low history lengths), +4 for high history lengths

#define NNN 2 // number of extra entries allocated on a TAGE misprediction (1+NNN)
#define HYSTSHIFT 2 // bimodal hysteresis shared by 4 entries
#define LOGB 17 // log of number of entries in bimodal predictor

std::array<int8_t, 1ull << LOGB> bim_pred;
std::array<int8_t, 1ull << LOGB - HYSTSHIFT> bim_hyst;

#define PHISTWIDTH 27 // width of the path history used in TAGE
#define UWIDTH 1 // u counter width on TAGE (2 bits not worth the effort for a 512 Kbits predictor 0.2 %)
#define CWIDTH 3 // predictor counter width on the TAGE tagged tables

// the counter(s) to chose between longest match and alternate prediction on TAGE when weak counters
#define LOGSIZEUSEALT 4
#define ALTWIDTH 5
#define SIZEUSEALT (1 << (LOGSIZEUSEALT))
#define INDUSEALT (((((pv.HitBank - 1) / 8) << 1) + pv.AltConf) % (SIZEUSEALT - 1))
int8_t use_alt_on_na[SIZEUSEALT];

// For the TAGE predictor
gentry* gtable[NHIST + 1]; // tagged TAGE tables
int m[NHIST + 1];
int TB[NHIST + 1];
int logg[NHIST + 1];

// Monitoring blocking frequency by u (useful)
static int BORNTICK = 1024;
int TICK; // [0, 1024)

void TICK_update(int NumberOfBlocks, int NumberOfAllocations)
{
    TICK += (NumberOfBlocks - 2 * NumberOfAllocations);
    if (TICK < 0) {
        TICK = 0;
    }
    if (TICK >= BORNTICK) {
        // Low Bank
        for (int j = 0; j < SizeTable[1]; ++j) {
            if (gtable[1][j].is_useful())
                gtable[1][j].useful_or_newly_alloc = false;
        }
        // High Bank
        for (int j = 0; j < SizeTable[BORN]; ++j) {
            if (gtable[BORN][j].is_useful())
                gtable[BORN][j].useful_or_newly_alloc = false;
        }
        // Reset TICK
        TICK = 0;
    }
}

uint64_t Seed; // for the pseudo-random number generator

class folded_history {
public:
    unsigned comp;
    int CLENGTH;
    int OLENGTH;
    int OUTPOINT;

    folded_history()
    {
    }

    void init(int original_length, int compressed_length)
    {
        comp = 0;
        OLENGTH = original_length;
        CLENGTH = compressed_length;
        OUTPOINT = OLENGTH % CLENGTH;
    }

    void update(std::array<uint8_t, HISTBUFFERLENGTH>& h, int PT)
    {
        comp = (comp << 1) ^ h[PT & (HISTBUFFERLENGTH - 1)];
        comp ^= h[(PT + OLENGTH) & (HISTBUFFERLENGTH - 1)] << OUTPOINT;
        comp ^= (comp >> CLENGTH);
        comp = (comp) & ((1 << CLENGTH) - 1);
    }
};
using tage_index_t = std::array<folded_history, NHIST + 1>;
using tage_tag_t = std::array<folded_history, NHIST + 1>;

struct cbp_hist_t {
    // Checkpoint information
    tage_index_t ch_i;
    std::array<tage_tag_t, 2> ch_t;
    int perceptron_sum_at_prediction;
    std::array<uint64_t, 65> register_values = {}; // Resister value, only best_reg is neeeded (max 8 registers)
    int best_reg[RBiasNBanks];

    // Begin Conventional Histories
    uint64_t phist; // path history
    std::array<uint8_t, HISTBUFFERLENGTH> ghist;
    int ptghist;

    // For SC
    uint64_t GHIST; // backward history
    uint64_t fphist; // forward taken path history
    std::array<uint64_t, sLocal1::FeatureSize> L_shist;
    std::array<uint64_t, sLocal2::FeatureSize> S_slhist;
    std::array<uint64_t, sLocal3::FeatureSize> T_slhist;
    std::array<uint64_t, sCallStack::FeatureSize> C_hist;
    size_t CallStackPtr = 0;

    uint64_t last_backward_target = 0;
    uint64_t last_backward_pc = 0;
    uint64_t BrIMLI = 0;
    uint64_t TaIMLI = 0;

    uint64_t& local1_hist(uint64_t PC) { return L_shist.at(sLocal1::get_index(PC)); }
    uint64_t& local2_hist(uint64_t PC) { return S_slhist.at(sLocal2::get_index(PC)); }
    uint64_t& local3_hist(uint64_t PC) { return T_slhist.at(sLocal3::get_index(PC)); }
    uint64_t& call_stack_hist() { return C_hist.at(CallStackPtr); }
    uint64_t local1_hist(uint64_t PC) const { return L_shist.at(sLocal1::get_index(PC)); }
    uint64_t local2_hist(uint64_t PC) const { return S_slhist.at(sLocal2::get_index(PC)); }
    uint64_t local3_hist(uint64_t PC) const { return T_slhist.at(sLocal3::get_index(PC)); }
    uint64_t call_stack_hist() const { return C_hist.at(CallStackPtr); }

    cbp_hist_t()
    {
    }
};

void print_predictorsize()
{
    int STORAGESIZE = 0;

    int bim = (1 << LOGB) + (1 << (LOGB - HYSTSHIFT));
    printf("(BIM %d) ", bim);
    STORAGESIZE += bim;

    int tagged = 0;
    tagged += NBANKHIGH * (1 << (logg[BORN])) * (CWIDTH + UWIDTH + TB[BORN]);
    tagged += NBANKLOW * (1 << (logg[1])) * (CWIDTH + UWIDTH + TB[1]);

    tagged += (SIZEUSEALT)*ALTWIDTH;
    tagged += m[NHIST];
    tagged += PHISTWIDTH;
    tagged += 10; // the TICK counter

    tagged += LogMaxNewlyCounters * 2; // Allocation throttling counters (NewlyDecay and NewlyUseful)

    printf("(Tagged %d) ", tagged);
    STORAGESIZE += tagged;

    int inter = 0;
#ifdef SC
    inter += WIDTHRES;
    inter = WIDTHRESP * ((1 << LOGSIZEUP)); // the update threshold counters

    inter += bias_components.storage_size();
    inter += global_GEHL_components.storage_size();
    inter += global_GEHL_components.MaxLength; // global backward history for SC
    inter += path_GEHL_components.storage_size();
    inter += path_GEHL_components.MaxLength; // forward taken pathhistory

    inter += local1_GEHL_components.storage_size();
    inter += sLocal1::FeatureSize * local1_GEHL_components.MaxLength;
    inter += local2_GEHL_components.storage_size();
    inter += sLocal2::FeatureSize * local2_GEHL_components.MaxLength;
    inter += local3_GEHL_components.storage_size();
    inter += sLocal3::FeatureSize * local3_GEHL_components.MaxLength;

    inter += call_stack_GEHL_components.storage_size();
    inter += sCallStack::FeatureSize * call_stack_GEHL_components.MaxLength;
    inter += 3; // CallStackPtr

    inter += IMLI_components.storage_size();
    inter += 64; // last_backward_target
    inter += 64; // last_backward_pc
    inter += sBrIMLI::LogSize; // BrIMLI
    inter += sTaIMLI::LogSize; // TaIMLI

    printf("(TraditionalSC %d) ", inter);

    inter += 2 * CONFWIDTH; // the 2 counters in the choser

    inter += RBias.storage_size();
    inter += WR.storage_size();
    inter += 65 * (1 + 14 + 8); // valid(1bit) + union{last_write_instr_id, hashed_value}(14bit), ctr(8bit)

    printf("(TotalSC %d) ", inter);
    STORAGESIZE += inter;
#endif

    printf("\nStorageSizeKiB = %f (%d bits)", STORAGESIZE / 8192., STORAGESIZE);
}

struct PredRelatedVariables {
    int LSUM;
    bool MedConf;
    bool AltConf; // Confidence on the alternate prediction
    int8_t BIM;
    bool tage_pred; // TAGE prediction
    bool alttaken; // alternate  TAGEprediction
    bool LongestMatchPred;
    int HitBank; // longest matching bank
    int AltBank; // alternate matching bank
    bool pred_inter;

    bool LowConf;
    bool HighConf;

    // state set by predict
    int GI[NHIST + 1]; // indexes to the different tables are computed only once
    uint GTAG[NHIST + 1]; // tags for the different tables are computed only once
    int BI; // index of the bimodal table

    //
    int THRES;
    //

    bool final_prediction;
    bool SCPRED;

    int chooser() const
    {
        if (pred_inter != SCPRED) {
            if (HighConf) {
                if (abs(LSUM) < THRES / 4) {
                    return 0; // use pred_inter
                } else if (abs(LSUM) < THRES / 2) {
                    return 2; // use SecondH<0 ? SCPRED : pred_inter
                } else {
                    return 3; // use SCPRED
                }
            } else if (MedConf) {
                if (abs(LSUM) < THRES / 4) {
                    return 1; // use FirstH<0 ? SCPRED : pred_inter
                } else {
                    return 3; // use SCPRED
                }
            } else {
                return 3; // use SCPRED
            }
        } else {
            return 3; // use SCPRED (== pred_inter)
        }
    }
};



