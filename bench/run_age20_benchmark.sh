#!/usr/bin/env bash
set -euo pipefail

make >/dev/null
rm -rf data
mkdir -p data

awk 'BEGIN {
  print "id,name,age";
  for (i = 1; i <= 1000000; i++) {
    print i ",user" i "," (20 + (i % 50));
  }
}' > data/users.csv

start_ms=$(date +%s%3N)
if timeout 600s ./sql_processor bench/select_age20.sql >/tmp/select_age20.out 2>/tmp/select_age20.err; then
  end_ms=$(date +%s%3N)
  echo "select_age20_1000000 $((end_ms - start_ms))ms"
  grep -E "rows selected" /tmp/select_age20.out | tail -n 1
else
  end_ms=$(date +%s%3N)
  echo "select_age20_1000000 TIMEOUT_AFTER_$((end_ms - start_ms))ms"
  tail -n 20 /tmp/select_age20.err
fi
