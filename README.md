# Mini SQL Processor 성능 실험

이 프로젝트는 C로 구현한 간단한 SQL 처리기입니다. `INSERT`, `SELECT`, `DELETE`를 파싱해 CSV 파일 기반 저장소에 실행합니다.

이번 실험의 목적은 현재 구조가 대용량 데이터에서 어떤 성능 한계를 보이는지 확인하고, B+Tree 인덱스가 왜 필요한지 수치로 확인하는 것입니다.

## 현재 구조

현재 데이터는 `data/<table>.csv` 파일에 저장됩니다.

`SELECT ... WHERE ...` 실행 흐름은 다음과 같습니다.

1. CSV 테이블 전체를 메모리에 로드합니다.
2. WHERE 대상 컬럼에 대해 인메모리 인덱스를 생성합니다.
3. 인덱스에서 조건에 맞는 row offset을 찾습니다.
4. offset을 이용해 필요한 행을 다시 읽습니다.
5. 결과를 표 형태로 출력합니다.

핵심 함수는 다음과 같습니다.

| 역할 | 함수 |
| --- | --- |
| 테이블 전체 로드 | `storage_load_table()` |
| SELECT 실행 | `executor_execute_select()` |
| 인덱스 캐시 확인/생성 | `executor_get_cached_index()` |
| 인메모리 인덱스 생성 | `index_build()` |
| 등호 조건 조회 | `index_query_equals()` |
| 범위 조건 조회 | `index_query_range()` |

즉, 현재 인덱스는 디스크에 유지되는 영구 인덱스가 아니라 조회 시점에 메모리에 만드는 임시 인덱스입니다.

## 테스트 환경

테스트는 Docker 컨테이너 안에서 진행했습니다.

```bash
docker build -t mini-sql-btree:bench .
docker run --rm mini-sql-btree:bench bash bench/run_query_scale.sh
```

테스트 SQL은 다음과 같습니다.

```sql
SELECT id, name, age FROM users WHERE id = 1000000;
```

테스트 데이터는 `users` 테이블에 다음 형태로 생성했습니다.

```csv
id,name,age
1,user1,21
2,user2,22
...
1000000,user1000000,20
```

## INSERT 성능

100만 건 전체 INSERT는 끝까지 수행하지 않았습니다. 현재 `storage_insert()`는 매번 INSERT할 때 기존 CSV 파일을 훑으며 기본키 중복 여부를 검사합니다. 따라서 행 수가 증가할수록 한 번의 INSERT 비용도 같이 증가합니다.

작은 구간에서 측정한 결과는 다음과 같습니다.

| 레코드 수 | INSERT 시간 |
| ---: | ---: |
| 1,000 | 113ms |
| 2,000 | 523ms |
| 5,000 | 2,598ms |
| 10,000 | 10,095ms |

레코드 수가 10배 증가할 때 시간이 단순히 10배 증가하지 않고 훨씬 더 크게 증가합니다. 이 결과는 현재 INSERT 경로가 대량 입력에 적합하지 않다는 것을 보여줍니다.

## SELECT WHERE 성능

`SELECT id, name, age FROM users WHERE id = 1000000;` 기준 조회 성능입니다.

| 레코드 수 | 조회 시간 |
| ---: | ---: |
| 100,000 | 2,119ms |
| 300,000 | 36,997ms |
| 500,000 | 125,186ms |
| 1,000,000 | 300초 제한 초과 |

100만 건에서는 300초 제한 안에 단일 WHERE 조회가 끝나지 않았습니다.

## 병목 원인

현재 `SELECT WHERE`는 인덱스를 사용하긴 하지만, 조회 전에 이미 많은 비용을 사용합니다.

가장 큰 병목은 다음 두 단계입니다.

1. `storage_load_table()`에서 CSV 전체를 메모리에 로드합니다.
2. `index_build()`에서 로드된 전체 행을 다시 훑으며 인메모리 인덱스를 만듭니다.

따라서 `WHERE id = 1000000`처럼 결과가 1건이어도, 실제 실행은 전체 데이터를 대상으로 준비 작업을 수행합니다.

