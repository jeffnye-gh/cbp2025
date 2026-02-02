/*
  Copyright (C) 2025 Jun Fan

  The EVES value predictor source code is downloaded from https://www.microarch.org/cvp1/code/Seznec.tar.gz.
  Copyright (C) <2018>, INRIA : Institut National de Recherche en Informatique et en Automatique (French National Research Institute for Computer Science and Applied Mathematics)

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>
#include <unordered_set>

// ==========================================================
// Macros for debug
// ==========================================================
//#define BALL_DEBUG_PRINT
//#define BALL_DEBUG_PRINT_RF_UPDATE
//#define BALL_DEBUG_PRINT_COMMIT_QUEUE_UPDATE
//#define BALL_DEBUG_PRINT_PREDICT
//#define BALL_DEBUG_PRINT_MISPREDICT
//#define BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
//#define BALL_DEBUG_PRINT_VTAGE_PREDICT
//#define BALL_DEBUG_PRINT_RESOURE_CONSUMPTION
//#define BALL_DEBUG_PRINT_COVERAGE_ACCURACY

#define BALL_DEBUG_PRINT_PC_EN 1
#define BALL_DEBUG_PRINT_PC 0x4bf194
#define BALL_DEBUG_PRINT_SEQ_EN 0
#define BALL_DEBUG_PRINT_SEQ_NO 0xbb8
#define BALL_DEBUG_PRINT_DST_REG_EN 0
#define BALL_DEBUG_PRINT_DST_REG_IND 0xd

#define BALL_DEBUG_PRINT_ALL_EN 0

#ifdef BALL_DEBUG_PRINT
#ifdef BALL_DEBUG_PRINT_COVERAGE_ACCURACY
uint64_t predict_cnt = 0;
uint64_t predict_hit_cnt = 0;
uint64_t predict_mis_cnt = 0;

uint64_t tage_predict_cnt = 0;
uint64_t tage_predict_hit_cnt = 0;
uint64_t tage_predict_mis_cnt = 0;

uint64_t tage_predict_hit_not_use_ball_predict_cnt = 0;
uint64_t tage_predict_hit_use_ball_predict_cnt = 0;
uint64_t tage_predict_hit_use_ball_predict_hit_cnt = 0;
uint64_t tage_predict_hit_use_ball_predict_mis_cnt = 0;

uint64_t tage_predict_mis_not_use_ball_predict_cnt = 0;
uint64_t tage_predict_mis_use_ball_predict_cnt = 0;
uint64_t tage_predict_mis_use_ball_predict_hit_cnt = 0;
uint64_t tage_predict_mis_use_ball_predict_mis_cnt = 0;
#endif // BALL_DEBUG_PRINT_COVERAGE_ACCURACY
#endif // BALL_DEBUG_PRINT

// ==========================================================
// Prediction Mechanism Enable
// ==========================================================
#define BALL_PREDICT_EN 1
#define BALL_LOAD_CONST_EN 1
#define BALL_LOAD_CONST_WRAP_EN 1
#define BALL_LOAD_OFFSET_EN 1
#define BALL_LOAD_VTAGE_EN 1
#define BALL_ALU_CMP_EN 1
#define BALL_ALU_FCMP_ZERO_EN 1
#define BALL_B_CMP_ZERO_EN 1

// ==========================================================
// ==========================================================
// Resource Consumption
// - tage-sc-l: 524288 bits, 64.000000 KB
// - rf: 6272 bits, 0.765625 KB
// - commit_queue: 4992 bits, 0.609375 KB
// - store_resolve_queue: 392 bits, 0.047852 KB
// - cond_br_chain_table: 47104 bits, 5.750000 KB
// - load_pattern_table: 90624 bits, 11.062500 KB
// - alu_pattern_table: 2560 bits, 0.312500 KB
// - bp_data_cache: 798336 bits, 97.453125 KB
// - vtage: 54130 bits, 6.607666 KB
//
// Total: 1528698 bits, 186.608643 KB
// ==========================================================
// ==========================================================
#define CLOG2(x) ((x <= 1) ? 1 : (static_cast<uint64_t>(std::ceil(std::log2(x)))))
#define BITMASK(x) ((x <= 1) ? 1 : ((1 << static_cast<uint64_t>(std::ceil(std::log2(x)))) - 1))

// in the training traces, the actuall max piece for load is 0x7 (the total max piece is 0x8, another one for alu).
// for load, piece is part of lowest bit as index to lookup load_pattern_table,
// balance between area and performance, only record piece[0].
#define BALL_MAX_LOAD_PIECE_NUM 0x1
#define BALL_MAX_LOAD_PIECE_NUM_BIT_WIDTH CLOG2(BALL_MAX_LOAD_PIECE_NUM)
#define BALL_MAX_LOAD_PIECE_NUM_BIT_MASK BITMASK(BALL_MAX_LOAD_PIECE_NUM)

#define BALL_MEM_VA_WIDTH 48
#define BALL_MEM_VA_MASK ((((uint64_t)1) << BALL_MEM_VA_WIDTH) - 1)

// ==========================================================
// Appendix A Const Analysis, Component #2
// ==========================================================
// architecture register file
//
// - 32 int reg, each 64 bits
// - 32 fp/simd reg, each 128 bits
// - 1 flag reg, 64 bits
// - 1 zero reg, 64 bits
//
// Total: 32 * 64 + 32 * 128 + 64 + 64
//        = 6272 bits = 0.765625 KB
// ==========================================================
// Note:
// Because cond_branch_predictor_interface.cc not provide interface to
// get value of Register File, track them here.
// The number of Resiger File is defined in lib/uarchsim.h.
//#define RFSIZE 66   // integer: r0-r31.  fp/simd: r32-r63. flags: r64.
//#define RFFLAGS 64  // flags register is r64 (65th register)
//#define RFZERO 65   // zero register is r65 (66th register)

// From CBP2025 Google Group, https://groups.google.com/g/cbp2025/c/pQSxUVTN6bk/m/UPgWrl5VAAAJ,
// 1) r31 is the stack pointer register.
// 2) In the CBP2025 workloads, vector length is 128b.
//    Instructions that write 128b registers get broken into two pieces: one writes the lower 64b and the other writes the upper 64b.
#define INT_REG_MIN_IND 0
#define INT_REG_MAX_IND 31
#define FP_REG_MIN_IND 32
#define FP_REG_MAX_IND 63
#define FLAG_REG_IND 64
#define ZERO_REG_IND 65

struct RFEntry 
{
    // int/flag/zero reg only use low (64bits)
    // fp/simd reg use both low and high (128bits)
    uint64_t low;
    uint64_t high;

    RFEntry()
    {
        reset();
    }

    void reset()
    {
        low = 0;
        high = 0;
    }
};

RFEntry reg_file[66] = {};

// ==========================================================
// Appendix A Const Analysis, Component #3
// ==========================================================
// commit_queue
//
// - valid: 1 bit
// - inst_class: 2 bits (only use alu, load, fp and slowAlu)
// - pc: COMMIT_QUEUE_PC_WIDTH = 23 bits
// - piece: COMMIT_QUEUE_PIECE_WIDTH = 1 bit
// - src_reg_0_valid: 1 bit
// - src_reg_0: 7 bits
// - src_reg_1_valid: 1 bit
// - src_reg_1: 7 bits
// - src_reg_size_more_than_two: 1 bit
// - dst_reg_valid: 1 bit
// - dst_reg: 7 bits
//
// Total: COMMIT_QUEUE_ENTRY_NUM *
//        (1 + COMMIT_QUEUE_INST_CLASS_WIDTH + COMMIT_QUEUE_PC_WIDTH + COMMIT_QUEUE_PIECE_WIDTH + 1 + 7 + 1 + 7 + 1 + 1 + 7)
//        = 4992 bits = 0.609375 KB
// ==========================================================
#define COMMIT_QUEUE_ENTRY_NUM 96
#define COMMIT_QUEUE_INST_CLASS_WIDTH 2
#define COMMIT_QUEUE_PC_RIGHT_SHIFT_WIDTH 2
#define COMMIT_QUEUE_PC_RIGHT_SHIFT_2 1
#define COMMIT_QUEUE_PC_WIDTH COND_BR_CHAIN_B_LOAD_ALU_NODE_MAX_PC_WIDTH
#define COMMIT_QUEUE_PC_MASK ((1 << COND_BR_CHAIN_B_LOAD_ALU_NODE_MAX_PC_WIDTH) - 1)
#define COMMIT_QUEUE_PIECE_WIDTH BALL_MAX_LOAD_PIECE_NUM_BIT_WIDTH
#define COMMIT_QUEUE_PIECE_MASK ((1 << BALL_MAX_LOAD_PIECE_NUM_BIT_WIDTH) - 1)

struct CommitQueueEntry
{
    bool valid;
    InstClass inst_class;
#ifdef BALL_DEBUG_PRINT
    uint64_t pc_full;
    uint8_t piece_full;
#endif // BALL_DEBUG_PRINT
    uint64_t pc;
    uint8_t piece;
    bool src_reg_0_valid;
    uint8_t src_reg_0;
    bool src_reg_1_valid;
    uint8_t src_reg_1;
    bool src_reg_size_more_than_two;
    bool dst_reg_valid;
    uint8_t dst_reg;

    CommitQueueEntry()
    {
        reset();
    }

    void reset()
    {
        valid = 0;
        inst_class = InstClass::aluInstClass;
#ifdef BALL_DEBUG_PRINT
        pc_full = 0;
        piece_full = 0;
#endif // BALL_DEBUG_PRINT
        pc = 0;
        piece = 0;
        src_reg_0_valid = 0;
        src_reg_0 = 0;
        src_reg_1_valid = 0;
        src_reg_1 = 0;
        src_reg_size_more_than_two = 0;
        dst_reg_valid = 0;
        dst_reg = 0;
    }
};

CommitQueueEntry commit_queue[COMMIT_QUEUE_ENTRY_NUM] = {};

// ==========================================================
// Appendix A Const Analysis, Component #4
// ==========================================================
// store_resolve_queue
//
// - valid: 1 bit
// - addr: BALL_MEM_VA_WIDTH = 48 bits
//
// Total: STORE_RESOLVE_QUEUE_ENTRY_NUM * (1 + BALL_MEM_VA_WIDTH)
//        = 392 bits = 0.047852 KB
// ==========================================================
// store_resolve_queue is used to track stores that is resolved but not committed,
// if the load data used by branch predictor has same address with stores
// in store_resolve_queue, then it should be miss and the branch predictor should
// not make a prediction.
// And it is not tracking stores accurately, store_resolve_queue only has limited entry,
// so the ready-commit store may not find itself in store_resolve_queue, because
// it's alreay been poped by younger resolved stores.
#define STORE_RESOLVE_QUEUE_ENTRY_NUM 8

struct StoreResolveQueueEntry
{
    bool     valid;
    uint64_t addr;

    StoreResolveQueueEntry()
    {
        reset();
    }

    void reset()
    {
        valid = 0;
        addr = 0;
    }
};

StoreResolveQueueEntry store_resolve_queue[STORE_RESOLVE_QUEUE_ENTRY_NUM] = {};

// ==========================================================
// Appendix A Const Analysis, Component #5
// ==========================================================
// cond_br_chain_table
//
// - B Node
// -- valid: 1 bit
// -- tag: COND_BR_CHAIN_B_NODE_TAG_WIDTH = 14 bits
// -- taken: ($clog2(COND_BR_CHAIN_B_NODE_TAKEN_CONF_MAX)+1)*COND_BR_CHAIN_B_NODE_COND_NUM = 48 bits
// -- ball_tage_sel: $clog2(COND_BR_CHAIN_B_NODE_SEL_MAX) = 8 bits
//
// - ALU Node
// -- valid: 1 bit
// -- src_1_valid: 1 bit
// -- pc: COND_BR_CHAIN_ALU_NODE_PC_WIDTH = 11 bits
//
// - Load Node
// -- valid: 1 bit
// -- pc: COND_BR_CHAIN_LOAD_NODE_PC_WIDTH = 23 bits
// -- piece: COND_BR_CHAIN_LOAD_NODE_PIECE_WIDTH = 1 bits
//
// Each cond_br_chain_table entry has 1 B Node, 1 ALU Node, 4 Load Node
// Total: COND_BR_CHAIN_TABLE_SET * 
//        (( 1 + COND_BR_CHAIN_B_NODE_TAG_WIDTH + ($clog2(COND_BR_CHAIN_B_NODE_TAKEN_CONF_MAX)+1)*COND_BR_CHAIN_B_NODE_COND_NUM + $clog2(COND_BR_CHAIN_B_NODE_SEL_MAX) ) +
//         ( 1 + 1 + COND_BR_CHAIN_ALU_NODE_PC_WIDTH ) * +
//         ( 1 + COND_BR_CHAIN_LOAD_NODE_PC_WIDTH + COND_BR_CHAIN_LOAD_NODE_PIECE_WIDTH) * 4)
//        =  47104 bits = 5.750000 KB
// ==========================================================
#define COND_BR_CHAIN_TABLE_SET 256
#define COND_BR_CHAIN_TABLE_SET_WIDTH CLOG2(COND_BR_CHAIN_TABLE_SET)

#define COND_BR_CHAIN_B_NODE_TAG_WIDTH 14
#define COND_BR_CHAIN_B_NODE_COND_NUM 16
#define COND_BR_CHAIN_B_NODE_TAKEN_CONF_MAX 3
#define COND_BR_CHAIN_B_NODE_TAKEN_CONF_MIN -COND_BR_CHAIN_B_NODE_TAKEN_CONF_MAX

#define COND_BR_CHAIN_ALU_NODE_PC_WIDTH (ALU_PATTERN_TABLE_SET_WIDTH + ALU_PATTERN_TABLE_TAG_WIDTH)
#define COND_BR_CHAIN_ALU_NODE_PC_MASK ((1 << COND_BR_CHAIN_ALU_NODE_PC_WIDTH) - 1)
#define COND_BR_CHAIN_LOAD_NODE_PC_WIDTH (LOAD_PATTERN_TABLE_SET_WIDTH + LOAD_PATTERN_TABLE_TAG_WIDTH)
#define COND_BR_CHAIN_LOAD_NODE_PC_MASK ((1 << COND_BR_CHAIN_LOAD_NODE_PC_WIDTH) - 1)
#define COND_BR_CHAIN_LOAD_NODE_PIECE_WIDTH BALL_MAX_LOAD_PIECE_NUM_BIT_WIDTH
#define COND_BR_CHAIN_LOAD_NODE_PIECE_MASK ((1 << COND_BR_CHAIN_LOAD_NODE_PIECE_WIDTH) -1)

#define COND_BR_CHAIN_B_NODE_SEL_MAX 255

#define COND_BR_CHAIN_B_NODE_PC_WIDTH (COND_BR_CHAIN_TABLE_SET_WIDTH + COND_BR_CHAIN_B_NODE_TAG_WIDTH)
#define COND_BR_CHAIN_LOAD_ALU_NODE_MAX_PC_WIDTH ((COND_BR_CHAIN_LOAD_NODE_PC_WIDTH > COND_BR_CHAIN_ALU_NODE_PC_WIDTH) ? COND_BR_CHAIN_LOAD_NODE_PC_WIDTH : COND_BR_CHAIN_ALU_NODE_PC_WIDTH)
#define COND_BR_CHAIN_B_LOAD_ALU_NODE_MAX_PC_WIDTH ((COND_BR_CHAIN_B_NODE_PC_WIDTH > COND_BR_CHAIN_LOAD_ALU_NODE_MAX_PC_WIDTH) ? COND_BR_CHAIN_B_NODE_PC_WIDTH : COND_BR_CHAIN_LOAD_ALU_NODE_MAX_PC_WIDTH)

struct CondBRChainBNode
{
    bool valid;
#ifdef BALL_DEBUG_PRINT
    uint64_t pc;
#endif
    uint64_t tag;
    int8_t taken[COND_BR_CHAIN_B_NODE_COND_NUM];

    uint8_t ball_tage_sel;

    CondBRChainBNode()
    {
        reset();
    }

    void reset()
    {
        valid = 0;
#ifdef BALL_DEBUG_PRINT
        pc = 0;
#endif
        tag = 0;
        for (uint64_t i = 0; i < COND_BR_CHAIN_B_NODE_COND_NUM; i++) {
            taken[i] = 0;
        }

        ball_tage_sel = (COND_BR_CHAIN_B_NODE_SEL_MAX+1)/2;
    }
};

struct CondBRChainALUNode
{
    bool valid;
    bool src_1_valid;
#ifdef BALL_DEBUG_PRINT
    uint64_t pc_full;
    uint8_t piece_full;
#endif
    uint64_t pc;

    CondBRChainALUNode()
    {
        reset();
    }

    void reset()
    {
        valid = 0;
        src_1_valid = 0;
#ifdef BALL_DEBUG_PRINT
        pc_full = 0;
        piece_full = 0;
#endif
        pc = 0;
    }
};

struct CondBRChainLoadNode
{
    bool valid;
#ifdef BALL_DEBUG_PRINT
    uint64_t pc_full;
    uint8_t piece_full;
#endif
    uint64_t pc;
    uint8_t piece;

    CondBRChainLoadNode()
    {
        reset();
    }

    void reset()
    {
        valid = 0;
#ifdef BALL_DEBUG_PRINT
        pc_full = 0;
        piece_full = 0;
#endif
        pc = 0;
        piece = 0;
    }
};

CondBRChainBNode b_0_0[COND_BR_CHAIN_TABLE_SET] = {};
CondBRChainALUNode alu_1_0[COND_BR_CHAIN_TABLE_SET] = {};
CondBRChainLoadNode load_2_0[COND_BR_CHAIN_TABLE_SET] = {};
CondBRChainLoadNode load_2_1[COND_BR_CHAIN_TABLE_SET] = {};
CondBRChainLoadNode load_3_0[COND_BR_CHAIN_TABLE_SET] = {};
CondBRChainLoadNode load_3_1[COND_BR_CHAIN_TABLE_SET] = {};

// ==========================================================
// Appendix A Const Analysis, Component #6
// ==========================================================
// load_pattern_table
//
// valid: 1 bit
// tag: LOAD_PATTERN_TABLE_TAG_WIDTH = 14 bits
// mem_sz: LOAD_PATTERN_TABLE_MEM_SZ_WIDTH = 2 bits
// last_mem_va: BALL_MEM_VA_WIDTH = 48 bits
// const_stride: ($clog2(LOAD_PATTERN_TABLE_CONST_STRIDE_MAX)+1) = 13 bits
// const_stride_conf: $clog2(LOAD_PATTERN_TABLE_CONST_STRIDE_CONF_MAX) = 3 bits
// wrap_mem_va: LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH = 12 bits
// wrap_target_mem_va: LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH = 12 bits
// wrap_conf: $clog2(LOAD_PATTERN_TABLE_WRAP_CONF_MAX) = 3 bits
// offset: ($clog2(LOAD_PATTERN_TABLE_OFFSET_MAX)+1) = 6 bits
// offset_conf: $clog2(LOAD_PATTERN_TABLE_OFFSET_CONF_MAX) = 3 bits
// vtage_predict_addr_valid: 1 bit
// vtage_predict_addr: BALL_MEM_VA_WIDTH = 48 bits
// in_flight_cnt_valid: 1 bit
// in_flight_cnt: $clog2(MAXINFLIGHT) = 10 bit
//
// Total: LOAD_PATTERN_TABLE_SET *
//        (1 + LOAD_PATTERN_TABLE_TAG_WIDTH + LOAD_PATTERN_TABLE_MEM_SZ_WIDTH + BALL_MEM_VA_WIDTH +
//         ($clog2(LOAD_PATTERN_TABLE_CONST_STRIDE_MAX)+1) + $clog2(LOAD_PATTERN_TABLE_CONST_STRIDE_CONF_MAX) + 
//         2 * LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH + $clog2(LOAD_PATTERN_TABLE_WRAP_CONF_MAX) +
//         ($clog2(LOAD_PATTERN_TABLE_OFFSET_MAX)+1) + $clog2(LOAD_PATTERN_TABLE_OFFSET_CONF_MAX) +
//         1 + BALL_MEM_VA_WIDTH +
//         1 + $clog2(MAXINFLIGHT)
//        )
//        = 90624 bits = 11.062500 KB
// ==========================================================
#define LOAD_PATTERN_TABLE_SET 512
#define LOAD_PATTERN_TABLE_SET_WIDTH CLOG2(LOAD_PATTERN_TABLE_SET)
#define LOAD_PATTERN_TABLE_SET_PC_WIDTH (LOAD_PATTERN_TABLE_SET_WIDTH-BALL_MAX_LOAD_PIECE_NUM_BIT_WIDTH)
#define LOAD_PATTERN_TABLE_SET_PC_WIDTH_MASK ((1 << LOAD_PATTERN_TABLE_SET_PC_WIDTH) - 1)
#define LOAD_PATTERN_TABLE_TAG_WIDTH 14
#define LOAD_PATTERN_TABLE_MEM_SZ_WIDTH 2
#define LOAD_PATTERN_TABLE_CONST_STRIDE_MAX 4095
#define LOAD_PATTERN_TABLE_CONST_STRIDE_MIN -LOAD_PATTERN_TABLE_CONST_STRIDE_MAX
#define LOAD_PATTERN_TABLE_CONST_STRIDE_CONF_MAX 7
#define LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH 12 // treat wrap_mem_va[47:12] and wrap_target_mem_va[47:12] same as mem_va[47:12]
#define LOAD_PATTERN_TABLE_WRAP_ADDR_MASK ((1 << LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH) - 1)
#define LOAD_PATTERN_TABLE_WRAP_CONF_MAX 7
#define LOAD_PATTERN_TABLE_OFFSET_MAX 31
#define LOAD_PATTERN_TABLE_OFFSET_MIN -LOAD_PATTERN_TABLE_OFFSET_MAX
#define LOAD_PATTERN_TABLE_OFFSET_CONF_MAX 7

struct LoadPatternTableEntry
{
    bool valid;
#ifdef BALL_DEBUG_PRINT
    uint64_t pc;
    uint64_t piece;
#endif // BALL_DEBUG_PRINT
    uint64_t tag;
    // mem_sz
    // 00: 1 byte
    // 01: 2 byte
    // 10: 4 byte
    // 11: 8 byte
    uint8_t mem_sz;
    uint64_t last_mem_va;

    int64_t const_stride;
    uint8_t const_stride_conf;
    uint64_t wrap_mem_va;
    uint64_t wrap_target_mem_va;
    uint8_t wrap_conf;

    int64_t offset;
    uint8_t  offset_conf;

    bool vtage_predict_addr_valid;
    uint64_t vtage_predict_addr;

    bool in_flight_cnt_valid;
    uint16_t in_flight_cnt;

    LoadPatternTableEntry()
    {
        reset();
    }

    void reset()
    {
        valid = 0;
#ifdef BALL_DEBUG_PRINT
        pc = 0;
        piece = 0;
#endif // BALL_DEBUG_PRINT
        tag = 0;
        mem_sz = 0;
        last_mem_va = 0;

        const_stride = 0;
        const_stride_conf = 0;
        wrap_mem_va = 0;
        wrap_target_mem_va = 0;
        wrap_conf = 0;

        offset = 0;
        offset_conf = 0;

        vtage_predict_addr_valid = 0;
        vtage_predict_addr = 0;

        in_flight_cnt_valid = 0;
        in_flight_cnt = 0;
    }
};

LoadPatternTableEntry load_pattern_table[LOAD_PATTERN_TABLE_SET] = {};

// ==========================================================
// Appendix A Const Analysis, Component #7
// ==========================================================
// alu_pattern_table
//
// - valid: 1 bit
// - tag: ALU_PATTERN_TABLE_TAG_WIDTH = 2 bits
// - cmp_conf: $clog2(ALU_PATTERN_TABLE_CMP_CONF_MAX) = 1 bit
// - fcmp_zero_conf: $clog2(ALU_PATTERN_TABLE_FCMP_ZERO_CONF_MAX) = 1 bit
//
// Total: ALU_PATTERN_TABLE_SET *
//        ( 1 + ALU_PATTERN_TABLE_TAG_WIDTH + $clog2(ALU_PATTERN_TABLE_CMP_CONF_MAX) + $clog2(ALU_PATTERN_TABLE_FCMP_ZERO_CONF_MAX) )
//        = 2560 bits = 0.3125 KB
// ==========================================================
#define ALU_PATTERN_TABLE_SET 512
#define ALU_PATTERN_TABLE_SET_WIDTH CLOG2(ALU_PATTERN_TABLE_SET)
#define ALU_PATTERN_TABLE_TAG_WIDTH 2
#define ALU_PATTERN_TABLE_CMP_CONF_MAX 1
#define ALU_PATTERN_TABLE_FCMP_ZERO_CONF_MAX 1

struct ALUPatternTableEntry
{
    bool valid;
#ifdef BALL_DEBUG_PRINT
    uint64_t pc;
#endif // BALL_DEBUG_PRINT
    uint64_t tag;
    uint8_t cmp_conf;
    uint8_t fcmp_zero_conf;

    ALUPatternTableEntry()
    {
        reset();
    }

    void reset()
    {
        valid = 0;
#ifdef BALL_DEBUG_PRINT
        pc = 0;
#endif // BALL_DEBUG_PRINT
        tag = 0;
        cmp_conf = 0;
        fcmp_zero_conf = 0;
    }
};

ALUPatternTableEntry alu_pattern_table[ALU_PATTERN_TABLE_SET] = {};

// ==========================================================
// Appendix A Const Analysis, Component #8
// ==========================================================
// bp_data_cache
//
// - valid: BP_DATA_CACHE_LINE_GRANULE_NUM = 16 bits
// - tag: BP_DATA_CACHE_TAG_WIDTH = BALL_MEM_VA_WIDTH - $clog2(BP_DATA_CACHE_LINE_SIZE) - $clog2(BP_DATA_CACHE_SET) =  35 bits
// - data: BP_DATA_CACHE_LINE_SIZE * 8 = 512 bits
// - age: $clog2(BP_DATA_CACHE_WAY) = 4 bits
//
// Total: BP_DATA_CACHE_SET * BP_DATA_CACHE_WAY *
//        (BP_DATA_CACHE_LINE_GRANULE_NUM + BP_DATA_CACHE_TAG_WIDTH + BP_DATA_CACHE_LINE_SIZE * 8 + $clog2(BP_DATA_CACHE_WAY))
//       = 798336 bits = 97.453125 KB
// ==========================================================
#define BP_DATA_CACHE_SET 128
#define BP_DATA_CACHE_SET_WIDTH CLOG2(BP_DATA_CACHE_SET)
#define BP_DATA_CACHE_WAY 11
#define BP_DATA_CACHE_LINE_SIZE 64
#define BP_DATA_CACHE_LINE_GRANULE_SIZE 4
#define BP_DATA_CACHE_LINE_GRANULE_NUM (BP_DATA_CACHE_LINE_SIZE / BP_DATA_CACHE_LINE_GRANULE_SIZE)
#define BP_DATA_CACHE_TAG_WIDTH (BALL_MEM_VA_WIDTH - CLOG2(BP_DATA_CACHE_SET) - CLOG2(BP_DATA_CACHE_LINE_SIZE))

struct BPDataCacheEntry
{
    bool valid[BP_DATA_CACHE_LINE_GRANULE_NUM];
    uint64_t tag;
    uint64_t data[BP_DATA_CACHE_LINE_GRANULE_NUM];
    uint8_t age;

    BPDataCacheEntry()
    {
        reset();
    }

    void reset()
    {
        for (uint64_t i = 0; i < BP_DATA_CACHE_LINE_GRANULE_NUM; i++) {
            valid[i] = 0;
            data[i] = 0;
        }
        tag = 0;
        age = 0;
    }
};

BPDataCacheEntry bp_data_cache[BP_DATA_CACHE_SET][BP_DATA_CACHE_WAY] = {};

struct BPDataCacheReadResult
{
    bool valid;
    uint64_t data;

    BPDataCacheReadResult()
    {
        reset();
    }

    void reset()
    {
        valid = 0;
        data = 0;
    }
};

// ==========================================================
// Appendix A Const Analysis, Component #10, #11
// ==========================================================
// Prediction Time Histories
// (NOT counted in resource consumption)
// ==========================================================
struct ball_pred_time_hist_t
{
    bool tage_sc_l_predict;

    bool use_ball_predict;
    bool ball_predict_valid;
    bool ball_predict;

    ball_pred_time_hist_t()
    {
        reset();
    }

    void reset()
    {
        tage_sc_l_predict = 0;

        use_ball_predict = 0;
        ball_predict_valid = 0;
        ball_predict = 0;
    }
};

struct load_pred_time_hist_t
{
    bool load_pattern_table_hit;

    bool make_prediction;
    uint64_t predicted_addr;

    load_pred_time_hist_t()
    {
        reset();
    }

    void reset()
    {
        load_pattern_table_hit = 0;
        make_prediction = 0;
        predicted_addr = 0;
    }
};

std::unordered_map<uint64_t/*key*/, ball_pred_time_hist_t/*val*/> ball_pred_time_histories;
std::unordered_map<uint64_t/*key*/, load_pred_time_hist_t/*val*/> load_pred_time_histories;

