//
// Created by Isaac Nudelman on 2025-04-20.
//

#ifndef CORRELATION_H
#define CORRELATION_H
#include <cassert>
// #include <vector>
#include <map>
#include <string_view>
#include <unordered_map>
#include <stdint.h>
#include <stdlib.h>
#include "lib/spdlog/fmt/bundled/format.h"
#include <cmath>
#include "knobs.h"


struct Feature {
    using Weight = int64_t;
    using Tag = uint64_t;
    // std::vector<Tag> tags;
    // std::vector<Weight> weights;
    //
    // std::pair<Tag, Weight> operator[](size_t idx) {
    //     assert(tags.size() == weights.size());
    //     assert(idx < tags.size());
    //
    //     return {tags[idx], weights[idx]};
    // }
    
    constexpr static uint64_t folded_tag_size = LOGFT;
    constexpr static uint64_t weight_size = LOGWS;
    std::string featureName; 
    std::map<Tag, Weight> x;

    Tag fold_tag(Tag unfolded_tag) {
      int64_t unfolded_tag_size = sizeof(Tag) * 8; 
      Tag folded_tag = 0;
      while(unfolded_tag_size > 0) {
        folded_tag = folded_tag ^ (unfolded_tag & ((1<<folded_tag_size)-1));
        unfolded_tag_size -= folded_tag_size;
        unfolded_tag = unfolded_tag >> unfolded_tag_size;
      }
      assert(folded_tag < 1<<folded_tag_size && "folded tag too big!!!!!!!");
      return folded_tag;
    }


    void update(Tag const tag, const bool direction) noexcept {
        assert(x.find(tag) != x.end() && "Failed to find tag when updating!");
	
        if (direction) {
            if(x[fold_tag(tag)] != (1LL << (weight_size - 1)) - 1)
                ++x[fold_tag(tag)]; 
        }
        else {
            if(x[fold_tag(tag)] != -(1LL << (weight_size - 1)))
                --x[fold_tag(tag)];
        }
    }

	void dump() const
	{
		fmt::print("{}: ", featureName);
		for (auto const &[tag, weight]: x) {
			fmt::print(" [{}, {}], ", tag, weight);
		}
	}

    size_t size() {
        // elements * ele size
        return (1 << folded_tag_size) * weight_size;
    }
};


class Correlation {
	std::vector<Feature> features;
	//     std::map<uint64_t, std::pair<std::vector<Feature::Tag>, std::vector<Feature>>> checkpoints;
	std::map<uint64_t, std::vector<Feature::Tag> > checkpoints;
	int theta;
	static constexpr uint32_t TC_INIT = (1 << (TCB - 1)) - 1; // this is for "easy" "negative" saturation logic
	uint32_t TC = TC_INIT;
    bool use_theta = CSC_USE_THETA;

	// dropping certain features
	std::vector<bool> mask;

	void theta_update(bool const correct_pred, int64_t sum) noexcept
	{
		sum = abs(sum);

		// update algo
		if (!correct_pred) {
			TC++;
			// check for TC saturation
			if (TC == ((1 << TCB) - 1)) {
				theta++;
				TC = TC_INIT;
			}
		} else {
			if (sum <= theta) {
				TC--;
				if (TC == 0) {
					TC = TC_INIT;
					theta--;
				}
			}
		}
	}

public:

    size_t size() {
		size_t sum = 0;
		for (int table_idx = 0; table_idx < features.size(); table_idx++) {
			if (mask[table_idx])
				sum += features[table_idx++].size();
		}
		return sum;
    }

	void init_pred(std::vector<std::string> const& names, std::vector<bool> const& enable) noexcept
	{
		theta = std::count(enable.begin(), enable.end(), true); //names.size() << 1;
		mask = enable;

		if (features.empty()) {
			features.insert(features.end(), names.size(), {});
		}

		size_t i = 0;
		for (auto &f: features) {
			f.featureName = names[i++];
		}

        printf("CSC TABLE SIZE: %lu", size());
	}

	bool pred(std::vector<Feature::Tag> tags, uint64_t unique_id) noexcept
	{
		checkpoints.insert({unique_id, std::vector(tags)});

		for (size_t i = 0; i < tags.size(); ++i) {
			features[i].x.insert({tags[i], 0});
		}

		return sum_tags(tags) >= 0;
	}

  int64_t raw_pred(std::vector<Feature::Tag> tags, uint64_t unique_id) noexcept {

		for (size_t i = 0; i < tags.size(); ++i) {
			features[i].x.insert({tags[i], 0});
		}

		return sum_tags(tags);
  }

	int64_t sum_tags(std::vector<Feature::Tag> const& tags) noexcept
	{
		int64_t sum = 0;
		int table_idx = 0;
		for (const auto &t: tags) {
			if (mask[table_idx])
				sum += features[table_idx++].x[t];
		}
		return sum;
	}

	// update the tables once we have direction
	void update(bool const direction, bool const pred, uint64_t const id) noexcept
	{
		assert(checkpoints.find(id) != checkpoints.end() && "Can't update a branch we haven't seen before");
        // re-run the prediction that the csc made.
        auto& old_inps = checkpoints[id];
        bool old_csc_pred = this->pred(old_inps, id);

		// correct update tracking for theta
		// bool correct_update = direction == pred;
        bool correct_update = direction == old_csc_pred;
		int64_t sum = sum_tags(checkpoints[id]);
		theta_update(correct_update, sum);

		// bail updating on over trained shit
		if (use_theta && abs(sum) > theta && correct_update) {
			return;
		}

		for (size_t i = 0; i < features.size(); i++) {
			features[i].update(checkpoints[id][i], direction);
		}

		checkpoints.erase(id);
	}

	void dump() const
	{
		for (auto const &f: features)
			fmt::print("{{"), f.dump(), fmt::print(" }}\n");

		fmt::print("pred params: theta: {} TC_BITS: {} N_FEATURES: {}\n", theta, TCB, features.size());
	}
};


#endif //CORRELATION_H
