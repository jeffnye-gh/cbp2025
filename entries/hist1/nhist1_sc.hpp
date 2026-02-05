static constexpr int RBiasScale = 5;
static constexpr size_t RBiasNBanks = 8;

// Captures register correlation
class RBias_ {
    std::array<std::array<int8_t, 4096 / RBiasNBanks>, RBiasNBanks> array1; // 3KiB
    std::array<std::array<int8_t, 2048 / RBiasNBanks>, RBiasNBanks> array2; // 1.5KiB
    std::array<std::array<int8_t, 1024 / RBiasNBanks>, RBiasNBanks> array3; // 0.75KiB
    size_t index1(uint64_t PR, int reg_number) const
    {
        return (PR + reg_number * 633) % array1[0].size();
    }
    size_t index2(uint64_t PR, int reg_number) const
    {
        return ((PR >> 2 ^ PR << 6) + reg_number) % array2[0].size();
    }
    size_t index3(uint64_t PR, int reg_number) const
    {
        return (PR ^ reg_number << 3) % array3[0].size();
    }
    void ctrupdate(int8_t& ctr, bool taken)
    {
        if (taken)
            ctr < 31 && ++ctr;
        if (not taken)
            ctr > -32 && --ctr;
    }

public:
    RBias_()
        : array1 {}
        , array2 {}
        , array3 {}
    {
    }
    int get(uint64_t PR, int reg_number) const
    {
        int sum = 0;
        sum += array1.at(reg_number % RBiasNBanks).at(index1(PR, reg_number));
        sum += array2.at(reg_number % RBiasNBanks).at(index2(PR, reg_number));
        sum += array3.at(reg_number % RBiasNBanks).at(index3(PR, reg_number));
        return sum;
    }
    void train(uint64_t PR, int reg_number, bool useful)
    {
        ctrupdate(array1.at(reg_number % RBiasNBanks).at(index1(PR, reg_number)), useful);
        ctrupdate(array2.at(reg_number % RBiasNBanks).at(index2(PR, reg_number)), useful);
        ctrupdate(array3.at(reg_number % RBiasNBanks).at(index3(PR, reg_number)), useful);
    }
    int storage_size() const
    {
        return (array1[0].size() + array2[0].size() + array3[0].size()) * 6 * RBiasNBanks;
    }
} RBias;

// Usefulness of RBias, for each (PC, reg_number) pair
class WR_ {
    std::array<int8_t, 65 * 8> array1; // 0.381 KiB
    std::array<int8_t, 65 * 8> array2; // 0.381 KiB
    std::array<int8_t, 65 * 8> array3; // 0.381 KiB

    // No bank interleave needed
    size_t index1(uint64_t PC, int reg_number) const
    {
        return reg_number * 8 + (PC ^ PC >> 2) % 8;
    }
    size_t index2(uint64_t PC, int reg_number) const
    {
        return reg_number * 8 + (PC ^ PC >> 4) % 8;
    }
    size_t index3(uint64_t PC, int reg_number) const
    {
        return reg_number * 8 + (PC ^ PC >> 6) % 8;
    }
    void ctrupdate(int8_t& ctr, bool useful)
    {
        if (useful)
            ctr < 31 && ++ctr;
        if (not useful)
            ctr > -32 && --ctr;
    }

public:
    WR_()
        : array1 {}
        , array2 {}
        , array3 {}
    {
        std::fill(array1.begin(), array1.end(), -8);
        std::fill(array2.begin(), array2.end(), -8);
        std::fill(array3.begin(), array3.end(), -8);
    }
    int get(uint64_t PC, int reg_number) const
    {
        int sum = 0;
        sum += array1.at(index1(PC, reg_number));
        sum += array2.at(index2(PC, reg_number));
        sum += array3.at(index3(PC, reg_number));

        return sum;
    }
    void train(uint64_t PC, int reg_number, bool useful)
    {
        ctrupdate(array1.at(index1(PC, reg_number)), useful);
        ctrupdate(array2.at(index2(PC, reg_number)), useful);
        ctrupdate(array3.at(index3(PC, reg_number)), useful);
    }
    int storage_size() const
    {
        return (array1.size() + array2.size() + array3.size()) * 6;
    }
} WR;

struct RegFileStateEntry {
    bool valid; // 1 bit
    uint64_t payload; // union{last_write_instr_id, hashed_value} 14 bits
    uint64_t ctr; // 8 bits
};

