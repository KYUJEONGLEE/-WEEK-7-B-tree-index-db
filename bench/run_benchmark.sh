#!/usr/bin/env bash
set -euo pipefail

make >/dev/null
mkdir -p data
if [ ! -f data/users.csv ]; then
  echo "data/users.csv not found" >&2
  exit 1
fi

measure_ms() {
  local label="$1"
  local sql_file="$2"
  local out_file="$3"
  local err_file="$4"
  local start_ms
  local end_ms

  start_ms=$(date +%s%3N)
  ./sql_processor "$sql_file" >"$out_file" 2>"$err_file"
  end_ms=$(date +%s%3N)
  echo "$label $((end_ms - start_ms))ms"
}

measure_ms "select_eq_once" "bench/select_eq.sql" "/tmp/select_eq.out" "/tmp/select_eq.err"
measure_ms "select_range_once" "bench/select_range.sql" "/tmp/select_range.out" "/tmp/select_range.err"

cat > /tmp/select_eq_twice.sql <<'EOF'
SELECT id, name, age FROM users WHERE id = 1000000;
SELECT id, name, age FROM users WHERE id = 1000000;
EOF

measure_ms "select_eq_twice_same_process" "/tmp/select_eq_twice.sql" "/tmp/select_eq_twice.out" "/tmp/select_eq_twice.err"

echo "---EQ_OUTPUT---"
tail -n 3 /tmp/select_eq.out
echo "---RANGE_OUTPUT---"
tail -n 12 /tmp/select_range.out
echo "---TWICE_OUTPUT---"
tail -n 8 /tmp/select_eq_twice.out
