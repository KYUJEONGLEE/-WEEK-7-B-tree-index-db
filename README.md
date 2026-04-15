# SQL Processor + B+ Tree Index

기존 C 기반 SQL Processor에 **플레이어 전적 테이블 전용 B+ 트리 인덱스 2개**를 추가한 프로젝트.

## 프로젝트 개요

이 프로젝트는 C 언어로 구현한 SQL 처리기(SQL Processor)입니다.
- `SELECT`, `INSERT`, `DELETE` 세 가지 SQL 문을 지원합니다.
- 파일 모드(.sql 파일 실행)와 REPL 모드(대화형 셸) 두 가지 방식으로 동작합니다.
- CSV 파일 기반 저장소를 사용합니다.
- **B+ 트리 인덱스**를 통해 대량 데이터에서의 검색 성능을 대폭 향상시켰습니다.

## 이번 과제 목표

기존 SQL 문법은 유지하면서, 플레이어 전적 테이블(`id`, `nickname`, `game_win_count`, `game_loss_count`, `total_game_count`)에 대해:

1. `id` 자동 증가 (meta 파일 기반, O(1))
2. `id` B+ 트리 인덱스
3. `game_win_count` B+ 트리 인덱스

를 구현하고, 1,000,000건 데이터에 대해 B+ 트리 vs 선형 탐색 성능 비교를 수행한다.

## 플레이어 전적 테이블 스키마

```csv
id,nickname,game_win_count,game_loss_count,total_game_count
1,player_000001,17,5,22
2,player_000002,8,14,22
```

- `id`: 자동 생성 정수 PK
- `nickname`: 플레이어 닉네임
- `game_win_count`: 승리 횟수
- `game_loss_count`: 패배 횟수
- `total_game_count`: `game_win_count + game_loss_count`

## B+ 트리 설계 요약

### id tree
- 키: `long long id` (unique)
- 값: `RowRef*` (CSV row offset)
- exact lookup 전용

### game_win_count tree
- 키: `long long game_win_count` (트리 내부에서는 unique)
- 값: `POffsetList*` (동일 승리 횟수를 가진 row offset 목록)
- 중복 처리: 트리 key는 unique 유지, value에 offset을 list로 관리

### 핵심 설계 결정
- B+ 트리 코어는 duplicate key를 허용하지 않는다
- `game_win_count`의 중복은 value를 `POffsetList`로 두어 해결
- 같은 B+ 트리 구현을 `id`/`game_win_count` 모두에 재사용
- DELETE 시 B+ 트리를 직접 수정하지 않고 cache invalidate -> lazy rebuild

## 실행 계획 분기 기준

| WHERE 조건 | 실행 계획 | 인덱스 |
|---|---|---|
| `WHERE id = ?` | `BPTREE_ID_LOOKUP` | id B+ 트리 |
| `WHERE game_win_count = ?` | `BPTREE_WIN_LOOKUP` | game_win_count B+ 트리 |
| `WHERE nickname = ?` | `LINEAR_SCAN` | 없음 (선형 탐색) |
| `WHERE game_loss_count = ?` | `LINEAR_SCAN` | 없음 (선형 탐색) |
| `WHERE total_game_count = ?` | `LINEAR_SCAN` | 없음 (선형 탐색) |
| `=` 이외 연산자 | `LINEAR_SCAN` | 기존 hash/range 인덱스 사용 |

## Auto ID 관리 방식

- `data/<table>.meta` 파일에 `next_id`를 저장
- INSERT 시 meta에서 O(1)으로 next_id를 읽고, 저장 후 meta 갱신
- meta 파일이 없으면 한 번만 CSV를 스캔하여 복구
- 기존의 매 INSERT마다 CSV 전체 스캔 방식을 제거하여 대량 INSERT 성능 확보

## 빌드 및 실행

```bash
# 빌드
make

# 테스트 실행
make tests

# SQL 파일 실행
./sql_processor tests/test_cases/player_bptree.sql

# REPL 모드
./sql_processor

# 벤치마크 (1,000,000건)
make benchmark
```

## 테스트 방법

### 단위 테스트
- `test_bptree`: B+ 트리 코어 (순차/역순 삽입, leaf/internal split, duplicate 방어, 10000건)
- `test_index`: 플레이어 인덱스 매니저 (id 검색, win_count 중복 검색, row insert, meta auto-id)
- `test_tokenizer`, `test_parser`, `test_storage`, `test_executor`: 기존 테스트 유지