std::array<RegFileStateEntry, 65> RegFileState = {};

static constexpr int LogMaxNewlyCounters = 16;

static constexpr int NHIST = 23;
static constexpr double HistRate = 1.19;
static constexpr int Born2 = 18;
static constexpr int MINHIST = 6;

using UINT64 = uint64_t;
#define SC

#define ASSERT_EQ(a, b)                                                                                            \
    if (a != b) {                                                                                                  \
        std::cerr << "assertion [" #a "(" << a << ") == " #b "(" << b << ")] failed at " << __LINE__ << std::endl; \
    }
#define ASSERT_LT(a, b)                                                                                           \
    if (a >= b) {                                                                                                 \
        std::cerr << "assertion [" #a "(" << a << ") < " #b "(" << b << ")] failed at " << __LINE__ << std::endl; \
    }

struct BiasArgs {
    int HitBank;
    bool HighConf;
    bool LowConf;
    bool LongestMatchPred;
    bool alttaken;
    bool pred_inter;
};

struct SimpleComponent {
    // Constants
    size_t log_size;
    size_t ctr_width;
    std::function<size_t(UINT64 pc_hash, uint64_t full_hist, BiasArgs args)> index_func;
    int8_t ctr_min;
    int8_t ctr_max;

    // Actual Data
    std::vector<int8_t> values;

    SimpleComponent() = default; // default constructible
    SimpleComponent(
        size_t log_size, size_t ctr_width, decltype(index_func) index_func, int8_t (*init_val_gen)(size_t idx) = [](size_t i) -> int8_t { return i % 2 == 0 ? -1 : 0; })
        : log_size(log_size)
        , ctr_width(ctr_width)
        , index_func(std::move(index_func))
        , ctr_min(-(1 << (ctr_width - 1)))
        , ctr_max(~ctr_min)
        , values(1 << log_size)
    {
        for (size_t i = 0; i < values.size(); ++i)
            values.at(i) = init_val_gen(i);
    }

    int get_value(UINT64 pc_hash, uint64_t full_hist, BiasArgs args) const
    {
        ASSERT_LT(index_func(pc_hash, full_hist, args), values.size());
        int8_t ctr = values.at(index_func(pc_hash, full_hist, args));
        return 2 * ctr + 1; // convert to the balanced representation
    }

    void update(bool taken /* resolveDir */, UINT64 pc_hash, uint64_t full_hist, BiasArgs args)
    {
        ASSERT_LT(index_func(pc_hash, full_hist, args), values.size());
        int8_t& ctr = values.at(index_func(pc_hash, full_hist, args));
        if (taken)
            if (ctr < ctr_max)
                ++ctr;
        if (not taken)
            if (ctr > ctr_min)
                --ctr;
    }

    size_t storage_size() const { return ctr_width * values.size(); }
};

namespace sBiasNormal {
    // Parameter definition
    static constexpr size_t Width = 7; // PERCWIDTH
    static constexpr size_t LogSize = 10; // LOGBIAS

    static size_t index(UINT64 PC, uint64_t, BiasArgs args)
    {
        const bool LowConf = args.LowConf;
        const bool LongestMatchPred = args.LongestMatchPred;
        const bool alttaken = args.alttaken;
        const bool pred_inter = args.pred_inter;

        const bool uncertain = LowConf & (LongestMatchPred != alttaken);
        const size_t index_hash = PC ^ (PC >> 2);
        const size_t total_hash = (index_hash << 2) | (uncertain << 1) | pred_inter;
        return total_hash % (1 << LogSize);
    }
    static int8_t init_value(size_t i)
    {
        switch (i % 4) {
        case 0:
            return -32; // certain untaken
        case 1:
            return 31; // certain taken
        case 2:
            return -1; // uncertain untaken
        case 3:
            return 0; // uncertain taken
        }
        throw nullptr; // gcc is so fool
    }
};

namespace sBiasSkew {
    // Parameter definition
    static constexpr size_t Width = 7; // PERCWIDTH
    static constexpr size_t LogSize = 10; // LOGBIAS

