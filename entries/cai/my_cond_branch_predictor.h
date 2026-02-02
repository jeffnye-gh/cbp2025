#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>
#include <cstdint>       // For fixed-width integer types like uint64_t
#include <unordered_map> // For std::unordered_map
#include <fstream>       // For file I/O (std::ifstream, std::ofstream)
#include <iostream>      // For standard I/O (std::cout, std::cerr)
#include <cassert>       // For the assert macro
#include <string>        // For std::string
#include <cstring>       // For C-string functions (if needed)
#include "lib/parameters.h"

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

class SampleCondPredictor
{
    SampleHist active_hist;
    std::unordered_map<uint64_t /*key*/, SampleHist /*val*/> pred_time_histories;

public:
    SampleCondPredictor(void)
    {
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

    bool predict(uint64_t seq_no, uint8_t piece, uint64_t PC, const bool tage_pred)
    {
        active_hist.tage_pred = tage_pred;
        // checkpoint current hist
        pred_time_histories.emplace(get_unique_inst_id(seq_no, piece), active_hist);
        const bool pred_taken = predict_using_given_hist(seq_no, piece, PC, active_hist, true /*pred_time_predict*/);
        return pred_taken;
    }

    bool predict_using_given_hist(uint64_t seq_no, uint8_t piece, uint64_t PC, const SampleHist &hist_to_use, const bool pred_time_predict)
    {
        return hist_to_use.tage_pred;
    }

    void history_update(uint64_t seq_no, uint8_t piece, uint64_t PC, bool taken, uint64_t nextPC)
    {
        active_hist.ghist = active_hist.ghist << 1;
        if (taken)
        {
            active_hist.ghist |= 1;
        }
    }

    void update(uint64_t seq_no, uint8_t piece, uint64_t PC, bool resolveDir, bool predDir, uint64_t nextPC)
    {
        const auto pred_hist_key = get_unique_inst_id(seq_no, piece);
        const auto &pred_time_history = pred_time_histories.at(pred_hist_key);
        update(PC, resolveDir, predDir, nextPC, pred_time_history);
        pred_time_histories.erase(pred_hist_key);
    }

    void update(uint64_t PC, bool resolveDir, bool pred_taken, uint64_t nextPC, const SampleHist &hist_to_use)
    {
    }

    // Dump the branch predictor state to a binary file.
    bool dump_state(const std::string &filename) const
    {
        std::ofstream ofs(filename, std::ios::binary);
        if (!ofs)
        {
            std::cerr << "Error opening file for dumping state: " << filename << std::endl;
            return false;
        }
        // Dump the active history: ghist and tage_pred.
        ofs.write(reinterpret_cast<const char *>(&active_hist.ghist), sizeof(active_hist.ghist));
        ofs.write(reinterpret_cast<const char *>(&active_hist.tage_pred), sizeof(active_hist.tage_pred));

        // Dump the number of entries in pred_time_histories.
        size_t map_size = pred_time_histories.size();
        ofs.write(reinterpret_cast<const char *>(&map_size), sizeof(map_size));

        // Dump each key and corresponding SampleHist.
        for (const auto &entry : pred_time_histories)
        {
            uint64_t key = entry.first;
            const SampleHist &hist = entry.second;
            ofs.write(reinterpret_cast<const char *>(&key), sizeof(key));
            ofs.write(reinterpret_cast<const char *>(&hist.ghist), sizeof(hist.ghist));
            ofs.write(reinterpret_cast<const char *>(&hist.tage_pred), sizeof(hist.tage_pred));
        }
        ofs.close();
        return true;
    }

    // Load the branch predictor state from a binary file.
    bool load_state(const std::string &filename)
    {
        std::ifstream ifs(filename, std::ios::binary);
        if (!ifs)
        {
            std::cerr << "Error opening file for loading state: " << filename << std::endl;
            return false;
        }
        // Load the active history.
        ifs.read(reinterpret_cast<char *>(&active_hist.ghist), sizeof(active_hist.ghist));
        ifs.read(reinterpret_cast<char *>(&active_hist.tage_pred), sizeof(active_hist.tage_pred));

        // Load the number of entries in pred_time_histories.
        size_t map_size = 0;
        ifs.read(reinterpret_cast<char *>(&map_size), sizeof(map_size));
        pred_time_histories.clear();

        // Load each entry.
        for (size_t i = 0; i < map_size; ++i)
        {
            uint64_t key;
            SampleHist hist;
            ifs.read(reinterpret_cast<char *>(&key), sizeof(key));
            ifs.read(reinterpret_cast<char *>(&hist.ghist), sizeof(hist.ghist));
            ifs.read(reinterpret_cast<char *>(&hist.tage_pred), sizeof(hist.tage_pred));
            pred_time_histories.emplace(key, hist);
        }
        ifs.close();
        return true;
    }
};

// =================
// Predictor End
// =================

#endif
static SampleCondPredictor cond_predictor_impl;
