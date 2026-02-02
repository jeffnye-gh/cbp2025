#pragma once
#include <cstdint>
#include <array>
#include <limits>
#include <vector>
#include <string>
#include <algorithm> // For std::find and std::sort
#include <random>    // For std::mt19937
#include <cmath>
#include <deque>
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>

// LUTS from GEM5 implementation
static constexpr int16_t XLAT [32] =
    { 1, 3, 4, 5, 7, 8, 9,11,12,14,15,17,19,21,23,25,
     27,29,32,34,37,41,45,49,53,58,63,69,76,85,94,106 };
static constexpr int16_t XLAT4[16] =
    { 0, 4, 5, 7, 9,11,12,14,16,17,19,22,28,33,39,45 };

static constexpr size_t INDEX_MAX_BITS = 13;

static constexpr size_t PC_HASH(size_t PC) {
    return PC ^ (PC << 5 ) ^ (PC >> 12) ^ (PC >> 19) ^ (PC >> 44);
}

template <size_t SIZE, size_t BITS>
class WeightTable {
    private:
        std::array<uint8_t, SIZE> magnitudes;
        static constexpr std::size_t N_SIGNS = 2;
        std::array<std::array<bool, N_SIGNS>, SIZE> signs;
        static constexpr uint8_t MAX_WEIGHT = (1 << (BITS)) - 1;

    size_t getUnsignedIndex(size_t idx) const {
        if (N_SIGNS == 1 ){
            return idx % SIZE;
        } else {
            assert (N_SIGNS == 2);
            return (idx>>1) % SIZE;
        }
    }

    public:
        // Constructor - initialize all weights to 0
        WeightTable() {
            magnitudes.fill(0);
            for (auto& row : signs) {
                row.fill(false);
            }
        }

        uint8_t getMagnitude(size_t idx) const {
            uint8_t mag = magnitudes[getUnsignedIndex(idx)];
            return mag;    
        }

        int16_t getTransformedWeight(size_t idx) const {
            size_t index = getUnsignedIndex(idx);
            uint8_t mag = magnitudes[index];
            bool  sign = signs[index][idx % N_SIGNS];
            int16_t base = (BITS == 5) ? XLAT[mag] : XLAT4[mag];
            
            return sign ? -base : base;    
        }

        void decreaseMagnitude(size_t idx) {
            size_t index = getUnsignedIndex(idx);
            if (magnitudes[index] > 1){
                magnitudes[index]--;
            } 
        }

        void updateWeight(size_t idx, bool taken) {
            size_t index = getUnsignedIndex(idx);
            bool&  sign = signs[index][idx % N_SIGNS];
            uint8_t& mag = magnitudes[index];
            
            assert(mag<=MAX_WEIGHT);
            if (taken) {
                // Increment weight (in sign/magnitude)
                if (sign) { // Negative
                    if (mag == 0) {
                        sign = false; // -0 becomes +0
                    } else {
                        mag--; // Move toward zero
                    }
                } else { // Positive
                    if (mag < MAX_WEIGHT) {
                        mag++; // Move away from zero
                    }
                }
            } else {
                // Decrement weight (in sign/magnitude)
                if (sign) { // Negative
                    if (mag < MAX_WEIGHT) {
                        mag++; // Move away from zero (more negative)
                    }
                } else { // Positive
                    if (mag == 0) {
                        sign = true; // +0 becomes -0
                    } else {
                        mag--; // Move toward zero
                    }
                }
            }    
        }

        // BITS is the number of magnitude bits per entry
        // N_SIGNS is the number of sign-bits per entry
        // SIZE is the size of the table
        static constexpr size_t getSizeInBits() {
            return SIZE * (BITS+N_SIGNS);
        }
    };


template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class Feature {
protected:
    std::string name;
    uint32_t mispredictions = 0;
    WeightTable<TABLE_SIZE, BIT_WIDTH> weights;
    double coefficient = 1.0; 

    public:
        Feature(const std::string& featureName) : name(featureName) {}
        virtual ~Feature() = default;
        
        virtual void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) = 0;

        virtual size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const = 0;

        // For older results
        virtual int getWeightedPredictionAtIndex(size_t index) const {
            return static_cast<int>(coefficient * weights.getTransformedWeight(index));
        }

        void setCoefficient(double coef) { coefficient = coef; }

        uint32_t getMispredictions() const { return mispredictions; }

        bool updateAccuracy(bool prediction, bool actual) {
            if (prediction != actual){
                mispredictions++;
            }

            return (mispredictions == (1U<<31)-2);
        }

        void shrinkAccCounts() { // Could be done with more care but I'm writing this past midnight
            mispredictions = mispredictions >> 10;
        }
    
        virtual void updateWeight(size_t index, bool taken) {
            weights.updateWeight(index, taken);
        }
    
        virtual void decreaseMagnitude(size_t index) {
            weights.decreaseMagnitude(index);
        }

        const std::string& getName() const {
            return name;
        }

        // Meant to track data other than the weight table
        virtual size_t getTrackedDataSizeInBits() const = 0;

        size_t getSizeInBits() const {
            size_t total_size = 0;
            total_size += weights.getSizeInBits(); // Track the size of the weight table
            total_size += getTrackedDataSizeInBits(); // Each feature will track other things, like histories
            total_size += sizeof(mispredictions)*8; // Track the prediction counters
            return total_size;
        }
    };

//########################################################
//##              Global history feature                ##
//########################################################
// Can't live without it

