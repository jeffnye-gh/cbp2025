#!/usr/bin/env python3
"""
Summarize workload characteristics from a *baseline* CBP results directory.

Uses only baseline logs to extract:
- total instructions (from "instructions = N")
- branch-type counts (NumBr only) from the "Type ... NumBr ..." table
  by matching the known row keys directly:
    CondDirect, JumpDirect, JumpIndirect, JumpReturn, Not control
- derived densities:
    * branch density        = (CondDirect+JumpDirect+JumpIndirect+JumpReturn)
                              / instructions
    * conditional density   = CondDirect / instructions
    * indirect density      = JumpIndirect / instructions
    * jump-direct density   = JumpDirect / instructions
    * return density        = JumpReturn / instructions

Expected layout (your example):
  results_root/
    <group>/
      <group>/
        *_trace.log
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Dict, List, Tuple


TYPE_ORDER = [
    "CondDirect",
    "JumpDirect",
    "JumpIndirect",
    "JumpReturn",
    "Not control",
]

RE_INSTR = re.compile(r"^\s*instructions\s*=\s*(\d+)\s*$", re.MULTILINE)

# Match the known branch-type rows and capture NumBr (2nd column).
# Example line:
# CondDirect         11747875     384858   3.2760%   3.2071
RE_TYPE_ROW = re.compile(
    r"^\s*(CondDirect|JumpDirect|JumpIndirect|JumpReturn|Not control)"
    r"\s+(\d+)\s+",
    re.MULTILINE,
)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument(
        "--results_root",
        required=True,
        help="baseline results dir, e.g. ./results/tage192",
    )
    p.add_argument(
        "--groups",
        nargs="*",
        default=[],
        help="optional subset of groups (default: auto-discover)",
    )
    p.add_argument(
        "--log_glob",
        default="*_trace.log",
        help="glob inside <group>/<group>/ (default: *_trace.log)",
    )
    p.add_argument(
        "--debug",
        action="store_true",
        help="print per-log parse diagnostics",
    )
    return p.parse_args()


def discover_groups(root: Path) -> List[str]:
    groups: List[str] = []
    for p in root.iterdir():
        if not p.is_dir():
            continue
        # Expect <group>/<group>/... logs
        if (p / p.name).is_dir():
            groups.append(p.name)
    return sorted(groups)


def parse_one_log(text: str) -> Tuple[int | None, Dict[str, int]]:
    instr_m = RE_INSTR.search(text)
    instr = int(instr_m.group(1)) if instr_m else None

    counts: Dict[str, int] = {}
    for m in RE_TYPE_ROW.finditer(text):
        t = m.group(1)
        n = int(m.group(2))
        counts[t] = n

    return instr, counts


def fmt_int(x: int, w: int) -> str:
    return f"{x:d}".rjust(w)


def fmt_float(x: float, w: int, prec: int = 6) -> str:
    return f"{x:.{prec}f}".rjust(w)


def main() -> int:
    args = parse_args()
    root = Path(args.results_root)
    if not root.is_dir():
        raise SystemExit(f"-E: not a directory: {root}")

    groups = args.groups or discover_groups(root)
    if not groups:
        raise SystemExit(f"-E: no groups found under {root}")

    instr_tot: Dict[str, int] = {g: 0 for g in groups}
    type_tot: Dict[str, Dict[str, int]] = {
        g: {t: 0 for t in TYPE_ORDER} for g in groups
    }

    parsed_logs = 0
    skipped_logs = 0

    for g in groups:
        log_dir = root / g / g
        if not log_dir.is_dir():
            if args.debug:
                print(f"-D: missing log dir: {log_dir}")
            continue

        logs = sorted(log_dir.glob(args.log_glob))
        if args.debug:
            print(f"-D: group={g} logs={len(logs)} dir={log_dir}")

        for lp in logs:
            try:
                text = lp.read_text(encoding="utf-8", errors="replace")
            except Exception as e:
                skipped_logs += 1
                if args.debug:
                    print(f"-D: skip read {lp}: {e}")
                continue

            instr, counts = parse_one_log(text)

            missing = []
            if instr is None:
                missing.append("instructions")
            for t in TYPE_ORDER:
                if t not in counts:
                    missing.append(t)

            if missing:
                skipped_logs += 1
                if args.debug:
                    miss_str = ", ".join(missing)
                    print(f"-D: skip parse {lp.name}: missing {miss_str}")
                continue

            instr_tot[g] += instr
            for t in TYPE_ORDER:
                type_tot[g][t] += counts[t]

            parsed_logs += 1
            if args.debug:
                print(
                    f"-D: parsed {lp.name}: instr={instr} "
                    f"CondDirect={counts['CondDirect']}"
                )

    if parsed_logs == 0:
        raise SystemExit(
            "-E: parsed 0 logs. Check --results_root and --log_glob."
        )

    # ---- Table 1: branch type totals ----
    type_w = max(len("Type"), max(len(t) for t in TYPE_ORDER))
    col_w = max(14, max(len(g) for g in groups))

    print("\nBranch-type counts (baseline, NumBr only)\n")
    hdr = "Type".ljust(type_w) + "  " + "  ".join(
        g.upper().rjust(col_w) for g in groups
    )
    print(hdr)
    print("-" * len(hdr))

    for t in TYPE_ORDER:
        row = [t.ljust(type_w)]
        for g in groups:
            row.append(fmt_int(type_tot[g][t], col_w))
        print("  ".join(row))

    # ---- Table 2: densities ----
    print("\nDerived densities (baseline)\n")
    hdr2 = (
        "Group".ljust(10)
        + "  Instr".rjust(col_w)
        + "  Br/Instr".rjust(col_w)
        + "  Cond/Instr".rjust(col_w)
        + "  Indir/Instr".rjust(col_w)
        + "  JumpDir/Instr".rjust(col_w)
        + "  Ret/Instr".rjust(col_w)
    )
    print(hdr2)
    print("-" * len(hdr2))

    for g in groups:
        instr = instr_tot[g]
        if instr == 0:
            continue

        total_br = (
            type_tot[g]["CondDirect"]
            + type_tot[g]["JumpDirect"]
            + type_tot[g]["JumpIndirect"]
            + type_tot[g]["JumpReturn"]
        )

        br_density = total_br / instr
        cond_density = type_tot[g]["CondDirect"] / instr
        indir_density = type_tot[g]["JumpIndirect"] / instr
        jumpdir_density = type_tot[g]["JumpDirect"] / instr
        ret_density = type_tot[g]["JumpReturn"] / instr

        print(
            f"{g.ljust(10)}"
            f"  {fmt_int(instr, col_w)}"
            f"  {fmt_float(br_density, col_w)}"
            f"  {fmt_float(cond_density, col_w)}"
            f"  {fmt_float(indir_density, col_w)}"
            f"  {fmt_float(jumpdir_density, col_w)}"
            f"  {fmt_float(ret_density, col_w)}"
        )

    if args.debug:
        print(
            f"\n-D: parsed_logs={parsed_logs} skipped_logs={skipped_logs}\n"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

