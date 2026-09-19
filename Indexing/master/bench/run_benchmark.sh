#!/usr/bin/env bash
# Runs one configuration of the code tree comparison on the whole TPTP library.
#
#   run_benchmark.sh CONFIG          e.g.  run_benchmark.sh t60
#
# CONFIG selects codetree-compare-CONFIG.xml next to this script. Everything is written to
#   $WORK/CONFIG/   (bin/, benchmark.xml, results/, benchexec.log, codetree_compare.csv, .pdf)
#
# Environment (defaults in brackets):
#   TPTP_DIR  TPTP root                              [/home/mleiwig/TPTP-v9.3.0]
#             (not the variable $TPTP: that may be set system-wide to another TPTP version)
#   BUILD     Vampire build dir with ./vampire       [<repo>/build]  (needs -DTIME_PROFILING=ON)
#   WORK      where results go                       [/home/mleiwig/codetree-compare]
#   PARALLEL  number of concurrent Vampire runs      [60]
#   TASKSET_FILE  list of problem files to run         [$WORK/tptp_all.set, all of TPTP]
set -euo pipefail

CONFIG=${1:?usage: run_benchmark.sh CONFIG}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
TPTP_DIR=${TPTP_DIR:-/home/mleiwig/TPTP-v9.3.0}
export TPTP=$TPTP_DIR   # used by Vampire to resolve include(...)
BUILD=${BUILD:-$REPO/build}
WORK=${WORK:-/home/mleiwig/codetree-compare}
PARALLEL=${PARALLEL:-60}
OUT=$WORK/$CONFIG

[ -f "$HERE/codetree-compare-$CONFIG.xml" ] || { echo "no such configuration: $CONFIG"; exit 1; }
[ -x "$BUILD/vampire" ] || { echo "no $BUILD/vampire; build with -DTIME_PROFILING=ON first"; exit 1; }
[ ! -e "$OUT/results" ] || { echo "$OUT/results already exists, refusing to overwrite"; exit 1; }
mkdir -p "$OUT/bin" "$OUT/results"

# 1. all problems of the library (unless a task set is given)
if [ -z "${TASKSET_FILE:-}" ]; then
  TASKSET_FILE=$WORK/tptp_all.set
  mkdir -p "$WORK"
  [ -f "$TASKSET_FILE" ] || find "$TPTP_DIR/Problems" -name '*.p' | sort > "$TASKSET_FILE"
fi

# 2. snapshot the binary (so rebuilding while the run is going is harmless) and the setup
cp "$BUILD/vampire" "$OUT/bin/vampire"
sed "s#TASKSET#$TASKSET_FILE#" "$HERE/codetree-compare-$CONFIG.xml" > "$OUT/benchmark.xml"
{ echo "date:   $(date -Is)"; echo "commit: $(git -C "$REPO" rev-parse HEAD)"
  echo "dirty:"; git -C "$REPO" status --short; } > "$OUT/provenance.txt"

# 3. run; --no-container as in vbenchmarking/compare_branches.py
cd "$OUT"
benchexec --no-container -N "$PARALLEL" --tool-directory "$OUT/bin" --outputpath results/ \
  benchmark.xml > benchexec.log 2>&1

# 4. one CSV row per problem, then the plot
python3 "$HERE/extract_csv.py" results/*.results.*.xml.bz2 results/*.logfiles.zip codetree_compare.csv
python3 "$HERE/plot_codetree_compare.py" codetree_compare.csv codetree_compare.pdf \
  || echo "plotting failed; rerun plot_codetree_compare.py by hand"
