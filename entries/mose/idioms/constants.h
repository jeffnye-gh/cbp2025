#ifndef IDIOM_CONSTANTS_
#define IDIOM_CONSTANTS_

static constexpr uint64_t piece_size_in_bits = 4;
/* pc + part */
static constexpr uint64_t pc_t_size_in_bits = 64 + piece_size_in_bits;
/* 32 gpr, 32 fpr, some others */
static constexpr uint64_t reg_id_size_in_bits = 7;

static constexpr unsigned maximum_src_regs_per_instr = 3;

static constexpr unsigned window_size = 1024;

static constexpr unsigned phys_reg_id_size_in_bits = 10;


#endif
