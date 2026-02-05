#!/usr/bin/env python3
"""
Run CBP traces and extract key IPC/MPKI metrics.

Behavior:
- Accepts either:
    --trace_list <manifest.txt>   (preferred; one trace path per line)
  or
    --trace_dir <dir>            (walks dir recursively for *_trace.gz)
- Uses --cbp_path (required) to select the cbp binary.
- Avoids shell=True; uses subprocess.run([...]) for robust paths.
- Writes per-trace logs under results_dir/<group>/ if trace paths contain a
  group directory (traces/<group>/...); otherwise uses the parent directory
  name as the group.
- Produces results_dir/results.csv

Notes:
- Uses 50Perc and Full sections as emitted by cbp.
- Keeps parsing strict: if required lines are missing, values remain 0.
"""

from __future__ import annotations

import argparse
import datetime
import multiprocessing as mp
import os
import re
import subprocess
import time
from pathlib import Path
from typing import List, Tuple

import pandas as pd


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument(
        "--trace_dir",
        help="path to trace directory (used if --trace_list not provided)",
        required=False,
    )
    parser.add_argument(
        "--trace_list",
        help="path to manifest file (one .gz trace per line); overrides "
        "--trace_dir",
        required=False,
    )
    parser.add_argument(
        "--results_dir",
        help="path to results directory (output)",
        required=True,
    )
    parser.add_argument(
        "--cbp_path",
        help="path to cbp binary (e.g. ./bin/cbp.cbp2016)",
        required=True,
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=0,
        help="parallel workers (0 => use mp default / cpu count)",
    )
    return parser.parse_args()


def get_trace_paths(start_path: Path) -> List[str]:
    ret_list: List[str] = []
    for root, _, files in os.walk(start_path):
        for my_file in files:
            if my_file.endswith("_trace.gz"):
                ret_list.append(os.path.join(root, my_file))
    return ret_list