### SQL 통합 테스트
- `player_bptree.sql`: INSERT -> id 조회 -> win_count 중복 조회 -> nickname 선형 탐색 -> DELETE -> 재조회

## 파일 구조

| 파일 | 역할 | 변경 |
|---|---|---|
| `src/bptree.h`, `src/bptree.c` | B+ 트리 코어 자료구조 및 알고리즘 | **신규** |
| `src/player_index.h`, `src/player_index.c` | id/game_win_count 전용 B+ 트리 인덱스 매니저 | **신규** |
| `src/executor.h`, `src/executor.c` | SQL 실행기 (B+ 트리 분기, 벤치마크 모드 추가) | **수정** |
| `src/storage.h`, `src/storage.c` | CSV 스토리지 (meta auto-id, insert result 추가) | **수정** |
| `src/index.h`, `src/index.c` | 기존 hash/range 인덱스 (비인덱스 컬럼용 유지) | 유지 |
| `src/parser.*`, `src/tokenizer.*`, `src/main.c`, `src/utils.*` | 기존 코드 | 유지 |
| `tests/test_bptree.c` | B+ 트리 코어 단위 테스트 | **신규** |
| `tests/test_index.c` | 플레이어 인덱스 매니저 테스트 | **신규** |
| `tests/benchmark.c` | 1M건 벤치마크 전용 실행 파일 | **신규** |
| `tests/test_cases/player_bptree.sql` | SQL 통합 테스트 | **신규** |

---

## AI 작업 로그

### Step 1. Base 코드 분석

- **목표**: 기존 SQL Processor의 전체 구조와 모듈 간 인터페이스 파악
- **변경 파일**: 없음
- **결과**: tokenizer -> parser -> executor -> storage -> index 파이프라인 확인. 기존 index는 hash+sorted range 방식. executor는 LRU 캐시로 테이블/인덱스 관리. storage_insert에서 auto-id는 CSV 전체 스캔 방식.

### Step 2. B+ 트리 자료구조 추가

- **목표**: B+ 트리 코어 (생성, 검색, 삽입, 분할, 소멸) 구현
- **변경 파일**: `src/bptree.h`, `src/bptree.c` (신규)
- **왜 바꿨는가**: 기존 hash/range 인덱스는 범용이지만, 대량 데이터에서의 exact match 검색에 B+ 트리가 더 효율적이다. 특히 정렬된 키에 대한 O(log n) 검색이 핵심.
- **핵심 구현**: 
  - order 기반 B+ 트리 구현 (기본 ORDER=128)
  - leaf split: 오른쪽 leaf 첫 key **복사** -> 부모
  - internal split: 중간 key **이동**(promote) -> 부모
  - duplicate key 거부
  - leaf linked list로 범위 검색 기반 마련
- **검증**: test_bptree.c로 순차/역순/대량 삽입, split, duplicate 방어 등 10,153개 assert 통과

### Step 3. Storage auto id 개선

- **목표**: CSV 전체 스캔 대신 meta 파일 기반 next_id 관리
- **변경 파일**: `src/storage.h`, `src/storage.c` (수정)
- **왜 바꿨는가**: 1,000,000건 INSERT 시 매번 CSV 전체 스캔은 O(n^2). meta 파일로 O(1)에 next_id를 읽을 수 있다.
- **핵심 구현**: 
  - `data/<table>.meta` 파일에 next_id 저장/읽기
  - meta 없을 시 1회 CSV 스캔 복구 (마이그레이션 호환)
  - `StorageInsertResult` 구조체로 INSERT 결과 반환
  - `storage_insert_with_result()` 확장 함수
- **검증**: test_index.c의 test_meta_auto_id에서 CSV 복구/meta 저장/읽기 검증

### Step 4. 2개 인덱스 (id, game_win_count) 연동