template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class GhistFeature : public Feature<TABLE_SIZE, BIT_WIDTH> {
    private:
        size_t startIdx;
        size_t endIdx;
    public:
        GhistFeature(size_t start, size_t end)
            : Feature<TABLE_SIZE, BIT_WIDTH>("GHIST_" + std::to_string(start) + "_" + std::to_string(end)),
            startIdx(start), endIdx(end) {}

        void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
            // No internal history
        }

        size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
            assert(endIdx < ghist.size());
            size_t idx = 0;
            size_t blocksize = 4;
            for (size_t i = startIdx; i < endIdx; i += blocksize) {
                size_t block = 0;
                for (size_t j = 0; j < blocksize && (i+j) < endIdx; ++j) {
                    block |= (ghist[i+j] << j);
                }
                idx ^= block << (i % INDEX_MAX_BITS); // Mix blocks
                idx ^= idx >> INDEX_MAX_BITS;
            }
            return idx ^ PC_HASH(branchPC);
        }

        // This only uses the centrally tracked global history
        size_t getTrackedDataSizeInBits() const {
            return 0;
        }
    };    
    
//########################################################
//##               Path history feature                 ##
//########################################################
template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class PathFeature : public Feature<TABLE_SIZE, BIT_WIDTH> {
    private:
        size_t shift;
        size_t hist_depth;
        bool add_style;

        // add_style
        // Very unclear from the paper what this is actually supposed to do
        // If true: Shift the accumulator and add in the next address (I assume cyclical?)
        // If false: xor fold
        
    public:
        PathFeature(size_t historyLength, size_t shift, bool add_style)
            : Feature<TABLE_SIZE, BIT_WIDTH>("PATH_" + std::to_string(historyLength) + "_" + std::to_string(shift)), shift(shift), add_style(add_style), hist_depth(historyLength) {}
        
        void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
            // No internal history
        }
        
        size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
            assert(hist_depth < gpath.size());
            size_t accumulator = 0;

            if (add_style){
                for (size_t i = 0; i < hist_depth; i++) {
                    accumulator = accumulator << shift;
                    size_t upper_bits = (accumulator) >> INDEX_MAX_BITS;
                    accumulator ^= upper_bits;
                    accumulator += gpath[i];
                }
            } else {
                for (size_t i = 0; i < hist_depth; i++) {
                    // This one I didn't know how to do, I defaulted to use just 5 bits similar to a hashing method I saw in PHAST

                    size_t hashed_path_addr = ((gpath[i]) ^ (gpath[i] >> 5) ^ (gpath[i] >> 12)) % (1 << 5);
                    accumulator ^= hashed_path_addr << ((i*5)%(INDEX_MAX_BITS));
                    size_t upper_bits = (accumulator) >> INDEX_MAX_BITS;
                    accumulator = accumulator ^ upper_bits;
                }
            }

            return (accumulator ^ PC_HASH(branchPC));
        }

        // Similar to global history, we only use the shared history here
        size_t getTrackedDataSizeInBits() const {
            return 0;
        }
    };

//########################################################
//##            GHISTPATH feature implementation        ##
//########################################################
template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class GhistPathFeature : public Feature<TABLE_SIZE, BIT_WIDTH> {
    private:
    GhistFeature<TABLE_SIZE, BIT_WIDTH> ghistComponent;
    PathFeature<TABLE_SIZE, BIT_WIDTH> pathComponent;
public:
    GhistPathFeature(size_t historyLength, size_t shift, bool add_style) : 
    Feature<TABLE_SIZE, BIT_WIDTH>("GHISTPATH_" + std::to_string(historyLength) + "_" + std::to_string(shift)),
    ghistComponent(0, historyLength),
    pathComponent(historyLength, shift, add_style) {}
    
    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
        ghistComponent.updateWithBranchResult(branchPC, taken, isForward);
        pathComponent.updateWithBranchResult(branchPC, taken, isForward);
    }
    
    size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        size_t ghistIndex = ghistComponent.toIndex(branchPC, ghist, gpath, thist);
        size_t pathIndex = pathComponent.toIndex(branchPC, ghist, gpath, thist);
        return ghistIndex ^ pathIndex ^ PC_HASH(branchPC); // For XOR, 3 times the charm ( or any odd number really )
    }

    // Similar GHIST / PATH
    size_t getTrackedDataSizeInBits() const {
        return 0;
    }
};

//########################################################
//##           BlurryPath feature implementation        ##
//########################################################
 template <size_t TABLE_SIZE, size_t BIT_WIDTH>
 class BlurryPathFeature : public Feature<TABLE_SIZE,BIT_WIDTH>
 {
     std::vector<uint16_t> regions;
     uint32_t              shiftAmt;   // region granularity (bits right‑shift)
     uint32_t              stride;     // how much to shift each region when folding
 public:
     BlurryPathFeature(uint32_t histLen,uint32_t sh,uint32_t str)
     : Feature<TABLE_SIZE,BIT_WIDTH>("BLURRY_"+std::to_string(sh)),
       regions(histLen,0), shiftAmt(sh), stride(str) {}
 
     void updateWithBranchResult(uint64_t pc, bool taken, bool isForward) override {
         uint16_t reg = static_cast<uint16_t>(PC_HASH(pc>>shiftAmt));
         if (regions.front()!=reg) {            // only when new region entered
             regions.insert(regions.begin(),reg);
             regions.pop_back();
         }
     }
     size_t toIndex(uint64_t pc, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
         size_t acc=0;
         for (uint32_t i=0;i<regions.size();++i){
             acc ^= regions[i] << ((i*stride) % INDEX_MAX_BITS);
             acc ^= (acc >> INDEX_MAX_BITS); // Fix the upper bits
            }
         return PC_HASH(pc) ^ acc;
     }

     // This register is unique to this feature, so we account for it
     // We track 16 bits per address
     size_t getTrackedDataSizeInBits() const {
         return regions.size() * 16;
     }
 };

 /****************************************************************
 * AcyclicHistoryFeature – keeps last outcome (or addr) per PC mod N
 ****************************************************************/
 // Currently I'm only implementing the direction version, not address version
