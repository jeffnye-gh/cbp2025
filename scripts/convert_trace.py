#!/usr/bin/env python3
"""
convert_trace.py

Dump either:
  - ChampSim .xz traces (fixed 64-byte input_instr records)
  - CBP .gz traces (variable-length records per CBP trace reader format)

Usage:
  ./convert_trace.py -i <input.xz|input.gz> -o out.txt -n 1000
"""

from __future__ import annotations

import argparse
import gzip
import io
import lzma
import os
import struct
from dataclasses import dataclass
from typing import BinaryIO, List, Optional, Tuple


# ----------------------------
# ChampSim (from trace_instruction.h)
# ----------------------------
# struct input_instr {
#   unsigned long long ip;                         // 8
#   unsigned char is_branch;                       // 1
#   unsigned char branch_taken;                    // 1
#   unsigned char destination_registers[2];        // 2
#   unsigned char source_registers[4];             // 4
#   unsigned long long destination_memory[2];      // 16
#   unsigned long long source_memory[4];           // 32
# }; // total 64 bytes

CHAMPSIM_INPUT_INSTR_SIZE = 64
CHAMPSIM_INPUT_INSTR_STRUCT = struct.Struct("<QBB2B4B2Q4Q")  # little-endian


@dataclass
class ChampSimInputInstr:
    ip: int
    is_branch: int
    branch_taken: int
    dst_regs: List[int]
    src_regs: List[int]
    dst_mem: List[int]
    src_mem: List[int]


def parse_champsim_input_instr(blob: bytes) -> ChampSimInputInstr:
    if len(blob) != CHAMPSIM_INPUT_INSTR_SIZE:
        raise ValueError(f"ChampSim record wrong size: got {len(blob)}, expected {CHAMPSIM_INPUT_INSTR_SIZE}")
    unpacked = CHAMPSIM_INPUT_INSTR_STRUCT.unpack(blob)
    ip = unpacked[0]
    is_branch = unpacked[1]
    branch_taken = unpacked[2]
    dst_regs = list(unpacked[3:5])
    src_regs = list(unpacked[5:9])
    dst_mem = list(unpacked[9:11])
    src_mem = list(unpacked[11:15])
    return ChampSimInputInstr(
        ip=ip,
        is_branch=is_branch,
        branch_taken=branch_taken,
        dst_regs=dst_regs,
        src_regs=src_regs,
        dst_mem=dst_mem,
        src_mem=src_mem,
    )


# ----------------------------
# CBP trace record parsing (from your trace_reader.h comment format)
# ----------------------------

# Your InstClass enum (you pasted earlier)
INSTCLASS_NAMES = {
    0: "aluInstClass",
    1: "loadInstClass",
    2: "storeInstClass",
    3: "condBranchInstClass",
    4: "uncondDirectBranchInstClass",
    5: "uncondIndirectBranchInstClass",
    6: "fpInstClass",
    7: "slowAluInstClass",
    8: "undefInstClass",
    9: "callDirectInstClass",
    10: "callIndirectInstClass",
    11: "ReturnInstClass",
}

# Register encoding rules from your comment:
# INT: 0-30(GPRs), 31(SP), 64(FLAGS), 65(ZERO)
# SIMD: 32-63
VEC_OFFSET = 32
CC_OFFSET = 64
ZERO_OFFSET = 65


def reg_is_int(reg: int) -> bool:
    return (reg < VEC_OFFSET) or (reg == CC_OFFSET) or (reg == ZERO_OFFSET)


@dataclass
class CbpRecord:
    pc: int
    inst_type: int
    is_mem: bool
    eff_addr: Optional[int]
    mem_size: Optional[int]
    base_upd: Optional[int]
    has_reg_offset: Optional[int]
    is_branch: bool
    taken: Optional[int]
    target: Optional[int]
    num_in: int
    in_regs: List[int]
    num_out: int
    out_regs: List[int]
    out_vals: List[int]  # u64 values; SIMD contributes two u64 (lo, hi)


def _read_exact(f: BinaryIO, n: int) -> bytes:
    data = f.read(n)
    if len(data) != n:
        raise EOFError
    return data


def _read_u8(f: BinaryIO) -> int:
    return struct.unpack("<B", _read_exact(f, 1))[0]


def _read_u64(f: BinaryIO) -> int:
    return struct.unpack("<Q", _read_exact(f, 8))[0]


def parse_cbp_record(f: BinaryIO) -> CbpRecord:
    # Inst PC - 8 bytes
    pc_bytes = f.read(8)
    if len(pc_bytes) == 0:
        raise EOFError
    if len(pc_bytes) != 8:
        raise EOFError
    pc = struct.unpack("<Q", pc_bytes)[0]

    # Inst Type - 1 byte
    inst_type = _read_u8(f)

    is_mem = inst_type in (1, 2)  # load/store per enum
    eff_addr = None
    mem_size = None
    base_upd = None
    has_reg_offset = None

    if is_mem:
        eff_addr = _read_u64(f)
        mem_size = _read_u8(f)
        base_upd = _read_u8(f)
        if inst_type == 2:  # store
            has_reg_offset = _read_u8(f)
        else:
            has_reg_offset = 0

    # Branch?
    is_branch = inst_type in (3, 4, 5, 9, 10, 11)  # cond/uncond/call/ret in your enum
    taken = None
    target = None
    if is_branch:
        taken = _read_u8(f)
        if taken != 0:
            target = _read_u64(f)

    # Num Input Regs - 1 byte
    num_in = _read_u8(f)
    in_regs = list(_read_exact(f, num_in))

    # Num Output Regs - 1 byte
    num_out = _read_u8(f)
    out_regs = list(_read_exact(f, num_out))

    # Output Reg Values:
    # For each out reg:
    #   If INT: 8 bytes
    #   If SIMD: 16 bytes (two u64)
    out_vals: List[int] = []
    for r in out_regs:
        lo = _read_u64(f)
        out_vals.append(lo)
        if not reg_is_int(r):
            hi = _read_u64(f)
            out_vals.append(hi)

    return CbpRecord(
        pc=pc,
        inst_type=inst_type,
        is_mem=is_mem,
        eff_addr=eff_addr,
        mem_size=mem_size,
        base_upd=base_upd,
        has_reg_offset=has_reg_offset,
        is_branch=is_branch,
        taken=taken,
        target=target,
        num_in=num_in,
        in_regs=in_regs,
        num_out=num_out,
        out_regs=out_regs,
        out_vals=out_vals,
    )