- **목표**: id/game_win_count 전용 B+ 트리 인덱스 매니저 + executor 연동
- **변경 파일**: `src/player_index.h`, `src/player_index.c` (신규), `src/executor.h`, `src/executor.c` (수정)
- **왜 바꿨는가**: executor가 WHERE 조건에 따라 B+ 트리와 선형 탐색을 자동으로 선택해야 한다. 벤치마크를 위해 실행 경로를 강제 지정하는 모드도 필요하다.
- **핵심 구현**:
  - `PlayerIndexSet` (id_tree + win_tree) 관리
  - id tree: `RowRef*` (unique, 단일 offset)
  - win tree: `POffsetList*` (중복 key -> value list append)
  - executor에 `ExecPlan`, `ExecMode`, `ExecStats` 추가
  - `executor_decide_plan()`으로 WHERE 조건에 따라 자동 분기
  - `executor_execute_select_with_mode()`으로 벤치마크용 강제 실행 경로 지원
  - DELETE 시 player index cache invalidate (lazy rebuild)
- **검증**: test_index.c에서 id 검색, win_count 중복 검색, row insert, dup id 실패 검증. SQL 통합 테스트 통과.

### Step 5. 테스트 추가/수정

- **목표**: B+ 트리/인덱스 단위 테스트, SQL 통합 테스트 추가
- **변경 파일**: `tests/test_bptree.c`, `tests/test_index.c`, `tests/test_cases/player_bptree.sql`, `tests/run_tests.sh`
- **검증 결과**: 기존 11개 + 신규 4개 = **15개 테스트 전부 통과**. 기존 테스트 깨지지 않음.

### Step 6. Benchmark 추가

- **목표**: 1,000,000건 데이터에 대해 B+ 트리 vs 선형 탐색 성능 비교
- **변경 파일**: `tests/benchmark.c` (신규), `Makefile` (수정)
- **왜 SQL INSERT 대신 직접 CSV를 생성하는가**: 1M개 SQL을 파서까지 태우면 benchmark 목적이 흐려진다. 이번 benchmark는 "B+ 트리 vs 선형 탐색" 비교가 중심이다.
- **핵심 구현**: ExecMode 플래그로 같은 조건/다른 경로 비교, silent 모드로 I/O 제거
- **검증**: `make benchmark`로 1M건 실행 성공

### Step 7. 최종 검증 및 결과 정리

- 기존 SQL Processor 정상 동작 OK
- INSERT 시 id 자동 부여 OK
- auto id가 meta 파일 기반 O(1) OK
- `WHERE id = ?` -> B+ 트리 사용 OK
- `WHERE game_win_count = ?` -> B+ 트리 사용 OK
- `WHERE nickname = ?` -> 선형 탐색 사용 OK
- B+ 트리 단위 테스트 통과 OK
- 인덱스 매니저 테스트 통과 OK
- 기존 테스트 깨지지 않음 OK
- 벤치마크 전용 실행 파일 존재 OK
- 벤치마크 1,000,000건 실행 가능 OK
- 벤치마크에서 ExecMode 플래그 사용 가능 OK
- 같은 조건에 대해 선형/인덱스의 결과 개수와 시간 차이 확인 가능 OK

---

## 최종 Benchmark 결과

```
========================================================
B+ Tree Benchmark (order=128, records: 1,000,000)
========================================================

Data generation:                          129.61 ms
B+ tree build (id + win_count):           554.80 ms
  id tree keys:                         1,000,000
  win_count tree keys:                    100,000

[Compare] WHERE id = 500000

  FORCE_LINEAR:
    plan: LINEAR_SCAN
    matched rows: 1
    average: 28.678 ms (1000 queries)

  FORCE_ID_INDEX:
    plan: BPTREE_ID_LOOKUP
    matched rows: 1
    average: 0.008 ms (1000 queries)

  speedup: 3,540x

[Compare] WHERE game_win_count = 120

  FORCE_LINEAR:
    plan: LINEAR_SCAN
    matched rows: 10
    average: 23.354 ms (1000 queries)

  FORCE_WIN_INDEX:
    plan: BPTREE_WIN_LOOKUP
    matched rows: 10
    average: 0.081 ms (1000 queries)

  speedup: 289x

[Reference] WHERE nickname = 'player_500000' (LINEAR only)
    average: 5.190 ms (1000 queries)
========================================================
```

### 단순화한 부분

- B+ 트리 DELETE/merge/rebalance 미구현 -> cache invalidate + lazy rebuild 전략
- `total_game_count` 자동 계산은 `storage_insert_with_result()`에서만 지원 (기존 `storage_insert()`는 유지)
- 범위 검색(range query)은 선택 구현 미포함 (기존 hash/range 인덱스로 처리)