template <size_t TABLE_SIZE, size_t BIT_WIDTH, bool STORE_ADDR=false>
class AcyclicHistoryFeature : public Feature<TABLE_SIZE,BIT_WIDTH>
{
    uint64_t hist;   // size N, stores outcome or addr hash
    uint32_t N;
public:
    AcyclicHistoryFeature(uint32_t n)
    : Feature<TABLE_SIZE,BIT_WIDTH>(STORE_ADDR?"ACYCLIC_ADDR":"ACYCLIC_OUT"), hist(0), N(n) {}

    void updateWithBranchResult(uint64_t pc, bool taken, bool isForward) override {
        size_t idx = PC_HASH(pc) % N;

        size_t mask = ~(1 << idx);
        hist &= mask;

        mask = (taken << idx);
        hist |= mask;
    }
    size_t toIndex(uint64_t pc, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        return PC_HASH(pc) ^ hist ^ (hist >> 25);
    }

    // Feature also unique, hence we track it
    size_t getTrackedDataSizeInBits() const {
        return N;
    }
};
    
//########################################################
//##             IMLI feature implementation            ##
//########################################################
class IMLICounter {
    private:
        uint16_t counter = 0;
        bool isForwardMode;
        
    public:
        IMLICounter(bool forwardMode) : isForwardMode(forwardMode) {}
        
        void update(bool branchIsForward, bool branchIsTaken) {
            if (branchIsForward == isForwardMode) {
                if (branchIsTaken != isForwardMode) {
                    counter++;
                } else {
                    counter = 0;
                }
            }
        }
        
        uint16_t getCounter() const { return counter; }
    };    

template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class IMLIFeature : public Feature<TABLE_SIZE, BIT_WIDTH> {
private:
    IMLICounter imli;
    
public:
    IMLIFeature(bool forward): Feature<TABLE_SIZE, BIT_WIDTH>(forward ? "IMLI_forward" : "IMLI_backward"), imli(forward) {}

    
    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
        imli.update(isForward, taken);
    }
    
    size_t toIndex(uint64_t pc, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        return PC_HASH(pc) ^ imli.getCounter();
    }

    size_t getTrackedDataSizeInBits() const {
        return 16; // IMLI counter uses only 16 bits
    }
};

//########################################################
//##            MOD HIST feature implementation         ##
//########################################################
template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class ModHistFeature : public Feature<TABLE_SIZE,BIT_WIDTH>
{
    std::vector<bool> history;        // filtered global outcomes
    uint32_t          modulus;        // small integer ≥2
public:
    ModHistFeature(size_t len, uint32_t mod)
    : Feature<TABLE_SIZE,BIT_WIDTH>("MODHIST_"+std::to_string(mod)),
      history(len,false), modulus(mod) {}

    void updateWithBranchResult(uint64_t pc, bool taken, bool isForward) override {
        if (PC_HASH(pc) % modulus == 0) {
            history.insert(history.begin(), taken);
            history.pop_back();
        }
    }
    size_t toIndex(uint64_t pc, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        size_t idx = 0;
        size_t blockSize = 8; // Match original's block size
        for (size_t i = 0; i < history.size(); i += blockSize) {
            size_t block = 0;
            for (size_t j = 0; j < blockSize && (i+j) < history.size(); ++j) {
                block |= (history[i+j] << j);
            }
            idx ^= block << (i % INDEX_MAX_BITS); // Mix blocks
            idx ^= idx >> INDEX_MAX_BITS;
        }
        return PC_HASH(pc) ^ idx;
    }
    
    // This is also a unique feature, hence we track it
    size_t getTrackedDataSizeInBits() const {
        return history.size();
    }
};

//########################################################
//##            MOD PATH feature implementation         ##
//########################################################
template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class ModPathFeature : public Feature<TABLE_SIZE,BIT_WIDTH>
{
    std::vector<uint16_t> history;   // filtered addresses
    uint32_t              modulus;
public:
    ModPathFeature(size_t len,uint32_t mod)
    : Feature<TABLE_SIZE,BIT_WIDTH>("MODPATH_"+std::to_string(mod)),
      history(len,0), modulus(mod) {}

    void updateWithBranchResult(uint64_t pc, bool taken,bool isForward) override {
        if (PC_HASH(pc) % modulus == 0) {
            history.insert(history.begin(), static_cast<uint16_t>(pc & 0xFFFF));
            history.pop_back();
        }
    }
    size_t toIndex(uint64_t pc, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        size_t acc = 0;
        for (uint16_t a: history) acc = ((acc<<2)|(acc>>62)) + a; // rotate+add
        return PC_HASH(pc) ^ (acc ^ (acc >> 20));
    }

    // Similar store, we need to track this individually since the modulo will
    // ... Make it diverge from the global path register
    size_t getTrackedDataSizeInBits() const {
        return history.size()*16;
    }
};