// ==========================================================
// Function to handle NZCV computation.
// ==========================================================
struct NZCV {
    bool N;  // Negative flag
    bool Z;  // Zero flag
    bool C;  // Carry flag
    bool V;  // Overflow flag

    // Convert struct to uint64_t for easy comparison
    uint64_t to_uint64() const {
        return (static_cast<uint64_t>(N) << 3) |
               (static_cast<uint64_t>(Z) << 2) |
               (static_cast<uint64_t>(C) << 1) |
               (static_cast<uint64_t>(V) << 0);
    }

    // Overload == operator
    bool operator==(const NZCV& other) const {
        return this->to_uint64() == other.to_uint64();
    }

    // Overload != operator (optional)
    bool operator!=(const NZCV& other) const {
        return !(*this == other);
    }

   // Convert uint64_t to struct (unpacking flags)
   static NZCV from_uint64(uint64_t value) {
       NZCV flags;
       flags.N = (value >> 3) & 1;
       flags.Z = (value >> 2) & 1;
       flags.C = (value >> 1) & 1;
       flags.V = (value >> 0) & 1;
       return flags;
   }
};

NZCV calculate_nzcv_add(uint64_t a, uint64_t b);
NZCV calculate_nzcv_sub(uint64_t a, uint64_t b);

NZCV calculate_nzcv_fcmp_zero(uint64_t a);

// Function to calculate NZCV flags for addition
NZCV calculate_nzcv_add(uint64_t a, uint64_t b) {
    NZCV flags;
    uint64_t sum = a + b;

    // Carry flag (C) - Unsigned overflow detection
    flags.C = (sum < a);

    // Overflow flag (V) - Signed overflow detection
    int64_t sa = static_cast<int64_t>(a);
    int64_t sb = static_cast<int64_t>(b);
    int64_t ssum = static_cast<int64_t>(sum);
    flags.V = ((sa > 0 && sb > 0 && ssum < 0) || (sa < 0 && sb < 0 && ssum > 0));

    // Zero flag (Z)
    flags.Z = (sum == 0);

    // Negative flag (N) - Check MSB (sign bit)
    flags.N = (sum >> 63) & 1;

    return flags;
}

// Function to calculate NZCV flags for subtraction
NZCV calculate_nzcv_sub(uint64_t a, uint64_t b) {
    NZCV flags;
    uint64_t diff = a - b;

    // Carry flag (C) - Unsigned borrow detection (set if NO borrow occurs)
    flags.C = (a >= b);

    // Overflow flag (V) - Signed underflow detection
    int64_t sa = static_cast<int64_t>(a);
    int64_t sb = static_cast<int64_t>(b);
    int64_t sdiff = static_cast<int64_t>(diff);
    flags.V = ((sa > 0 && sb < 0 && sdiff < 0) || (sa < 0 && sb > 0 && sdiff > 0));

    // Zero flag (Z)
    flags.Z = (diff == 0);

    // Negative flag (N) - Check MSB (sign bit)
    flags.N = (diff >> 63) & 1;

    return flags;
}

// Function to calculate NZCV flags for fcmp_zero
NZCV calculate_nzcv_fcmp_zero(uint64_t a) {
    // assume only fp32
    NZCV flags;

    bool negative = ((a & 0x0000000080000000) >> 31);
    bool positive = !((a & 0x0000000080000000) >> 31);
    bool zero = ((a & 0x000000007FFFFFFF) == 0);

    flags.N = negative & ~zero;
    flags.Z = zero;
    flags.C = zero | positive;
    flags.V = 0;

    return flags;
}

// ==========================================================
// Appendix A Const Analysis, Component #9
// ==========================================================
// ==========================================================
// ==========================================================
// EVES from CVP1 (1/n) (begin)
// ==========================================================
// ==========================================================
/* Same predictor for the 3 tracks, but with different parameters*/

#include <vector>
#include <deque>
// BALL use load_pattern_table to predict constant stride, disable E-stride
//#define PREDSTRIDE
#define PREDVTAGE

// 32KB //
//#define K32
#ifdef K32

// 4.202 //3.729 for stride only  //3.570 for VTAGE only 
// 262018 bits
#define _UWIDTH 2
#define LOGLDATA 9
#define LOGBANK 7
#define TAGWIDTH 11
#define NBBANK 49


#define _NHIST 8
int _HL[_NHIST + 1] = { 0, 0, 3, 7, 15, 31, 63, 90, 127 };

#define LOGSTR 4
#define NBWAYSTR 3
#define TAGWIDTHSTR 14
#define LOGSTRIDE 20
#endif
//END 32 KB//

// 8KB //
#define K8
#ifdef K8
// 8KB
// 4.026 //3.729 Stride only // 3.437 for TAGE  only
// 65378 bits
#define _UWIDTH 2
#define LOGLDATA 7
#define LOGBANK 5
#define TAGWIDTH 11
#define NBBANK 47

#define _NHIST 7
int _HL[_NHIST + 1] = { 0, 0, 1, 3, 6, 12, 18, 30 };

#define LOGSTR 4
#define NBWAYSTR 3
#define TAGWIDTHSTR 14
#define LOGSTRIDE 20
#endif
//END 8KB //


//UNLIMITED//
//#define LIMITSTUDY
#ifdef LIMITSTUDY
// 4.408 //3.730 Stride only // 3.732 for TAGE  only
#define _UWIDTH 1
#define LOGLDATA 20
#define LOGBANK 20
#define TAGWIDTH 15
#define NBBANK 63



#define _NHIST 14
int _HL[_NHIST + 1] =
  { 0, 0, 1, 3, 7, 15, 31, 47, 63, 95, 127, 191, 255, 383, 511 };
#define LOGSTR 20
#define TAGWIDTHSTR 15
#define LOGSTRIDE 30
#define NBWAYSTR 3

#endif
//END UNLIMITED //

#define WIDTHCONFID 3
#define MAXCONFID ((1<< WIDTHCONFID)-1)
#define WIDTHCONFIDSTR 5
#define MAXCONFIDSTR  ((1<< WIDTHCONFIDSTR)-1)
#define MAXU  ((1<< _UWIDTH)-1)

#define BANKDATA (1<<LOGLDATA)
#define MINSTRIDE -(1<<(LOGSTRIDE-1))
#define MAXSTRIDE (-MINSTRIDE-1)
#define BANKSIZE (1<<LOGBANK)
#define PREDSIZE (NBBANK*BANKSIZE)


// Global path history

