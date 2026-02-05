#!/usr/bin/env python3
"""
Merge CBP results across predictors for one or more groups and summarize vs
baseline.

Expected layout:
  results_root/
    <predictor>/
      <group>/
        results.csv

Writes:
- combined long CSV: out_csv
- summary vs baseline: summary_csv

Summary (default uses 50PercIPC/50PercMPKI):
- per-group and ALL:
  - IPC geomean speedup vs baseline
  - IPC mean delta vs baseline
  - MPKI mean delta vs baseline
  - MPKI mean reduction vs baseline

Adds spreadsheet-friendly percent columns (numeric; let sheet format):
- IPC_geomean_speedup_pct = (IPC_geomean_speedup - 1) * 100
- MPKI_mean_reduction_pct = MPKI_mean_reduction * 100
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Iterable

import pandas as pd


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--results_root", required=True)
    p.add_argument("--out_csv", required=True)
    p.add_argument("--summary_csv", required=True)
    p.add_argument("--baseline", required=True)
    p.add_argument(
        "--groups",
        nargs="+",
        required=True,
        help="one or more of: compress fp infra int media web",
    )
    p.add_argument("--drop_fail", action="store_true")
    p.add_argument("--use_full", action="store_true")
    return p.parse_args()


def geomean(values: Iterable[float]) -> float:
    vals = [v for v in values if v is not None and not math.isnan(v) and v > 0]
    if not vals:
        return float("nan")
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def load_predictor_group_csv(
    pred_dir: Path,
    predictor: str,
    group: str,
) -> pd.DataFrame | None:
    csv_path = pred_dir / group / "results.csv"
    if not csv_path.exists():
        return None

    df = pd.read_csv(csv_path)

    if "Predictor" not in df.columns:
        df.insert(0, "Predictor", predictor)

    df["Predictor"] = df["Predictor"].fillna(predictor)
    df.loc[df["Predictor"] == "", "Predictor"] = predictor

    # Force Workload to the group label (runner already does this, but keep
    # merge stable if that changes later).
    df["Workload"] = group
    return df


def coerce_numeric(df: pd.DataFrame, cols: list[str]) -> pd.DataFrame:
    for c in cols:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    return df


def main() -> int:
    args = parse_args()
    results_root = Path(args.results_root)
    out_csv = Path(args.out_csv)
    summary_csv = Path(args.summary_csv)

    if args.use_full:
        ipc_col = "IPC"
        mpki_col = "MPKI"
    else:
        ipc_col = "50PercIPC"
        mpki_col = "50PercMPKI"

    frames: list[pd.DataFrame] = []

    for pred_dir in sorted(results_root.iterdir()):
        if not pred_dir.is_dir():
            continue

        predictor = pred_dir.name
        for g in args.groups:
            df = load_predictor_group_csv(pred_dir, predictor, g)
            if df is None:
                continue
            if args.drop_fail and "Status" in df.columns:
                df = df[df["Status"] == "Pass"].copy()
            frames.append(df)

    if not frames:
        raise RuntimeError("No results.csv files found for given groups.")

    merged = pd.concat(frames, ignore_index=True)
    merged = coerce_numeric(merged, [ipc_col, mpki_col])

    # Write combined long CSV.
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    merged.to_csv(out_csv, index=False)

    # Summary vs baseline (paired by Workload+Run).
    if args.baseline not in set(merged["Predictor"].unique()):
        raise RuntimeError(
            f"Baseline '{args.baseline}' not found. "
            f"Found: {sorted(set(merged['Predictor']))}"
        )

    for c in ["Workload", "Run", ipc_col, mpki_col]:
        if c not in merged.columns:
            raise RuntimeError(f"Missing required column: {c}")

    base = merged[merged["Predictor"] == args.baseline].copy()
    oth = merged[merged["Predictor"] != args.baseline].copy()

    base = base.dropna(subset=[ipc_col, mpki_col])

    keys = ["Workload", "Run"]
    base_small = base[keys + [ipc_col, mpki_col]].rename(
        columns={
            ipc_col: f"{ipc_col}_base",
            mpki_col: f"{mpki_col}_base",
        }
    )

    joined = oth.merge(base_small, on=keys, how="inner")
    joined = joined.dropna(
        subset=[ipc_col, mpki_col, f"{ipc_col}_base", f"{mpki_col}_base"]
    )

    joined["ipc_speedup"] = joined[ipc_col] / joined[f"{ipc_col}_base"]
    joined["ipc_delta"] = joined[ipc_col] - joined[f"{ipc_col}_base"]
    joined["mpki_delta"] = joined[mpki_col] - joined[f"{mpki_col}_base"]
    joined["mpki_reduction"] = 1.0 - (
        joined[mpki_col] / joined[f"{mpki_col}_base"]
    )

    def summarize(df_in: pd.DataFrame, group_name: str) -> pd.DataFrame:
        rows: list[dict] = []
        for pred in sorted(df_in["Predictor"].unique()):
            dfp = df_in[df_in["Predictor"] == pred]
            ipc_gm = geomean(dfp["ipc_speedup"].tolist())
            mpki_red = float(dfp["mpki_reduction"].mean()) if len(dfp) else float(
                "nan"
            )

            rows.append(
                {
                    "Group": group_name,
                    "Baseline": args.baseline,
                    "Predictor": pred,
                    "MetricIPC": ipc_col,
                    "MetricMPKI": mpki_col,
                    "Pairs": int(len(dfp)),
                    "IPC_geomean_speedup": ipc_gm,
                    "IPC_geomean_speedup_pct": (ipc_gm - 1.0) * 100.0
                    if not math.isnan(ipc_gm)
                    else float("nan"),
                    "IPC_mean_delta": float(dfp["ipc_delta"].mean())
                    if len(dfp)
                    else float("nan"),
                    "MPKI_mean_delta": float(dfp["mpki_delta"].mean())
                    if len(dfp)
                    else float("nan"),
                    "MPKI_mean_reduction": mpki_red,
                    "MPKI_mean_reduction_pct": mpki_red * 100.0
                    if not math.isnan(mpki_red)
                    else float("nan"),
                }
            )
        return pd.DataFrame(rows)

    summaries: list[pd.DataFrame] = []
    for g in sorted(joined["Workload"].unique()):
        summaries.append(summarize(joined[joined["Workload"] == g], g))
    summaries.append(summarize(joined, "ALL"))

    summary = pd.concat(summaries, ignore_index=True)
    summary_csv.parent.mkdir(parents=True, exist_ok=True)
    summary.to_csv(summary_csv, index=False)

    print(f"Wrote merged:  {out_csv}")
    print(f"Wrote summary: {summary_csv}")
    print(f"Baseline: {args.baseline}")
    print(f"Metrics: {ipc_col}, {mpki_col}")
    print(f"Groups: {args.groups}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