//########################################################
//##          GHISTMODPATH feature implementation       ##
//########################################################
template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class GhistModPathFeature : public Feature<TABLE_SIZE, BIT_WIDTH> {
private:
    ModHistFeature<TABLE_SIZE, BIT_WIDTH> modHistComponent;
    ModPathFeature<TABLE_SIZE, BIT_WIDTH> modPathComponent;
public:
    GhistModPathFeature(size_t histlen, uint32_t mod)
        : Feature<TABLE_SIZE, BIT_WIDTH>("GHISTMODPATH_" + std::to_string(mod)),
          modHistComponent(histlen, mod),
          modPathComponent(histlen, mod) {
    }

    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
        modHistComponent.updateWithBranchResult(branchPC, taken, isForward);
        modPathComponent.updateWithBranchResult(branchPC, taken, isForward);
    }

    size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        // Get indices from both component features
        size_t histIndex = modHistComponent.toIndex(branchPC, ghist, gpath, thist);
        size_t pathIndex = modPathComponent.toIndex(branchPC, ghist, gpath, thist);
        
        // Combine the indices
        return histIndex ^ pathIndex ^ PC_HASH(branchPC);
    }

    size_t getTrackedDataSizeInBits() const {
        return modHistComponent.getTrackedDataSizeInBits()+modPathComponent.getTrackedDataSizeInBits();
    }
};

//########################################################
//##              RECENCY feature implementation        ##
//########################################################
// Recency stack thingy
class RecencyStackBase {
    protected:
        // LRU stack of recently seen branch addresses
        std::vector<uint64_t> recencyStack;
        size_t stackSize;
        
        // Update the recency stack using LRU replacement
        void updateStack(uint64_t branchPC) {
            // Check if branch is already in the stack
            auto it = std::find(recencyStack.begin(), recencyStack.end(), branchPC);
            
            if (it != recencyStack.end()) {
                // Branch is in stack - remove it from current position
                recencyStack.erase(it);
            } else if (recencyStack.size() == stackSize) {
                // Stack is full and branch not present - remove least recently used
                recencyStack.pop_back();
            }
            
            // Insert branch at the front (most recently used position)
            recencyStack.insert(recencyStack.begin(), branchPC);
        }
        
        // Find position of a branch in the stack (returns -1 if not found)
        int findPositionInStack(uint64_t branchPC) const {
            for (size_t i = 0; i < recencyStack.size(); i++) {
                if (recencyStack[i] == branchPC) {
                    return static_cast<int>(i);
                }
            }
            return -1; // Not found
        }
        
    public:
        // Constructor with stack size
        RecencyStackBase(size_t stackSize) : stackSize(stackSize){
            recencyStack.assign(stackSize, 0);
        }
    };

template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class RecencyFeature : public Feature<TABLE_SIZE, BIT_WIDTH>, public RecencyStackBase {
private:
    size_t shift;   // How much to shift the accumulator after each hash
    bool add_style; // Controls how addresses are mixed into the hash
    
public:
    RecencyFeature(size_t stackSize, size_t shift, bool add_style)
        : Feature<TABLE_SIZE, BIT_WIDTH>("RECENCY_" + std::to_string(stackSize) + "_" + std::to_string(shift)),
          RecencyStackBase(stackSize), shift(shift), add_style(add_style) {
    }
    
    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
        // Update the LRU stack with this branch
        updateStack(branchPC);
    }
    
    size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        size_t index = branchPC;
        size_t accumulator = 0;
        
        // Hash the addresses in the recency stack up to depth
        const size_t stackDepth = recencyStack.size();
        
        for (size_t i = 0; i < stackDepth; i++) {
            if (add_style) {
                // Shift and add style
                accumulator = (accumulator << shift) + (recencyStack[i] & 0xFFFF);
            } else {
                // XOR specific bits (similar to PATH feature)
                uint16_t addr = static_cast<uint16_t>(recencyStack[i] & 0xFFFF);
                accumulator ^= ((addr & (1 << shift)) != 0) ? (1 << (i % 16)) : 0;
            }
        }
        
        return index ^ accumulator;
    }

    // Full PC tracked in recency stack
    size_t getTrackedDataSizeInBits() const {
        return 64 * recencyStack.size(); // We track 64 bits per entry in the stack
    }
};

//########################################################
//##           RECENCYPOS feature implementation        ##
//########################################################
// This thing does terribly, not sure if I've done it wrong
template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class RecencyPosFeature : public Feature<TABLE_SIZE, BIT_WIDTH>, public RecencyStackBase {
private:
    size_t maxDepth; // Maximum depth to search in the stack
    
public:
    RecencyPosFeature(size_t stackSize, size_t maxDepth)
        : Feature<TABLE_SIZE, BIT_WIDTH>("RECENCYPOS_" + std::to_string(maxDepth)),
          RecencyStackBase(stackSize),
          maxDepth(maxDepth) {
    }
    
    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
        // Update the LRU stack with this branch
        updateStack(branchPC);
    }
    
    size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        // Find position of the branch in the recency stack
        int position = findPositionInStack(branchPC);
        
        // If branch is found within maxDepth, use its position as part of the index
        // Otherwise, use a default value (maxDepth)
        size_t posValue;
        if (position >= 0 && static_cast<size_t>(position) < maxDepth) {
            posValue = static_cast<size_t>(position);
        } else {
            posValue = maxDepth;
        }
        
        // XOR the branch address with the position value shifted
        // This creates an index that reflects both the branch and its recency
        return branchPC ^ (posValue << 2);
    }

    // Same state as earlier
    // Technically could be inferred from others, but accounted for here to be safe
    size_t getTrackedDataSizeInBits() const {
        return 64 * maxDepth;
    }
};
    