현재 구조는 다음과 같이 요약할 수 있습니다.

```text
CSV 전체 읽기 -> 인메모리 인덱스 생성 -> 조건 조회 -> offset으로 행 읽기
```

이 방식은 작은 데이터에서는 동작하지만, 데이터가 커질수록 조회 한 번의 준비 비용이 너무 커집니다.

## B+Tree가 필요한 이유

B+Tree 또는 B+Tree 계열 인덱스를 도입하면 조회 시점에 전체 테이블을 매번 다시 읽고 인덱스를 재생성하는 비용을 줄일 수 있습니다.

목표 구조는 다음과 같습니다.

```text
INSERT 시 인덱스 갱신
SELECT WHERE 시 B+Tree에서 key 검색
찾은 offset으로 필요한 행만 읽기
```

기대 효과는 다음과 같습니다.

| 현재 구조 | B+Tree 적용 후 목표 |
| --- | --- |
| 조회 시 전체 CSV 로드 | 필요한 key만 인덱스로 탐색 |
| 조회 시 인메모리 인덱스 재생성 | 저장된 인덱스 재사용 |
| 대용량 WHERE 조회가 느림 | 단건/범위 조회 성능 개선 |
| INSERT마다 기본키 중복 검사가 전체 스캔 | B+Tree로 기본키 존재 여부 확인 |

특히 이 프로젝트에서는 B+Tree가 두 곳에 직접적인 도움이 됩니다.

1. `SELECT ... WHERE id = value` 같은 단건 조회
2. `INSERT` 시 기본키 중복 검사

현재 실험 결과는 B+Tree 구현이 단순한 추가 기능이 아니라, 대용량 데이터에서 프로젝트가 동작 가능한 구조로 가기 위한 핵심 개선임을 보여줍니다.

## 벤치마크 파일

성능 실험에 사용한 파일은 `bench/` 디렉터리에 있습니다.

| 파일 | 설명 |
| --- | --- |
| `bench/select_eq.sql` | 단건 WHERE 조회 SQL |
| `bench/select_range.sql` | 범위 WHERE 조회 SQL |
| `bench/run_query_scale.sh` | 10만, 30만, 50만, 100만 건 조회 스케일 테스트 |
| `bench/run_benchmark.sh` | 100만 건 데이터셋 기반 조회 테스트 |

## 결론

현재 프로젝트는 SQL 파싱과 CSV 기반 실행 흐름은 갖추고 있지만, 대용량 데이터에서는 조회와 입력 모두에서 전체 스캔 성격의 병목이 드러납니다.

따라서 다음 단계는 B+Tree 기반 인덱스를 추가해 `id -> row offset` 매핑을 유지하고, 조회와 기본키 검사를 전체 스캔 없이 수행하도록 바꾸는 것입니다.

---

## 이번 과제 목표

이번 브랜치의 구현 목표는 기존 SQL 문법을 유지하면서 `players` 전적 테이블에 메모리 기반 B+ 트리 인덱스를 붙이는 것입니다.

고정 스키마는 아래와 같습니다.

```csv
id,nickname,game_win_count,game_loss_count,total_game_count
```

- `id`: 자동 증가 정수 PK
- `nickname`: 플레이어 닉네임
- `game_win_count`: 승리 횟수
- `game_loss_count`: 패배 횟수
- `total_game_count`: `game_win_count + game_loss_count` 로 내부 계산

## B+ 트리 설계 요약

새 B+ 트리 코어는 `src/bptree.h`, `src/bptree.c`에 분리했습니다.

- 내부 노드는 탐색용 separator key만 저장합니다.
- leaf 노드는 실제 value 포인터를 저장합니다.
- leaf들은 `next` 포인터로 연결됩니다.
- 기본 `BPTREE_ORDER`는 64입니다.

인덱스 manager는 `src/index.h`, `src/index.c`에 있습니다.

| 인덱스 | key | value | 중복 처리 |
| --- | --- | --- | --- |
| `id_tree` | `id` | `RowRef*` (`row offset`) | duplicate id 실패 |
| `win_tree` | `game_win_count` | `OffsetList*` | 같은 key의 offset list append |

