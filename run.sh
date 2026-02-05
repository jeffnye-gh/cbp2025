#!/usr/bin/env bash
set -euo pipefail

MANIFEST=manifests/all_traces.txt
OUTROOT=results

declare -A P
#P[cbp2016]=./bin/cbp.cbp2016
P[tage192]=./bin/cbp.tage192
P[behrendt]=./bin/cbp.behrendt
P[cai]=./bin/cbp.cai
P[fan]=./bin/cbp.fan
P[jimenez]=./bin/cbp.jimenez
P[koizumi]=./bin/cbp.koizumi
P[man]=./bin/cbp.man
P[mose]=./bin/cbp.mose
P[ros]=./bin/cbp.ros
P[seznec]=./bin/cbp.seznec

for name in "${!P[@]}"; do
  python3 scripts/trace_exec_training_list.py \
    --predictor "${name}" \
    --cbp_path "${P[$name]}" \
    --trace_list "${MANIFEST}" \
    --results_dir "${OUTROOT}/${name}"
done