    static size_t index(UINT64 PC, uint64_t, BiasArgs args)
    {
        const bool HighConf = args.HighConf;
        const bool pred_inter = args.pred_inter;

        const size_t index_hash = PC ^ (PC >> (LogSize - 2));
        const size_t total_hash = (index_hash << 2) | (HighConf << 1) | pred_inter;
        return total_hash % (1 << LogSize);
    }
    static int8_t init_value(size_t i)
    {
        switch (i % 4) {
        case 0:
            return -8; // not high confidence untaken
        case 1:
            return 7; // not high confidence taken
        case 2:
            return -32; // high confidence untaken
        case 3:
            return 31; // high confidence taken
        }
        throw nullptr; // gcc is so fool
    }
};

namespace sBiasBank {
    // Parameter definition
    static constexpr size_t Width = 7; // PERCWIDTH
    static constexpr size_t LogSize = 10; // LOGBIAS

    static size_t index(UINT64 PC, uint64_t, BiasArgs args)
    {
        const int HitBank = args.HitBank;
        const bool HighConf = args.HighConf;
        const bool alttaken = args.alttaken;
        const int pred_inter = args.pred_inter;

        const size_t pc_hash = PC ^ (PC >> 2);
        const size_t hit_bank_hash = HitBank / 3; // 0 <= HitBank <= 23, thus 0 <= HitBank/3 <= 7
        const size_t total_hash = (pc_hash << 6) | (hit_bank_hash << 3) | (alttaken << 2) | (HighConf << 1) | pred_inter;
        return total_hash % (1 << LogSize);
    }
    static int8_t init_value(size_t i)
    {
        switch (i % 4) {
        case 0:
            return -32; // high confidence untaken
        case 1:
            return 31; // high confidence taken
        case 2:
            return -1; // not high confidence untaken
        case 3:
            return 0; // not high confidence taken
        }
        throw nullptr; // gcc is so fool
    }
};

namespace sBrIMLI {
    // Parameter definition
    static constexpr size_t Width = 6; // PERCWIDTH
    static constexpr size_t LogSize = 10; // LOG_BrIMLI

    static size_t index(UINT64 PC, uint64_t BrIMLI, BiasArgs)
    {
        const size_t pc_hash = (PC >> 2) ^ (PC >> 8);
        const size_t total_hash = pc_hash ^ BrIMLI;
        return total_hash % (1 << LogSize);
    }
}

namespace sTaIMLI {
    // Parameter definition
    static constexpr size_t Width = 6; // PERCWIDTH
    static constexpr size_t LogSize = 11; // LOG_TaIMLI

    static size_t index(UINT64 PC, uint64_t TaIMLI, BiasArgs)
    {
        const size_t pc_hash = (PC << 1) ^ (PC >> 6);
        const size_t total_hash = pc_hash ^ TaIMLI;
        return total_hash % (1 << LogSize);
    }
}

struct WeightGroup {
    // Constants
    static constexpr size_t Width = 6; // EWIDTH
    static constexpr size_t LogSize = 3; // LOGSIZEUPS = LOGSIZEUP/2
    static constexpr int8_t ctr_min = -(1 << (Width - 1));
    static constexpr int8_t ctr_max = ~ctr_min;

    // Actual Data
    std::array<int8_t, 1 << LogSize> weight_ctr;
    std::vector<SimpleComponent> components; // We can add components after call the constructor

    WeightGroup(int init_val)
    {
        std::fill(weight_ctr.begin(), weight_ctr.end(), init_val);
    }

    size_t index_for_weight(UINT64 PC) const
    {
        return (PC ^ (PC >> 2)) % weight_ctr.size();
    }

    int get_value(UINT64 pc_hash, uint64_t full_hist, BiasArgs args) const
    {
        int sum = 0;
        for (const auto& c : components)
            sum += c.get_value(pc_hash, full_hist, args);
        return sum;
    }

    int get_weighted_value(UINT64 pc_hash, uint64_t full_hist, BiasArgs args) const
    {
        int weight = weight_ctr[index_for_weight(pc_hash)] >= 0 ? 2 : 1; // This seems wrong because GGEHL uses both PC and PC<<1|pred_inter as weight index arguments
        return weight * get_value(pc_hash, full_hist, args);
    }