## 실행 계획 분기 기준

`src/executor.c`는 `WHERE` 조건을 보고 아래처럼 실행 계획을 고릅니다.

| WHERE 조건 | 실행 계획 |
| --- | --- |
| `WHERE id = ?` | `BPTREE_ID_LOOKUP` |
| `WHERE game_win_count = ?` | `BPTREE_WIN_LOOKUP` |
| `WHERE nickname = ?` | `LINEAR_SCAN` |
| `WHERE game_loss_count = ?` | `LINEAR_SCAN` |
| `WHERE total_game_count = ?` | `LINEAR_SCAN` |
| WHERE 없음 | `FULL_SCAN` |

SQL 문법은 새로 추가하지 않았습니다. benchmark에서는 같은 SQL 의미를 두고 `EXEC_MODE_FORCE_LINEAR`, `EXEC_MODE_FORCE_ID_INDEX`, `EXEC_MODE_FORCE_WIN_INDEX` 플래그로 실행 경로만 바꿉니다.

## Auto ID 관리 방식

기존처럼 INSERT마다 CSV 전체를 스캔해 다음 id를 찾으면 1,000,000건 입력에서 병목이 심합니다.

이번 구현은 `data/<table>.meta` sidecar 파일에 다음 id만 저장합니다.

- 새 `players` 테이블 첫 INSERT: `id = 1`, meta는 `2`
- 이후 INSERT: meta에서 next id 읽기
- INSERT 성공 후 meta를 `used_id + 1`로 갱신
- 기존 CSV는 있는데 meta가 없으면 한 번만 CSV를 스캔해 복구

## Benchmark 방법

Docker 환경에서 실행하는 방법:

```bash
docker build -t mini-sql-btree:test .
docker run --rm mini-sql-btree:test /bin/sh -lc "make benchmark"
```

직접 row 수와 반복 횟수를 줄여 빠르게 확인할 수도 있습니다.

```bash
docker run --rm mini-sql-btree:test /bin/sh -lc "make && build/benchmark 100000 100"
```

benchmark는 CSV를 직접 생성합니다. 이유는 이번 비교 대상이 SQL parser/tokenizer가 아니라 B+ 트리 lookup과 선형 탐색이기 때문입니다.

## 테스트 방법

```bash
docker build -t mini-sql-btree:test .
docker run --rm mini-sql-btree:test /bin/sh -lc "make tests"
```

대량 INSERT SQL 파일을 실행할 때는 출력 I/O를 줄이기 위해 `--silent` 옵션을 사용할 수 있습니다.

```bash
docker run --rm mini-sql-btree:test /bin/sh -lc "make && rm -f data/players.csv data/players.meta && ./sql_processor --silent bench/insert_1m_players.sql"
```

추가된 테스트:

| 파일 | 검증 내용 |
| --- | --- |
| `tests/test_bptree.c` | B+ 트리 insert/search/split/duplicate |
| `tests/test_index.c` | `id`, `game_win_count` index manager |
| `tests/test_executor.c` | 실행 계획 분기와 cache invalidate |
| `tests/test_cases/players_bptree.sql` | SQL 통합 흐름 |

## AI 작업 로그

### Step 1. Base 코드 분석

- 목표: 기존 SQL Processor의 저장소, executor, index 연결 방식을 파악
- 변경한 파일: 없음
- 핵심 내용: 기존 구조는 조회 시점에 전체 테이블을 읽고 임시 인덱스를 만드는 방식임을 확인
- 검증 방법: `src/storage.c`, `src/executor.c`, `src/index.c` 흐름 확인
- 결과: B+ 트리는 `id`, `game_win_count` 전용으로 붙이는 것이 가장 안전하다고 판단
- 남은 이슈: 기존 README의 과거 성능 실험 내용은 참고용으로 유지

### Step 2. B+ 트리 자료구조 추가