static uint64_t gpath[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

/* using up to 512 bits of path history was found to result in some performance benefit : essentially in the unlimited case. I did not explore longer histories */

static uint64_t gtargeth = 0;
/* history of the targets : limited to  64 bits*/


// The E-Stride predictor
//entry in the stride predictor
struct strdata
{
  uint64_t LastValue;		//64 bits
  uint64_t Stride;		// LOGSTRIDE bits
  uint8_t conf;			// WIDTHCONFIDSTR bits
  uint16_t tag;			//TAGWIDTHSTR bits
  uint16_t NotFirstOcc;		//1 bits
  int u;			// 2 bits
  //67 + LOGSTRIDE + WIDTHCONFIDSTR + TAGWIDTHSTR bits 
};
static strdata STR[NBWAYSTR * (1 << LOGSTR)];


static int SafeStride = 0;	// 16 bits

/////////////////////////////////// For E-VTAGE
//the data values
struct longdata
{
  uint64_t data;
  uint8_t u;
};
static longdata LDATA[3 * BANKDATA];
//  managed as a a skewed associative array
//each entry is 64-LOGLDATA bits for the data (since the other bits can be deduced from the index) + 2 bits for u

//VTAGE
struct vtentry
{
  uint64_t hashpt;		// hash of the value + number of way in the value array ; LOGLDATA + 2 bits 
  uint8_t conf;			//WIDTHCONFID bits
  uint16_t tag;			// TAGWIDTH bits
  uint8_t u;			//2 bits
  //LOGLDATA +4 +WIDTHCONFID +TAGWIDTH bits
};

static vtentry Vtage[PREDSIZE];

#define  MAXTICK 1024
static int _TICK;		//10 bits // for managing replacement on the VTAGE entries
static int LastMispVT = 0;	//8 bits //for tracking the last misprediction on VTAGE

////// for managing speculative state and forwarding information to the back-end
struct ForUpdate
{
  bool predvtage;
  bool predstride;
  bool prediction_result;
  uint8_t todo;
  uint64_t pc;
  uint32_t GI[_NHIST + 1];
  uint32_t GTAG[_NHIST + 1];
  int B[NBWAYSTR];
  int TAGSTR[NBWAYSTR];
  int STHIT;
  int HitBank;
  //int8_t INSTTYPE;
  InstClass INSTTYPE;
  int8_t NbOperand;

};

#define MAXINFLIGHT 1024
static ForUpdate Update[MAXINFLIGHT];	// there may be 256 instructions inflight

int _seq_commit;


#define NOTLLCMISS (actual_latency < 150)
#define NOTL2MISS (actual_latency < 60)
#define NOTL1MISS (actual_latency < 12)
#define FASTINST (actual_latency ==1)
#define MFASTINST (actual_latency <3)

// ==========================================================
// ==========================================================
// EVES from CVP1 (1/n) (end)
// ==========================================================
// ==========================================================

class BALLPredictor
{
    public:

        BALLPredictor (void)
        {
        }

        void setup()
        {
            #ifdef BALL_DEBUG_PRINT
            #ifdef BALL_DEBUG_PRINT_RESOURE_CONSUMPTION
            // 64KB TAGE-SC-L unmodified
            uint64_t tage_bits = 64 * 8 * 1024;

            uint64_t rf_bits = 32 * 64 + 32 * 128 + 64 + 64;

            uint64_t commit_queue_bits = COMMIT_QUEUE_ENTRY_NUM * 
                                         (1 + COMMIT_QUEUE_INST_CLASS_WIDTH + COMMIT_QUEUE_PC_WIDTH + COMMIT_QUEUE_PIECE_WIDTH +
                                          1 + 7 + 1 + 7 + 1 + 1 + 7);

            uint64_t store_resolve_queue_bits = STORE_RESOLVE_QUEUE_ENTRY_NUM * (1 + BALL_MEM_VA_WIDTH);

            uint64_t cond_br_chain_table_bits = COND_BR_CHAIN_TABLE_SET *
                                                (1 + COND_BR_CHAIN_B_NODE_TAG_WIDTH + (CLOG2(COND_BR_CHAIN_B_NODE_TAKEN_CONF_MAX) + 1) * COND_BR_CHAIN_B_NODE_COND_NUM +
                                                 CLOG2(COND_BR_CHAIN_B_NODE_SEL_MAX) +
                                                 (1 + 1 + COND_BR_CHAIN_ALU_NODE_PC_WIDTH) * 1 +
                                                 (1 + COND_BR_CHAIN_LOAD_NODE_PC_WIDTH + COND_BR_CHAIN_LOAD_NODE_PIECE_WIDTH) * 4);

            uint64_t load_pattern_table_bits = LOAD_PATTERN_TABLE_SET *
                                               (1 + LOAD_PATTERN_TABLE_TAG_WIDTH + LOAD_PATTERN_TABLE_MEM_SZ_WIDTH + BALL_MEM_VA_WIDTH +
                                                (CLOG2(LOAD_PATTERN_TABLE_CONST_STRIDE_MAX) + 1) +
                                                CLOG2(LOAD_PATTERN_TABLE_CONST_STRIDE_CONF_MAX) +
                                                LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH +
                                                LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH +
                                                (CLOG2(LOAD_PATTERN_TABLE_WRAP_CONF_MAX)) +
                                                (CLOG2(LOAD_PATTERN_TABLE_OFFSET_MAX) + 1) +
                                                (CLOG2(LOAD_PATTERN_TABLE_OFFSET_CONF_MAX)) +
                                                1 + BALL_MEM_VA_WIDTH +
                                                1 + CLOG2(MAXINFLIGHT));

            uint64_t alu_pattern_table_bits = ALU_PATTERN_TABLE_SET *
                                              (1 + ALU_PATTERN_TABLE_TAG_WIDTH +
                                               CLOG2(ALU_PATTERN_TABLE_CMP_CONF_MAX) +
                                               CLOG2(ALU_PATTERN_TABLE_FCMP_ZERO_CONF_MAX));

            uint64_t bp_data_cache_bits = BP_DATA_CACHE_SET * BP_DATA_CACHE_WAY *
                                          (BP_DATA_CACHE_LINE_GRANULE_NUM + BP_DATA_CACHE_TAG_WIDTH + BP_DATA_CACHE_LINE_SIZE * 8 + CLOG2(BP_DATA_CACHE_WAY));

            uint64_t vtage_bits = 0;
            // EVES predictor, copied and modified from its endPredictor() code
            #ifndef LIMITSTUDY
            int SIZE = 0;
            #ifdef PREDSTRIDE
            SIZE = NBWAYSTR * (1 << LOGSTR) * (67 + LOGSTRIDE + TAGWIDTHSTR + WIDTHCONFIDSTR) + 16;	//the SafeStride counter
            //printf ("STORAGE SIZE: STRIDE (%d bits)", SIZE);
            #endif // PREDSTRIDE

            #ifdef PREDVTAGE
            //==vtage original==//int INTER = (((64 - LOGLDATA) + 2) * 3 << LOGLDATA);	//the 64 bits data words - LOGLDATA implicits bits  + 2 u bits
            int INTER = (((BALL_MEM_VA_WIDTH - LOGLDATA) + 2) * 3 << LOGLDATA);	//the BALL_MEM_VA_WIDTH bits data words - LOGLDATA implicits bits  + 2 u bits
            //printf (" |Value array:  (%d bits)", INTER);
            SIZE += INTER;
            INTER = BANKSIZE * NBBANK * (TAGWIDTH + (LOGLDATA + 2) + WIDTHCONFID + _UWIDTH)	//the VTAGE entries
              + 8			// the LastMispVT counter
              + 10;			// the TICK counter

            //printf (" |VTAGE:  (%d bits)", INTER);
            SIZE += INTER;
            #endif // PREDVTAGE
            //printf (" ||| TOTAL SIZE: %d bits\n", SIZE);
            vtage_bits = SIZE;
            #endif
                                                 
            uint64_t total_bits = tage_bits + rf_bits + commit_queue_bits + store_resolve_queue_bits + cond_br_chain_table_bits + load_pattern_table_bits + alu_pattern_table_bits + bp_data_cache_bits + vtage_bits;

            fprintf(stdout, "// Resource Consumption\n");
            fprintf(stdout, "// - tage-sc-l: %lu bits, %f KB\n", tage_bits, ((float)tage_bits) / (8 * 1024));
            fprintf(stdout, "// - rf: %lu bits, %f KB\n", rf_bits, ((float)rf_bits) / (8 * 1024));
            fprintf(stdout, "// - commit_queue: %lu bits, %f KB\n", commit_queue_bits, ((float)commit_queue_bits) / (8 * 1024));
            fprintf(stdout, "// - store_resolve_queue: %lu bits, %f KB\n", store_resolve_queue_bits, ((float)store_resolve_queue_bits) / (8 * 1024));
            fprintf(stdout, "// - cond_br_chain_table: %lu bits, %f KB\n", cond_br_chain_table_bits, ((float)cond_br_chain_table_bits) / (8 * 1024));
            fprintf(stdout, "// - load_pattern_table: %lu bits, %f KB\n", load_pattern_table_bits, ((float)load_pattern_table_bits) / (8 * 1024));
            fprintf(stdout, "// - alu_pattern_table: %lu bits, %f KB\n", alu_pattern_table_bits, ((float)alu_pattern_table_bits) / (8 * 1024));
            fprintf(stdout, "// - bp_data_cache: %lu bits, %f KB\n", bp_data_cache_bits, ((float)bp_data_cache_bits) / (8 * 1024));
            fprintf(stdout, "// - vtage: %lu bits, %f KB\n", vtage_bits, ((float)vtage_bits) / (8 * 1024));
            fprintf(stdout, "//\n");
            fprintf(stdout, "// Total: %lu bits, %f KB\n", total_bits, ((float)total_bits) / (8 * 1024));
            fprintf(stdout, "\n");
            #endif // BALL_DEBUG_PRINT_RESOURE_CONSUMPTION
            #endif // BALL_DEBUG_PRINT
        }

        void terminate()
        {
            #ifdef BALL_DEBUG_PRINT
            #ifdef BALL_DEBUG_PRINT_COVERAGE_ACCURACY
            fprintf(stdout, "=====================\n");
            fprintf(stdout, "Coverage and Accuracy\n");

            fprintf(stdout, "CondBR Total: %lu;\n- TageHit: %lu(%.2f%%); TageMis: %lu(%.2f%%).\n",
                            predict_cnt,
                            tage_predict_hit_cnt, (float)tage_predict_hit_cnt/(float)predict_cnt * 100,
                            tage_predict_mis_cnt, (float)tage_predict_mis_cnt/(float)predict_cnt * 100);

            fprintf(stdout, "CondBR Total: %lu;\n- TageHitBallInvalid: %lu(%.2f%%); TageHitBallHit: %lu(%.2f%%); TageHitBallMis: %lu(%.2f%%)\n- TageMisBallInvalid: %lu(%.2f%%); TageMisBallHit: %lu(%.2f%%); TageMisBallMis: %lu(%.2f%%)\n",
                            predict_cnt,
                            tage_predict_hit_not_use_ball_predict_cnt, (float)tage_predict_hit_not_use_ball_predict_cnt/(float)predict_cnt * 100,
                            tage_predict_hit_use_ball_predict_hit_cnt, (float)tage_predict_hit_use_ball_predict_hit_cnt/(float)predict_cnt * 100,
                            tage_predict_hit_use_ball_predict_mis_cnt, (float)tage_predict_hit_use_ball_predict_mis_cnt/(float)predict_cnt * 100,
                            tage_predict_mis_not_use_ball_predict_cnt, (float)tage_predict_mis_not_use_ball_predict_cnt/(float)predict_cnt * 100,
                            tage_predict_mis_use_ball_predict_hit_cnt, (float)tage_predict_mis_use_ball_predict_hit_cnt/(float)predict_cnt * 100,
                            tage_predict_mis_use_ball_predict_mis_cnt, (float)tage_predict_mis_use_ball_predict_mis_cnt/(float)predict_cnt * 100);

            fprintf(stdout, "TageMis Total: %lu;\n- TageMisBallHit: %lu(%.2f%%); TageMisBallMis: %lu(%.2f%%); TageMisBallInvalid: %lu(%.2f%%)\n",
                            tage_predict_mis_cnt,
                            tage_predict_mis_use_ball_predict_hit_cnt, (float)tage_predict_mis_use_ball_predict_hit_cnt/(float)tage_predict_mis_cnt * 100,
                            tage_predict_mis_use_ball_predict_mis_cnt, (float)tage_predict_mis_use_ball_predict_mis_cnt/(float)tage_predict_mis_cnt * 100,
                            tage_predict_mis_not_use_ball_predict_cnt, (float)tage_predict_mis_not_use_ball_predict_cnt/(float)tage_predict_mis_cnt * 100);

            fprintf(stdout, "TageMis Total: %lu;\n- TageMisBallHit(ex TageHitBallMiss): %lu(%.2f%%); TageHitBallMiss: %lu(%.2f%%); TageMisBallMis: %lu(%.2f%%); TageMisBallInvalid: %lu(%.2f%%)\n",
                            tage_predict_mis_cnt,
                            tage_predict_mis_use_ball_predict_hit_cnt-tage_predict_hit_use_ball_predict_mis_cnt, (float)(tage_predict_mis_use_ball_predict_hit_cnt-tage_predict_hit_use_ball_predict_mis_cnt)/(float)tage_predict_mis_cnt * 100,
                            tage_predict_hit_use_ball_predict_mis_cnt, (float)tage_predict_hit_use_ball_predict_mis_cnt/(float)tage_predict_mis_cnt * 100,
                            tage_predict_mis_use_ball_predict_mis_cnt, (float)tage_predict_mis_use_ball_predict_mis_cnt/(float)tage_predict_mis_cnt * 100,
                            tage_predict_mis_not_use_ball_predict_cnt, (float)tage_predict_mis_not_use_ball_predict_cnt/(float)tage_predict_mis_cnt * 100);

            fprintf(stdout, "Coverage: %.2f%%, Accuracy: %.2f%%.\n",
                            (float)(tage_predict_mis_use_ball_predict_hit_cnt-tage_predict_hit_use_ball_predict_mis_cnt)/(float)tage_predict_mis_cnt * 100,
                            (float)(tage_predict_hit_use_ball_predict_hit_cnt+tage_predict_mis_use_ball_predict_hit_cnt)/(float)(tage_predict_hit_use_ball_predict_hit_cnt+tage_predict_hit_use_ball_predict_mis_cnt+tage_predict_mis_use_ball_predict_hit_cnt+tage_predict_mis_use_ball_predict_mis_cnt) * 100);

            fprintf(stdout, "=====================\n");

            #endif // BALL_DEBUG_PRINT_COVERAGE_ACCURACY
            #endif // BALL_DEBUG_PRINT
        }

        void print_commit_queue() {
            #ifdef BALL_DEBUG_PRINT
            fprintf(stdout, "commit_queue\n");
            for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                fprintf(stdout, "%#x ", i);
                fprintf(stdout, "%#x ", commit_queue[i].valid);
                fprintf(stdout, "%#x ", (uint8_t)commit_queue[i].inst_class);
                fprintf(stdout, "%#zx ", commit_queue[i].pc_full);
                fprintf(stdout, "%#x ", commit_queue[i].piece_full);
                fprintf(stdout, "%#zx ", commit_queue[i].pc);
                fprintf(stdout, "%#x ", commit_queue[i].piece);
                fprintf(stdout, "%#x ", commit_queue[i].src_reg_0_valid);
                fprintf(stdout, "%#x ", commit_queue[i].src_reg_0);
                fprintf(stdout, "%#x ", commit_queue[i].src_reg_1_valid);
                fprintf(stdout, "%#x ", commit_queue[i].src_reg_1);
                fprintf(stdout, "%#x ", commit_queue[i].src_reg_size_more_than_two);
                fprintf(stdout, "%#x ", commit_queue[i].dst_reg_valid);
                fprintf(stdout, "%#x ", commit_queue[i].dst_reg);
                fprintf(stdout, "\n");
            }
            fprintf(stdout, "\n");
            #endif // BALL_DEBUG_PRINT
        }

        void print_store_resolve_queue() {
            #ifdef BALL_DEBUG_PRINT
            fprintf(stdout, "store_resolve_queue\n");
            for (uint8_t i = 0; i < STORE_RESOLVE_QUEUE_ENTRY_NUM; i++) {
                fprintf(stdout, "%#x ", i);
                fprintf(stdout, "%#x ", store_resolve_queue[i].valid);
                fprintf(stdout, "%#zx ", store_resolve_queue[i].addr);
                fprintf(stdout, "\n");
            }
            fprintf(stdout, "\n");
            #endif // BALL_DEBUG_PRINT
        }

        void print_cond_br_chain_table() {
            #ifdef BALL_DEBUG_PRINT
            fprintf(stdout, "cond_br_chain_table\n");
            for (uint64_t i = 0; i < COND_BR_CHAIN_TABLE_SET; i++) {
                fprintf(stdout, "%#zx\n", i);
                fprintf(stdout, "b_0_0: %#x, %#zx, %#zx\n", b_0_0[i].valid, b_0_0[i].pc, b_0_0[i].tag);
                for (uint64_t j = 0; j < COND_BR_CHAIN_B_NODE_COND_NUM; j++) {
                    fprintf(stdout, "%#x,", b_0_0[i].taken[j]);
                }
                fprintf(stdout, "\n");
                fprintf(stdout, "alu_1_0: %#x, %#x, %#zx, %#zx\n", alu_1_0[i].valid, alu_1_0[i].src_1_valid, alu_1_0[i].pc_full, alu_1_0[i].pc);
                fprintf(stdout, "load_2_0: %#x, %#zx, %#zx\n", load_2_0[i].valid, load_2_0[i].pc_full, load_2_0[i].pc);
                fprintf(stdout, "load_2_1: %#x, %#zx, %#zx\n", load_2_1[i].valid, load_2_1[i].pc_full, load_2_1[i].pc);
                fprintf(stdout, "load_3_0: %#x, %#zx, %#zx\n", load_3_0[i].valid, load_3_0[i].pc_full, load_3_0[i].pc);
                fprintf(stdout, "load_3_1: %#x, %#zx, %#zx\n", load_3_1[i].valid, load_3_1[i].pc_full, load_3_1[i].pc);
                fprintf(stdout, "\n");
            }
            fprintf(stdout, "\n");
            #endif // BALL_DEBUG_PRINT
        }

        void print_load_pattern_table() {
            #ifdef BALL_DEBUG_PRINT
            fprintf(stdout, "load_pattern_table\n");
            for (uint64_t i = 0; i < LOAD_PATTERN_TABLE_SET; i++) {
                fprintf(stdout, "%#zx\n", i);

                fprintf(stdout, "valid: %#x\n", load_pattern_table[i].valid);
                fprintf(stdout, "pc: %#zx\n", load_pattern_table[i].pc);
                fprintf(stdout, "piece: %#zx\n", load_pattern_table[i].piece);
                fprintf(stdout, "tag: %#zx\n", load_pattern_table[i].tag);
                fprintf(stdout, "mem_sz: %#x\n", load_pattern_table[i].mem_sz);
                fprintf(stdout, "last_mem_va: %#zx\n", load_pattern_table[i].last_mem_va);

                fprintf(stdout, "const_stride: %#zx\n", load_pattern_table[i].const_stride);
                fprintf(stdout, "const_stride_conf: %#x\n", load_pattern_table[i].const_stride_conf);
                fprintf(stdout, "wrap_mem_va: %#zx\n", load_pattern_table[i].wrap_mem_va);
                fprintf(stdout, "wrap_target_mem_va: %#zx\n", load_pattern_table[i].wrap_target_mem_va);
                fprintf(stdout, "wrap_conf: %#x\n", load_pattern_table[i].wrap_conf);

                fprintf(stdout, "offset: %#zx\n", load_pattern_table[i].offset);
                fprintf(stdout, "offset_conf: %#x\n", load_pattern_table[i].offset_conf);

                fprintf(stdout, "vtage_predict_addr_valid: %#x\n", load_pattern_table[i].vtage_predict_addr_valid);
                fprintf(stdout, "vtage_predict_addr: %#zx\n", load_pattern_table[i].vtage_predict_addr);

                fprintf(stdout, "in_flight_cnt_valid: %#x\n", load_pattern_table[i].in_flight_cnt_valid);
                fprintf(stdout, "in_flight_cnt: %#x\n", load_pattern_table[i].in_flight_cnt);

                fprintf(stdout, "\n");
            }
            fprintf(stdout, "\n");
            #endif // BALL_DEBUG_PRINT
        }

        void print_load_pattern_table_entry(LoadPatternTableEntry load_pattern_table_entry) {
            #ifdef BALL_DEBUG_PRINT
            fprintf(stdout, "load_pattern_table_entry\n");

            fprintf(stdout, "valid: %#x\n", load_pattern_table_entry.valid);
            fprintf(stdout, "pc: %#zx\n", load_pattern_table_entry.pc);
            fprintf(stdout, "piece: %#zx\n", load_pattern_table_entry.piece);
            fprintf(stdout, "tag: %#zx\n", load_pattern_table_entry.tag);
            fprintf(stdout, "mem_sz: %#x\n", load_pattern_table_entry.mem_sz);
            fprintf(stdout, "last_mem_va: %#zx\n", load_pattern_table_entry.last_mem_va);

            fprintf(stdout, "const_stride: %#zx\n", load_pattern_table_entry.const_stride);
            fprintf(stdout, "const_stride_conf: %#x\n", load_pattern_table_entry.const_stride_conf);
            fprintf(stdout, "wrap_mem_va: %#zx\n", load_pattern_table_entry.wrap_mem_va);
            fprintf(stdout, "wrap_target_mem_va: %#zx\n", load_pattern_table_entry.wrap_target_mem_va);
            fprintf(stdout, "wrap_conf: %#x\n", load_pattern_table_entry.wrap_conf);

            fprintf(stdout, "offset: %#zx\n", load_pattern_table_entry.offset);
            fprintf(stdout, "offset_conf: %#x\n", load_pattern_table_entry.offset_conf);

            fprintf(stdout, "vtage_predict_addr_valid: %#x\n", load_pattern_table_entry.vtage_predict_addr_valid);
            fprintf(stdout, "vtage_predict_addr: %#zx\n", load_pattern_table_entry.vtage_predict_addr);

            fprintf(stdout, "in_flight_cnt_valid: %#x\n", load_pattern_table_entry.in_flight_cnt_valid);
            fprintf(stdout, "in_flight_cnt: %#x\n", load_pattern_table_entry.in_flight_cnt);

            fprintf(stdout, "\n");
            #endif // BALL_DEBUG_PRINT
        }

        void print_alu_pattern_table() {
            #ifdef BALL_DEBUG_PRINT
            fprintf(stdout, "alu_pattern_table\n");
            for (uint64_t i = 0; i < ALU_PATTERN_TABLE_SET; i++) {
                fprintf(stdout, "%#zx\n", i);
                fprintf(stdout, "valid: %#x\n", alu_pattern_table[i].valid);
                fprintf(stdout, "pc: %#zx\n", alu_pattern_table[i].pc);
                fprintf(stdout, "tag: %#zx\n", alu_pattern_table[i].tag);
                fprintf(stdout, "cmp_conf: %#x\n", alu_pattern_table[i].cmp_conf);
                fprintf(stdout, "fcmp_zero_conf: %#x\n", alu_pattern_table[i].fcmp_zero_conf);
                fprintf(stdout, "\n");
            }
            fprintf(stdout, "\n");
            #endif // BALL_DEBUG_PRINT
        }

        void print_alu_pattern_table_entry(ALUPatternTableEntry alu_pattern_table_entry) {
            #ifdef BALL_DEBUG_PRINT
            fprintf(stdout, "alu_pattern_table_entry\n");
            fprintf(stdout, "valid: %#x\n", alu_pattern_table_entry.valid);
            fprintf(stdout, "pc: %#zx\n", alu_pattern_table_entry.pc);
            fprintf(stdout, "tag: %#zx\n", alu_pattern_table_entry.tag);
            fprintf(stdout, "cmp_conf: %#x\n", alu_pattern_table_entry.cmp_conf);
            fprintf(stdout, "fcmp_zero_conf: %#x\n", alu_pattern_table_entry.fcmp_zero_conf);
            fprintf(stdout, "\n");
            #endif // BALL_DEBUG_PRINT
        }

        void rf_update (uint64_t seq_no, uint8_t piece, std::optional<uint64_t> dst_reg_info, std::optional<uint64_t> dst_reg_value) {
            bool dst_reg_valid = dst_reg_info.has_value();

            if (dst_reg_valid) {
                uint8_t  dst_reg_ind = dst_reg_info.value();

                if ((dst_reg_ind >= INT_REG_MIN_IND && dst_reg_ind <= INT_REG_MAX_IND) || dst_reg_ind == FLAG_REG_IND) {
                    reg_file[dst_reg_ind].low = dst_reg_value.value();
                } else if (dst_reg_ind >= FP_REG_MIN_IND && dst_reg_ind <= FP_REG_MAX_IND) {
                    if (piece % 2 == 0) {
                        reg_file[dst_reg_ind].low = dst_reg_value.value();
                    } else {
                        reg_file[dst_reg_ind].high = dst_reg_value.value();
                    }
                }

                #ifdef BALL_DEBUG_PRINT
                #ifdef BALL_DEBUG_PRINT_RF_UPDATE
                if (dst_reg_ind == BALL_DEBUG_PRINT_DST_REG_IND && BALL_DEBUG_PRINT_DST_REG_EN ||
                    seq_no == BALL_DEBUG_PRINT_SEQ_NO && BALL_DEBUG_PRINT_SEQ_EN ) {
                    fprintf(stdout, "seq_no: %#zx, dst_reg_valid: %#x, dst_reg_ind: %#x, dst_reg_value: %#zx\n", seq_no, dst_reg_valid, dst_reg_ind, dst_reg_value.value());
                }
                #endif // BALL_DEBUG_PRINT_RF_UPDATE
                #endif // BALL_DEBUG_PRINT
            }

        }

        void commit_queue_update (uint64_t pc, uint8_t piece, InstClass inst_class, std::vector<uint64_t> src_reg_info, std::optional<uint64_t> dst_reg_info) {

            bool src_reg_0_valid = (src_reg_info.size() >= 1);
            bool src_reg_1_valid = (src_reg_info.size() >= 2);
            bool src_reg_size_more_than_two = (src_reg_info.size() >= 3);

            bool dst_reg_valid = dst_reg_info.has_value();

            bool commit_queue_need_update = dst_reg_valid &&
                                            ((inst_class == InstClass::aluInstClass) ||
                                             (inst_class == InstClass::loadInstClass) ||
                                             (inst_class == InstClass::fpInstClass) ||
                                             (inst_class == InstClass::slowAluInstClass));

            if (commit_queue_need_update) {
                // always push youngest uop to entry 0 and drop entry (COMMIT_QUEUE_ENTRY_NUM-1),
                // so no need write pointer variable to mark the youngest uop entry
                for (uint8_t i = COMMIT_QUEUE_ENTRY_NUM - 1; i > 0; i--) {
                    commit_queue[i].valid = commit_queue[i-1].valid;
                    commit_queue[i].inst_class = commit_queue[i-1].inst_class;
                    #ifdef BALL_DEBUG_PRINT
                    commit_queue[i].pc_full = commit_queue[i-1].pc_full;
                    commit_queue[i].piece_full = commit_queue[i-1].piece_full;
                    #endif // BALL_DEBUG_PRINT
                    commit_queue[i].pc = commit_queue[i-1].pc;
                    commit_queue[i].piece = commit_queue[i-1].piece;
                    commit_queue[i].src_reg_0_valid = commit_queue[i-1].src_reg_0_valid;
                    commit_queue[i].src_reg_0 = commit_queue[i-1].src_reg_0;
                    commit_queue[i].src_reg_1_valid = commit_queue[i-1].src_reg_1_valid;
                    commit_queue[i].src_reg_1 = commit_queue[i-1].src_reg_1;
                    commit_queue[i].src_reg_size_more_than_two = commit_queue[i-1].src_reg_size_more_than_two;
                    commit_queue[i].dst_reg_valid = commit_queue[i-1].dst_reg_valid;
                    commit_queue[i].dst_reg = commit_queue[i-1].dst_reg;
                }

                commit_queue[0].valid = 1;
                commit_queue[0].inst_class = inst_class;
                #ifdef BALL_DEBUG_PRINT
                commit_queue[0].pc_full = pc;
                commit_queue[0].piece_full = piece;
                #endif // BALL_DEBUG_PRINT
                commit_queue[0].pc = (COMMIT_QUEUE_PC_RIGHT_SHIFT_2 ? (pc >> COMMIT_QUEUE_PC_RIGHT_SHIFT_WIDTH) : pc) & COMMIT_QUEUE_PC_MASK;
                commit_queue[0].piece = piece & COMMIT_QUEUE_PIECE_MASK;
                commit_queue[0].src_reg_0_valid = src_reg_0_valid;
                commit_queue[0].src_reg_0 = src_reg_0_valid ? src_reg_info[0] : 0;
                commit_queue[0].src_reg_1_valid = src_reg_1_valid;
                commit_queue[0].src_reg_1 = src_reg_1_valid ? src_reg_info[1] : 0;
                commit_queue[0].src_reg_size_more_than_two = src_reg_size_more_than_two;
                commit_queue[0].dst_reg_valid = dst_reg_valid;
                commit_queue[0].dst_reg = dst_reg_info.value_or(0);
            }

            #ifdef BALL_DEBUG_PRINT
            #ifdef BALL_DEBUG_PRINT_COMMIT_QUEUE_UPDATE
            if (pc == BALL_DEBUG_PRINT_PC && BALL_DEBUG_PRINT_PC_EN) {
                print_commit_queue();
            }
            #endif // BALL_DEBUG_PRINT_COMMIT_QUEUE_UPDATE
            #endif // BALL_DEBUG_PRINT
        }

        void store_resolve_queue_update_resolve(uint64_t mem_va) {
            // find an invalid entry,
            // if there is an invalid entry, move entries younger to entries with larger entry number,
            // and push new store to entry 0.
            // if there is no invalid entry, pop the entry with the largest entry number,
            // and push new store to entry 0.
            int8_t invalid_index = 0;
            for (int8_t i = STORE_RESOLVE_QUEUE_ENTRY_NUM - 1; i > 0; i--) {
                if (!store_resolve_queue[i].valid) {
                    invalid_index = i;
                    break;
                }
            }

            int8_t start_index = (invalid_index > 0) ? invalid_index : (STORE_RESOLVE_QUEUE_ENTRY_NUM-1);
            for (int8_t i = start_index; i > 0; i--) {
                store_resolve_queue[i].valid = store_resolve_queue[i-1].valid;
                store_resolve_queue[i].addr = store_resolve_queue[i-1].addr;
            }

            store_resolve_queue[0].valid = 1;
            store_resolve_queue[0].addr = mem_va;
        }

        void store_resolve_queue_update_commit(uint64_t mem_va) {
            for (int8_t i = STORE_RESOLVE_QUEUE_ENTRY_NUM - 1; i >= 0; i--) {
                if (store_resolve_queue[i].valid && (store_resolve_queue[i].addr == mem_va)) {
                    store_resolve_queue[i].valid = 0;

                    break;
                }
            }
        }

        uint64_t get_cond_br_chain_table_lookup_pc (uint64_t pc) {
            return (pc >> 2);
        }

        bool cond_br_chain_table_lookup(uint64_t pc, uint64_t& cond_br_chain_table_lookup_index, uint64_t& cond_br_chain_table_lookup_tag) {
            uint64_t cond_br_chain_table_lookup_pc = get_cond_br_chain_table_lookup_pc(pc);

            cond_br_chain_table_lookup_index = cond_br_chain_table_lookup_pc % COND_BR_CHAIN_TABLE_SET;
            cond_br_chain_table_lookup_tag = (cond_br_chain_table_lookup_pc / COND_BR_CHAIN_TABLE_SET) & ((1 << COND_BR_CHAIN_B_NODE_TAG_WIDTH) - 1);

            bool cond_br_chain_table_lookup_hit = 0;
            if (b_0_0[cond_br_chain_table_lookup_index].valid &&
                (b_0_0[cond_br_chain_table_lookup_index].tag == cond_br_chain_table_lookup_tag)) {
                cond_br_chain_table_lookup_hit = 1;
            }

            return cond_br_chain_table_lookup_hit;
        }

        void cond_br_chain_table_update_resolve (uint64_t seq_no, uint64_t pc, bool resolve_dir, bool pred_dir, std::vector<uint64_t> src_reg_info) {
            uint64_t cond_br_chain_table_lookup_index;
            uint64_t cond_br_chain_table_lookup_tag;
            bool cond_br_chain_table_lookup_hit = cond_br_chain_table_lookup(pc, cond_br_chain_table_lookup_index, cond_br_chain_table_lookup_tag);

            // train selection between ball and tage_sc_l on prediction accuracy
            if (cond_br_chain_table_lookup_hit) {
                bool inc_ball_tage_sel =  ball_pred_time_histories.at(seq_no).ball_predict_valid && (ball_pred_time_histories.at(seq_no).ball_predict == resolve_dir) &&
                                           (ball_pred_time_histories.at(seq_no).tage_sc_l_predict != resolve_dir) &&
                                           (b_0_0[cond_br_chain_table_lookup_index].ball_tage_sel < COND_BR_CHAIN_B_NODE_SEL_MAX);
                bool dec_ball_tage_sel =  ball_pred_time_histories.at(seq_no).ball_predict_valid && (ball_pred_time_histories.at(seq_no).ball_predict != resolve_dir) &&
                                           (b_0_0[cond_br_chain_table_lookup_index].ball_tage_sel > 0);

                if (inc_ball_tage_sel) {
                    b_0_0[cond_br_chain_table_lookup_index].ball_tage_sel += 1;
                } else if (dec_ball_tage_sel) {
                    b_0_0[cond_br_chain_table_lookup_index].ball_tage_sel -= 1;
                }
            }
        }

        void cond_br_chain_table_update_commit (uint64_t seq_no, uint64_t pc, bool resolve_dir, bool pred_dir, std::vector<uint64_t> src_reg_info) {
            uint64_t cond_br_chain_table_lookup_index;
            uint64_t cond_br_chain_table_lookup_tag;
            bool cond_br_chain_table_lookup_hit = cond_br_chain_table_lookup(pc, cond_br_chain_table_lookup_index, cond_br_chain_table_lookup_tag);

            bool cond_br_src_reg_size_one = (src_reg_info.size() ==1);

            // train taken/non-taken for different nzcv value
            // (cannot update after resolve because need accurate architecture register result)
            if (cond_br_chain_table_lookup_hit) {
                if (cond_br_src_reg_size_one) {
                    uint64_t flag_reg_val = 0;

                    // if src_reg of cond_br is the flag register, then condition is nzcv flag,
                    // otherwise condition is nzxv flag after comparing register value with zero
                    if (src_reg_info[0] == 0x40) {
                        flag_reg_val = reg_file[src_reg_info[0]].low & (COND_BR_CHAIN_B_NODE_COND_NUM-1);
                    } else {
                        flag_reg_val = calculate_nzcv_add(reg_file[src_reg_info[0]].low, 0).to_uint64();
                    }

                    if (resolve_dir) {
                        if (b_0_0[cond_br_chain_table_lookup_index].taken[flag_reg_val] < COND_BR_CHAIN_B_NODE_TAKEN_CONF_MAX) {
                            b_0_0[cond_br_chain_table_lookup_index].taken[flag_reg_val] += 1;
                        }
                    } else {
                        if (b_0_0[cond_br_chain_table_lookup_index].taken[flag_reg_val] > COND_BR_CHAIN_B_NODE_TAKEN_CONF_MIN) {
                            b_0_0[cond_br_chain_table_lookup_index].taken[flag_reg_val] -= 1;
                        }
                    }
                }
            }

            // build cond_br chain if the cond_br is mispredicted and lookup miss in the cond_br_chain_table
            bool cond_br_mispredict = (resolve_dir != pred_dir);

            bool cond_br_chain_establish_finished = 0;
            bool cond_br_chain_establish_fail = 0;

            if (cond_br_mispredict && cond_br_src_reg_size_one && !cond_br_chain_table_lookup_hit) {

                // step 0: reset the entry before establishing the chain
                b_0_0[cond_br_chain_table_lookup_index].reset();
                alu_1_0[cond_br_chain_table_lookup_index].reset();
                load_2_0[cond_br_chain_table_lookup_index].reset();
                load_2_1[cond_br_chain_table_lookup_index].reset();
                load_3_0[cond_br_chain_table_lookup_index].reset();
                load_3_1[cond_br_chain_table_lookup_index].reset();

                // step 2: establish the chain
                // step 2.1: update b_0_0
                b_0_0[cond_br_chain_table_lookup_index].valid = 1;
                #ifdef BALL_DEBUG_PRINT
                b_0_0[cond_br_chain_table_lookup_index].pc = pc;
                #endif // BALL_DEBUG_PRINT
                b_0_0[cond_br_chain_table_lookup_index].tag = cond_br_chain_table_lookup_tag;

                uint8_t b_0_0_src_reg = src_reg_info[0];

                #ifdef BALL_DEBUG_PRINT
                #ifdef BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                if (pc == BALL_DEBUG_PRINT_PC && BALL_DEBUG_PRINT_PC_EN ||
                    BALL_DEBUG_PRINT_ALL_EN) {
                    fprintf(stdout, "seq_no: %#zx; pc: %#zx; tag: %#zx\n", seq_no, pc, cond_br_chain_table_lookup_tag);
                    print_commit_queue();
                }
                #endif // BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                #endif // BALL_DEBUG_PRINT

                // alu and load uop can be chained, when their src reg size is not more than two;
                // fp uop can be chained, when its scr reg size is only one
                bool commit_queue_can_chained[COMMIT_QUEUE_ENTRY_NUM] = {0};
                for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                    commit_queue_can_chained[i] = commit_queue[i].valid &&
                                                      (
                                                       ((commit_queue[i].inst_class == InstClass::aluInstClass) &&
                                                        commit_queue[i].src_reg_0_valid &&
                                                        (1 || commit_queue[i].src_reg_1_valid) &&
                                                        !commit_queue[i].src_reg_size_more_than_two 
                                                       ) ||
                                                       ((commit_queue[i].inst_class == InstClass::fpInstClass) &&
                                                        commit_queue[i].src_reg_0_valid &&
                                                        !commit_queue[i].src_reg_1_valid &&
                                                        !commit_queue[i].src_reg_size_more_than_two
                                                       ) ||
                                                       ((commit_queue[i].inst_class == InstClass::loadInstClass) &&
                                                        commit_queue[i].src_reg_0_valid &&
                                                        (1 || commit_queue[i].src_reg_1_valid) &&
                                                        !commit_queue[i].src_reg_size_more_than_two
                                                       )
                                                      );
                    #ifdef BALL_DEBUG_PRINT
                    #ifdef BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                    if (pc == BALL_DEBUG_PRINT_PC && BALL_DEBUG_PRINT_PC_EN ||
                        BALL_DEBUG_PRINT_ALL_EN) {
                        fprintf(stdout, "commit_queue_can_chained %#x: %#x\n", i, commit_queue_can_chained[i]);
                    }
                    #endif // BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                    #endif // BALL_DEBUG_PRINT
                }

                bool commit_queue_alu_dst_reg_match_b_0_0_src_reg[COMMIT_QUEUE_ENTRY_NUM] = {0};
                bool commit_queue_load_dst_reg_match_b_0_0_src_reg[COMMIT_QUEUE_ENTRY_NUM] = {0};
                for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                    //for fp inst, only chain for src reg of cond br
                    commit_queue_alu_dst_reg_match_b_0_0_src_reg[i] = commit_queue[i].valid && (commit_queue[i].inst_class == InstClass::aluInstClass ||
                                                                                                  commit_queue[i].inst_class == InstClass::fpInstClass) &&
                                                                        commit_queue[i].dst_reg_valid && (commit_queue[i].dst_reg == b_0_0_src_reg);
                    commit_queue_load_dst_reg_match_b_0_0_src_reg[i] = commit_queue[i].valid && (commit_queue[i].inst_class == InstClass::loadInstClass) &&
                                                                         commit_queue[i].dst_reg_valid && (commit_queue[i].dst_reg == b_0_0_src_reg);
                }

                bool commit_queue_alu_dst_reg_match_src_reg_0[COMMIT_QUEUE_ENTRY_NUM][COMMIT_QUEUE_ENTRY_NUM] = {{0}};
                bool commit_queue_alu_dst_reg_match_src_reg_1[COMMIT_QUEUE_ENTRY_NUM][COMMIT_QUEUE_ENTRY_NUM] = {{0}};
                bool commit_queue_load_dst_reg_match_src_reg_0[COMMIT_QUEUE_ENTRY_NUM][COMMIT_QUEUE_ENTRY_NUM] = {{0}};
                bool commit_queue_load_dst_reg_match_src_reg_1[COMMIT_QUEUE_ENTRY_NUM][COMMIT_QUEUE_ENTRY_NUM] = {{0}};
                for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                    for (uint8_t j = i + 1; j < COMMIT_QUEUE_ENTRY_NUM; j++) {
                        commit_queue_alu_dst_reg_match_src_reg_0[i][j] = commit_queue[i].valid && commit_queue[i].src_reg_0_valid &&
                                                                         commit_queue[j].valid && (commit_queue[j].inst_class == InstClass::aluInstClass) &&
                                                                         commit_queue[j].dst_reg_valid && (commit_queue[j].dst_reg == commit_queue[i].src_reg_0);
                        commit_queue_alu_dst_reg_match_src_reg_1[i][j] = commit_queue[i].valid && commit_queue[i].src_reg_1_valid &&
                                                                         commit_queue[j].valid && (commit_queue[j].inst_class == InstClass::aluInstClass) &&
                                                                         commit_queue[j].dst_reg_valid && (commit_queue[j].dst_reg == commit_queue[i].src_reg_1);
                        commit_queue_load_dst_reg_match_src_reg_0[i][j] = commit_queue[i].valid && commit_queue[i].src_reg_0_valid &&
                                                                          commit_queue[j].valid && (commit_queue[j].inst_class == InstClass::loadInstClass) &&
                                                                          commit_queue[j].dst_reg_valid && (commit_queue[j].dst_reg == commit_queue[i].src_reg_0);
                        commit_queue_load_dst_reg_match_src_reg_1[i][j] = commit_queue[i].valid && commit_queue[i].src_reg_1_valid &&
                                                                          commit_queue[j].valid && (commit_queue[j].inst_class == InstClass::loadInstClass) &&
                                                                          commit_queue[j].dst_reg_valid && (commit_queue[j].dst_reg == commit_queue[i].src_reg_1);
                    }
                }

                // step 2.2: update alu_1_0, load_2_0, load_3_0
                uint8_t alu_1_0_commit_queue_entry_index = 0;
                if (!cond_br_chain_establish_finished) {
                    for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                        if (commit_queue_load_dst_reg_match_b_0_0_src_reg[i] && commit_queue[i].src_reg_0_valid) {
                            if (!commit_queue_can_chained[i]) {
                                cond_br_chain_establish_finished = 1;
                                cond_br_chain_establish_fail = 1;
                            }

                            break;
                        }

                        if (commit_queue_alu_dst_reg_match_b_0_0_src_reg[i] && commit_queue[i].src_reg_0_valid) {
                            alu_1_0[cond_br_chain_table_lookup_index].valid = commit_queue_can_chained[i];
                            alu_1_0[cond_br_chain_table_lookup_index].src_1_valid = commit_queue_can_chained[i] && commit_queue[i].src_reg_1_valid;
                            alu_1_0[cond_br_chain_table_lookup_index].pc = commit_queue[i].pc & COND_BR_CHAIN_ALU_NODE_PC_MASK;
                            #ifdef BALL_DEBUG_PRINT
                            alu_1_0[cond_br_chain_table_lookup_index].pc_full = commit_queue[i].pc_full;
                            #endif // BALL_DEBUG_PRINT
                           
                            alu_1_0_commit_queue_entry_index = i;

                            if (!commit_queue_can_chained[i]) {
                                cond_br_chain_establish_finished = 1;
                                cond_br_chain_establish_fail = 1;
                            }

                            break;
                        }
                    }
                }

                #ifdef BALL_DEBUG_PRINT
                #ifdef BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                if (pc == BALL_DEBUG_PRINT_PC && BALL_DEBUG_PRINT_PC_EN) {
                    fprintf(stdout, "cond_br_chain_table_update: alu_1_0\n");
                    fprintf(stdout, "cond_br_chain_establish_finished: %#x; cond_br_chain_establish_fail: %#x\n", cond_br_chain_establish_finished, cond_br_chain_establish_fail);
                    fprintf(stdout, "alu_1_0_valid: %#x; alu_1_0_src_1_valid: %#x; alu_1_0_pc_full: %#zx; alu_1_0_pc: %#zx\n", alu_1_0[cond_br_chain_table_lookup_index].valid, alu_1_0[cond_br_chain_table_lookup_index].src_1_valid, alu_1_0[cond_br_chain_table_lookup_index].pc_full, alu_1_0[cond_br_chain_table_lookup_index].pc);
                }
                #endif // BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                #endif // BALL_DEBUG_PRINT

                uint8_t load_2_0_commit_queue_entry_index = 0;
                if (!cond_br_chain_establish_finished) {
                    if (!alu_1_0[cond_br_chain_table_lookup_index].valid) {
                        for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                            if (commit_queue_load_dst_reg_match_b_0_0_src_reg[i] && commit_queue[i].src_reg_0_valid) {
                                load_2_0[cond_br_chain_table_lookup_index].valid = commit_queue_can_chained[i];
                                load_2_0[cond_br_chain_table_lookup_index].pc = commit_queue[i].pc & COND_BR_CHAIN_LOAD_NODE_PC_MASK;
                                load_2_0[cond_br_chain_table_lookup_index].piece = commit_queue[i].piece & COND_BR_CHAIN_LOAD_NODE_PIECE_MASK;
                                #ifdef BALL_DEBUG_PRINT
                                load_2_0[cond_br_chain_table_lookup_index].pc_full = commit_queue[i].pc_full;
                                load_2_0[cond_br_chain_table_lookup_index].piece_full = commit_queue[i].piece_full;
                                #endif // BALL_DEBUG_PRINT

                                load_2_0_commit_queue_entry_index = i;

                                if (!commit_queue_can_chained[i]) {
                                    cond_br_chain_establish_finished = 1;
                                    cond_br_chain_establish_fail = 1;
                                }

                                // if load_2_0 has two src reg, then the chain is built finished,
                                // and we expect load_2_0 address can be predicted
                                if (commit_queue[i].src_reg_0_valid && commit_queue[i].src_reg_1_valid) {
                                    cond_br_chain_establish_finished = 1;
                                    cond_br_chain_establish_fail = 0;
                                }

                                break;
                            }

                            if (i == COMMIT_QUEUE_ENTRY_NUM - 1) {
                                cond_br_chain_establish_finished = 1;
                                cond_br_chain_establish_fail = 1;
                            }
                        }
                    }

                    if (alu_1_0[cond_br_chain_table_lookup_index].valid) {
                        for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                            if (commit_queue_load_dst_reg_match_src_reg_0[alu_1_0_commit_queue_entry_index][i] && commit_queue[i].src_reg_0_valid) {
                                load_2_0[cond_br_chain_table_lookup_index].valid = commit_queue_can_chained[i];
                                load_2_0[cond_br_chain_table_lookup_index].pc = commit_queue[i].pc & COND_BR_CHAIN_LOAD_NODE_PC_MASK;
                                load_2_0[cond_br_chain_table_lookup_index].piece = commit_queue[i].piece & COND_BR_CHAIN_LOAD_NODE_PIECE_MASK;
                                #ifdef BALL_DEBUG_PRINT
                                load_2_0[cond_br_chain_table_lookup_index].pc_full = commit_queue[i].pc_full;
                                load_2_0[cond_br_chain_table_lookup_index].piece_full = commit_queue[i].piece_full;
                                #endif // BALL_DEBUG_PRINT

                                load_2_0_commit_queue_entry_index = i;

                                if (!commit_queue_can_chained[i]) {
                                    cond_br_chain_establish_finished = 1;
                                    cond_br_chain_establish_fail = 1;
                                }

                                break;
                            }

                            if (i == COMMIT_QUEUE_ENTRY_NUM - 1) {
                                cond_br_chain_establish_finished = 1;
                                cond_br_chain_establish_fail = 1;
                            }
                        }
                    }
                }

                uint8_t load_3_0_commit_queue_entry_index = 0;
                if (!cond_br_chain_establish_finished) {
                    if (load_2_0[cond_br_chain_table_lookup_index].valid) {
                        for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                            if (commit_queue_load_dst_reg_match_src_reg_0[load_2_0_commit_queue_entry_index][i] && commit_queue[i].src_reg_0_valid) {
                                load_3_0[cond_br_chain_table_lookup_index].valid = commit_queue_can_chained[i];
                                load_3_0[cond_br_chain_table_lookup_index].pc = commit_queue[i].pc & COND_BR_CHAIN_LOAD_NODE_PC_MASK;
                                load_3_0[cond_br_chain_table_lookup_index].piece = commit_queue[i].piece & COND_BR_CHAIN_LOAD_NODE_PIECE_MASK;
                                #ifdef BALL_DEBUG_PRINT
                                load_3_0[cond_br_chain_table_lookup_index].pc_full = commit_queue[i].pc_full;
                                load_3_0[cond_br_chain_table_lookup_index].piece_full = commit_queue[i].piece_full;
                                #endif // BALL_DEBUG_PRINT

                                load_3_0_commit_queue_entry_index = i;

                                if (!commit_queue_can_chained[i]) {
                                    cond_br_chain_establish_finished = 1;
                                    cond_br_chain_establish_fail = 1;
                                }

                                break;
                            }
                        }
                    }
                }

                #ifdef BALL_DEBUG_PRINT
                #ifdef BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                if (pc == BALL_DEBUG_PRINT_PC && BALL_DEBUG_PRINT_PC_EN) {
                    fprintf(stdout, "cond_br_chain_table_update: load_3_0\n");
                    fprintf(stdout, "cond_br_chain_establish_finished: %#x; cond_br_chain_establish_fail: %#x\n", cond_br_chain_establish_finished, cond_br_chain_establish_fail);
                    fprintf(stdout, "load_3_0_valid: %#x; load_3_0_pc_full: %#zx; load_3_0_pc: %#zx\n", load_3_0[cond_br_chain_table_lookup_index].valid, load_3_0[cond_br_chain_table_lookup_index].pc_full, load_3_0[cond_br_chain_table_lookup_index].pc);
                }
                #endif // BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                #endif // BALL_DEBUG_PRINT

                // step 2.3: update (alu_1_0), load_2_1, load_3_1
                uint8_t load_2_1_commit_queue_entry_index = 0;
                if (!cond_br_chain_establish_finished) {
                    if (alu_1_0[cond_br_chain_table_lookup_index].src_1_valid) {
                        for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                            if (commit_queue_load_dst_reg_match_src_reg_1[alu_1_0_commit_queue_entry_index][i] && commit_queue[i].src_reg_0_valid) {
                                load_2_1[cond_br_chain_table_lookup_index].valid = commit_queue_can_chained[i];
                                load_2_1[cond_br_chain_table_lookup_index].pc = commit_queue[i].pc & COND_BR_CHAIN_LOAD_NODE_PC_MASK;
                                load_2_1[cond_br_chain_table_lookup_index].piece = commit_queue[i].piece & COND_BR_CHAIN_LOAD_NODE_PIECE_MASK;
                                #ifdef BALL_DEBUG_PRINT
                                load_2_1[cond_br_chain_table_lookup_index].pc_full = commit_queue[i].pc_full;
                                load_2_1[cond_br_chain_table_lookup_index].piece_full = commit_queue[i].piece_full;
                                #endif // BALL_DEBUG_PRINT

                                load_2_1_commit_queue_entry_index = i;

                                if (!commit_queue_can_chained[i]) {
                                    cond_br_chain_establish_finished = 1;
                                    cond_br_chain_establish_fail = 1;
                                }

                                break;
                            }

                            if (i == COMMIT_QUEUE_ENTRY_NUM - 1) {
                                cond_br_chain_establish_finished = 1;
                                cond_br_chain_establish_fail = 1;
                            }
                        }
                    }
                }

                #ifdef BALL_DEBUG_PRINT
                #ifdef BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                if (pc == BALL_DEBUG_PRINT_PC && BALL_DEBUG_PRINT_PC_EN) {
                    fprintf(stdout, "cond_br_chain_table_update: load_2_1\n");
                    fprintf(stdout, "cond_br_chain_establish_finished: %#x; cond_br_chain_establish_fail: %#x\n", cond_br_chain_establish_finished, cond_br_chain_establish_fail);
                    fprintf(stdout, "load_2_1_valid: %#x; load_2_1_pc_full: %#zx; load_2_1_pc: %#zx\n", load_2_1[cond_br_chain_table_lookup_index].valid, load_2_1[cond_br_chain_table_lookup_index].pc_full, load_2_1[cond_br_chain_table_lookup_index].pc);
                }
                #endif // BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                #endif // BALL_DEBUG_PRINT

                uint8_t load_3_1_commit_queue_entry_index = 0;
                if (!cond_br_chain_establish_finished) {
                    if (load_2_1[cond_br_chain_table_lookup_index].valid) {
                        for (uint8_t i = 0; i < COMMIT_QUEUE_ENTRY_NUM; i++) {
                            if (commit_queue_load_dst_reg_match_src_reg_0[load_2_1_commit_queue_entry_index][i] && commit_queue[i].src_reg_0_valid) {
                                load_3_1[cond_br_chain_table_lookup_index].valid = commit_queue_can_chained[i];
                                load_3_1[cond_br_chain_table_lookup_index].pc = commit_queue[i].pc & COND_BR_CHAIN_LOAD_NODE_PC_MASK;
                                load_3_1[cond_br_chain_table_lookup_index].piece = commit_queue[i].piece & COND_BR_CHAIN_LOAD_NODE_PIECE_MASK;
                                #ifdef BALL_DEBUG_PRINT
                                load_3_1[cond_br_chain_table_lookup_index].pc_full = commit_queue[i].pc_full;
                                load_3_1[cond_br_chain_table_lookup_index].piece_full = commit_queue[i].piece_full;
                                #endif // BALL_DEBUG_PRINT

                                load_3_1_commit_queue_entry_index = i;

                                if (!commit_queue_can_chained[i]) {
                                    cond_br_chain_establish_finished = 1;
                                    cond_br_chain_establish_fail = 1;
                                }

                                break;
                            }
                        }
                    }
                }

                #ifdef BALL_DEBUG_PRINT
                #ifdef BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                if (pc == BALL_DEBUG_PRINT_PC && BALL_DEBUG_PRINT_PC_EN) {
                    fprintf(stdout, "cond_br_chain_table_update: load_3_1\n");
                    fprintf(stdout, "cond_br_chain_establish_finished: %#x; cond_br_chain_establish_fail: %#x\n", cond_br_chain_establish_finished, cond_br_chain_establish_fail);
                    fprintf(stdout, "load_3_1_valid: %#x; load_3_1_pc_full: %#zx; load_3_1_pc: %#zx\n", load_3_1[cond_br_chain_table_lookup_index].valid, load_3_1[cond_br_chain_table_lookup_index].pc_full, load_3_1[cond_br_chain_table_lookup_index].pc);
                }
                #endif // BALL_DEBUG_PRINT_BR_COND_CHAIN_UPDATE
                #endif // BALL_DEBUG_PRINT

                cond_br_chain_establish_finished = 1;

                // step 3: reset the entry if the chain is established unsuccessfully
                if (cond_br_chain_establish_fail) {
                    b_0_0[cond_br_chain_table_lookup_index].reset();
                    alu_1_0[cond_br_chain_table_lookup_index].reset();
                    load_2_0[cond_br_chain_table_lookup_index].reset();
                    load_2_1[cond_br_chain_table_lookup_index].reset();
                    load_3_0[cond_br_chain_table_lookup_index].reset();
                    load_3_1[cond_br_chain_table_lookup_index].reset();
                }

                // step 4: update pc of load_2_0, load_2_1, load_3_0, load_3_1
                //         if chain is established successfully
                //for fp inst, only chain for alu_1_0
                if (!cond_br_chain_establish_fail) {
                    if (alu_1_0[cond_br_chain_table_lookup_index].valid) {
                        alu_pattern_table_update_commit_by_cond_br(alu_1_0[cond_br_chain_table_lookup_index].pc);
                    }
                    if (load_2_0[cond_br_chain_table_lookup_index].valid) {
                        load_pattern_table_update_commit_by_cond_br(load_2_0[cond_br_chain_table_lookup_index].pc, load_2_0[cond_br_chain_table_lookup_index].piece);
                    }
                    if (load_2_1[cond_br_chain_table_lookup_index].valid) {
                        load_pattern_table_update_commit_by_cond_br(load_2_1[cond_br_chain_table_lookup_index].pc, load_2_1[cond_br_chain_table_lookup_index].piece);
                    }
                    if (load_3_0[cond_br_chain_table_lookup_index].valid) {
                        load_pattern_table_update_commit_by_cond_br(load_3_0[cond_br_chain_table_lookup_index].pc, load_3_0[cond_br_chain_table_lookup_index].piece);
                    }
                    if (load_3_1[cond_br_chain_table_lookup_index].valid) {
                        load_pattern_table_update_commit_by_cond_br(load_3_1[cond_br_chain_table_lookup_index].pc, load_3_1[cond_br_chain_table_lookup_index].piece);
                    }
                }
            }
        }

        uint64_t get_load_pattern_table_lookup_pc (uint64_t pc, uint8_t piece, bool pc_right_shift_2) {
            uint64_t pc_after_right_shift = (!COMMIT_QUEUE_PC_RIGHT_SHIFT_2 || pc_right_shift_2) ? (pc >> 2) : pc;
            // LOAD_PATTERN_TABLE_SET 1024
            //return (pc_after_right_shift << BALL_MAX_LOAD_PIECE_NUM_BIT_WIDTH) | (piece & BALL_MAX_LOAD_PIECE_NUM_BIT_MASK);

            // LOAD_PATTERN_TABLE_SET 512
            // exchange pc_after_right_shift[0] and pc_after_right_shift[8]
            uint64_t pc_0 = (pc_after_right_shift >> LOAD_PATTERN_TABLE_SET_PC_WIDTH) & 1;
            uint64_t pc_1 = ((pc_after_right_shift & LOAD_PATTERN_TABLE_SET_PC_WIDTH_MASK) >> 1) << 1;
            uint64_t pc_2 = ((pc_after_right_shift & LOAD_PATTERN_TABLE_SET_PC_WIDTH_MASK) & 1) << LOAD_PATTERN_TABLE_SET_PC_WIDTH;
            uint64_t pc_3 = ((pc_after_right_shift >> LOAD_PATTERN_TABLE_SET_PC_WIDTH) >> 1) << (LOAD_PATTERN_TABLE_SET_PC_WIDTH+1);
            uint64_t pc_final = pc_3 | pc_2 | pc_1 | pc_0;

            return (pc_final << BALL_MAX_LOAD_PIECE_NUM_BIT_WIDTH) | (piece & BALL_MAX_LOAD_PIECE_NUM_BIT_MASK);

        }

        uint64_t get_load_vtage_lookup_pc (uint64_t pc, uint8_t piece) {
            return ((pc >> 2) << BALL_MAX_LOAD_PIECE_NUM_BIT_WIDTH);
        }

        bool load_pattern_table_lookup(uint64_t pc, uint64_t piece, bool pc_right_shift_2, uint64_t& load_pattern_table_lookup_index, uint64_t& load_pattern_table_lookup_tag) {
            uint64_t load_pattern_table_lookup_pc = get_load_pattern_table_lookup_pc(pc, piece, pc_right_shift_2);

            load_pattern_table_lookup_index = load_pattern_table_lookup_pc % LOAD_PATTERN_TABLE_SET;
            load_pattern_table_lookup_tag = (load_pattern_table_lookup_pc / LOAD_PATTERN_TABLE_SET) & ((1 << LOAD_PATTERN_TABLE_TAG_WIDTH) - 1);

            bool load_pattern_table_lookup_hit = 0;
            if (load_pattern_table[load_pattern_table_lookup_index].valid &&
                (load_pattern_table[load_pattern_table_lookup_index].tag == load_pattern_table_lookup_tag) &&
                (piece <= BALL_MAX_LOAD_PIECE_NUM)) {
                load_pattern_table_lookup_hit = 1;
            }

            return load_pattern_table_lookup_hit;
        }

        void load_pattern_table_update_commit_by_cond_br (uint64_t pc, uint8_t piece) {
            uint64_t load_pattern_table_lookup_index;
            uint64_t load_pattern_table_lookup_tag;
            bool load_pattern_table_lookup_hit = load_pattern_table_lookup(pc, piece, 0, load_pattern_table_lookup_index, load_pattern_table_lookup_tag);

            if(!load_pattern_table_lookup_hit) {
                load_pattern_table[load_pattern_table_lookup_index].valid = 1;
                #ifdef BALL_DEBUG_PRINT
                load_pattern_table[load_pattern_table_lookup_index].pc = COMMIT_QUEUE_PC_RIGHT_SHIFT_2 ? (pc << COMMIT_QUEUE_PC_RIGHT_SHIFT_WIDTH) : pc;
                load_pattern_table[load_pattern_table_lookup_index].piece = piece;
                #endif // BALL_DEBUG_PRINT
                load_pattern_table[load_pattern_table_lookup_index].tag = load_pattern_table_lookup_tag;
                load_pattern_table[load_pattern_table_lookup_index].mem_sz = 0;
                load_pattern_table[load_pattern_table_lookup_index].last_mem_va = 0;
                load_pattern_table[load_pattern_table_lookup_index].const_stride = 0;
                load_pattern_table[load_pattern_table_lookup_index].const_stride_conf = 0;
                load_pattern_table[load_pattern_table_lookup_index].wrap_mem_va = 0;
                load_pattern_table[load_pattern_table_lookup_index].wrap_target_mem_va = 0;
                load_pattern_table[load_pattern_table_lookup_index].wrap_conf = 0;
                load_pattern_table[load_pattern_table_lookup_index].offset = 0;
                load_pattern_table[load_pattern_table_lookup_index].offset_conf = 0;
                load_pattern_table[load_pattern_table_lookup_index].vtage_predict_addr_valid = 0;
                load_pattern_table[load_pattern_table_lookup_index].vtage_predict_addr = 0;
                load_pattern_table[load_pattern_table_lookup_index].in_flight_cnt_valid = 0;
                load_pattern_table[load_pattern_table_lookup_index].in_flight_cnt = 0;
            }
        }

        void load_pattern_table_update_fetch_by_load (uint64_t seq_no, uint64_t pc, uint8_t piece) {
            uint64_t load_pattern_table_lookup_index;
            uint64_t load_pattern_table_lookup_tag;
            bool load_pattern_table_lookup_hit = load_pattern_table_lookup(pc, piece, 1, load_pattern_table_lookup_index, load_pattern_table_lookup_tag);

            load_pred_time_hist_t load_pred_time_hist = {};

            uint64_t load_pc = get_load_vtage_lookup_pc(pc, piece);

            bool load_make_prediction = 0;
            uint64_t load_predicted_addr = 0;

            if (load_pattern_table_lookup_hit) {

                if (load_pattern_table[load_pattern_table_lookup_index].in_flight_cnt < (MAXINFLIGHT-1)) {
                    load_pattern_table[load_pattern_table_lookup_index].in_flight_cnt += 1;
                }

                load_make_prediction = getPrediction(seq_no, load_pc, piece, load_predicted_addr, 1);

                load_pred_time_hist.make_prediction = load_make_prediction;
                load_pred_time_hist.predicted_addr = load_predicted_addr & BALL_MEM_VA_MASK;

                if (load_make_prediction) {
                    load_pattern_table[load_pattern_table_lookup_index].vtage_predict_addr_valid = 1;
                    load_pattern_table[load_pattern_table_lookup_index].vtage_predict_addr = load_predicted_addr & BALL_MEM_VA_MASK;
                } else {
                    load_pattern_table[load_pattern_table_lookup_index].vtage_predict_addr_valid = 0;
                }
            } else {
                load_make_prediction = getPrediction(seq_no, load_pc, piece, load_predicted_addr, 0);
            }

            if (piece <= BALL_MAX_LOAD_PIECE_NUM) {
                load_pred_time_hist.load_pattern_table_hit = load_pattern_table_lookup_hit;
                load_pred_time_histories.emplace(seq_no, load_pred_time_hist);
            }
        }

        void load_pattern_table_update_commit_by_load (uint64_t seq_no, uint64_t pc, uint8_t piece, uint64_t mem_va, uint64_t mem_sz, std::vector<uint64_t> src_reg_info) {
            uint64_t load_pattern_table_lookup_index;
            uint64_t load_pattern_table_lookup_tag;
            bool load_pattern_table_lookup_hit = load_pattern_table_lookup(pc, piece, 1, load_pattern_table_lookup_index, load_pattern_table_lookup_tag);

            if (load_pattern_table_lookup_hit) {
                load_pattern_table[load_pattern_table_lookup_index].mem_sz = (static_cast<uint8_t>(std::ceil(std::log2(mem_sz)))) & ((1 << LOAD_PATTERN_TABLE_MEM_SZ_WIDTH) - 1);

                uint64_t current_addr = mem_va;
                uint64_t last_addr = load_pattern_table[load_pattern_table_lookup_index].last_mem_va;
                assert(current_addr <= BALL_MEM_VA_MASK);
                assert(last_addr <= BALL_MEM_VA_MASK);

                // const stride training
                int64_t last_stride = load_pattern_table[load_pattern_table_lookup_index].const_stride;
                assert((last_stride <= LOAD_PATTERN_TABLE_CONST_STRIDE_MAX) && (last_stride >= LOAD_PATTERN_TABLE_CONST_STRIDE_MIN));
                int64_t current_stride = (current_addr - last_addr);
                bool current_stride_valid = (current_stride <= LOAD_PATTERN_TABLE_CONST_STRIDE_MAX) &&
                                            (current_stride >= LOAD_PATTERN_TABLE_CONST_STRIDE_MIN);
                bool wrap_mem_va_valid = (last_addr >> LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH) == (current_addr >> LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH);
                bool same_stride = (last_stride == current_stride) && current_stride_valid;
                bool diff_stride_direction = ((last_stride < 0) && (current_stride >= 0) ||
                                              (last_stride > 0) && (current_stride <= 0) ||
                                              (last_stride == 0) && (current_stride != 0)) && current_stride_valid;

                bool same_wrap_pattern = (load_pattern_table[load_pattern_table_lookup_index].wrap_mem_va == (last_addr & LOAD_PATTERN_TABLE_WRAP_ADDR_MASK)) &&
                                         (load_pattern_table[load_pattern_table_lookup_index].wrap_target_mem_va == (current_addr & LOAD_PATTERN_TABLE_WRAP_ADDR_MASK));

                if (same_stride) {
                    if (load_pattern_table[load_pattern_table_lookup_index].const_stride_conf < LOAD_PATTERN_TABLE_CONST_STRIDE_CONF_MAX) {
                        load_pattern_table[load_pattern_table_lookup_index].const_stride_conf += 1;
                    }
                } else {
                    if (load_pattern_table[load_pattern_table_lookup_index].const_stride_conf == 0) {
                        if (current_stride_valid) {
                            load_pattern_table[load_pattern_table_lookup_index].const_stride = current_stride;
                            load_pattern_table[load_pattern_table_lookup_index].const_stride_conf += 1;
                        }
                    } else {
                        if (diff_stride_direction && !same_wrap_pattern) {
                           load_pattern_table[load_pattern_table_lookup_index].const_stride_conf -= 1;
                        }
                    }
                }

                if (diff_stride_direction) {
                    if (same_wrap_pattern) {
                        if (load_pattern_table[load_pattern_table_lookup_index].wrap_conf < LOAD_PATTERN_TABLE_WRAP_CONF_MAX) {
                            load_pattern_table[load_pattern_table_lookup_index].wrap_conf += 1;
                        }
                    } else {
                        if (load_pattern_table[load_pattern_table_lookup_index].wrap_conf == 0) {
                            if (wrap_mem_va_valid) {
                                load_pattern_table[load_pattern_table_lookup_index].wrap_mem_va = (last_addr & LOAD_PATTERN_TABLE_WRAP_ADDR_MASK);
                                load_pattern_table[load_pattern_table_lookup_index].wrap_target_mem_va = (current_addr & LOAD_PATTERN_TABLE_WRAP_ADDR_MASK);
                                load_pattern_table[load_pattern_table_lookup_index].wrap_conf = 1;
                            }
                        } else {
                            load_pattern_table[load_pattern_table_lookup_index].wrap_conf -= 1;
                        }
                    }
                }

                // offset training
                // there is load uop with no src reg in the web_19_trace, so need check src_reg_info.size()
                if (src_reg_info.size()) {
                    int64_t current_offset = mem_va - (reg_file[src_reg_info[0]].low & BALL_MEM_VA_MASK);
                    bool current_offset_valid = (current_offset <= LOAD_PATTERN_TABLE_OFFSET_MAX) &&
                                                (current_offset >= LOAD_PATTERN_TABLE_OFFSET_MIN);
                    bool same_offset = (load_pattern_table[load_pattern_table_lookup_index].offset == current_offset) && current_offset_valid;
                    assert((load_pattern_table[load_pattern_table_lookup_index].offset <= LOAD_PATTERN_TABLE_OFFSET_MAX) &&
                           (load_pattern_table[load_pattern_table_lookup_index].offset >= LOAD_PATTERN_TABLE_OFFSET_MIN));
                    if (same_offset) {
                        if (load_pattern_table[load_pattern_table_lookup_index].offset_conf < LOAD_PATTERN_TABLE_OFFSET_CONF_MAX) {
                            load_pattern_table[load_pattern_table_lookup_index].offset_conf += 1;
                        }
                    } else {
                        if (load_pattern_table[load_pattern_table_lookup_index].offset_conf == 0) {
                            if (current_offset_valid) {
                                load_pattern_table[load_pattern_table_lookup_index].offset = current_offset;
                                load_pattern_table[load_pattern_table_lookup_index].offset_conf += 1;
                            }
                        } else {
                            load_pattern_table[load_pattern_table_lookup_index].offset_conf -= 1;
                        }
                    }
                }

                // update last_mem_va
                load_pattern_table[load_pattern_table_lookup_index].last_mem_va = mem_va;

                if (load_pred_time_histories.find(seq_no) != load_pred_time_histories.end()) {
                    if (load_pred_time_histories.at(seq_no).load_pattern_table_hit) {
                        load_pattern_table[load_pattern_table_lookup_index].in_flight_cnt_valid = 1;

                        if (load_pattern_table[load_pattern_table_lookup_index].in_flight_cnt > 0) {
                            load_pattern_table[load_pattern_table_lookup_index].in_flight_cnt -= 1;
                        }
                    }
                }
            }
        }

        void clear_load_pattern_table_vtage_predict_addr_valid(uint64_t pc, uint8_t piece) {
            uint64_t load_pattern_table_lookup_index;
            uint64_t load_pattern_table_lookup_tag;
            bool load_pattern_table_lookup_hit = load_pattern_table_lookup(pc, 0, piece, load_pattern_table_lookup_index, load_pattern_table_lookup_tag);

            load_pattern_table[load_pattern_table_lookup_index].vtage_predict_addr_valid = 0;
        }

        LoadPatternTableEntry read_load_pattern_table(uint64_t pc, uint8_t piece) {
            uint64_t load_pattern_table_lookup_index;
            uint64_t load_pattern_table_lookup_tag;
            bool load_pattern_table_lookup_hit = load_pattern_table_lookup(pc, piece, 0, load_pattern_table_lookup_index, load_pattern_table_lookup_tag);

            LoadPatternTableEntry load_pattern_table_entry = load_pattern_table[load_pattern_table_lookup_index];

            assert(load_pattern_table_entry.tag <= ((1 << LOAD_PATTERN_TABLE_TAG_WIDTH) - 1));
            assert(load_pattern_table_entry.mem_sz <= ((1 << LOAD_PATTERN_TABLE_MEM_SZ_WIDTH) - 1));
            assert(load_pattern_table_entry.last_mem_va <= BALL_MEM_VA_MASK);
            assert(load_pattern_table_entry.const_stride <= LOAD_PATTERN_TABLE_CONST_STRIDE_MAX);
            assert(load_pattern_table_entry.const_stride >= LOAD_PATTERN_TABLE_CONST_STRIDE_MIN);
            assert(load_pattern_table_entry.const_stride_conf <= LOAD_PATTERN_TABLE_CONST_STRIDE_CONF_MAX);
            assert(load_pattern_table_entry.wrap_mem_va <= LOAD_PATTERN_TABLE_WRAP_ADDR_MASK);
            assert(load_pattern_table_entry.wrap_target_mem_va <= LOAD_PATTERN_TABLE_WRAP_ADDR_MASK);
            assert(load_pattern_table_entry.wrap_conf <= LOAD_PATTERN_TABLE_WRAP_CONF_MAX);
            assert(load_pattern_table_entry.offset <= LOAD_PATTERN_TABLE_OFFSET_MAX);
            assert(load_pattern_table_entry.offset >= LOAD_PATTERN_TABLE_OFFSET_MIN);
            assert(load_pattern_table_entry.offset_conf <= LOAD_PATTERN_TABLE_OFFSET_CONF_MAX);
            assert(load_pattern_table_entry.vtage_predict_addr <= BALL_MEM_VA_MASK);
            assert(load_pattern_table_entry.in_flight_cnt <= (MAXINFLIGHT-1));

            // recover higher bits of wrap_mem_va and wrap_target_mem_va from last_mem_va
            load_pattern_table_entry.wrap_mem_va = ((load_pattern_table_entry.last_mem_va >> LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH) << LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH) |
                                                   load_pattern_table_entry.wrap_mem_va;
            load_pattern_table_entry.wrap_target_mem_va = ((load_pattern_table_entry.last_mem_va >> LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH) << LOAD_PATTERN_TABLE_WRAP_ADDR_WIDTH) |
                                                   load_pattern_table_entry.wrap_target_mem_va;

            return load_pattern_table_entry;
        }

        uint64_t get_alu_pattern_table_lookup_pc (uint64_t pc, bool pc_right_shift_2) {
            return (!COMMIT_QUEUE_PC_RIGHT_SHIFT_2 || pc_right_shift_2) ? (pc >> 2) : pc;
        }

        bool alu_pattern_table_lookup(uint64_t pc, bool pc_right_shift_2, uint64_t& alu_pattern_table_lookup_index, uint64_t& alu_pattern_table_lookup_tag) {
            uint64_t alu_pattern_table_lookup_pc = get_alu_pattern_table_lookup_pc(pc, pc_right_shift_2);

            alu_pattern_table_lookup_index = alu_pattern_table_lookup_pc % ALU_PATTERN_TABLE_SET;
            alu_pattern_table_lookup_tag = (alu_pattern_table_lookup_pc / ALU_PATTERN_TABLE_SET) & ((1 << ALU_PATTERN_TABLE_TAG_WIDTH) - 1);

            bool alu_pattern_table_lookup_hit = 0;
            if (alu_pattern_table[alu_pattern_table_lookup_index].valid &&
                (alu_pattern_table[alu_pattern_table_lookup_index].tag == alu_pattern_table_lookup_tag)) {
                alu_pattern_table_lookup_hit = 1;
            }

            return alu_pattern_table_lookup_hit;
        }

        void alu_pattern_table_update_commit_by_cond_br (uint64_t pc) {
            uint64_t alu_pattern_table_lookup_index;
            uint64_t alu_pattern_table_lookup_tag;
            bool alu_pattern_table_lookup_hit = alu_pattern_table_lookup(pc, 0, alu_pattern_table_lookup_index, alu_pattern_table_lookup_tag);

            if(!alu_pattern_table_lookup_hit) {
                alu_pattern_table[alu_pattern_table_lookup_index].valid = 1;
                #ifdef BALL_DEBUG_PRINT
                alu_pattern_table[alu_pattern_table_lookup_index].pc = COMMIT_QUEUE_PC_RIGHT_SHIFT_2 ? (pc << COMMIT_QUEUE_PC_RIGHT_SHIFT_WIDTH) : pc;
                #endif // BALL_DEBUG_PRINT
                alu_pattern_table[alu_pattern_table_lookup_index].tag = alu_pattern_table_lookup_tag;
                alu_pattern_table[alu_pattern_table_lookup_index].cmp_conf = 0;
                alu_pattern_table[alu_pattern_table_lookup_index].fcmp_zero_conf = 0;
            }
        }

        void alu_pattern_table_update_commit_by_alu (uint64_t pc, std::vector<uint64_t> src_reg_info, std::optional<uint64_t> dst_reg_info, std::optional<uint64_t> dst_reg_value) {
            uint64_t alu_pattern_table_lookup_index;
            uint64_t alu_pattern_table_lookup_tag;
            bool alu_pattern_table_lookup_hit = alu_pattern_table_lookup(pc, 1, alu_pattern_table_lookup_index, alu_pattern_table_lookup_tag);

            bool alu_has_one_src_reg = (src_reg_info.size() == 1);
            bool alu_has_two_src_reg = (src_reg_info.size() == 2);
            bool alu_has_dst_reg = (dst_reg_info.has_value());

            bool src_reg_0_is_fp = 0;
            if (alu_has_one_src_reg || alu_has_two_src_reg) {
                src_reg_0_is_fp = (src_reg_info[0] >= FP_REG_MIN_IND) && (src_reg_info[0] <= FP_REG_MAX_IND);
            }
            
            bool dst_is_flag = 0;
            if (alu_has_dst_reg) {
                dst_is_flag = (dst_reg_info.value() == FLAG_REG_IND);
            }

            if (alu_pattern_table_lookup_hit && alu_has_dst_reg && dst_is_flag) {
                if (alu_has_two_src_reg) {
                    uint64_t a = reg_file[src_reg_info[0]].low;
                    uint64_t b = reg_file[src_reg_info[1]].low;
                    uint64_t res = dst_reg_value.value();

                    bool is_cmp = (calculate_nzcv_sub(a, b) == NZCV::from_uint64(res));

                    if (is_cmp) {
                        if (alu_pattern_table[alu_pattern_table_lookup_index].cmp_conf < ALU_PATTERN_TABLE_CMP_CONF_MAX) {
                            alu_pattern_table[alu_pattern_table_lookup_index].cmp_conf += 1;
                        }
                    } else {
                        if (alu_pattern_table[alu_pattern_table_lookup_index].cmp_conf != 0) {
                            alu_pattern_table[alu_pattern_table_lookup_index].cmp_conf -= 1;
                        }
                    }
                }

                if (alu_has_one_src_reg && src_reg_0_is_fp) {
                    uint64_t a = reg_file[src_reg_info[0]].low;
                    uint64_t b = 0;
                    uint64_t res = dst_reg_value.value();

                    bool is_fcmp_zero = (calculate_nzcv_fcmp_zero(a) == NZCV::from_uint64(res));

                    if (is_fcmp_zero) {
                        if (alu_pattern_table[alu_pattern_table_lookup_index].fcmp_zero_conf < ALU_PATTERN_TABLE_FCMP_ZERO_CONF_MAX) {
                            alu_pattern_table[alu_pattern_table_lookup_index].fcmp_zero_conf += 1;
                        }
                    } else {
                        if (alu_pattern_table[alu_pattern_table_lookup_index].fcmp_zero_conf != 0) {
                            alu_pattern_table[alu_pattern_table_lookup_index].fcmp_zero_conf -= 1;
                        }
                    }
                }
            }

        }

        ALUPatternTableEntry read_alu_pattern_table(uint64_t pc) {
            uint64_t alu_pattern_table_lookup_index;
            uint64_t alu_pattern_table_lookup_tag;
            bool alu_pattern_table_lookup_hit = alu_pattern_table_lookup(pc, 0, alu_pattern_table_lookup_index, alu_pattern_table_lookup_tag);

            ALUPatternTableEntry alu_pattern_table_entry = alu_pattern_table[alu_pattern_table_lookup_index];

            assert(alu_pattern_table_entry.tag <= ((1 << LOAD_PATTERN_TABLE_TAG_WIDTH) - 1));
            assert(alu_pattern_table_entry.cmp_conf <= ALU_PATTERN_TABLE_CMP_CONF_MAX);
            assert(alu_pattern_table_entry.fcmp_zero_conf <= ALU_PATTERN_TABLE_FCMP_ZERO_CONF_MAX);

            return alu_pattern_table_entry;
        }

        void bp_data_cache_update_commit(uint64_t seq_no, uint64_t pc, uint8_t piece, uint64_t mem_va, uint64_t mem_sz, InstClass inst_class, std::vector<uint64_t> src_reg_info, std::optional<uint64_t> dst_reg_value) {

            uint64_t update_time = mem_sz / BP_DATA_CACHE_LINE_GRANULE_SIZE;
            if (update_time == 0) {
                update_time = 1;
            }

            bool is_store = (inst_class == InstClass::storeInstClass);
            bool is_load = (inst_class == InstClass::loadInstClass);


            for (uint8_t update_time_index = 0; update_time_index < update_time; update_time_index++) {
                uint64_t mem_va_update = mem_va + update_time_index * BP_DATA_CACHE_LINE_GRANULE_SIZE;
                uint64_t mem_va_offset = mem_va_update % BP_DATA_CACHE_LINE_SIZE;
                uint64_t mem_va_granule = mem_va_offset / BP_DATA_CACHE_LINE_GRANULE_SIZE;
                uint64_t mem_va_set = (mem_va_update / BP_DATA_CACHE_LINE_SIZE) % BP_DATA_CACHE_SET;
                uint64_t mem_va_tag = (mem_va_update / (BP_DATA_CACHE_SET * BP_DATA_CACHE_LINE_SIZE));
                uint64_t update_data = 0;

                bool same_cacheline_first_update = (mem_va_update == mem_va);
                bool cross_cacheline_first_update = ((mem_va_update / BP_DATA_CACHE_LINE_SIZE) != (mem_va / BP_DATA_CACHE_LINE_SIZE)) &&
                                                    (((mem_va_update % BP_DATA_CACHE_LINE_SIZE) / BP_DATA_CACHE_LINE_GRANULE_SIZE) == 0);

                bool update_data_cache = 0;
                if (is_store) {
                    // if store has two source regs, then 2nd source reg is store data
                    // if store has three source regs, then 3nd source reg is store data
                    if (src_reg_info.size() == 2) {
                        update_data_cache = 1;
                        if (update_time_index == 0) {
                            update_data = reg_file[src_reg_info[1]].low & (0x00000000FFFFFFFF);
                        } else if (update_time_index == 1) {
                            update_data = reg_file[src_reg_info[1]].low >> 32;
                        } else if (update_time_index == 2) {
                            update_data = reg_file[src_reg_info[1]].high & (0x00000000FFFFFFFF);
                        } else if (update_time_index == 3) {
                            update_data = reg_file[src_reg_info[1]].high >> 32;
                        }
                    } else if (src_reg_info.size() == 3) {
                        update_data_cache = 1;
                        if (update_time_index == 0) {
                            update_data = reg_file[src_reg_info[2]].low & (0x00000000FFFFFFFF);
                        } else if (update_time_index == 1) {
                            update_data = reg_file[src_reg_info[2]].low >> 32;
                        } else if (update_time_index == 2) {
                            update_data = reg_file[src_reg_info[2]].high & (0x00000000FFFFFFFF);
                        } else if (update_time_index == 3) {
                            update_data = reg_file[src_reg_info[2]].high >> 32;
                        }
                    }
                } else if (is_load) {
                    if (dst_reg_value.has_value()) {
                        update_data_cache = 1;
                        if (update_time_index == 0) {
                            update_data = dst_reg_value.value() & (0x00000000FFFFFFFF);
                        } else if (update_time_index == 1) {
                            update_data = dst_reg_value.value() >> 32;
                        }
                    }
                }

                int64_t bp_data_cache_hit_way = -1;
                int64_t bp_data_cache_alloc_way = -1;
                uint64_t bp_data_cache_alloc_way_age = 0;

                if (update_data_cache) {
                    bool bp_data_cacheline_valid[BP_DATA_CACHE_SET][BP_DATA_CACHE_WAY];
                    for (uint64_t i = 0; i < BP_DATA_CACHE_SET; i++) {
                        for (uint64_t j = 0; j < BP_DATA_CACHE_WAY; j++) {
                            bool _bp_data_cacheline_valid = 0;
                            for(uint64_t k = 0; k < BP_DATA_CACHE_LINE_GRANULE_NUM; k++) {
                                _bp_data_cacheline_valid |= bp_data_cache[i][j].valid[k];
                            }
                            bp_data_cacheline_valid[i][j] = _bp_data_cacheline_valid;
                        }
                    }

                    // hit
                    for (uint64_t i = 0; i < BP_DATA_CACHE_WAY; i++) {
                        if (bp_data_cacheline_valid[mem_va_set][i] && (mem_va_tag == bp_data_cache[mem_va_set][i].tag)) {
                            bp_data_cache_hit_way = i;
                            break;
                        }
                    }

                    if (bp_data_cache_hit_way > -1) {
                        uint8_t hit_way_age = bp_data_cache[mem_va_set][bp_data_cache_hit_way].age;
                        if (same_cacheline_first_update || cross_cacheline_first_update) {
                            for (uint64_t i = 0; i < BP_DATA_CACHE_WAY; i++) {
                                if (bp_data_cache[mem_va_set][i].age < hit_way_age) {
                                    bp_data_cache[mem_va_set][i].age += 1;
                                }
                            }
                        }

                        bp_data_cache[mem_va_set][bp_data_cache_hit_way].valid[mem_va_granule] = 1;
                        bp_data_cache[mem_va_set][bp_data_cache_hit_way].data[mem_va_granule] = update_data;

                        if (same_cacheline_first_update || cross_cacheline_first_update) {
                            bp_data_cache[mem_va_set][bp_data_cache_hit_way].age = 0;
                        }

                        assert (bp_data_cache_hit_way >= 0 &&bp_data_cache_hit_way < BP_DATA_CACHE_WAY);
                    }

                    // miss
                    if (bp_data_cache_hit_way == -1) {
                        // find first invalid way
                        for(uint64_t i = 0; i < BP_DATA_CACHE_WAY; i++) {
                            if (!bp_data_cacheline_valid[mem_va_set][i]) {
                                bp_data_cache_alloc_way = i;
                                break;
                            }

                        }

                        // if all way are valid, then find the victim way by oldest age
                        if (bp_data_cache_alloc_way == -1) {
                            for(uint64_t i = 0; i < BP_DATA_CACHE_WAY; i++) {
                                if (bp_data_cache[mem_va_set][i].age == BP_DATA_CACHE_WAY-1) {
                                    bp_data_cache_alloc_way = i;
                                    break;
                                }
                            }

                        }

                        for (uint64_t i = 0; i < BP_DATA_CACHE_WAY; i++) {
                            if (bp_data_cache[mem_va_set][i].age < BP_DATA_CACHE_WAY-1) {
                                bp_data_cache[mem_va_set][i].age += 1;
                            }
                        }

                        assert(bp_data_cache_alloc_way >= 0 &&
                               bp_data_cache_alloc_way <= (BP_DATA_CACHE_WAY - 1));

                        for(uint64_t i = 0; i < BP_DATA_CACHE_LINE_GRANULE_NUM; i++) {
                            bp_data_cache[mem_va_set][bp_data_cache_alloc_way].valid[i] = 0;
                        }

                        bp_data_cache[mem_va_set][bp_data_cache_alloc_way].valid[mem_va_granule] = 1;
                        bp_data_cache[mem_va_set][bp_data_cache_alloc_way].data[mem_va_granule] = update_data;
                        bp_data_cache[mem_va_set][bp_data_cache_alloc_way].tag = mem_va_tag;
                        bp_data_cache[mem_va_set][bp_data_cache_alloc_way].age = 0;

                        assert (mem_va_set >= 0 && mem_va_set < BP_DATA_CACHE_SET);
                        assert (bp_data_cache_alloc_way >= 0 &&bp_data_cache_alloc_way < BP_DATA_CACHE_WAY);
                        assert (mem_va_granule >= 0 && mem_va_granule < BP_DATA_CACHE_LINE_GRANULE_NUM);
                    }
                }
            }
        }

        void predict_load_data(CondBRChainLoadNode& load, uint64_t child_load_data_valid, uint64_t child_load_data, uint64_t& load_addr, bool& load_data_valid, uint64_t& load_data) {
            LoadPatternTableEntry load_entry = read_load_pattern_table(load.pc, load.piece);
            bool load_valid = load_entry.valid && load.valid;

            bool load_addr_const_valid = load_entry.in_flight_cnt_valid && (load_entry.const_stride_conf == LOAD_PATTERN_TABLE_CONST_STRIDE_CONF_MAX) && BALL_LOAD_CONST_EN;
            bool load_addr_offset_valid = (load_entry.offset_conf == LOAD_PATTERN_TABLE_OFFSET_CONF_MAX) && child_load_data_valid && BALL_LOAD_OFFSET_EN;
            bool load_addr_vtage_valid = load_entry.vtage_predict_addr_valid && BALL_LOAD_VTAGE_EN;
            if (load_addr_vtage_valid) {
                clear_load_pattern_table_vtage_predict_addr_valid(load.pc, load.piece);
            }
            bool load_addr_valid = load_addr_const_valid || load_addr_offset_valid || load_addr_vtage_valid;

            uint64_t load_addr_const = load_entry.last_mem_va + load_entry.in_flight_cnt * load_entry.const_stride;
            uint64_t load_addr_offset = child_load_data + load_entry.offset;
            uint64_t load_addr_vtage = load_entry.vtage_predict_addr;

            // Below code can be optimized considering sign carefully
            uint64_t load_wrap_mem_va = load_entry.wrap_mem_va;
            uint64_t load_wrap_target_mem_va = load_entry.wrap_target_mem_va;
            uint64_t load_wrap_distance = (load_wrap_mem_va > load_wrap_target_mem_va) ? (load_wrap_mem_va - load_wrap_target_mem_va) :
                                                (load_wrap_mem_va < load_wrap_target_mem_va) ? (load_wrap_target_mem_va - load_wrap_mem_va) : 1;
            bool load_wrap_back = (load_entry.const_stride > 0) && (load_addr_const > load_wrap_mem_va);
            bool load_wrap_forward = (load_entry.const_stride < 0) && (load_addr_const < load_wrap_mem_va);

            bool load_addr_wrap_valid = load_entry.in_flight_cnt_valid &&
                                        (load_entry.const_stride_conf == LOAD_PATTERN_TABLE_CONST_STRIDE_CONF_MAX) &&
                                        (load_entry.wrap_conf == LOAD_PATTERN_TABLE_WRAP_CONF_MAX) &&
                                        (load_wrap_back || load_wrap_forward) && BALL_LOAD_CONST_WRAP_EN;
            uint64_t load_addr_wrap_back = load_wrap_target_mem_va + ((load_addr_const - load_wrap_mem_va) % load_wrap_distance) - load_entry.const_stride;
            uint64_t load_addr_wrap_forward = load_wrap_target_mem_va - ((load_wrap_mem_va - load_addr_const) % load_wrap_distance) - load_entry.const_stride ;

            uint64_t load_addr_wrap = load_wrap_back ? load_addr_wrap_back :
                                      load_wrap_forward ? load_addr_wrap_forward : 0;

            load_addr = load_addr_vtage_valid ? load_addr_vtage :
                        load_addr_wrap_valid ? load_addr_wrap :
                        load_addr_const_valid ? load_addr_const :
                        load_addr_offset_valid ? load_addr_offset : 0;

            load_data_valid = read_bp_data_cache(load_addr, load_entry.mem_sz).valid && load_valid && load_addr_valid;
            load_data = read_bp_data_cache(load_addr, load_entry.mem_sz).data;
        }

        bool predict (uint64_t seq_no, uint8_t piece, uint64_t pc, const bool tage_sc_l_predict) {

            // step 1. lookup cond_br_chain_table
            uint64_t cond_br_chain_table_lookup_index;
            uint64_t cond_br_chain_table_lookup_tag;
            bool cond_br_chain_table_lookup_hit = cond_br_chain_table_lookup(pc, cond_br_chain_table_lookup_index, cond_br_chain_table_lookup_tag);

            // step 2. lookup load_pattern_table and predict load value
            uint64_t load_3_0_addr = 0;
            bool load_3_0_data_valid = 0;
            uint64_t load_3_0_data = 0;

            uint64_t load_2_0_addr = 0;
            bool load_2_0_data_valid = 0;
            uint64_t load_2_0_data = 0;

            predict_load_data(load_3_0[cond_br_chain_table_lookup_index], 0, 0, load_3_0_addr, load_3_0_data_valid, load_3_0_data);
            predict_load_data(load_2_0[cond_br_chain_table_lookup_index], load_3_0_data_valid, load_3_0_data, load_2_0_addr, load_2_0_data_valid, load_2_0_data);

            uint64_t load_3_1_addr = 0;
            bool load_3_1_data_valid = 0;
            uint64_t load_3_1_data = 0;

            uint64_t load_2_1_addr = 0;
            bool load_2_1_data_valid = 0;
            uint64_t load_2_1_data = 0;

            predict_load_data(load_3_1[cond_br_chain_table_lookup_index], 0, 0, load_3_1_addr, load_3_1_data_valid, load_3_1_data);
            predict_load_data(load_2_1[cond_br_chain_table_lookup_index], load_3_1_data_valid, load_3_1_data, load_2_1_addr, load_2_1_data_valid, load_2_1_data);

            // step 3. lookup alu_pattern_table, and predict alu result
            ALUPatternTableEntry alu_1_0_entry = read_alu_pattern_table(alu_1_0[cond_br_chain_table_lookup_index].pc);

            // 1_0
            bool alu_1_0_0_valid = alu_1_0_entry.valid && alu_1_0[cond_br_chain_table_lookup_index].valid;
            bool alu_1_0_0_data_valid = load_2_0_data_valid && alu_1_0_0_valid;
            uint64_t alu_1_0_0_data = load_2_0_data_valid ? load_2_0_data : 0;

            bool alu_1_0_1_valid = alu_1_0_entry.valid && alu_1_0[cond_br_chain_table_lookup_index].src_1_valid;
            bool alu_1_0_1_data_valid = load_2_1_data_valid && alu_1_0_1_valid;
            uint64_t alu_1_0_1_data = load_2_1_data_valid ? load_2_1_data : 0;

            // step 4. make prediction
            bool load_2_0_res_valid = (!alu_1_0[cond_br_chain_table_lookup_index].valid) &&
                                      load_2_0_data_valid && BALL_B_CMP_ZERO_EN;
            NZCV load_2_0_res = calculate_nzcv_add(load_2_0_data, 0);

            bool alu_1_0_cmp_valid = (alu_1_0_entry.cmp_conf == ALU_PATTERN_TABLE_CMP_CONF_MAX) && BALL_ALU_CMP_EN;
            bool alu_1_0_fcmp_zero_valid = (alu_1_0_entry.fcmp_zero_conf == ALU_PATTERN_TABLE_FCMP_ZERO_CONF_MAX) && BALL_ALU_FCMP_ZERO_EN;
            bool alu_1_0_res_valid = alu_1_0_0_data_valid && alu_1_0_1_data_valid && alu_1_0_cmp_valid ||
                                     alu_1_0_0_data_valid && alu_1_0_fcmp_zero_valid;
            NZCV alu_1_0_res = alu_1_0_cmp_valid ? calculate_nzcv_sub(alu_1_0_0_data, alu_1_0_1_data) :
                                                   calculate_nzcv_fcmp_zero(alu_1_0_0_data);

            bool ball_res_valid = load_2_0_res_valid || alu_1_0_res_valid;
            NZCV ball_res = load_2_0_res_valid ? load_2_0_res :
                                                 alu_1_0_res;

            bool ball_taken_threshold = (b_0_0[cond_br_chain_table_lookup_index].taken[ball_res.to_uint64()] == COND_BR_CHAIN_B_NODE_TAKEN_CONF_MAX);
            bool ball_not_taken_threshold = (b_0_0[cond_br_chain_table_lookup_index].taken[ball_res.to_uint64()] == COND_BR_CHAIN_B_NODE_TAKEN_CONF_MIN);
            bool ball_predict_valid = cond_br_chain_table_lookup_hit &&
                                       ball_res_valid &&
                                       (ball_taken_threshold || ball_not_taken_threshold);
            const bool ball_predict = ball_taken_threshold;

            bool ball_higher_accuracy = (b_0_0[cond_br_chain_table_lookup_index].ball_tage_sel >= (COND_BR_CHAIN_B_NODE_SEL_MAX+1)/2);
            bool use_ball_predict = ball_predict_valid && ball_higher_accuracy && BALL_PREDICT_EN;

            ball_pred_time_hist_t ball_pred_time_hist = {};
            ball_pred_time_hist.tage_sc_l_predict = tage_sc_l_predict;
            ball_pred_time_hist.use_ball_predict = use_ball_predict;
            ball_pred_time_hist.ball_predict_valid = ball_predict_valid;
            ball_pred_time_hist.ball_predict = ball_predict;

            ball_pred_time_histories.emplace(seq_no, ball_pred_time_hist);

            #ifdef BALL_DEBUG_PRINT
            #ifdef BALL_DEBUG_PRINT_PREDICT
            if (pc == BALL_DEBUG_PRINT_PC && BALL_DEBUG_PRINT_PC_EN ||
                seq_no == BALL_DEBUG_PRINT_SEQ_NO && BALL_DEBUG_PRINT_SEQ_EN ||
                BALL_DEBUG_PRINT_ALL_EN) {

                fprintf(stdout, "\n");
                fprintf(stdout, "seq_no: %#zx; pc: %#zx\n", seq_no, pc);
                fprintf(stdout, "b_0_0: %#x, %#zx\n", b_0_0[cond_br_chain_table_lookup_index].valid, b_0_0[cond_br_chain_table_lookup_index].pc);
                for (uint64_t j = 0; j < COND_BR_CHAIN_B_NODE_COND_NUM; j++) {
                    fprintf(stdout, "%#x,", b_0_0[cond_br_chain_table_lookup_index].taken[j]);
                }
                fprintf(stdout, "\n");
                fprintf(stdout, "alu_1_0: %#x, %#x, %#zx, %#zx\n", alu_1_0[cond_br_chain_table_lookup_index].valid, alu_1_0[cond_br_chain_table_lookup_index].src_1_valid, alu_1_0[cond_br_chain_table_lookup_index].pc_full, alu_1_0[cond_br_chain_table_lookup_index].pc);
                fprintf(stdout, "load_2_0: %#x, %#zx, %#zx, %#x\n", load_2_0[cond_br_chain_table_lookup_index].valid, load_2_0[cond_br_chain_table_lookup_index].pc_full, load_2_0[cond_br_chain_table_lookup_index].pc, load_2_0[cond_br_chain_table_lookup_index].piece);
                fprintf(stdout, "load_2_1: %#x, %#zx, %#zx, %#x\n", load_2_1[cond_br_chain_table_lookup_index].valid, load_2_1[cond_br_chain_table_lookup_index].pc_full, load_2_1[cond_br_chain_table_lookup_index].pc, load_2_1[cond_br_chain_table_lookup_index].piece);
                fprintf(stdout, "load_3_0: %#x, %#zx, %#zx, %#x\n", load_3_0[cond_br_chain_table_lookup_index].valid, load_3_0[cond_br_chain_table_lookup_index].pc_full, load_3_0[cond_br_chain_table_lookup_index].pc, load_3_0[cond_br_chain_table_lookup_index].piece);
                fprintf(stdout, "load_3_1: %#x, %#zx, %#zx, %#x\n", load_3_1[cond_br_chain_table_lookup_index].valid, load_3_1[cond_br_chain_table_lookup_index].pc_full, load_3_1[cond_br_chain_table_lookup_index].pc, load_3_1[cond_br_chain_table_lookup_index].piece);
                fprintf(stdout, "\n");

                fprintf(stdout, "load_2_0\n");
                print_load_pattern_table_entry(read_load_pattern_table(load_2_0[cond_br_chain_table_lookup_index].pc, load_2_0[cond_br_chain_table_lookup_index].piece));
                fprintf(stdout, "load_3_0\n");
                print_load_pattern_table_entry(read_load_pattern_table(load_3_0[cond_br_chain_table_lookup_index].pc, load_3_0[cond_br_chain_table_lookup_index].piece));
                fprintf(stdout, "load_2_1\n");
                print_load_pattern_table_entry(read_load_pattern_table(load_2_1[cond_br_chain_table_lookup_index].pc, load_2_1[cond_br_chain_table_lookup_index].piece));
                fprintf(stdout, "load_3_1\n");
                print_load_pattern_table_entry(read_load_pattern_table(load_3_1[cond_br_chain_table_lookup_index].pc, load_3_1[cond_br_chain_table_lookup_index].piece));

                fprintf(stdout, "alu_1_0\n");
                print_alu_pattern_table_entry(alu_1_0_entry);

                fprintf(stdout, "load_2_0_addr: %#zx\n", load_2_0_addr);
                fprintf(stdout, "load_2_0_data_valid: %#x\n", load_2_0_data_valid);
                fprintf(stdout, "load_2_0_data: %#zx\n", load_2_0_data);

                fprintf(stdout, "load_3_0_addr: %#zx\n", load_3_0_addr);
                fprintf(stdout, "load_3_0_data_valid: %#x\n", load_3_0_data_valid);
                fprintf(stdout, "load_3_0_data: %#zx\n", load_3_0_data);

                fprintf(stdout, "load_2_1_addr: %#zx\n", load_2_1_addr);
                fprintf(stdout, "load_2_1_data_valid: %#x\n", load_2_1_data_valid);
                fprintf(stdout, "load_2_1_data: %#zx\n", load_2_1_data);

                fprintf(stdout, "load_3_1_addr: %#zx\n", load_3_1_addr);
                fprintf(stdout, "load_3_1_data_valid: %#x\n", load_3_1_data_valid);
                fprintf(stdout, "load_3_1_data: %#zx\n", load_3_1_data);

                fprintf(stdout, "alu_1_0_0_data_valid: %#x\n", alu_1_0_0_data_valid);
                fprintf(stdout, "alu_1_0_0_data: %#zx\n", alu_1_0_0_data);
                fprintf(stdout, "alu_1_0_1_data_valid: %#x\n", alu_1_0_1_data_valid);
                fprintf(stdout, "alu_1_0_1_data: %#zx\n", alu_1_0_1_data);

                fprintf(stdout, "alu_1_0_res_valid: %#x\n", alu_1_0_res_valid);
                fprintf(stdout, "alu_1_0_res: %#zx\n", alu_1_0_res.to_uint64());

                print_store_resolve_queue();

                fprintf(stdout, "ball_predict_valid: %#x\n", ball_predict_valid);
                fprintf(stdout, "ball_predict: %#x\n", ball_predict);
                fprintf(stdout, "tage_sc_l_predict: %#x\n", tage_sc_l_predict);
                fprintf(stdout, "ball predict: seq_no: %#zx, pc: %#zx, ball_predict_valid: %#x, ball_predict: %#x\n", seq_no, pc, ball_predict_valid, ball_predict);
                fprintf(stdout, "\n");
            }
            #endif // BALL_DEBUG_PRINT_PREDICT
            #endif // BALL_DEBUG_PRINT

            return use_ball_predict ? ball_predict: tage_sc_l_predict;
        }

        BPDataCacheReadResult read_bp_data_cache(uint64_t mem_va, uint8_t mem_sz) {
            BPDataCacheReadResult res = {};

            uint8_t read_time = 2;
            uint64_t data_mask = 0xFFFFFFFFFFFFFFFF;
            if (mem_sz == 3) {
                read_time = 2;
                data_mask = 0xFFFFFFFFFFFFFFFF;
            } else if (mem_sz == 2) {
                read_time = 1;
                data_mask = 0x00000000FFFFFFFF;
            } else if (mem_sz == 1) {
                read_time = 1;
                // fp_8 trace mem_sz is 2 byte, but use 4 byte to fcmp_zero
                data_mask = 0x00000000FFFFFFFF;
                //data_mask = 0x000000000000FFFF;
            } else {
                read_time = 1;
                data_mask = 0x00000000000000FF;
            }

            for (uint8_t read_time_index = 0; read_time_index < read_time; read_time_index++) {
                uint64_t mem_va_read = mem_va + read_time_index * BP_DATA_CACHE_LINE_GRANULE_SIZE;
                uint64_t mem_va_offset = mem_va_read % BP_DATA_CACHE_LINE_SIZE;
                uint64_t mem_va_granule = mem_va_offset / BP_DATA_CACHE_LINE_GRANULE_SIZE;
                uint64_t mem_va_set = (mem_va_read / BP_DATA_CACHE_LINE_SIZE) % BP_DATA_CACHE_SET;
                uint64_t mem_va_tag = (mem_va_read / (BP_DATA_CACHE_SET * BP_DATA_CACHE_LINE_SIZE));

                bool bp_data_cache_hit = 0;
                uint64_t bp_data_cache_hit_way = 0;
                for (uint64_t i = 0; i < BP_DATA_CACHE_WAY; i++) {
                    if(bp_data_cache[mem_va_set][i].valid[mem_va_granule] &&
                       (bp_data_cache[mem_va_set][i].tag == mem_va_tag)) {
                        bp_data_cache_hit = 1;
                        bp_data_cache_hit_way = i;
                        break;
                    }
                }


                assert (mem_va_set >= 0 && mem_va_set < BP_DATA_CACHE_SET);
                assert (bp_data_cache_hit_way >= 0 && bp_data_cache_hit_way < BP_DATA_CACHE_WAY);
                assert (mem_va_granule >= 0 && mem_va_granule < BP_DATA_CACHE_LINE_GRANULE_NUM);

                if (read_time_index == 0) {
                    res.valid = bp_data_cache_hit; 
                    res.data = bp_data_cache[mem_va_set][bp_data_cache_hit_way].data[mem_va_granule] & 0x00000000FFFFFFFF;
                } else if (read_time_index == 1) {
                    res.valid = res.valid && bp_data_cache_hit;
                    res.data = (res.data) | (bp_data_cache[mem_va_set][bp_data_cache_hit_way].data[mem_va_granule] << (BP_DATA_CACHE_LINE_GRANULE_SIZE * 8));
                }
            }

            res.data = res.data & data_mask;

            // if load data need by branch predictor is same address with un-committed store,
            // return miss.
            for (uint64_t i = 0; i < STORE_RESOLVE_QUEUE_ENTRY_NUM; i++) {
                if (store_resolve_queue[i].valid && (store_resolve_queue[i].addr == mem_va)) {
                    res.valid = 0;
                    break;
                }
            }

            return res;
        }

