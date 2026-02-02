#ifndef CAMBRIDGE_DEBUG_OP_PRINtER_H_
#define CAMBRIDGE_DEBUG_OP_PRINtER_H_

#include <iostream>

#include "uop_tracker.h"

struct named_reg {
    uint64_t reg;
    named_reg(uint64_t reg) : reg(reg) {}
};

inline std::ostream &operator<<(std::ostream &os, named_reg reg) {
    if (reg.reg == 64)
        return os << "COND";
    else if (reg.reg == 31)
        return os << "XSP";
    else if (reg.reg < 32)
        return os << "X" << std::dec << reg.reg;
    else if (reg.reg < 64)
        return os << "D" << std::dec << (reg.reg -32);
    else
        return os << "?" << std::dec << reg.reg;
}

static inline void debug_print_execute(
          uint64_t seq_no, uint8_t piece, uint64_t pc,
          const ExecuteInfo& _exec_info, const uop_execute_info_t &uop_info) {
#ifdef DEBUG_OP_PRINT
    std::cout << std::hex << pc << std::dec << "." << int(piece);
    std::cout << std::hex << ": " << cInfo[static_cast<uint8_t>(_exec_info.dec_info.insn_class)]  << "(";
    bool first = true;
    for (auto value : uop_info.src_reg_values) {
        if (!first)
            std::cout << ",";
        std::cout << value;
        first = false;
    }
    std::cout<<")";
    if (uop_info.dst_reg_value) {
        uint64_t value = uop_info.dst_reg_value.value();
        std::cout << "=";
        if (_exec_info.dec_info.dst_reg_info == 64) {
            /* TODO verify this encoding is correct */
            if (value & 0x10)
                std::cout << "N";
            if (value & 8)
                std::cout << "Z";
            if (value & 4)
                std::cout << "C";
            if (value & 2)
                std::cout << "V";
            if (value & 1)
                std::cout << "Q";
            if (value == 0)
                std::cout << "-";
        } else
            std::cout << value;
    } else if (uop_info.was_taken.has_value())
        std::cout<<"=" << (uop_info.was_taken.value() ? "T" : "N");

    if (is_mem(_exec_info.dec_info.insn_class)) {
        std::cout << "; MEM[" << std::hex << _exec_info.mem_va.value() << ","
            << _exec_info.mem_sz.value() << "]";
    }
    std::cout << std::dec << "; ";

    if (_exec_info.dec_info.dst_reg_info) {
        std::cout << named_reg(_exec_info.dec_info.dst_reg_info.value()) << " = ";
    }
    std::cout << cInfo[static_cast<uint8_t>(_exec_info.dec_info.insn_class)] << "(";
    first = true;
    for (auto reg : _exec_info.dec_info.src_reg_info) {
        if (!first)
            std::cout << ", ";
        std::cout << named_reg(reg);
        first = false;
    }
    if (is_mem(_exec_info.dec_info.insn_class) &&
            uop_info.src_reg_values.size() == 1 &&
            _exec_info.mem_va.has_value()) {
        std::cout << ", #0x" << std::hex
            << ( _exec_info.mem_va.value() - uop_info.src_reg_values[0])
            << std::dec;
    }
    std::cout << ")" << std::endl;
#endif
}
#endif