- 목표: 메모리 기반 B+ 트리 코어 구현
- 변경한 파일: `src/bptree.h`, `src/bptree.c`
- 핵심 구현: `bptree_create`, `bptree_search`, `bptree_insert`, leaf split, internal split, destroy
- 검증 방법: Docker 컨테이너에서 `make` 컴파일
- 결과: 컴파일 통과
- 남은 이슈: delete rebalance는 요구사항에서 제외

### Step 3. Storage Auto ID 개선

- 목표: auto-increment id 계산에서 반복 full scan 제거
- 변경한 파일: `src/storage.c`
- 핵심 구현: `data/<table>.meta` 기반 next id 관리, `players` 고정 스키마 처리, `total_game_count` 내부 계산
- 검증 방법: 컴파일 및 executor/storage 테스트 예정
- 결과: 일반 테이블 기존 동작은 유지하고 `players`만 과제 스키마로 특별 처리
- 남은 이슈: meta 파일은 `make clean`에서 함께 삭제

### Step 4. 2개 인덱스 연동

- 목표: `id`, `game_win_count` 전용 B+ 트리 index manager 추가
- 변경한 파일: `src/index.h`, `src/index.c`, `src/executor.c`, `src/executor.h`
- 핵심 구현: `PlayerIndexSet`, `RowRef`, `OffsetList`, executor plan 분기, benchmark silent mode
- 검증 방법: Docker 컨테이너에서 `make` 컴파일
- 결과: `WHERE id = ?`, `WHERE game_win_count = ?`만 B+ 트리 사용
- 남은 이슈: range query는 선택 구현으로 남김

### Step 5. 테스트 추가/수정

- 목표: B+ 트리와 실행 계획을 자동 검증
- 변경한 파일: `tests/test_bptree.c`, `tests/test_index.c`, `tests/test_executor.c`, `tests/run_tests.sh`, `tests/test_cases/players_bptree.sql`
- 핵심 구현: B+ 트리 단위 테스트, secondary index 중복 key 테스트, executor plan 테스트
- 검증 방법: `make tests`
- 결과: 최종 테스트 실행 단계에서 확인
- 남은 이슈: 없음

### Step 6. Benchmark 추가

- 목표: 1,000,000건 데이터셋에서 선형 탐색과 B+ 트리 lookup 비교
- 변경한 파일: `bench/benchmark.c`, `Makefile`
- 핵심 구현: players CSV 생성기, 강제 실행 모드 비교, 평균 시간과 speedup 출력
- 검증 방법: `make benchmark`
- 결과: 최종 benchmark 실행 단계에서 확인
- 남은 이슈: 실제 시간은 실행 환경에 따라 달라짐

### Step 7. 최종 검증 및 결과 정리

- 목표: 빌드, 테스트, benchmark를 실행해 완료 기준 확인
- 변경한 파일: 검증 결과에 따라 업데이트 예정
- 핵심 구현: 없음
- 검증 방법: Docker 기반 `make tests`, 축소 benchmark
- 결과: 아래 최종 benchmark 결과에 기록
- 남은 이슈: 현재 Windows PowerShell에는 `make`/`gcc`가 없어 Docker로 검증

## 최종 Benchmark 결과

전체 1,000,000건 benchmark는 `make benchmark` 기본값으로 실행할 수 있습니다. 현재 작업 검증에서는 빠른 확인을 위해 10,000건/10회 축소 benchmark를 실행했습니다.

| 비교 | 선형 탐색 평균 | B+ 트리 평균 | speedup |
| --- | ---: | ---: | ---: |
| `WHERE id = 5000` | 0.489600 ms | 0.018000 ms | 27.20x |
| `WHERE game_win_count = 85000` | 0.563300 ms | 0.024700 ms | 22.81x |

현재 구현 상태:

- `WHERE id = ?`: B+ 트리 사용
- `WHERE game_win_count = ?`: B+ 트리 사용
- 그 외 WHERE: 선형 탐색
- 1,000,000건 benchmark 실행 파일: `build/benchmark`

검증 명령:

```bash
docker run --rm mini-sql-btree:test /bin/sh -lc "make benchmark BENCH_ROWS=10000 BENCH_QUERIES=10"
```
