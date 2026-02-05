#!/usr/bin/env bash
set -euo pipefail

# ---- Embedded selector lists ----
PREDICTORS=(hist1 behrendt cai cbp2016 fan jimenez koizumi man mose ros seznec tage192)
TRACE_GROUPS=(sample_traces sml compress fp infra int media web)

RESULTS_ROOT="results"
MANIFEST_DIR="manifests"
RUNNER="scripts/trace_exec_training_list.py"
PYTHON="${PYTHON:-python3}"

usage() {
  cat <<EOF
usage:
  $0 --pred <all|p1,p2,...> --group <all|g1,g2,...> [--dryrun]

examples:
  $0 --pred all --group int
  $0 --pred cbp2016,koizumi --group int,fp
  $0 --pred tage192 --group all
EOF
}

contains() {
  local needle="$1"; shift
  for x in "$@"; do
    [[ "$x" == "$needle" ]] && return 0
  done
  return 1
}

split_csv() {
  IFS=',' read -r -a _out <<< "$1"
  printf '%s\n' "${_out[@]}"
}

PSEL=""
GSEL=""
DRYRUN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pred)  PSEL="${2:-}"; shift 2 ;;
    --group) GSEL="${2:-}"; shift 2 ;;
    --dryrun) DRYRUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "-E: unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -z "$PSEL" || -z "$GSEL" ]]; then
  echo "-E: --pred and --group are required" >&2
  usage
  exit 2
fi

# Resolve predictors
SEL_PRED=()
if [[ "$PSEL" == "all" ]]; then
  SEL_PRED=("${PREDICTORS[@]}")
else
  while read -r p; do
    contains "$p" "${PREDICTORS[@]}" || {
      echo "-E: unknown predictor: $p" >&2
      exit 2
    }
    SEL_PRED+=("$p")
  done < <(split_csv "$PSEL")
fi

# Resolve trace groups
SEL_GRP=()
if [[ "$GSEL" == "all" ]]; then
  SEL_GRP=("${TRACE_GROUPS[@]}")
else
  while read -r g; do
    contains "$g" "${TRACE_GROUPS[@]}" || {
      echo "-E: unknown group: $g" >&2
      exit 2
    }
    SEL_GRP+=("$g")
  done < <(split_csv "$GSEL")
fi

# Execute
for p in "${SEL_PRED[@]}"; do
  CBP="./bin/cbp.${p}"
  [[ -x "$CBP" ]] || { echo "-E: missing $CBP" >&2; exit 1; }

  for g in "${SEL_GRP[@]}"; do
    MANIFEST="${MANIFEST_DIR}/${g}.txt"
    [[ -f "$MANIFEST" ]] || {
      echo "-E: missing manifest: $MANIFEST" >&2
      exit 1
    }

    OUTDIR="${RESULTS_ROOT}/${p}/${g}"
    mkdir -p "$OUTDIR"

    cmd=(
      "$PYTHON" "$RUNNER"
      --cbp_path "$CBP"
      --trace_list "$MANIFEST"
      --results_dir "$OUTDIR"
    )

    echo "=== predictor=$p group=$g ==="
    if [[ "$DRYRUN" -eq 1 ]]; then
      printf 'DRYRUN:'; printf ' %q' "${cmd[@]}"; printf '\n'
    else
      "${cmd[@]}"
    fi
  done
done