//########################################################
//##              LOCAL feature implementation          ##
//########################################################
template <size_t TABLE_SIZE, size_t BIT_WIDTH, size_t LOCAL_HISTORY_TABLE_SIZE, size_t HISTORY_LENGTH>
class LocalFeature : public Feature<TABLE_SIZE, BIT_WIDTH> {
private:
    // First-level table: Maps branch addresses to history registers
    std::vector<std::vector<bool>> localHistoryTable;
    
public:
    LocalFeature()
        : Feature<TABLE_SIZE, BIT_WIDTH>("LOCAL_"+std::to_string(LOCAL_HISTORY_TABLE_SIZE)+"_"+std::to_string(HISTORY_LENGTH)) {
        // Initialize the local history table
        localHistoryTable.resize(LOCAL_HISTORY_TABLE_SIZE);
        for (auto& history : localHistoryTable) {
            history.assign(HISTORY_LENGTH, false);
        }
    }
    
    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
        // Get index into first-level table
        size_t tableIndex = getBranchIndex(branchPC);
        
        // Update the local history for this branch
        localHistoryTable[tableIndex].insert(localHistoryTable[tableIndex].begin(), taken);
        localHistoryTable[tableIndex].pop_back();
    }
    
    size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        // First level: Get branch-specific history from the table
        size_t tableIndex = getBranchIndex(branchPC) % localHistoryTable.size();
        const auto& localHistory = localHistoryTable[tableIndex];
        
        // Second level: Hash the local history to get an index
        size_t index = 0;
        
        // Fold the local history into the index
        for (size_t i = 0; i < localHistory.size(); i++) {
            index = (index) ^ ((localHistory[i] ? 1 : 0) << (i % INDEX_MAX_BITS+1) );
        }
        
        return index ^ (index >> 10) ^ PC_HASH(branchPC);
    }

    // Local history is tracked using its own array of history tables
    // There's <LOCAL_HISTORY_TABLE_SIZE> number of entries, each of length <HISTORY_LENGTH>
    size_t getTrackedDataSizeInBits() const {
        return LOCAL_HISTORY_TABLE_SIZE * HISTORY_LENGTH;
    }
    
private:
    size_t getBranchIndex(uint64_t branchPC) const {
        // Vibe folding
        return ((branchPC ^ (branchPC >> 10) ^ 
            (branchPC >> 20)) % LOCAL_HISTORY_TABLE_SIZE);

    }
};
    
//########################################################
//##           LOCALMOD feature implementation          ##
//########################################################
template <size_t TABLE_SIZE, size_t BIT_WIDTH, size_t LOCAL_HISTORY_TABLE_SIZE, size_t HISTORY_LENGTH>
class LocalModFeature : public Feature<TABLE_SIZE, BIT_WIDTH> {
private:
    // First-level table: Maps branch addresses to history registers
    std::vector<std::vector<bool>> localHistoryTable;
    uint32_t modulus;
    
public:
    LocalModFeature(uint32_t modulus)
        : Feature<TABLE_SIZE, BIT_WIDTH>("LOCAL_MOD_"+std::to_string(LOCAL_HISTORY_TABLE_SIZE)+"_"+std::to_string(HISTORY_LENGTH)+"_"+std::to_string(modulus)),
        modulus(modulus) {
        // Initialize the local history table
        localHistoryTable.resize(LOCAL_HISTORY_TABLE_SIZE);
        for (auto& history : localHistoryTable) {
            history.assign(HISTORY_LENGTH, false);
        }
    }
    
    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
        // Get index into first-level table
        size_t tableIndex = PC_HASH(branchPC);
        tableIndex = tableIndex >> 5;
        tableIndex = tableIndex % localHistoryTable.size();

        size_t modFactor = tableIndex & ((1<<5)-1);

        // Update the local history for this branch
        if (modFactor%modulus==0){
            localHistoryTable[tableIndex].insert(localHistoryTable[tableIndex].begin(), taken);
            localHistoryTable[tableIndex].pop_back();
        }
    }
    
    size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        // First level: Get branch-specific history from the table
        size_t tableIndex = PC_HASH(branchPC);
        tableIndex = tableIndex >> 5;
        tableIndex = tableIndex % localHistoryTable.size();
        
        const auto& localHistory = localHistoryTable[tableIndex];
        
        // Second level: Hash the local history to get an index
        size_t index = 0;
        
        // Fold the local history into the index
        for (size_t i = 0; i < localHistory.size(); i++) {
            index = (index) ^ ((localHistory[i] ? 1 : 0) << (i % INDEX_MAX_BITS) );
        }
        
        return index ^ (index >> 10) ^ PC_HASH(branchPC);
    }

    // Similar to local history
    size_t getTrackedDataSizeInBits() const {
        return LOCAL_HISTORY_TABLE_SIZE * HISTORY_LENGTH;
    }
    
private:
    size_t getBranchIndex(uint64_t branchPC) const {
        // Vibe folding
        return ((branchPC ^ (branchPC >> 10) ^ 
            (branchPC >> 20)) % LOCAL_HISTORY_TABLE_SIZE);

    }
};