    void update(int LSUM, bool resolveDir, UINT64 pc_hash, uint64_t full_hist, BiasArgs args)
    {
        // Weight update
        const int sum = get_value(pc_hash, full_hist, args);
        const int weighted_sum = get_weighted_value(pc_hash, full_hist, args);

        const int LSUM_if_weight_is_1 = LSUM - weighted_sum + 1 * sum;
        const int LSUM_if_weight_is_2 = LSUM - weighted_sum + 2 * sum;
        const bool pred_if_weight_is_1 = LSUM_if_weight_is_1 >= 0;
        const bool pred_if_weight_is_2 = LSUM_if_weight_is_2 >= 0;

        if (pred_if_weight_is_1 != pred_if_weight_is_2) {
            const bool our_pred = sum >= 0;
            const bool useful = our_pred == resolveDir;
            int8_t& w = weight_ctr[index_for_weight(pc_hash)];
            if (useful)
                if (w < ctr_max)
                    ++w;
            if (not useful)
                if (w > ctr_min)
                    --w;
        }

        // updates each component
        for (auto& c : components)
            c.update(resolveDir, pc_hash, full_hist, args);
    }

    int get_extra_weight(UINT64 PC) const { return weight_ctr[index_for_weight(PC)] >= 0; }

    size_t storage_size() const
    {
        size_t total = 0;
        total += Width * weight_ctr.size();
        for (auto& c : components)
            total += c.storage_size();
        return total;
    }
};

struct wBias : WeightGroup {
    wBias()
        : WeightGroup(4)
    {
        WeightGroup::components.emplace_back(sBiasNormal::LogSize, sBiasNormal::Width, sBiasNormal::index, sBiasNormal::init_value);
        WeightGroup::components.emplace_back(sBiasSkew::LogSize, sBiasSkew::Width, sBiasSkew::index, sBiasSkew::init_value);
        WeightGroup::components.emplace_back(sBiasBank::LogSize, sBiasBank::Width, sBiasBank::index, sBiasBank::init_value);
    }
} bias_components;

struct wGEHL : WeightGroup {
    size_t MaxLength;
    wGEHL(std::vector<size_t> Lengths, size_t LogSize, size_t Width)
        : MaxLength(Lengths.at(0))
        , WeightGroup(7)
    {
        for (size_t i = 0; i < Lengths.size(); ++i) {
            const size_t log_size = LogSize - (i >= Lengths.size() - 2); // last two components are half size
            const auto index_func = [i, log_size, length = Lengths.at(i)](UINT64 pc_hash, uint64_t full_hist, BiasArgs) { return hash_func_template(pc_hash, full_hist, length, i) % (1 << log_size); };
            WeightGroup::components.emplace_back(log_size, Width, index_func);
        }
    }

    static size_t hash_func_template(UINT64 pc_hash, uint64_t BHIST, int length, int i)
    {
        const uint64_t bhist = BHIST & ((1ull << length) - 1);
        size_t hash = pc_hash;
        hash ^= bhist;
        hash ^= bhist >> (8 - i);
        hash ^= bhist >> (16 - 2 * i);
        hash ^= bhist >> (24 - 3 * i);
        hash ^= bhist >> (32 - 3 * i); // Trick in the TAGE-SC-L (2016)
        hash ^= bhist >> (40 - 4 * i);
        return hash;
    }
};

wGEHL global_GEHL_components {
    { 40, 24, 10 }, // Gm
    11, // LOGGNB
    6, // PERCWIDTH
};

wGEHL path_GEHL_components {
    { 16, 9 }, // Pm
    10, // LOGPNB
    6, // PRECWIDTH
};

wGEHL local1_GEHL_components {
    { 18, 11, 6, 3 }, // Lm
    11, // LOGLNB
    6, // PERCWIDTH
};

wGEHL local2_GEHL_components {
    { 21, 16, 11, 6 }, // Sm
    11, // LOGSNB
    6, // PERCWIDTH
};

wGEHL local3_GEHL_components {
    { 19, 14, 9, 4 }, // Tm
    11, // LOGTNB
    6, // PERCWIDTH
};

wGEHL call_stack_GEHL_components {
    { 47, 31, 18, 10, 5 }, // Cm
    11, // LOGCNB
    6, // PERCWIDTH
};

struct IMLI_WeightGroup {
    // Constants
    static constexpr size_t Width = 6; // EWIDTH
    static constexpr size_t LogSize = 8; // LOGSIZEUPS = LOGSIZEUP/2 = 3 is not sufficient
    static constexpr int8_t ctr_min = -(1 << (Width - 1));
    static constexpr int8_t ctr_max = ~ctr_min;

    // Actual Data
    std::array<int8_t, 1 << LogSize> weight_ctr;
    std::vector<SimpleComponent> components;