// ==========================================================
// ==========================================================
// EVES from CVP1 (2/n) (begin)
// ==========================================================
// ==========================================================

 //index function for VTAGE (use the global path history): just a complex hash function
uint32_t
gi (int i, uint64_t pc)
{
  int hl = (_HL[i] < 64) ? (_HL[i] % 64) : 64;
  uint64_t inter = (hl < 64) ? (((1 << hl) - 1) & gpath[0]) : gpath[0];
  uint64_t res = 0;
  inter ^= (pc >> (i)) ^ (pc);

  for (int t = 0; t < 8; t++)
    {
      res ^= inter;
      inter ^= ((inter & 15) << 16);
      inter >>= (LOGBANK - ((_NHIST - i + LOGBANK - 1) % (LOGBANK - 1)));
    }
  hl = (hl < (_HL[_NHIST] + 1) / 2) ? hl : ((_HL[_NHIST] + 1) / 2);

  inter ^= (hl < 64) ? (((1 << hl) - 1) & gtargeth) : gtargeth;
  for (int t = 0; t <= hl / LOGBANK; t++)
    {
      res ^= inter;
      inter ^= ((inter & 15) << 16);
      inter >>= LOGBANK;
    }

  if (_HL[i] >= 64)
    {
      int REMAIN = _HL[i] - 64;
      hl = REMAIN;
      int PT = 1;

      while (REMAIN > 0)
	{


	  inter ^= ((hl < 64) ? (((1 << hl) - 1) & gpath[PT]) : gpath[PT]);
	  for (int t = 0; t < 8; t++)
	    {
	      res ^= inter;
	      inter ^= ((inter & 15) << 16);

	      inter >>= (LOGBANK -
			 ((_NHIST - i + LOGBANK - 1) % (LOGBANK - 1)));

	    }
	  REMAIN = REMAIN - 64;
	  PT++;
	}
    }
  return ((uint32_t) res & (BANKSIZE - 1));
}