//########################################################
//##                TAGE history feature                ##
//########################################################
// Can't live without it
template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class ThistFeature : public Feature<TABLE_SIZE, BIT_WIDTH> {
private:
    std::vector<bool> globalHistory;
    size_t startIdx;
    size_t endIdx;
public:
    ThistFeature(size_t start, size_t end)
        : Feature<TABLE_SIZE, BIT_WIDTH>("THIST_" + std::to_string(start) + "_" + std::to_string(end)),
        startIdx(start), endIdx(end) {}

    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
        // No internal history
    }

    size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        assert(endIdx < thist.size());
        size_t idx = 0;
        size_t blocksize = 4;
        for (size_t i = startIdx; i < endIdx; i += blocksize) {
            size_t block = 0;
            for (size_t j = 0; j < blocksize && (i+j) < endIdx; ++j) {
                block |= (thist[i+j] << j);
            }
            idx ^= block << (i % INDEX_MAX_BITS); // Mix blocks
            idx ^= idx >> INDEX_MAX_BITS;
        }
        return idx ^ PC_HASH(branchPC);
    }

    // This only uses the centrally tracked global history
    size_t getTrackedDataSizeInBits() const {
        return 0;
    }
};    

//########################################################
//##            BIAS feature implementation             ##
//########################################################
template <size_t TABLE_SIZE, size_t BIT_WIDTH>
class BiasFeature : public Feature<TABLE_SIZE, BIT_WIDTH> {
public:
    BiasFeature()
        : Feature<TABLE_SIZE, BIT_WIDTH>("BIAS") {
    }
    
    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override {
        // BIAS feature doesn't track history - nothing to update
    }
    
    size_t toIndex(uint64_t branchPC, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override {
        return PC_HASH(branchPC);
    }
    
    size_t getTrackedDataSizeInBits() const {
        return 0;
    }
};

//########################################################
//##            A sacrifice to the template gods        ##
//########################################################
class FeatureBase {
    public:
        virtual ~FeatureBase() = default;
        virtual void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) = 0;
        virtual int getWeightedPredictionAtIndex(size_t index) const = 0;
        virtual void updateWeight(size_t index, bool taken) = 0;
        virtual void decreaseMagnitude(size_t index) = 0;
        virtual bool updateAccuracy(bool prediction, bool actual) = 0;
        virtual void shrinkAccCounts() = 0;
        virtual uint32_t getMispredictions() const = 0;
        virtual size_t toIndex(uint64_t pc, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const = 0;
        virtual std::string getName() const = 0;
        virtual void setCoefficient(double c) = 0;
        virtual size_t getSizeInBits() const = 0;
    };

template <typename FeatureType>
class FeatureWrapper : public FeatureBase {
private:
    FeatureType feature;
    
public:
    template <typename... Args>
    FeatureWrapper(Args&&... args) : feature(std::forward<Args>(args)...) {}
    void updateWithBranchResult(uint64_t branchPC, bool taken, bool isForward) override { feature.updateWithBranchResult(branchPC, taken, isForward); }
    int getWeightedPredictionAtIndex(size_t index) const override { return feature.getWeightedPredictionAtIndex(index); }
    void updateWeight(size_t index, bool taken) override { feature.updateWeight(index, taken); }
    void decreaseMagnitude(size_t idx) override { feature.decreaseMagnitude(idx); }
    bool updateAccuracy(bool prediction, bool actual) override { return feature.updateAccuracy(prediction, actual); }
    void shrinkAccCounts() override { feature.shrinkAccCounts(); }
    uint32_t getMispredictions() const override { return feature.getMispredictions(); }
    std::string getName() const override { return feature.getName(); }
    size_t toIndex(uint64_t pc, const std::vector<bool>& ghist, const std::vector<uint16_t>& gpath, const std::vector<bool>& thist) const override { return feature.toIndex(pc, ghist, gpath, thist); }
    void setCoefficient(double c) override { feature.setCoefficient(c); }
    size_t getSizeInBits() const override { return feature.getSizeInBits(); }
};

// This, along with the pendingUpdates deque is used to track speculative history
// It covers the PC of the branch, the prediction made, the magnitude (sum), and whether the branch was forward.
// It also stores the indices and predictions of each feature
// We consider this speculative history and so we don't count it
struct PendingUpdate {
    uint64_t branchPC;
    bool     pred;
    int16_t  sum;
    bool     isForward;

    std::vector<size_t> featureIndices;
    std::vector<bool>   featurePredictions;
};

class MultiperspectivePerceptronPredictor {
    private:
        // Collection of features
        std::vector<std::unique_ptr<FeatureBase>> features;

        // Global-global history registers
        std::vector<bool> globalHistory;
        std::vector<bool> tageHistory;
        std::vector<uint16_t> pathHistory;

        // These values are fixed, hence not part of the storage budget
        const uint8_t speed = 9;
        const size_t MAX_HIST_RECORDED = 336;
        const size_t MAX_PATHS_RECORDED = 72;
        const size_t MAX_TAGEPRED_RECORDED = 80;
        const int MAX_FEATURES_FOR_ALT = 15;  // How many best features to use for alternate prediction
        float FUDGE = 0.245; 

        // Adaptive threshold
        int theta = 26;
        int thetaThresholdCounter = 0;

        std::deque<PendingUpdate> pendingUpdates; // Not counting pending updates (spec-history)

        std::mt19937 gen; // Random generator, could be implemented with an LFSR etc
        
        template <typename FeatureType, typename... Args>
        void addFeature(double coefficient, Args&&... args) {
            features.push_back(std::make_unique<FeatureWrapper<FeatureType>>(std::forward<Args>(args)...));
            features.back()->setCoefficient(1.0); // We ignore the coefficient
        }

        int computePredictionSumForPc(uint64_t branchPC) const {
            int sum = 0;
            for (const auto& feature : features) {
                size_t index = feature->toIndex(branchPC, globalHistory, pathHistory, tageHistory);
                sum += feature->getWeightedPredictionAtIndex(index);
            }
            return static_cast<int>(sum);
        }

