#!/bin/bash

echo "=== SQL Processor Test Suite ==="
PASS=0
FAIL=0

run_unit_test() {
    local binary=$1
    if "$binary" >/tmp/sql_processor_test.log 2>&1; then
        echo "[PASS] $(basename "$binary")"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $(basename "$binary")"
        cat /tmp/sql_processor_test.log
        FAIL=$((FAIL + 1))
    fi
}

run_sql_test() {
    local test_name=$1
    local sql_file=$2
    local expected=$3
    local output

    rm -rf data
    mkdir -p data

    output=$(./sql_processor "$sql_file" 2>&1)
    if echo "$output" | grep -q "$expected"; then
        echo "[PASS] $test_name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $test_name"
        echo "Expected to find: $expected"
        echo "Actual output:"
        echo "$output"
        FAIL=$((FAIL + 1))
    fi
}

for binary in build/tests/test_tokenizer build/tests/test_parser \
              build/tests/test_storage build/tests/test_bptree \
              build/tests/test_index build/tests/test_executor
do
    run_unit_test "$binary"
done

run_sql_test "Basic INSERT" "tests/test_cases/basic_insert.sql" "users 테이블에 1행을 삽입했습니다."
run_sql_test "Basic SELECT" "tests/test_cases/basic_select.sql" "Alice"
run_sql_test "WHERE equals" "tests/test_cases/select_where.sql" "Bob"
run_sql_test "Edge cases" "tests/test_cases/edge_cases.sql" "Lee, Jr."
run_sql_test "Duplicate primary key" "tests/test_cases/duplicate_primary_key.sql" "Duplicate primary key value"
run_sql_test "Delete WHERE" "tests/test_cases/delete_where.sql" "users 테이블에서 1행을 삭제했습니다."
run_sql_test "Delete grouped slot" "tests/test_cases/delete_grouped_slot.sql" "jungle_menu 테이블에서 5행을 삭제했습니다."
run_sql_test "Players B+ tree flow" "tests/test_cases/players_bptree.sql" "player_3"

echo ""
echo "Results: $PASS passed, $FAIL failed"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