//tags for VTAGE: just another complex hash function "orthogonal" to the index function
uint32_t
gtag (int i, uint64_t pc)
{
  int hl = (_HL[i] < 64) ? (_HL[i] % 64) : 64;
  uint64_t inter = (hl < 64) ? (((1 << hl) - 1) & gpath[0]) : gpath[0];

  uint64_t res = 0;
  inter ^= ((pc >> (i)) ^ (pc >> (5 + i)) ^ (pc));
  for (int t = 0; t < 8; t++)
    {
      res ^= inter;
      inter ^= ((inter & 31) << 14);
      inter >>= (LOGBANK - ((_NHIST - i + LOGBANK - 2) % (LOGBANK - 1)));
    }
  hl = (hl < (_HL[_NHIST] + 1) / 2) ? hl : ((_HL[_NHIST] + 1) / 2);
  inter ^= ((hl < 64) ? (((1 << hl) - 1) & gtargeth) : gtargeth);
  for (int t = 0; t <= hl / TAGWIDTH; t++)
    {
      res ^= inter;
      inter ^= ((inter & 15) << 16);
      inter >>= TAGWIDTH;
    }

  if (_HL[i] >= 64)
    {
      int REMAIN = _HL[i] - 64;
      hl = REMAIN;
      int PT = 1;

      while (REMAIN > 0)
	{


	  inter ^= ((hl < 64) ? (((1 << hl) - 1) & gpath[PT]) : gpath[PT]);
	  for (int t = 0; t < 8; t++)
	    {
	      res ^= inter;
	      inter ^= ((inter & 31) << 14);
	      inter >>= (TAGWIDTH - (_NHIST - i - 1));


	    }
	  REMAIN = REMAIN - 64;
	  PT++;
	}
    }

  return ((uint32_t) res & ((1 << TAGWIDTH) - 1));
}

