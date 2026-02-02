#include "utils.h"
#include "knobs.h"
#include <cassert>
#include <unordered_map>
#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <numeric>
#include <array>
#include <deque>
#include <set>

// LOW LATENCY THRESHOLDS
// FOR REFERENCE, L1 LATENCY = 3, L2 LATENCY = 12, L3 LATENCY = 50, MAIN MEMORY LATENCY = 150
// THUS L1 HIT = 3, L2 HIT = 15, L3 HIT = 65, MEMORY ACCESS = 215
int low_latency_thresh = 30;
int med_latency_thresh = 75;
int long_latency_thresh = 150;

// Keep track of what pred seq no used (for correct mispred blaming)
std::unordered_map<uint64_t, uint8_t> pred_used;
std::unordered_map<uint64_t, bool> ldao; // loop disagree and override stat

// table to keep track of branch latency (for reducing wrong path cycles) (seq no, cycle predicted)
std::unordered_map<uint64_t, uint64_t> branch_latency_table;

int last_retired_seq_no = 0;
static uint64_t instr_count = 0;
static uint64_t training_time = 0;

// START HIGH LATENCY HEAP
// Define an entry: (pc, avg_wp)
using Entry = std::pair<uint64_t, uint64_t>;

// BUNCH OF HELPER FUNCTIONS BELOW

uint64_t end_cycle;