    IMLI_WeightGroup()
    {
        std::fill(weight_ctr.begin(), weight_ctr.end(), 0);
        components.emplace_back(sBrIMLI::LogSize, sBrIMLI::Width, sBrIMLI::index);
        components.emplace_back(sTaIMLI::LogSize, sTaIMLI::Width, sTaIMLI::index);
    }

    size_t index_for_weight(UINT64 PC) const
    {
        return (PC ^ (PC >> 2)) % weight_ctr.size();
    }

    int get_value(UINT64 PC, uint64_t BrIMLI, uint64_t TaIMLI) const
    {
        int sum = 0;
        sum += components.at(0).get_value(PC, BrIMLI, {});
        sum += components.at(1).get_value(PC, TaIMLI, {});
        return sum;
    }

    int get_weighted_value(uint64_t PC, uint64_t BrIMLI, uint64_t TaIMLI) const
    {
        int weight = 1 + get_extra_weight(PC);
        return weight * get_value(PC, BrIMLI, TaIMLI);
    }

    void update(int LSUM, bool resolveDir, uint64_t PC, uint64_t BrIMLI, uint64_t TaIMLI)
    {
        // Weight update
        const int sum = get_value(PC, BrIMLI, TaIMLI);
        const int weighted_sum = get_weighted_value(PC, BrIMLI, TaIMLI);

        const int LSUM_if_weight_is_1 = LSUM - weighted_sum + 1 * sum;
        const int LSUM_if_weight_is_3 = LSUM - weighted_sum + 3 * sum; // IMLIs sometimes provide highly correlated information, thus weight is not 2x but 3x.
        const bool pred_if_weight_is_1 = LSUM_if_weight_is_1 >= 0;
        const bool pred_if_weight_is_3 = LSUM_if_weight_is_3 >= 0;

        if (pred_if_weight_is_1 != pred_if_weight_is_3) {
            const bool our_pred = sum >= 0;
            const bool useful = our_pred == resolveDir;
            int8_t& w = weight_ctr[index_for_weight(PC)];
            if (useful)
                if (w < ctr_max)
                    ++w;
            if (not useful)
                if (w > ctr_min)
                    --w;
        }

        // updates each component
        for (int i = 0; i < 1 + get_extra_weight(PC); ++i) {
            components.at(0).update(resolveDir, PC, BrIMLI, {});
            components.at(1).update(resolveDir, PC, TaIMLI, {});
        }
    }

    int get_extra_weight(UINT64 PC) const { return 2 * (weight_ctr[index_for_weight(PC)] >= 0); }

    size_t storage_size() const
    {
        size_t total = 0;
        total += Width * weight_ctr.size();
        for (auto& c : components)
            total += c.storage_size();
        return total;
    }
} IMLI_components;

namespace sLocal1 {
    static constexpr size_t FeatureSize = 256;
    size_t get_index(uint64_t PC) { return (PC ^ (PC >> 2)) % FeatureSize; }
}

namespace sLocal2 {
    static constexpr size_t FeatureSize = 16;
    size_t get_index(uint64_t PC) { return (PC ^ (PC >> 5)) % FeatureSize; }
}

namespace sLocal3 {
    static constexpr size_t FeatureSize = 16;
    size_t get_index(uint64_t PC) { return (PC ^ (PC >> 11)) % FeatureSize; } // 11 is LOGTNB
}

namespace sCallStack {
    static constexpr size_t FeatureSize = 8;
}

// playing with putting more weights (x2)  on some of the SC components
// playing on using different update thresholds on SC
// update threshold for the statistical corrector
#define LOGSIZEUP 6 // not worth increasing
#define WIDTHRES 12
#define WIDTHRESP 8
int updatethreshold;
int Pupdatethreshold[(1 << LOGSIZEUP)]; // size is fixed by LOGSIZEUP
#define INDUPD (PC ^ (PC >> 2)) & ((1 << LOGSIZEUP) - 1)

// The two counters used to choose between TAGE and SC on Low Conf SC
int8_t FirstH, SecondH;

#define CONFWIDTH 7 // for the counters in the choser
#define HISTBUFFERLENGTH 8192 // we use a 8K entries history buffer to store the branch history (this allows us to explore using history length up to 8K)

// utility class for index computation
// this is the cyclic shift register for folding
// a long global history into a smaller number of bits; see P. Michaud's PPM-like predictor at CBP-1