void
getPredVtage (ForUpdate * U, uint64_t & predicted_value)
{
  bool predvtage = false;
  uint64_t pc = U->pc;
  uint64_t PCindex = ((pc) ^ (pc >> 2) ^ (pc >> 5)) % PREDSIZE;
  uint64_t PCbank = (PCindex >> LOGBANK) << LOGBANK;
  for (int i = 1; i <= _NHIST; i++)
    {
      U->GI[i] = (gi (i, pc) + (PCbank + (i << LOGBANK))) % PREDSIZE;
      U->GTAG[i] = gtag (i, pc);
    }
  U->GTAG[0] = (pc ^ (pc >> 4) ^ (pc >> TAGWIDTH)) & ((1 << TAGWIDTH) - 1);
  U->GI[0] = PCindex;
  U->HitBank = -1;

  for (int i = _NHIST; i >= 0; i--)
    {
      if (Vtage[U->GI[i]].tag == U->GTAG[i])
	{
	  U->HitBank = i;
	  break;
	}
    }

  if (LastMispVT >= 128)
// when a misprediction is encountered on VTAGE, we do not predict with VTAGE for 128 instructions;
// does not bring significant speed-up, but reduces the misprediction number significantly: mispredictions tend to be clustered       
    if (U->HitBank >= 0)
      {
	int index = Vtage[U->GI[U->HitBank]].hashpt;
	if (index < 3 * BANKDATA)
	  {
	    // the hash and the data are both present
	    predicted_value = LDATA[index].data;
	    predvtage = ((Vtage[U->GI[U->HitBank]].conf >= MAXCONFID));
      }
      }
  U->predvtage = predvtage;
}