# ----------------------------
# Dump formatting
# ----------------------------

def _hex0(x: int) -> str:
    return f"0x{x:x}"


def dump_champsim_record(idx: int, rec: ChampSimInputInstr) -> str:
    # Print non-zero regs/mem like ChampSim inflates (it removes zeros)
    dst_regs = [r for r in rec.dst_regs if r != 0]
    src_regs = [r for r in rec.src_regs if r != 0]
    dst_mem = [a for a in rec.dst_mem if a != 0]
    src_mem = [a for a in rec.src_mem if a != 0]

    return (
        f"[{idx}] CHAMPSIM "
        f"ip={_hex0(rec.ip)} "
        f"is_branch={int(rec.is_branch)} "
        f"taken={int(rec.branch_taken)} "
        f"dst_regs={dst_regs} "
        f"src_regs={src_regs} "
        f"dst_mem={[ _hex0(a) for a in dst_mem ]} "
        f"src_mem={[ _hex0(a) for a in src_mem ]}"
    )


def dump_cbp_record(idx: int, rec: CbpRecord) -> str:
    tname = INSTCLASS_NAMES.get(rec.inst_type, f"InstClass({rec.inst_type})")
    parts = [
        f"[{idx}] CBP pc={_hex0(rec.pc)} type={tname}({rec.inst_type})",
    ]

    if rec.is_mem:
        parts.append(f"ea={_hex0(rec.eff_addr or 0)} size={rec.mem_size} base_upd={rec.base_upd} reg_off={rec.has_reg_offset}")

    if rec.is_branch:
        parts.append(f"taken={rec.taken}")
        if rec.taken:
            parts.append(f"target={_hex0(rec.target or 0)}")

    parts.append(f"in_regs={list(rec.in_regs)}")
    parts.append(f"out_regs={list(rec.out_regs)}")

    # values: show paired lo/hi for SIMD regs
    vals_strs: List[str] = []
    vi = 0
    for r in rec.out_regs:
        if reg_is_int(r):
            if vi >= len(rec.out_vals):
                vals_strs.append(f"{r}:<MISSING>")
            else:
                vals_strs.append(f"{r}:{_hex0(rec.out_vals[vi])}")
                vi += 1
        else:
            lo = rec.out_vals[vi] if vi < len(rec.out_vals) else None
            hi = rec.out_vals[vi + 1] if (vi + 1) < len(rec.out_vals) else None
            vals_strs.append(f"{r}:[lo={_hex0(lo or 0)} hi={_hex0(hi or 0)}]")
            vi += 2

    parts.append(f"out_vals={vals_strs}")
    return " ".join(parts)


# ----------------------------
# Main
# ----------------------------

def open_by_extension(path: str) -> BinaryIO:
    lower = path.lower()
    if lower.endswith(".xz"):
        return lzma.open(path, "rb")
    if lower.endswith(".gz"):
        return gzip.open(path, "rb")
    # allow uncompressed for debugging
    return open(path, "rb")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", required=True, help="input trace: ChampSim .xz or CBP .gz")
    ap.add_argument("-o", "--output", required=True, help="output text dump")
    ap.add_argument("-n", "--num", type=int, required=True, help="number of records/instructions to dump")
    args = ap.parse_args()

    in_path: str = args.input
    out_path: str = args.output
    n: int = args.num
    if n <= 0:
        raise SystemExit("-n must be > 0")

    lower = in_path.lower()
    is_champsim = lower.endswith(".xz")
    is_cbp = lower.endswith(".gz")

    if not (is_champsim or is_cbp):
        raise SystemExit("Input must end with .xz (ChampSim) or .gz (CBP) for this script")

    with open_by_extension(in_path) as f_in, open(out_path, "w", encoding="utf-8") as f_out:
        if is_champsim:
            # Dump fixed 64-byte input_instr records
            for i in range(n):
                blob = f_in.read(CHAMPSIM_INPUT_INSTR_SIZE)
                if len(blob) == 0:
                    break
                if len(blob) != CHAMPSIM_INPUT_INSTR_SIZE:
                    f_out.write(f"[{i}] CHAMPSIM <EOF_PARTIAL> bytes={len(blob)}\n")
                    break
                rec = parse_champsim_input_instr(blob)
                f_out.write(dump_champsim_record(i, rec) + "\n")

        else:
            # Dump CBP variable-length records
            for i in range(n):
                try:
                    rec = parse_cbp_record(f_in)
                except EOFError:
                    break
                f_out.write(dump_cbp_record(i, rec) + "\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

