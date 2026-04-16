#!/usr/bin/env bash
set -euo pipefail

make >/dev/null
mkdir -p data

measure_ms() {
  local label="$1"
  local sql_file="$2"
  local out_file="$3"
  local err_file="$4"
  local start_ms
  local end_ms

  start_ms=$(date +%s%3N)
  if ! timeout 300s ./sql_processor "$sql_file" >"$out_file" 2>"$err_file"; then
    end_ms=$(date +%s%3N)
    echo "$label TIMEOUT_AFTER_$((end_ms - start_ms))ms"
    return 0
  fi
  end_ms=$(date +%s%3N)
  echo "$label $((end_ms - start_ms))ms"
}

for n in 100000 300000 500000 1000000; do
  awk -v n="$n" 'BEGIN {
    print "id,name,age";
    for (i = 1; i <= n; i++) {
      print i ",user" i "," (20 + (i % 50));
    }
  }' > data/users.csv

  measure_ms "select_eq_${n}" "bench/select_eq.sql" "/tmp/select_eq_${n}.out" "/tmp/select_eq_${n}.err"
done