void
getPredStride (ForUpdate * U, uint64_t & predicted_value, uint64_t seq_no)
{
  bool predstride = false;

  int B[NBWAYSTR];
  int TAG[NBWAYSTR];
  uint64_t pc = U->pc;
//use a 3-way skewed-associative structure  for the stride predictor
  for (int i = 0; i < NBWAYSTR; i++)
    {
      //B[i] index in way i ; TAG[i] tag in way i;
      B[i] = ((((pc) ^ (pc >> (2 * LOGSTR - i)) ^
		(pc >> (LOGSTR - i)) ^
		(pc >> (3 * LOGSTR - i))) * NBWAYSTR) +
	      i) % (NBWAYSTR * (1 << LOGSTR));
      int j = (NBWAYSTR - i);
      if (j < 0)
	j = 0;

      TAG[i] =
	((pc >> (LOGSTR - j)) ^ (pc >> (2 * LOGSTR - j)) ^
	 (pc >> (3 * LOGSTR - j)) ^ (pc >> (4 * LOGSTR - j))) & ((1 <<
								  TAGWIDTHSTR)
								 - 1);
      U->B[i] = B[i];
      U->TAGSTR[i] = TAG[i];
    }

  int STHIT = -1;
  for (int i = 0; i < NBWAYSTR; i++)
    {
      if (STR[B[i]].tag == TAG[i])
	{
	  STHIT = B[i];
	  break;
	}
    }
  U->STHIT = STHIT;
  if (STHIT >= 0)
    if (SafeStride >= 0)
      {				// hit
	uint64_t LastCommitedValue = STR[STHIT].LastValue;

	if (STR[STHIT].conf >= MAXCONFIDSTR / 4)


	  {
	    int inflight = 0;
	    // compute the number of inflight instances of the instruction
	    for (uint64_t i = _seq_commit + 1; i < seq_no; i++)
	      {
		inflight += (Update[i & (MAXINFLIGHT - 1)].pc == pc);
	      }
	    predicted_value =
	      (uint64_t) ((int64_t) LastCommitedValue +
			  ((inflight + 1) * ((int64_t) STR[STHIT].Stride)));
	    predstride = true;
	  }
      }
  U->predstride = predstride;
}


bool
getPrediction (uint64_t seq_no, uint64_t pc, uint8_t piece,
	       uint64_t & predicted_value, bool need_predict_for_ball)
{

  ForUpdate *U;
  U = &Update[seq_no & (MAXINFLIGHT - 1)];
  U->pc = pc + piece;
  U->predvtage = false;
  U->predstride = false;


if (need_predict_for_ball) {
#ifdef PREDSTRIDE
  getPredStride (U, predicted_value, seq_no);
#endif
#ifdef PREDVTAGE
  getPredVtage (U, predicted_value);
#endif
}
// the two predictions are very rarely both high confidence; when they are pick the VTAGE prediction

  return (U->predstride || U->predvtage);
}