    public:
        MultiperspectivePerceptronPredictor(): gen(12345) {
                globalHistory.assign(MAX_HIST_RECORDED, false);
                pathHistory.assign(MAX_PATHS_RECORDED, 0);
                tageHistory.assign(MAX_TAGEPRED_RECORDED, 0);
                
                //addFeature<AcyclicHistoryFeature<2048, 5>>(1.0, 20);
                addFeature<AcyclicHistoryFeature<4096, 5>>(1.0, 40);
                addFeature<BlurryPathFeature<4096, 5>>(1.0, 12, 5, 4);
                //addFeature<BlurryPathFeature<2048, 5>>(1.0, 10, 7, 5);
                // addFeature<GhistFeature<2048, 5>>(1.3125, 0, 19);
                // addFeature<GhistFeature<2048, 5>>(1.0, 21, 64);
                // addFeature<GhistFeature<2048, 5>>(1.0, 75, 150);
                // addFeature<GhistFeature<2048, 5>>(1.0, 115, 206);
                addFeature<GhistModPathFeature<4096, 5>>(1.45, 50, 5);
                //addFeature<GhistModPathFeature<2048, 5>>(1.0, 13, 13);
                //addFeature<GhistModPathFeature<2048, 5>>(1.0, 10, 17);
                // addFeature<GhistPathFeature<2048, 5>>(1.25, 11, 2, true);
                // addFeature<GhistPathFeature<2048, 5>>(1.225, 15, 4, true);
                addFeature<GhistPathFeature<4096, 5>>(1.0, 23, 4, true);
                // addFeature<GhistPathFeature<2048, 5>>(1.0, 36, 4, true);
                // addFeature<GhistPathFeature<2048, 5>>(1.0, 51, 1, true);
                // addFeature<GhistPathFeature<2048, 5>>(1.5, 7, 1, true);
                // addFeature<GhistPathFeature<2048, 5>>(1.0, 86, 4, true);
                addFeature<LocalModFeature<4096, 5, 200, 10>>(2.0, 4);
                // addFeature<LocalModFeature<2048, 5, 256, 23>>(2.0, 14);
                // addFeature<LocalFeature<2048, 5, 1024, 11>>(1.0);
                addFeature<LocalFeature<4096, 5, 1660, 11>>(2.0);
                addFeature<ModHistFeature<4096, 5>>(0.9375, 16, 7);
                //addFeature<PathFeature<2048, 5>>(1.4375, 13, 2, true);
                // addFeature<GhistFeature<2048, 5>>(0.75, 67, 203);
                // addFeature<GhistPathFeature<2048, 5>>(1.0, 105, 4, false);
                addFeature<GhistPathFeature<4096, 5>>(1.0, 72, 1, true);
                addFeature<BiasFeature<4096, 5>>(1.0);
                // addFeature<GhistPathFeature<2048, 5>>(1.0, 32, 2, true);
                addFeature<IMLIFeature<2048, 5>>(1.78125, true);
                addFeature<ThistFeature<2048, 5>>(1.0, 0, 13);
              }

        // Returns the signed magnitude of the perceptron
        int predict(uint64_t pc) {
            // Get prediction from sum of all features
            int sum = computePredictionSumForPc(pc);
            
            // Don't ask me why we fudge, it's got something to do with the non-linear activation function?
            // The papers on this feels like I discovered a tribe of early 2000s machine learning experts who
            // crash landed on an island and were asked to switch to computer architecture 
            int fudged = std::abs(static_cast<int>(sum * FUDGE + 0.5)); 
                    
            // Check confidence
            if (fudged <= theta) {
                // Low confidence - use alternate prediction from best features
                return alternateScore(pc);
            }
                    
            // Normal prediction: taken if sum >= 1
            return sum;
        }

        // Aka, update histories
        void onSpecBranch(uint64_t pc, bool taken, bool isForward, bool tagePred) {
            int sum = predict(pc);

            // Record for committing later
            PendingUpdate u;
            u.branchPC  = pc;
            u.pred      = (sum >= 1);
            u.sum       = sum;
            u.isForward = isForward;
            
            // Store indices for re-evaluation at commit time, where histories will be different
            u.featureIndices.reserve(features.size());
            u.featurePredictions.reserve(features.size());
            
            for (const auto& f : features) {
                size_t idx = f->toIndex(pc, globalHistory, pathHistory, tageHistory);
                u.featureIndices.push_back(idx);
                u.featurePredictions.push_back(f->getWeightedPredictionAtIndex(idx) >= 1);
            }
            pendingUpdates.push_back(std::move(u));
            
            // Branch histories are updated speculatively
            // The weight tables are left unchanged so far
            globalHistory.insert(globalHistory.begin(), taken);
            globalHistory.pop_back();

            //size_t hashed_pc = PC_HASH(pc);
            uint16_t truncatedPC = static_cast<uint16_t>(pc & 0xFFFF);
            pathHistory.insert(pathHistory.begin(), truncatedPC);
            pathHistory.pop_back();

            tageHistory.insert(tageHistory.begin(), tagePred);
            tageHistory.pop_back();

            // This updated individually trackes histories
            for (auto& feature : features) {
                feature->updateWithBranchResult(pc, taken, isForward);
            }
        }
    