def get_trace_paths_from_list(list_path: Path) -> List[str]:
    traces: List[str] = []
    with open(list_path, "r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            traces.append(s)
    return traces


def trace_group_from_path(trace_path: str) -> str:
    """
    For paths like traces/int/int_0_trace.gz -> 'int'
    Otherwise fall back to parent directory name.
    """
    p = Path(trace_path)
    parts = p.parts
    # Look for ".../traces/<group>/..."
    for i in range(len(parts) - 1):
        if parts[i] == "traces" and i + 1 < len(parts):
            return parts[i + 1]
    return p.parent.name


def trace_run_name(trace_path: str) -> str:
    """
    Stable run name from filename; assumes no extra dots beyond '.gz'.
    'int_10_trace.gz' -> 'int_10_trace'
    """
    name = Path(trace_path).name
    if name.endswith(".gz"):
        name = name[:-3]
    return name


def process_run_op(
    pass_status: bool,
    my_trace_path: str,
    op_file: str,
) -> dict:
    group = trace_group_from_path(my_trace_path)
    run_name = trace_run_name(my_trace_path)

    print(f"Extracting data from: {op_file} | Group:{group} | Run:{run_name}")

    exec_time = 0

    # Full simulation metrics (100%)
    _Instr = 0
    _Cycles = 0
    _IPC = 0
    _NumBr = 0
    _MispBr = 0
    _BrPerCyc = 0
    _MispBrPerCyc = 0
    _MR = 0
    _MPKI = 0
    _CycWP = 0
    _CycWPAvg = 0
    _CycWPPKI = 0

    # 50% metrics
    _50PercInstr = 0
    _50PercCycles = 0
    _50PercIPC = 0
    _50PercNumBr = 0
    _50PercMispBr = 0
    _50PercBrPerCyc = 0
    _50PercMispBrPerCyc = 0
    _50PercMR = 0
    _50PercMPKI = 0
    _50PercCycWP = 0
    _50PercCycWPAvg = 0
    _50PercCycWPPKI = 0

    trace_size = os.path.getsize(my_trace_path) / (1024 * 1024)
    pass_status_str = "Fail"

    header_50 = (
        "DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS "
        "(50 Perc instructions)"
    )
    header_100 = (
        "DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS "
        "(Full Simulation i.e. Counts Not Reset When Warmup Ends)"
    )

    header_fields = [
        "Instr",
        "Cycles",
        "IPC",
        "NumBr",
        "MispBr",
        "BrPerCyc",
        "MispBrPerCyc",
        "MR",
        "MPKI",
        "CycWP",
        "CycWPAvg",
    ]

    process_50 = False
    process_100 = False
    found_50 = False
    found_100 = False

    if pass_status:
        pass_status_str = "Pass"
        with open(op_file, "r", encoding="utf-8") as f:
            for line in f:
                if not line.strip():
                    continue

                if "ExecTime" in line:
                    exec_time = line.strip().split()[-1]

                if (not process_50) and (header_50 in line):
                    process_50 = True
                    process_100 = False
                    found_50 = False
                    continue

                if (not process_100) and (header_100 in line):
                    process_50 = False
                    process_100 = True
                    found_100 = False
                    continue

                if process_50:
                    if found_50:
                        curr = line.split()
                        if len(curr) >= 12:
                            _50PercInstr = curr[0]
                            _50PercCycles = curr[1]
                            _50PercIPC = curr[2]
                            _50PercNumBr = curr[3]
                            _50PercMispBr = curr[4]
                            _50PercBrPerCyc = curr[5]
                            _50PercMispBrPerCyc = curr[6]
                            _50PercMR = curr[7]
                            _50PercMPKI = curr[8]
                            _50PercCycWP = curr[9]
                            _50PercCycWPAvg = curr[10]
                            _50PercCycWPPKI = curr[11]
                        process_50 = False
                        found_50 = False
                        continue

                    if all(x in line for x in header_fields):
                        found_50 = True
                        continue

                if process_100:
                    if found_100:
                        curr = line.split()
                        if len(curr) >= 12:
                            _Instr = curr[0]
                            _Cycles = curr[1]
                            _IPC = curr[2]
                            _NumBr = curr[3]
                            _MispBr = curr[4]
                            _BrPerCyc = curr[5]
                            _MispBrPerCyc = curr[6]
                            _MR = curr[7]
                            _MPKI = curr[8]
                            _CycWP = curr[9]
                            _CycWPAvg = curr[10]
                            _CycWPPKI = curr[11]
                        process_100 = False
                        found_100 = False
                        continue

                    if all(x in line for x in header_fields):
                        found_100 = True
                        continue

    return {
        "Workload": group,
        "Run": run_name,
        "TraceSize": trace_size,
        "Status": pass_status_str,
        "ExecTime": exec_time,
        "Instr": _Instr,
        "Cycles": _Cycles,
        "IPC": _IPC,
        "NumBr": _NumBr,
        "MispBr": _MispBr,
        "BrPerCyc": _BrPerCyc,
        "MispBrPerCyc": _MispBrPerCyc,
        "MR": _MR,
        "MPKI": _MPKI,
        "CycWP": _CycWP,
        "CycWPAvg": _CycWPAvg,
        "CycWPPKI": _CycWPPKI,
        "50PercInstr": _50PercInstr,
        "50PercCycles": _50PercCycles,
        "50PercIPC": _50PercIPC,
        "50PercNumBr": _50PercNumBr,
        "50PercMispBr": _50PercMispBr,
        "50PercBrPerCyc": _50PercBrPerCyc,
        "50PercMispBrPerCyc": _50PercMispBrPerCyc,
        "50PercMR": _50PercMR,
        "50PercMPKI": _50PercMPKI,
        "50PercCycWP": _50PercCycWP,
        "50PercCycWPAvg": _50PercCycWPAvg,
        "50PercCycWPPKI": _50PercCycWPPKI,
    }


def execute_trace(
    cbp_path: str,
    results_dir: str,
    my_trace_path: str,
) -> Tuple[bool, str, str]:
    if not os.path.exists(my_trace_path):
        raise FileNotFoundError(my_trace_path)
    if not os.path.exists(cbp_path):
        raise FileNotFoundError(cbp_path)

    group = trace_group_from_path(my_trace_path)
    run_name = trace_run_name(my_trace_path)

    out_group_dir = Path(results_dir) / group
    out_group_dir.mkdir(parents=True, exist_ok=True)

    op_file = str(out_group_dir / f"{run_name}.log")

# Who the f* thought this was a good idea ?
# just silently not run something and return true
#    # If output already exists, don't rerun.
#    if os.path.exists(op_file):
#        return (True, my_trace_path, op_file)

    print(f"Begin processing run:{group}/{run_name}")

    pass_status = True
    try:
        begin_time = time.time()
        proc = subprocess.run(
            [cbp_path, my_trace_path],
            check=True,
            text=True,
            capture_output=True,
        )
        end_time = time.time()
        exec_time = end_time - begin_time

        with open(op_file, "w", encoding="utf-8") as f:
            print(f"CMD:{cbp_path} {my_trace_path}", file=f)
            print(proc.stdout, file=f)
            if proc.stderr:
                print(proc.stderr, file=f)
            print(f"ExecTime = {exec_time}", file=f)

    except Exception:
        print(f"-E: Run failed for {group}/{run_name}")
        pass_status = False

    return (pass_status, my_trace_path, op_file)


def main() -> int:
    args = parse_args()
    results_dir = Path(args.results_dir)
    cbp_path = str(Path(args.cbp_path))

    if args.trace_list:
        traces = get_trace_paths_from_list(Path(args.trace_list))
    else:
        if not args.trace_dir:
            raise SystemExit(
                "-E: must provide --trace_list or --trace_dir"
            )
        traces = get_trace_paths(Path(args.trace_dir))

    print(f"Got {len(traces)} traces")

    _timestamp = datetime.datetime.now().strftime("%m_%d_%H-%M-%S")
    results_dir.mkdir(parents=True, exist_ok=True)

    tasks = [(cbp_path, str(results_dir), t) for t in traces]

    if args.jobs and args.jobs > 0:
        pool = mp.Pool(processes=args.jobs)
    else:
        pool = mp.Pool()

    with pool:
        results = pool.starmap(execute_trace, tasks)

    columns = [
        "Workload",
        "Run",
        "TraceSize",
        "Status",
        "ExecTime",
        "Instr",
        "Cycles",
        "IPC",
        "NumBr",
        "MispBr",
        "BrPerCyc",
        "MispBrPerCyc",
        "MR",
        "MPKI",
        "CycWP",
        "CycWPAvg",
        "CycWPPKI",
        "50PercInstr",
        "50PercCycles",
        "50PercIPC",
        "50PercNumBr",
        "50PercMispBr",
        "50PercBrPerCyc",
        "50PercMispBrPerCyc",
        "50PercMR",
        "50PercMPKI",
        "50PercCycWP",
        "50PercCycWPAvg",
        "50PercCycWPPKI",
    ]

    rows: List[dict] = []
    for pass_status, trace_path, op_file in results:
        row = process_run_op(
            pass_status=pass_status,
            my_trace_path=trace_path,
            op_file=op_file,
        )
        rows.append(row)

    df = pd.DataFrame(rows, columns=columns)
    print(df)
    df.to_csv(str(results_dir / "results.csv"), index=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