// Update of the Stride predictor
// function determining whether to  update or not confidence on a correct prediction
bool
strideupdateconf (ForUpdate * U, uint64_t actual_value, int actual_latency,
		  int stride)
{
#define UPDATECONFSTR2 (((!U->prediction_result) || (U->predstride)) && ((random () & ((1 << (NOTLLCMISS + NOTL2MISS + NOTL1MISS + 2*MFASTINST  + 2*(U->INSTTYPE!=InstClass::loadInstClass))) - 1)) == 0))
#define UPDATECONFSTR1 (abs (stride >= 8) ? (UPDATECONFSTR2 || UPDATECONFSTR2) : (UPDATECONFSTR2))
#define UPDATECONFSTR (abs (stride >= 64) ? (UPDATECONFSTR1 || UPDATECONFSTR1) : (UPDATECONFSTR1))
  return (UPDATECONFSTR &
	  ((abs (stride) > 1) || (U->INSTTYPE != InstClass::loadInstClass)
	   || ((stride == -1) & ((random () & 1) == 0))
	   || ((stride == 1) & ((random () & 3) == 0))));
//All strides are not equal: the smaller the stride the smaller the benefit (not huge :-))

}

//Allocate or not if instruction absent from the predictor
bool
StrideAllocateOrNot (ForUpdate * U, uint64_t actual_value, int actual_latency)
{
#ifndef LIMITSTUDY
  bool X = false;
#define LOGPBINVSTR 4
  switch (U->INSTTYPE)
    {
    case InstClass::aluInstClass:
    case InstClass::storeInstClass:
      X = ((random () & ((1 << (LOGPBINVSTR + 2)) - 1)) == 0);
      break;
    case InstClass::fpInstClass:
      X = ((random () & ((1 << (LOGPBINVSTR)) - 1)) == 0);
      break;
    case InstClass::slowAluInstClass:
      X = ((random () & ((1 << (LOGPBINVSTR)) - 1)) == 0);
      break;
    case InstClass::loadInstClass:
      X =
	((random () &
	  ((1 << (NOTLLCMISS + NOTL2MISS + NOTL1MISS + MFASTINST)) - 1)) ==
	 0);
      break;
    };


  return (X);

#else
  return (true);
#endif
}

void
UpdateStridePred (ForUpdate * U, uint64_t actual_value, int actual_latency)
{


  int B[NBWAYSTR];
  int TAG[NBWAYSTR];
  for (int i = 0; i < NBWAYSTR; i++)
    {
      B[i] = U->B[i];
      TAG[i] = U->TAGSTR[i];
    }
  int STHIT = -1;

  for (int i = 0; i < NBWAYSTR; i++)
    {
      if (STR[B[i]].tag == TAG[i])
	{
	  STHIT = B[i];
	  break;

	}

    }





  if (STHIT >= 0)
    {
      uint64_t LastValue = STR[STHIT].LastValue;
      uint64_t Value =
	(uint64_t) ((int64_t) LastValue + (int64_t) STR[STHIT].Stride);
      int64_t INTER =
	abs (2 * ((int64_t) actual_value - (int64_t) LastValue) - 1);

      uint64_t stridetoalloc =
	(INTER <
	 (1 << LOGSTRIDE)) ? (uint64_t) ((int64_t) actual_value -
					 (int64_t) LastValue) : 0;

      STR[STHIT].LastValue = actual_value;

//special case when the stride is not determined
      if (STR[STHIT].NotFirstOcc > 0)
	{
	  if (Value == actual_value)
	    {

	      if (STR[STHIT].conf < MAXCONFIDSTR)
		{
		  if (strideupdateconf
		      (U, actual_value, actual_latency, (int) stridetoalloc))
		    STR[STHIT].conf++;
		}

	      if (STR[STHIT].u < 3)
		if (strideupdateconf
		    (U, actual_value, actual_latency, (int) stridetoalloc))
		  STR[STHIT].u++;
	      if (STR[STHIT].conf >= MAXCONFIDSTR / 4)
		STR[STHIT].u = 3;
	    }
	  else
	    {
//misprediction

	      {
		if (STR[STHIT].conf > (1 << (WIDTHCONFIDSTR - 3)))

		  {
		    STR[STHIT].conf -= (1 << (WIDTHCONFIDSTR - 3));
		  }
		else
		  {
		    STR[STHIT].conf = 0;
		    STR[STHIT].u = 0;
		  }


	      }

// this allows to  restart a new sequence with a different   stride              
	      STR[STHIT].NotFirstOcc = 0;
	    }
	}
      else
	{
//First occurence              
//        if (STR[STHIT].NotFirstOcc == 0)
	  if (stridetoalloc != 0)
//             if ((stridetoalloc != 0) && (stridetoalloc!=1)  && (((int64_t) stridetoalloc) != -1))
	    // we do not waste the stride predictor storage for stride zero
	    {
	      STR[STHIT].Stride = stridetoalloc;
	    }
	  else
	    {
	      // do not pollute the stride predictor with constant data or with invalid strides
	      STR[STHIT].Stride = 0xffff;
	      STR[STHIT].conf = 0;
	      STR[STHIT].u = 0;
	    }

	  STR[STHIT].NotFirstOcc++;
	}
    }
  else				// the address was not present and is not predicted by VTAGE
  if (!U->prediction_result)
    {
      if (StrideAllocateOrNot (U, actual_value, actual_latency))
	{			// try to allocate
	  int X = random () % NBWAYSTR;
	  bool done = false;
	  // the target entry is not a stride candidate
	  for (int i = 0; i < NBWAYSTR; i++)
	    {
	      STHIT = B[X];
	      if (STR[STHIT].conf == 0)
		{
		  STR[STHIT].conf = 1;	//just to allow not to ejected before testing if possible stride candidate
		  STR[STHIT].u = 0;
		  STR[STHIT].tag = TAG[X];
		  STR[STHIT].Stride = 0;
		  STR[STHIT].NotFirstOcc = 0;
		  STR[STHIT].LastValue = actual_value;
		  done = true;
		  break;

		}
	      X = (X + 1) % NBWAYSTR;
	    }
	  // the target entry has not been useful recently
	  if (!done)
	    for (int i = 0; i < NBWAYSTR; i++)
	      {
		STHIT = B[X];
		if (STR[STHIT].u == 0)
		  {
		    STR[STHIT].conf = 1;
		    STR[STHIT].u = 0;
		    STR[STHIT].tag = TAG[X];
		    STR[STHIT].Stride = 0;
		    STR[STHIT].NotFirstOcc = 0;
		    STR[STHIT].LastValue = actual_value;
		    done = true;
		    break;

		  }
		X = (X + 1) % NBWAYSTR;
	      }
//if unable to allocate: age some target entry
	  if (!done)
	    {
	      if ((random () &
		   ((1 <<
		     (2 + 2 * (STR[STHIT].conf > (MAXCONFIDSTR) / 8) +
		      2 * (STR[STHIT].conf >= MAXCONFIDSTR / 4))) - 1)) == 0)
		STR[STHIT].u--;
	    }

	}
    }
//////////////////////////////
}



/////////Update of  VTAGE
// function determining whether to  update or not confidence on a correct prediction

bool
vtageupdateconf (ForUpdate * U, uint64_t actual_value, int actual_latency)
{
#define LOWVAL ((abs (2*((int64_t) actual_value)+1)<(1<<16))+ (actual_value==0))


#ifdef K8
#define updateconf ((random () & (((1 << (LOWVAL+NOTLLCMISS+2*FASTINST+NOTL2MISS+NOTL1MISS + ((U->INSTTYPE!=InstClass::loadInstClass) ||NOTL1MISS)      + (U->HitBank > 1) ))- 1)))==0)
#else
#define updateconf ((random () & (((1 << (LOWVAL+NOTLLCMISS+2*FASTINST+NOTL2MISS+NOTL1MISS + ((U->INSTTYPE!=InstClass::loadInstClass) ||NOTL1MISS)       ))- 1)))==0)
#endif


#ifdef LIMITSTUDY
#define UPDATECONF2 ((U->HitBank<=1) ? (updateconf || updateconf ) || (updateconf || updateconf ) : (updateconf || updateconf ))
#define UPDATECONF (UPDATECONF2 || UPDATECONF2)
#else
#ifdef K32
#define UPDATECONF (((U->HitBank<=1) ? (updateconf || updateconf) : updateconf))
#else
  // K8
#define UPDATECONF updateconf
#endif
#endif
  switch (U->INSTTYPE)
    {
    case InstClass::aluInstClass:

    case InstClass::fpInstClass:
    case InstClass::slowAluInstClass:
    case InstClass::undefInstClass:
    case InstClass::loadInstClass:
    case InstClass::storeInstClass:
      return (UPDATECONF);
      break;
    case InstClass::uncondIndirectBranchInstClass:
      return (true);
      break;
    default:
      return (false);
    };
}

// Update of the U counter or not 
bool
VtageUpdateU (ForUpdate * U, uint64_t actual_value, int actual_latency)
{

#define UPDATEU ((!U->prediction_result) && ((random () & ((1<<( LOWVAL + 2*NOTL1MISS  + (U->INSTTYPE!=InstClass::loadInstClass) + FASTINST + 2*(U->INSTTYPE==InstClass::aluInstClass)*(U->NbOperand<2)))-1)) == 0))

  switch (U->INSTTYPE)
    {
    case InstClass::aluInstClass:
    case InstClass::fpInstClass:
    case InstClass::slowAluInstClass:
    case InstClass::undefInstClass:
    case InstClass::loadInstClass:
    case InstClass::storeInstClass:
      return (UPDATEU);
      break;
    case InstClass::uncondIndirectBranchInstClass:
      return (true);
      break;
    default:
      return (false);
    };



}

bool
VtageAllocateOrNot (ForUpdate * U, uint64_t actual_value, int actual_latency,
		    bool MedConf)
{
  bool X = false;


  switch (U->INSTTYPE)
    {
    case InstClass::undefInstClass:
    case InstClass::aluInstClass:
    case InstClass::storeInstClass:
#ifndef LIMITSTUDY
      if (((U->NbOperand >= 2)
	   & ((random () & 15) == 0))
	  || ((U->NbOperand < 2) & ((random () & 63) == 0)))
#endif
    case InstClass::fpInstClass:
    case InstClass::slowAluInstClass:
    case InstClass::loadInstClass:


#ifndef LIMITSTUDY
	X = (((random () &
#ifdef K8
	       ((4 <<
#else
	       //K32
	       ((2 <<
#endif
		 (
#ifdef K32
		   ((U->INSTTYPE != InstClass::loadInstClass) || (NOTL1MISS)) *
#endif
		   LOWVAL + NOTLLCMISS + NOTL2MISS +
#ifdef K8
		   2 *
#endif
		   NOTL1MISS + 2 * FASTINST)) - 1)) == 0) ||
#ifdef K8
	     (((random () & 3) == 0) && MedConf));
#else
	     (MedConf));
#endif
#else
	X = true;
#endif

      break;
    case InstClass::uncondIndirectBranchInstClass:
      X = true;
      break;
    default:
      X = false;

    };
  return (X);
}


void
UpdateVtagePred (ForUpdate * U, uint64_t actual_value, int actual_latency)
{

  bool MedConf = false;
  uint64_t HashData = ((actual_value ^ (actual_value >> 7) ^
			(actual_value >> 13) ^ (actual_value >> 21) ^
			(actual_value >> 29) ^ (actual_value >> 34) ^
			(actual_value >> 43) ^ (actual_value >> 52) ^
			(actual_value >> 57)) & (BANKDATA - 1)) +
    3 * BANKDATA;

  bool ShouldWeAllocate = true;
  if (U->HitBank != -1)
    {
      // there was  an  hitting entry in VTAGE
      uint64_t index = U->GI[U->HitBank];
      // the entry may have dissappeared in the interval between the prediction and  the commit


      if (Vtage[index].tag == U->GTAG[U->HitBank])
	{
	  //  update the prediction
	  uint64_t indindex = Vtage[index].hashpt;
	  ShouldWeAllocate =
	    ((indindex >= 3 * BANKDATA) & (indindex != HashData))
	    || ((indindex < 3 * BANKDATA) &
		(LDATA[indindex].data != actual_value));
	  if (!ShouldWeAllocate)
	    {
	      // the predicted result is satisfactory: either a good hash without data, or a pointer on the correct data
	      ShouldWeAllocate = false;

	      if (Vtage[index].conf < MAXCONFID)
		if (vtageupdateconf (U, actual_value, actual_latency))
		  Vtage[index].conf++;

	      if (Vtage[index].u < MAXU)
		if ((VtageUpdateU (U, actual_value, actual_latency))
		    || (Vtage[index].conf == MAXCONFID))

		  Vtage[index].u++;
	      if (indindex < 3 * BANKDATA)
		if (LDATA[indindex].u < 3)
		  if (Vtage[index].conf == MAXCONFID)
		    LDATA[indindex].u++;


	      if (indindex >= 3 * BANKDATA)
		{

		  //try to allocate an effective data entry when the confidence has reached a reasonable level
		  if (Vtage[index].conf >= MAXCONFID - 1)
		    {
		      int X[3];
		      for (int i = 0; i < 3; i++)
			X[i] =
			  (((actual_value) ^
			    (actual_value >> (LOGLDATA + (i + 1))) ^
			    (actual_value >> (3 * (LOGLDATA + (i + 1)))) ^
			    (actual_value >> (4 * (LOGLDATA + (i + 1)))) ^
			    (actual_value >> (5 * (LOGLDATA + (i + 1)))) ^
			    (actual_value >> (6 * (LOGLDATA + (i + 1)))) ^
			    (actual_value >> (2 * (LOGLDATA + (i + 1))))) &
			   ((1 << LOGLDATA) - 1)) + i * (1 << LOGLDATA);
		      bool done = false;
		      for (int i = 0; i < 3; i++)
			{
			  if (LDATA[X[i]].data == actual_value)
			    {
			      //the data is already present
			      Vtage[index].hashpt = X[i];
			      done = true;
			      break;
			    }
			}
		      if (!done)
			if ((random () & 3) == 0)
			  {
			    //data absent: let us try try to steal an entry
			    int i = (((uint64_t) random ()) % 3);
			    bool done = false;
			    for (int j = 0; j < 3; j++)
			      {
				if ((LDATA[X[i]].u == 0))
				  {
				    LDATA[X[i]].data = actual_value;
				    LDATA[X[i]].u = 1;
				    Vtage[index].hashpt = X[i];
				    done = true;
				    break;
				  }
				i++;
				i = i % 3;

			      }
			    if (U->INSTTYPE == InstClass::loadInstClass)
			      if (!done)
				{
				  if ((LDATA[X[i]].u == 0))
				    {
				      LDATA[X[i]].data = actual_value;
				      LDATA[X[i]].u = 1;
				      Vtage[index].hashpt = X[i];
				    }
				  else
#ifdef K8
				  if ((random () & 31) == 0)
#else
				  if ((random () & 3) == 0)
#endif
				    LDATA[X[i]].u--;
				}
			  }
		    }
		}



	    }

	  else
	    {


	      Vtage[index].hashpt = HashData;
	      if ((Vtage[index].conf > MAXCONFID / 2)
		  || ((Vtage[index].conf == MAXCONFID / 2) &
		      (Vtage[index].u == 3))
		  || ((Vtage[index].conf > 0) &
		      (Vtage[index].conf < MAXCONFID / 2)))
		MedConf = true;

	      if (Vtage[index].conf == MAXCONFID)
		{

		  Vtage[index].u = (Vtage[index].conf == MAXCONFID);
		  Vtage[index].conf -= (MAXCONFID + 1) / 4;
		}
	      else
		{
		  Vtage[index].conf = 0;
		  Vtage[index].u = 0;
		}

	    }
	}
    }

  if (!U->prediction_result)
    //Don't waste your time allocating if it is predicted by the other component
    if (ShouldWeAllocate)
      {
// avoid allocating too often
	if (VtageAllocateOrNot (U, actual_value, actual_latency, MedConf))
	  {
	    int ALL = 0;
	    int NA = 0;
	    int DEP = (U->HitBank + 1) + ((random () & 7) == 0);
	    if (U->HitBank == 0)
	      DEP++;

	    if (U->HitBank == -1)
	      {
		if (random () & 7)
		  DEP = random () & 1;
		else
		  DEP = 2 + ((random () & 7) == 0);

	      }

	    if (DEP > 1)
	      {


		for (int i = DEP; i <= _NHIST; i++)
		  {
		    int index = U->GI[i];
		    if ((Vtage[index].u == 0)
			&& ((Vtage[index].conf == MAXCONFID / 2)
			    || (Vtage[index].conf <=
				(random () & MAXCONFID))))
//slightly favors the entries with real confidence
		      {
			Vtage[index].hashpt = HashData;
			Vtage[index].conf = MAXCONFID / 2;	//set to 3  for faster warming to  high confidence 
			Vtage[index].tag = U->GTAG[i];
			ALL++;

			break;

		      }
		    else
		      {
			NA++;
		      }

		  }
	      }
	    else
	      {

		for (int j = 0; j <= 1; j++)
		  {
		    int i = (j + DEP) & 1;

		    int index = U->GI[i];
		    if ((Vtage[index].u == 0)
			&& ((Vtage[index].conf == MAXCONFID / 2)
			    || (Vtage[index].conf <=
				(random () & MAXCONFID))))
		      {
			Vtage[index].hashpt = HashData;
			Vtage[index].conf = MAXCONFID / 2;
			if (U->NbOperand == 0)
			  if (U->INSTTYPE == InstClass::aluInstClass)
			    Vtage[index].conf = MAXCONFID;
			Vtage[index].tag = U->GTAG[i];
			ALL++;
			break;
		      }
		    else
		      {
			NA++;
		      }
		  }
	      }

#ifdef K8
	    _TICK += NA - (3 * ALL);
#else
	    _TICK += (NA - (5 * ALL));
#endif
	    if (_TICK < 0)
	      _TICK = 0;
	    if (_TICK >= MAXTICK)
	      {

		for (int i = 0; i < PREDSIZE; i++)
		  if (Vtage[i].u > 0)
		    Vtage[i].u--;
		_TICK = 0;
	      }
	  }

      }


}

void
updatePredictor (uint64_t
		 seq_no,
		 uint64_t
		 actual_addr, uint64_t actual_value, uint64_t actual_latency)
{
  ForUpdate *U;
  U = &Update[seq_no & (MAXINFLIGHT - 1)];
  if (U->todo == 1)
    {
#ifdef LIMITSTUDY
      //just to force allocations and update on both predictors
      U->prediction_result = 0;
#endif
#ifdef PREDVTAGE
      UpdateVtagePred (U, actual_value, (int) actual_latency);
#endif
#ifdef PREDSTRIDE
      UpdateStridePred (U, actual_value, (int) actual_latency);
#endif
      U->todo = 0;
    }
  _seq_commit = seq_no;

}




void
speculativeUpdate (uint64_t seq_no,	// dynamic micro-instruction # (starts at 0 and increments indefinitely)
		   bool eligible,	// true: instruction is eligible for value prediction. false: not eligible.
		   uint8_t prediction_result,	// 0: incorrect, 1: correct, 2: unknown (not revealed)
		   // Note: can assemble local and global branch history using pc, next_pc, and insn.
		   uint64_t
		   pc, uint64_t next_pc, InstClass insn, uint8_t piece,
		   // Note: up to 3 logical source register specifiers, up to 1 logical destination register specifier.
		   // 0xdeadbeef means that logical register does not exist.
		   // May use this information to reconstruct architectural register file state (using log. reg. and value at updatePredictor()).
		   uint64_t src1, uint64_t src2, uint64_t src3, uint64_t dst)
{

  // the framework does not really allow  to filter the predictions, so we predict every instruction
  ForUpdate *U;
  U = &Update[seq_no & (MAXINFLIGHT - 1)];

  LastMispVT++;
  
  if (eligible)
    {

      U->NbOperand =
	(src1 != 0xdeadbeef) + (src2 != 0xdeadbeef) + (src3 != 0xdeadbeef);
      U->todo = 1;
      U->INSTTYPE = insn;
      U->prediction_result = (prediction_result == 1);
      if (SafeStride < (1 << 15) - 1)
	SafeStride++;
      if (prediction_result != 2)
	{
	  if (prediction_result)
	    {
	      if (U->predstride)
		if (SafeStride < (1 << 15) - 1)
		  SafeStride += 4 * (1 + (insn == InstClass::loadInstClass));
	    }
	  else
	    {
	      if (U->predvtage)
		LastMispVT = 0;
	      if (U->predstride)
		SafeStride -= 1024;
	    }
	}
    }

  bool isCondBr = insn == InstClass::condBranchInstClass;
  bool isUnCondBr = insn == InstClass::uncondIndirectBranchInstClass
    || insn == InstClass::uncondDirectBranchInstClass;
//path history 
  // just to have a longer history without (software) management
  if ((isCondBr) || (isUnCondBr))
    if (pc != next_pc - 4)
      {
	for (int i = 7; i > 0; i--)
	  gpath[i] = (gpath[i] << 1) ^ ((gpath[i - 1] >> 63) & 1);
	gpath[0] = (gpath[0] << 1) ^ (pc >> 2);
	gtargeth = (gtargeth << 1) ^ (next_pc >> 2);
      }
}

// ==========================================================
// ==========================================================
// EVES from CVP1 (2/n) (end)
// ==========================================================
// ==========================================================

};
// =================
// Predictor End
// =================
#endif

static BALLPredictor ball_predictor_impl;