        // Called when a branch commits (actual weights are updated here)
        void onBranchCommit(bool disagreedWithTage, bool taken) {
            assert(!pendingUpdates.empty());
        
            PendingUpdate up = std::move(pendingUpdates.front());
            pendingUpdates.pop_front();

            bool prediction = up.pred;
            int fudged = std::abs(static_cast<int>(up.sum * FUDGE + 0.5)); 

            if (prediction != taken){
                thetaThresholdCounter += 1;
                if (thetaThresholdCounter >= speed) {
                    theta += 1;
                    thetaThresholdCounter = 0;
                }        
            } else if (prediction == taken && fudged <= theta){
                thetaThresholdCounter -= 1;
                if (thetaThresholdCounter <= -speed) {
                    theta -= 1;
                    thetaThresholdCounter = 0;
                }        
            }

            bool should_train = fudged <= theta || (disagreedWithTage && fudged <= theta*2);

            /* 3. train if needed, using stored indices */
            if (prediction != taken || should_train) {
                // Initial training on all weights
                int sum_after_training = 0;
                for (size_t i = 0; i < features.size(); ++i) {

                    features[i]->updateWeight(up.featureIndices[i], taken);
                    sum_after_training += features[i]->getWeightedPredictionAtIndex(up.featureIndices[i]);
                }

                bool prediction_after_training = sum_after_training >= 1;
                bool correct_after_train = prediction_after_training == taken;

                // Now we enter wacky space
                // This logic attempts to do the same that they do in GEM5
                // None of this is mentioned in the paper
                // And to be honest, it's probably with good reason
                // Basically:
                // We try, for a few rounds, to check if a random weight=0 would invert the prediction
                // And make it better, if so => we decrease the magnitude of this weight
                // Repeat a couple times
                int extra_rounds;
                if (disagreedWithTage)
                    extra_rounds = 2;
                else    
                    extra_rounds = 1;
                if (!correct_after_train && extra_rounds > 0) {
                    std::uniform_int_distribution<> start_dist(0, features.size()-1);

                    int rounds = 0;
                    bool helpful_to_continue;
                    do {
                        helpful_to_continue = false;
                        int start_idx = start_dist(gen);

                        int best_feature = -1;
                        for (int offset = 0; offset < features.size(); ++offset) {
                            int i = (start_idx + offset) % features.size();
                            auto& feature = features[i];
                            size_t idx = up.featureIndices[i];
                            int current_contribution = feature->getWeightedPredictionAtIndex(idx);
                            int potential_sum = sum_after_training - current_contribution; // We're not fudging here

                            if ((potential_sum >= 1) == taken) {
                                best_feature = i;
                                break;
                            }
                        }
                        if (best_feature != -1) {
                            auto& feature = features[best_feature];
                            size_t idx = up.featureIndices[best_feature];
                            int old_contribution = feature->getWeightedPredictionAtIndex(idx);
                            feature->decreaseMagnitude(idx);
                            int new_contribution = feature->getWeightedPredictionAtIndex(idx);
                            sum_after_training = sum_after_training - old_contribution + new_contribution;
                            prediction_after_training = sum_after_training >= 1;
                            rounds++;
                            helpful_to_continue = prediction_after_training != taken;
                        }
                    } while (helpful_to_continue && rounds < extra_rounds);
                }
            }

            /* 4. feature-accuracy bookkeeping */
            bool overflow = false;
            if (disagreedWithTage){
                for (size_t i = 0; i < features.size(); ++i){
                    bool overflow_f =  features[i]->updateAccuracy(up.featurePredictions[i], taken);
                    overflow |= overflow_f;
                }
            }
            if (overflow){
                for (size_t i = 0; i < features.size(); ++i){
                    features[i]->shrinkAccCounts();
                }
            }
        }

        int bestFeatureSum(uint64_t pc, size_t maxN) const
        {
            /* gather (mispreds, featurePtr) pairs */
            struct Pair { uint64_t mp; const FeatureBase* f; };
            std::vector<Pair> vec; vec.reserve(features.size());
            for (auto const& up : features)
                vec.push_back({ up->getMispredictions(), up.get() });

            /* sort by fewest mispredictions */
            std::partial_sort(vec.begin(),
                            vec.begin() + std::min(maxN, vec.size()),
                            vec.end(),
                            [](const Pair& a, const Pair& b){ return a.mp < b.mp; });

            int sum = 0;
            size_t  n = std::min(maxN, vec.size());
            for (size_t i = 0; i < n; ++i){
                size_t idx = vec[i].f->toIndex(pc, globalHistory, pathHistory, tageHistory);
                sum += vec[i].f->getWeightedPredictionAtIndex(idx);
            }

            return static_cast<int>(sum);
        }
        int alternateScore(uint64_t pc) const
        {
            return bestFeatureSum(pc, MAX_FEATURES_FOR_ALT);
        }

        size_t GET_SIZE(){
            size_t total_size = 0;
    
            // 1. Sum up all feature sizes
            for (const auto& feature : features) {
                // Each feature reports its own size through getSizeInBits()
                // This includes both weight tables and feature-specific data
                total_size += feature->getSizeInBits();
            }

            const size_t GHIST_REG_SIZE = MAX_HIST_RECORDED;      // Maximum global history length
            const size_t TAGE_REG_SIZE = MAX_TAGEPRED_RECORDED;   // Maximum global history length
            const size_t PATH_REG_SIZE = 16 * MAX_PATHS_RECORDED; // Path history (16 bits per entry)
            total_size += 32;                                     // 32 bits for LFSR (Necessary for random generation)
            total_size += GHIST_REG_SIZE + PATH_REG_SIZE + MAX_TAGEPRED_RECORDED;
            total_size += sizeof(theta) * 8;                      // Confidence threshold
            total_size += sizeof(thetaThresholdCounter) * 8;      // Counter for threshold updates

            return total_size;
        }
    };