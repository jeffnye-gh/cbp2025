#!/usr/bin/env python3
"""
Emit CBP2025-style text tables from per-predictor results.csv files.

Adds optional "alias" names for entries:
  - --show_aliases: if set, print "entry (ALIAS)" where mapping exists.
  - Mapping is embedded in ENTRY_ALIASES below.

Input layout:
  results_root/
    <predictor>/
      <group>/
        results.csv
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Dict, List, Tuple

import pandas as pd


GROUP_ORDER_DEFAULT = ["int", "fp", "web", "media", "compress", "infra"]

# Optional display aliases (repo-dir-name -> CBP paper/slide name)
ENTRY_ALIASES: Dict[str, str] = {
    "behrendt": "BullsEye",
    "cai": "CS-TAGE",
    "fan": "BALL",
    "jimenez": "MPP2025",
    "koizumi": "RUNLTS",
    "man": "LVCP",
    "mose": "PIP",
    "seznec": "TAGE2025",
    "ros": "DD-TAGE",
}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--results_root", required=True)
    p.add_argument("--baseline", required=True)
    p.add_argument(
        "--groups",
        nargs="+",
        default=GROUP_ORDER_DEFAULT,
        help="groups to include (default: int fp web media compress infra)",
    )
    p.add_argument(
        "--use_full_window",
        action="store_true",
        help="use full-sim IPC/MPKI columns instead of 50Perc* columns",
    )
    p.add_argument(
        "--drop_fail",
        action="store_true",
        default=True,
        help="drop rows where Status != Pass (default: on)",
    )
    p.add_argument(
        "--show_aliases",
        action="store_true",
        help="append alternative entry name if known (e.g. koizumi (RUNLTS))",
    )
    return p.parse_args()


def _load_one_csv(path: Path) -> pd.DataFrame:
    return pd.read_csv(path)


def load_group(
    results_root: Path,
    predictor: str,
    group: str,
) -> pd.DataFrame | None:
    p = results_root / predictor / group / "results.csv"
    if not p.exists():
        return None
    return _load_one_csv(p)


def coerce_numeric(df: pd.DataFrame, cols: List[str]) -> None:
    for c in cols:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")


def pct(x: float) -> float:
    return x * 100.0


def _fmt_pct(x: float, width: int = 8, prec: int = 2) -> str:
    if pd.isna(x):
        return " " * (width - 3) + "nan"
    s = f"{x:.{prec}f}"
    return s.rjust(width)


def _fmt_int(x: int, width: int = 5) -> str:
    return f"{x:d}".rjust(width)


#def _display_name(pred: str, show_aliases: bool) -> str:
#    if not show_aliases:
#        return pred
#    alias = ENTRY_ALIASES.get(pred)
#    if not alias:
#        return pred
#    return f"{pred} ({alias})"

def _display_name(pred: str, show_aliases: bool) -> str:
    if not show_aliases:
        return pred
    return ENTRY_ALIASES.get(pred, pred)


def compute_reductions_for_group(
    base_df: pd.DataFrame,
    pred_df: pd.DataFrame,
    run_col: str,
    br_col: str,
    wp_col: str,
) -> Tuple[pd.Series, pd.Series, int]:
    base_small = base_df[[run_col, br_col, wp_col]].rename(
        columns={
            br_col: f"{br_col}_base",
            wp_col: f"{wp_col}_base",
        }
    )
    joined = pred_df[[run_col, br_col, wp_col]].merge(
        base_small, on=run_col, how="inner"
    )
    joined = joined.dropna(
        subset=[br_col, wp_col, f"{br_col}_base", f"{wp_col}_base"]
    )

    joined = joined[
        (joined[f"{br_col}_base"] > 0) & (joined[f"{wp_col}_base"] > 0)
    ].copy()

    br_red = 1.0 - (joined[br_col] / joined[f"{br_col}_base"])
    wp_red = 1.0 - (joined[wp_col] / joined[f"{wp_col}_base"])
    return br_red, wp_red, int(len(joined))


def table_overall(
    title: str,
    metric_label: str,
    overall_map: Dict[str, Tuple[float, int]],
    show_aliases: bool,
) -> str:
    rows = sorted(overall_map.items(), key=lambda kv: kv[1][0], reverse=True)

    lines: List[str] = []
    lines.append(title)
    lines.append(f"{metric_label} Reduction (%)  (vs baseline)")
    lines.append("")

    # Keep table readable: widen Entry col if aliases are enabled.
    entry_w = 28 if show_aliases else 18

    hdr = (
        f"{'Entry'.ljust(entry_w)}  "
        f"{'Pairs'.rjust(5)}  "
        f"{'Reduction%'.rjust(10)}"
    )
    lines.append(hdr)
    lines.append("-" * len(hdr))

    for pred, (red, pairs) in rows:
        name = _display_name(pred, show_aliases)
        lines.append(
            f"{name.ljust(entry_w)}  {_fmt_int(pairs, 5)}  "
            f"{_fmt_pct(pct(red), 10, 4)}"
        )

    return "\n".join(lines)


def table_top3_by_group(
    title: str,
    groups: List[str],
    per_group_map: Dict[str, Dict[str, Tuple[float, int]]],
    avg_map: Dict[str, Tuple[float, int]],
    show_aliases: bool,
) -> str:
    lines: List[str] = []
    lines.append(title)
    lines.append("(values are Reduction% vs baseline; top-3 per group)")
    lines.append("")

    def fmt_rank(pred: str, red: float) -> str:
        name = _display_name(pred, show_aliases)
        return f"{name}:{pct(red):.2f}%"

    # Widen rank columns if aliases are enabled.
    rank_w = 34 if show_aliases else 24

    hdr = (
        f"{'Group'.ljust(10)}  "
        f"{'Rank1'.ljust(rank_w)}  "
        f"{'Rank2'.ljust(rank_w)}  "
        f"{'Rank3'.ljust(rank_w)}"
    )
    lines.append(hdr)
    lines.append("-" * len(hdr))

    for g in groups:
        d = per_group_map.get(g, {})
        top = sorted(d.items(), key=lambda kv: kv[1][0], reverse=True)[:3]

        r: List[str] = []
        for pred, (red, _pairs) in top:
            r.append(fmt_rank(pred, red))
        while len(r) < 3:
            r.append("")

        lines.append(
            f"{g.ljust(10)}  "
            f"{r[0].ljust(rank_w)}  "
            f"{r[1].ljust(rank_w)}  "
            f"{r[2].ljust(rank_w)}"
        )

    lines.append("")
    lines.append("Average (ALL traces across selected groups):")
    avg_rows = sorted(avg_map.items(), key=lambda kv: kv[1][0], reverse=True)[:3]
    for i, (pred, (red, pairs)) in enumerate(avg_rows, start=1):
        name = _display_name(pred, show_aliases)
        lines.append(
            f"  Rank{i}: {name}  Reduction={pct(red):.2f}%  Pairs={pairs}"
        )

    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    results_root = Path(args.results_root)

    run_col = "Run"

    if args.use_full_window:
        br_col = "MPKI"
        wp_col = "CycWPPKI"
    else:
        br_col = "50PercMPKI"
        wp_col = "50PercCycWPPKI"

    seen = set()
    groups = [g for g in args.groups if not (g in seen or seen.add(g))]

    predictors = sorted([p.name for p in results_root.iterdir() if p.is_dir()])
    if args.baseline not in predictors:
        raise SystemExit(
            f"-E: baseline '{args.baseline}' not under {results_root}"
        )

    others = [p for p in predictors if p != args.baseline]
    if not others:
        raise SystemExit("-E: no non-baseline predictors found")

    br_group_map: Dict[str, Dict[str, Tuple[float, int]]] = {}
    wp_group_map: Dict[str, Dict[str, Tuple[float, int]]] = {}

    br_all_vals: Dict[str, List[float]] = {p: [] for p in others}
    wp_all_vals: Dict[str, List[float]] = {p: [] for p in others}
    all_pairs: Dict[str, int] = {p: 0 for p in others}

    for g in groups:
        base = load_group(results_root, args.baseline, g)
        if base is None:
            continue

        if args.drop_fail and "Status" in base.columns:
            base = base[base["Status"] == "Pass"].copy()

        coerce_numeric(base, [br_col, wp_col])
        base = base.dropna(subset=[run_col, br_col, wp_col])

        br_group_map[g] = {}
        wp_group_map[g] = {}

        for pred in others:
            dfp = load_group(results_root, pred, g)
            if dfp is None:
                continue

            if args.drop_fail and "Status" in dfp.columns:
                dfp = dfp[dfp["Status"] == "Pass"].copy()

            coerce_numeric(dfp, [br_col, wp_col])
            dfp = dfp.dropna(subset=[run_col, br_col, wp_col])

            br_red, wp_red, pairs = compute_reductions_for_group(
                base_df=base,
                pred_df=dfp,
                run_col=run_col,
                br_col=br_col,
                wp_col=wp_col,
            )
            if pairs == 0:
                continue

            br_mean = float(br_red.mean())
            wp_mean = float(wp_red.mean())

            br_group_map[g][pred] = (br_mean, pairs)
            wp_group_map[g][pred] = (wp_mean, pairs)

            br_all_vals[pred].extend(br_red.tolist())
            wp_all_vals[pred].extend(wp_red.tolist())
            all_pairs[pred] += pairs

    br_overall: Dict[str, Tuple[float, int]] = {}
    wp_overall: Dict[str, Tuple[float, int]] = {}
    for pred in others:
        if br_all_vals[pred]:
            br_overall[pred] = (
                float(pd.Series(br_all_vals[pred]).mean()),
                all_pairs[pred],
            )
        if wp_all_vals[pred]:
            wp_overall[pred] = (
                float(pd.Series(wp_all_vals[pred]).mean()),
                all_pairs[pred],
            )

    print()
    print(
        table_overall(
            title="Results – Overall BrMisPKI",
            metric_label="BrMisPKI",
            overall_map=br_overall,
            show_aliases=args.show_aliases,
        )
    )
    print("\n" + "=" * 78 + "\n")
    print(
        table_overall(
            title="Results – Overall CycWpPKI",
            metric_label="CycWpPKI",
            overall_map=wp_overall,
            show_aliases=args.show_aliases,
        )
    )
    print("\n" + "=" * 78 + "\n")
    print(
        table_top3_by_group(
            title="Results – Top-3 Per Workload Category (BrMisPKI, Full)",
            groups=groups,
            per_group_map=br_group_map,
            avg_map=br_overall,
            show_aliases=args.show_aliases,
        )
    )
    print("\n" + "=" * 78 + "\n")
    print(
        table_top3_by_group(
            title="Results – Top-3 Per Workload Category (CycWpPKI, Full)",
            groups=groups,
            per_group_map=wp_group_map,
            avg_map=wp_overall,
            show_aliases=args.show_aliases,
        )
    )
    print()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

