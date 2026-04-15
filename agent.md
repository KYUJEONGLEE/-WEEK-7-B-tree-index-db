# B+ 트리 인덱스 구현 명세서

> 기준 문서: `bplus_tree_spec(2).md`  
> 병합 반영 문서: `SPEC(1).md`  
> 목적: 기존 C 기반 SQL Processor에 플레이어 전적 테이블 전용 B+ 트리 인덱스를 붙이기 위한 최종 구현 명세서

이 문서는 AI 에이전트가 구현에 바로 들어갈 수 있도록 작성한 공식 명세서이다.  
구현 목표는 차별화 기능보다 **기본 요구사항을 정확하고 설명 가능하게 완성하는 것**이다.

---

## 0. 기존 프로젝트 구조

현재 base repo는 C 기반 SQL Processor이다.

지원 기능:

- `SELECT`, `INSERT`, `DELETE`
- REPL 모드
- `.sql` 파일 실행 모드
- CSV 기반 저장소
- tokenizer 캐시
- 조건 조회용 인메모리 인덱스 모듈

주요 파일 역할:

| 파일 | 역할 |
| --- | --- |
| `src/main.c` | 프로그램 진입점. REPL 모드와 SQL 파일 모드 처리 |
| `src/tokenizer.c` | SQL 문자열을 Token 배열로 변환. LRU 캐시 포함 |
| `src/parser.c` | Token 배열을 `SqlStatement` 구조체로 변환 |
| `src/executor.c` | `INSERT`, `SELECT`, `DELETE` 실행 분기 |
| `src/storage.c` | CSV 파일 읽기, 쓰기, 삭제. 테이블은 CSV 파일 1개 |
| `src/index.c` | 인메모리 인덱스 관리 레이어 |
| `src/utils.c` | 문자열, 메모리, 비교 유틸리티 |
| `Makefile` | 빌드 설정 |

기존 핵심 흐름:

```text
SQL 입력
-> tokenizer
-> parser
-> executor
-> storage(CSV)
```

이번 명세의 핵심 변경점은 기존 `id` 전용 인덱스 설계를 **플레이어 전적 테이블 전용 2개 B+ 트리 인덱스 설계**로 확장하는 것이다.

사용할 인덱스:

1. 자동 증가 PK 성격의 `id`
2. secondary index 성격의 `game_win_count`

SQL 문법은 새로 추가하지 않는다.  
기존 `WHERE column op value` 구조를 그대로 사용한다.

---

## 1. 최종 목표

이 프로젝트의 목표는 기존 SQL Processor에 **플레이어 전적 테이블 전용 B+ 트리 인덱스 2개**를 추가하는 것이다.

반드시 만족해야 하는 조건:

1. 기존 SQL 문법은 유지한다.
2. 플레이어 전적 테이블 컬럼은 아래 5개로 고정한다.
   - `id`
   - `nickname`
   - `game_win_count`
   - `game_loss_count`
   - `total_game_count`
3. `INSERT` 시 `id`는 자동 부여된다.
4. 자동 부여된 `id`는 B+ 트리 인덱스에서 검색 가능한 key가 된다.
5. `game_win_count`도 B+ 트리 인덱스에서 검색 가능한 key가 된다.
6. `SELECT ... WHERE id = ?`는 `id` B+ 트리를 사용한다.
7. `SELECT ... WHERE game_win_count = ?`는 `game_win_count` B+ 트리를 사용한다.
8. `SELECT ... WHERE <그 외 컬럼> = ?`는 선형 탐색을 사용한다.
9. 1,000,000건 이상 데이터를 쉽게 생성하고 benchmark할 수 있어야 한다.
10. 코드보다 이해를 우선할 수 있도록, 복잡한 일반화보다 명확한 구조와 쉬운 주석을 우선한다.
11. 작업 과정은 매 단계마다 `README.md`의 AI 작업 로그에 기록한다.

사용자 개념과 실제 코드 식별자는 아래처럼 통일한다.

| 사용자 개념 | 코드/CSV 컬럼명 |
| --- | --- |
| 자동 생성 id | `id` |
| 닉네임 | `nickname` |
| 게임 승리 횟수 | `game_win_count` |
| 게임 패배 횟수 | `game_loss_count` |
| 총 게임 횟수 | `total_game_count` |

---

## 2. 구현 원칙

## 2.1 범위는 좁게 잡는다

이번 구현은 범용 인덱스 엔진이 아니다.

고정 2컬럼 전용 B+ 트리 설계로 구현한다.

하지 않을 것:

- 임의의 여러 컬럼 인덱스 지원
- 문자열 key 인덱스
- 디스크 페이지 관리형 B+ 트리
- B+ 트리 delete rebalance
- `id`, `game_win_count` 외 컬럼 인덱스

반드시 지킬 것:

- 메모리 기반 B+ 트리만 구현한다.
- 인덱스 적용 대상은 오직 `id`, `game_win_count` 두 컬럼이다.
- `nickname`, `game_loss_count`, `total_game_count`에는 인덱스를 적용하지 않는다.

## 2.2 검색 전략은 단순하게 분리한다

검색 계획은 아래 규칙을 따른다.

| WHERE 조건 | 실행 계획 |
| --- | --- |
| `WHERE id = ?` | `id` B+ 트리 사용 |
| `WHERE game_win_count = ?` | `game_win_count` B+ 트리 사용 |
| `WHERE nickname = ?` | 선형 탐색 |
| `WHERE game_loss_count = ?` | 선형 탐색 |
| `WHERE total_game_count = ?` | 선형 탐색 |
| 인덱스 대상 컬럼이라도 `=` 외 연산자 | 기본 구현에서는 선형 탐색 |

선택 구현으로 range 검색을 넣을 수 있지만, 기본 완료 기준에는 포함하지 않는다.

## 2.3 DELETE는 단순화한다

B+ 트리의 delete, merge, rebalance는 기본 요구사항에서 구현하지 않는다.

DELETE 성공 시 처리:

```text
해당 table cache invalidate
해당 table id tree cache invalidate
해당 table game_win_count tree cache invalidate
다음 인덱스 조회 시 다시 build
```

이유:

- DELETE 후 CSV 파일이 재작성되면 row offset이 바뀔 수 있다.
- 기존 offset을 가진 B+ 트리를 계속 사용하면 stale offset 문제가 생긴다.
- 기본 구현에서는 tree 수정 대신 cache invalidate가 가장 안전하고 설명하기 쉽다.

## 2.4 auto id 성능 병목을 제거한다

기존의 "다음 auto id를 얻기 위해 CSV 전체를 매번 scan하는 방식"은 금지한다.

반드시 O(1)에 가까운 next id 획득 방식을 사용한다.

권장 방식:

```text
data/<table>.meta sidecar 파일에 next_id 저장
```

meta 파일이 없을 때만 CSV를 한 번 scan해서 next id를 복구한다.  
이후부터는 meta 파일만 읽고 쓴다.

## 2.5 플레이어 전적 스키마를 고정한다

기본 테이블 스키마:

```csv
id,nickname,game_win_count,game_loss_count,total_game_count
```

규칙:

- `id`: 자동 생성 정수 PK
- `nickname`: 플레이어 닉네임
- `game_win_count`: 승리 횟수
- `game_loss_count`: 패배 횟수
- `total_game_count`: 총 게임 횟수

`total_game_count`는 항상 아래 조건을 만족해야 한다.

```text
total_game_count = game_win_count + game_loss_count
```

학습용 기본 구현에서는 `total_game_count`를 입력받더라도 저장 전에 내부에서 재계산하는 방식을 권장한다.

---

## 3. 데이터 및 인덱스 설계

## 3.1 테스트 데이터 규칙

권장 CSV 예시:

```csv
id,nickname,game_win_count,game_loss_count,total_game_count
1,player_000001,17,5,22
2,player_000002,8,14,22
3,player_000003,105,33,138
```

대용량 데이터 생성 규칙:

| 컬럼 | 생성 규칙 |
| --- | --- |
| `id` | 1부터 순차 증가 |
| `nickname` | `player_<id>` 형태의 고유 문자열 |
| `game_win_count` | `(id * 37) % 100000` |
| `game_loss_count` | `((id * 17) % 500) + 1` |
| `total_game_count` | `game_win_count + game_loss_count` |

이 규칙의 목적:

- `id` 조회는 unique key 1건 조회가 된다.
- `nickname` 조회는 unique 값이지만 인덱스가 없으므로 선형 탐색 비교 대상이 된다.
- `game_win_count`는 중복 key가 발생하므로 secondary index 처리 능력을 확인할 수 있다.

## 3.2 B+ 트리 핵심 개념

주요 구현 주석과 README 설명에 반드시 포함할 개념:

```text
B+ 트리란?
- 모든 실제 데이터 포인터는 leaf node에만 존재한다.
- internal node는 탐색을 위한 guide key만 갖는다.
- leaf node들은 next 포인터로 연결되어 range search에 유리하다.
- order는 한 node가 가질 수 있는 최대 child 수이다.
- order가 4이면 key는 최대 3개, child pointer는 최대 4개이다.
```

## 3.3 B+ 트리 구조체 명세

`src/bptree.h`에 아래 개념의 구조체를 둔다.

```c
#define BPTREE_ORDER 4

typedef enum { BPTREE_INTERNAL, BPTREE_LEAF } NodeType;

typedef struct BPTreeNode {
    NodeType type;
    int num_keys;
    long long keys[BPTREE_ORDER - 1];
    struct BPTreeNode *children[BPTREE_ORDER];
    void *values[BPTREE_ORDER - 1];
    struct BPTreeNode *next;
    struct BPTreeNode *parent;
} BPTreeNode;

typedef struct {
    BPTreeNode *root;
    int order;
    int count;
} BPTree;
```

학습용 기본값은 `BPTREE_ORDER = 4`로 시작한다.  
benchmark 시에는 128 또는 256으로 변경해도 된다.

## 3.4 value 래퍼 구조

`id` tree와 `game_win_count` tree는 value 형태가 다르다.

권장 value 구조:

```c
typedef struct {
    long offset;
} RowRef;

typedef struct OffsetNode {
    long offset;
    struct OffsetNode *next;
} OffsetNode;

typedef struct {
    long count;
    OffsetNode *head;
    OffsetNode *tail;
} OffsetList;
```

의미:

| 인덱스 | key | value |
| --- | --- | --- |
| `id` tree | `id` | `RowRef*` |
| `game_win_count` tree | `game_win_count` | `OffsetList*` |

B+ 트리 core는 duplicate key를 허용하지 않는다.  
`game_win_count` 중복 문제는 B+ 트리 duplicate insert가 아니라 `OffsetList` append로 해결한다.

## 3.5 관리할 인덱스

이번 구현에서 관리하는 인덱스는 2개다.

```c
typedef struct {
    BPTree *id_tree;
    BPTree *win_tree;
} PlayerIndexSet;
```

### A. `id` 인덱스

- key: `long long id`
- value: `RowRef*`
- leaf node: `id -> CSV row offset`
- unique key
- exact lookup 전용

### B. `game_win_count` 인덱스

- key: `long long game_win_count`
- value: `OffsetList*`
- leaf node: `game_win_count -> 동일 승리 횟수를 가진 row offset 목록`
- tree key 자체는 unique
- 실제 중복 row는 value list로 관리

---

## 4. 파일별 작업 명세

## 4.1 `src/bptree.h`, `src/bptree.c`

B+ 트리 core를 새 파일로 분리한다.

`src/index.c`는 이 B+ 트리 core를 사용하는 index manager 역할을 맡는다.

필수 구현 함수:

| 함수 | 기능 |
| --- | --- |
| `bptree_create(int order)` | 빈 B+ 트리 생성 |
| `bptree_create_node(NodeType type)` | 새 node 생성 |
| `bptree_find_leaf(BPTree *tree, long long key)` | key가 들어갈 leaf 탐색 |
| `bptree_search(BPTree *tree, long long key)` | key에 해당하는 value 검색 |
| `bptree_insert(BPTree *tree, long long key, void *value)` | key-value 삽입 |
| `bptree_insert_into_leaf(...)` | 공간이 있는 leaf에 정렬 삽입 |
| `bptree_split_leaf(...)` | overflow leaf 분할 |
| `bptree_insert_into_parent(...)` | split 후 부모에 guide key 삽입 |
| `bptree_split_internal(...)` | overflow internal node 분할 |
| `bptree_destroy(BPTree *tree)` | 모든 node 메모리 해제 |

각 함수 위에는 반드시 아래 내용을 포함한 블록 주석을 단다.

```text
1. 기능 설명
2. 핵심 흐름
3. 관련 개념
4. 입력과 반환값
```

B+ 트리 core 테스트 확인 항목:

- 1~20 순차 삽입 후 각각 검색 성공
- 존재하지 않는 key 검색 시 `NULL`
- 역순 삽입 후 정상 검색
- leaf split 발생 케이스 정상
- internal split 발생 케이스 정상
- duplicate key insert 실패
- destroy 후 메모리 누수 없음

## 4.2 `src/index.h`, `src/index.c`

기존 generic equality/range index를 고정 2컬럼 B+ 트리 index manager로 교체한다.

외부 역할:

- 테이블 전체에서 `id` B+ 트리 build
- 테이블 전체에서 `game_win_count` B+ 트리 build
- `id` 1건 검색
- `game_win_count` 1건 검색 후 offset list 반환
- INSERT 시 두 인덱스 반영
- 인덱스 메모리 해제

권장 함수 역할:

| 함수 역할 | 설명 |
| --- | --- |
| `index_build_player_indexes` | `TableData`에서 `id`, `game_win_count` 위치를 찾아 두 tree build |
| `index_search_by_id` | `id_tree`에서 `RowRef*` 검색 |
| `index_search_by_win_count` | `win_tree`에서 `OffsetList*` 검색 |
| `index_insert_row` | 새 row의 `id`, `game_win_count`, `offset`을 두 tree에 반영 |
| `index_free` | tree node와 `RowRef`, `OffsetList`, `OffsetNode` 모두 해제 |

구현 요구사항:

- `id` duplicate key는 실패 처리한다.
- `game_win_count` duplicate는 tree duplicate insert가 아니라 기존 `OffsetList`에 append한다.
- `id`, `game_win_count` 외 다른 컬럼 인덱스는 만들지 않는다.
- order는 상수 매크로 하나로 관리한다.

필수 주석 위치:

- `game_win_count` 기존 list append 로직
- `id` duplicate 방어 로직
- leaf insert
- leaf split
- internal split
- recursive insert
- root split
- search

주석에서 설명할 내용:

- 왜 split 하는지
- 왜 오른쪽 leaf의 첫 key를 부모로 올리는지
- internal split에서 어떤 key가 promote되는지
- duplicate `id`는 왜 허용하지 않는지
- duplicate `game_win_count`는 왜 list append로 처리하는지

## 4.3 `src/storage.h`, `src/storage.c`

storage 계층은 세 가지를 해결해야 한다.

### A. auto-increment 성능 개선

기존 전체 scan 방식 대신 meta 기반 next id 관리를 사용한다.

필요 helper 역할:

- meta 파일 경로 만들기
- meta 파일에서 `next_id` 읽기
- meta 파일이 없을 때 CSV scan으로 `next_id` 복구
- `next_id` 저장
- 테이블 생성 시 고정 header 작성

### B. 플레이어 전적 스키마 저장

storage는 아래 스키마를 기준으로 row를 저장한다.

```text
id,nickname,game_win_count,game_loss_count,total_game_count
```

규칙:

- 새 테이블 생성 시 header 순서를 반드시 유지한다.
- `total_game_count`는 저장 직전에 `wins + losses`로 계산한다.
- 테스트 데이터 생성기와 SQL INSERT 경로가 같은 스키마를 사용해야 한다.
- 예전 샘플 스키마인 `name`, `age`, `payload` 등은 benchmark와 신규 테스트에서 사용하지 않는다.

### C. INSERT 결과 반환

executor가 INSERT 성공 후 인덱스를 갱신하려면 아래 정보를 알아야 한다.

```c
typedef struct {
    long long assigned_id;
    int game_win_count;
    int game_loss_count;
    int total_game_count;
    long file_offset;
    int id_was_auto_generated;
} StorageInsertResult;
```

권장 방식:

- 기존 `storage_insert()`는 compatibility wrapper로 유지한다.
- 실제 구현은 `storage_insert_with_result()` 같은 확장 함수로 분리한다.

필수 주석 내용:

- next id를 meta에서 읽는 이유
- meta가 없을 때만 full scan 하는 이유
- `total_game_count`를 저장 전 계산하는 이유
- append 직전 offset을 어떻게 잡는지
- executor가 index 갱신을 위해 insert result를 받아야 하는 이유

## 4.4 `src/executor.h`, `src/executor.c`

executor는 B+ 트리 인덱스와 SQL 실행을 연결하는 핵심 계층이다.

### A. 실행 계획 분기

반드시 아래 규칙을 따른다.

| 조건 | 실행 계획 |
| --- | --- |
| `WHERE id = 정수값` | `BPTREE_ID_LOOKUP` |
| `WHERE game_win_count = 정수값` | `BPTREE_WIN_LOOKUP` |
| 그 외 모든 WHERE | `LINEAR_SCAN` |

선형 탐색에서는 기존 `utils_compare_values()` 비교 규칙을 재사용한다.

### B. 캐시 구조

현재 generic index cache 대신 table별 player index cache를 유지한다.

cache entry 정보:

- 사용 여부
- 최근 사용 tick
- `table_name`
- `id_tree`
- `win_tree`

### C. INSERT 후 처리

INSERT 성공 시:

```text
table cache invalidate
해당 table index cache가 이미 있으면 새 row의 id/game_win_count/offset을 즉시 insert
cache가 없으면 즉시 build하지 않고 다음 인덱스 조회 때 lazy build
```

권장 정책:

```text
cache가 이미 존재할 때만 incremental insert
cache가 없으면 lazy build
```

### D. DELETE 후 처리

DELETE 성공 시:

```text
table cache invalidate
id tree cache invalidate
game_win_count tree cache invalidate
```

DELETE 후 B+ 트리를 즉시 수정하지 않는다.

### E. benchmark silent 실행

benchmark에서는 SELECT 결과 출력 I/O가 성능을 왜곡한다.  
따라서 benchmark 실행에서는 결과를 출력하지 않는 silent mode를 지원한다.

### F. benchmark 실행 모드 플래그

benchmark 전용으로 실행 경로를 강제할 수 있어야 한다.

권장 enum:

```c
typedef enum {
    EXEC_MODE_NORMAL,
    EXEC_MODE_FORCE_LINEAR,
    EXEC_MODE_FORCE_ID_INDEX,
    EXEC_MODE_FORCE_WIN_INDEX
} ExecMode;
```

동작 규칙:

| mode | 의미 |
| --- | --- |
| `EXEC_MODE_NORMAL` | 일반 SQL 실행. 조건에 따라 자동 plan 선택 |
| `EXEC_MODE_FORCE_LINEAR` | 인덱스 존재 여부와 상관없이 선형 탐색 강제 |
| `EXEC_MODE_FORCE_ID_INDEX` | `WHERE id = ?`에서만 id index 강제 |
| `EXEC_MODE_FORCE_WIN_INDEX` | `WHERE game_win_count = ?`에서만 win index 강제 |

SQL 문법 자체는 바꾸지 않는다.  
같은 SQL 의미를 benchmark 코드에서 다른 `ExecMode`로 실행한다.

통계 구조 권장:

```c
typedef struct {
    int plan_used;
    long matched_rows;
    long scanned_rows;
    double elapsed_ms;
} ExecStats;
```

benchmark에서 반드시 기록할 항목:

- 입력 조건
- 실제 사용 plan
- matched row count
- scanned row count
- elapsed time

필수 주석 내용:

- 왜 `id`, `game_win_count`만 index path로 보내는지
- 왜 `nickname`, `game_loss_count`, `total_game_count`는 linear scan인지
- DELETE 시 tree 수정 대신 invalidate하는 이유
- benchmark에서 print를 끄는 이유

## 4.5 `src/parser.*`, `src/tokenizer.*`, `src/main.c`

가급적 수정하지 않는다.

이번 과제는 SQL 문법 추가가 목표가 아니다.

금지:

- 새 SQL 키워드 추가
- `EXPLAIN`, `CREATE INDEX`, `BENCHMARK` 문법 추가
- benchmark를 일반 SQL 문법에 섞기

benchmark는 별도 실행 파일로 만든다.

## 4.6 `tests/`

반드시 아래 테스트를 추가 또는 보강한다.

### A. B+ 트리 core 단위 테스트

파일:

```text
tests/test_bptree.c
```

검증 항목:

- 빈 트리 검색
- 단일 insert/search
- 여러 insert 후 search
- 역순 insert 후 search
- leaf split 발생 케이스
- internal split 발생 케이스
- duplicate key insert 실패
- 존재하지 않는 key 검색 실패

### B. index manager 테스트

파일:

```text
tests/test_index.c
```

검증 항목:

- `id` index build 후 특정 id 검색 성공
- `game_win_count` index build 후 동일 승리 횟수 목록 검색 성공
- 같은 `game_win_count`에 여러 row가 매핑되는지 확인
- `id` duplicate insert 실패
- 신규 row insert 후 두 인덱스 모두 검색 결과 갱신 확인

### C. storage 테스트

검증 항목:

- auto id가 meta 기반으로 정상 증가
- 프로그램 재시작 후에도 `next_id` 유지
- `total_game_count = wins + losses` 저장 보장
- INSERT 결과 구조체에 `assigned_id`, `game_win_count`, `offset` 정상 반환

### D. executor 테스트

검증 항목:

- `WHERE id = 2`에서 plan이 `BPTREE_ID_LOOKUP`
- `WHERE game_win_count = 10`에서 plan이 `BPTREE_WIN_LOOKUP`
- `WHERE nickname = 'player_2'`에서 plan이 `LINEAR_SCAN`
- INSERT 후 id query 정상 동작
- INSERT 후 game_win_count query 정상 동작
- DELETE 후 cache invalidate 및 재조회 정상 동작

### E. SQL integration test

`tests/test_cases/`에 아래 성격의 SQL 파일을 추가한다.

- auto id insert 후 `WHERE id = ?` 조회
- `WHERE game_win_count = ?` 조회
- `WHERE nickname = ?` 조회
- DELETE 후 id 재조회 실패 또는 row count 변화 확인

### F. benchmark는 unit test와 분리

1,000,000건 benchmark는 `make tests`에 포함하지 않는다.  
별도 `make benchmark` 또는 별도 실행 파일로 분리한다.

## 4.7 `Makefile`

반영 사항:

- `src/bptree.c`가 빌드되도록 유지
- `tests/test_bptree.c`, `tests/test_index.c`가 테스트 빌드에 포함되도록 유지
- benchmark 전용 binary target 추가
- `clean` 시 `.meta` 파일도 삭제

---

## 5. Benchmark 명세

## 5.1 Benchmark 목적

비교 대상:

1. `WHERE id = ?` -> `id` B+ 트리 사용
2. `WHERE nickname = ?` -> 선형 탐색
3. `WHERE game_win_count = ?` -> `game_win_count` B+ 트리 사용
4. 같은 `game_win_count` 조건을 강제로 선형 탐색 수행

이 비교로 두 가지를 보여준다.

- unique key 인덱스 이점: `id`
- 중복 key secondary index 이점: `game_win_count`

## 5.2 실행 모드 플래그 기반 비교 원칙

benchmark는 SQL 문자열을 바꾸는 방식이 아니다.

같은 조건을 서로 다른 실행 모드로 실행해 비교한다.

예:

```sql
SELECT * FROM players WHERE game_win_count = 120;
```

같은 조건을 두 번 실행한다.

1. `EXEC_MODE_FORCE_LINEAR`
   - 전체 row를 순회하며 `game_win_count == 120` 비교
2. `EXEC_MODE_FORCE_WIN_INDEX`
   - B+ 트리에서 `120` key를 찾고 offset list 기반으로 row 읽기

반드시 검증할 것:

- 두 실행의 matched row count가 같아야 한다.
- 조건값이 같아야 한다.
- 데이터셋이 같아야 한다.

## 5.3 데이터 생성 방식

1,000,000건 삽입을 위해 전용 데이터 생성기를 둔다.

생성기 역할:

- CSV 빠른 생성
- header 작성
- meta 파일 생성
- 필요 시 index build에 바로 사용 가능

이유:

- 1,000,000개의 SQL 문을 tokenizer/parser까지 태우면 benchmark 목적이 흐려진다.
- benchmark 목적은 SQL 파싱 속도가 아니라 B+ 트리와 선형 탐색 비교이다.
- `game_win_count` secondary index 효과를 보려면 대량 중복 key 데이터가 필요하다.

단, 일반 기능 검증은 반드시 기존 SQL Processor 경로로 수행한다.

## 5.4 측정 항목

benchmark는 아래 항목을 분리해서 측정한다.

- 데이터 생성 시간
- `id` tree build 시간
- `game_win_count` tree build 시간
- warm-up 1회
- `WHERE id = ?` + `EXEC_MODE_NORMAL` 반복 조회 총 시간 / 평균 시간
- `WHERE nickname = ?` + `EXEC_MODE_NORMAL` 반복 조회 총 시간 / 평균 시간
- `WHERE game_win_count = ?` + `EXEC_MODE_FORCE_LINEAR` 반복 조회 총 시간 / 평균 시간
- `WHERE game_win_count = ?` + `EXEC_MODE_FORCE_WIN_INDEX` 반복 조회 총 시간 / 평균 시간
- 가능하면 `WHERE id = ?`에 대해서도 `EXEC_MODE_FORCE_LINEAR`와 `EXEC_MODE_FORCE_ID_INDEX` 비교
- speedup 비율

권장 반복 횟수:

```text
rows: 1,000,000 이상
queries: 1,000 ~ 10,000회
```

주의:

- benchmark에서는 출력 I/O를 제거하기 위해 silent mode를 사용한다.
- 같은 조건의 선형 탐색과 인덱스 탐색은 반드시 결과 row 수가 같아야 한다.
- 실행 모드 플래그는 benchmark 전용 binary에서만 사용한다.
- 일반 SQL 실행 경로는 항상 자동 계획 선택을 유지한다.

## 5.5 출력 형식

benchmark 결과는 사람이 바로 읽을 수 있어야 한다.

반드시 포함할 항목:

- row 수
- query 수
- 조건값
- 실제 사용 plan
- matched row count
- 평균 시간
- speedup 배수

권장 출력 예시:

```text
========================================================
성능 테스트 결과 (레코드 수: 1,000,000)
========================================================
데이터 생성 시간:                         XX.XX초
id B+ 트리 빌드 시간:                    XX.XX초
win_count B+ 트리 빌드 시간:             XX.XX초

[Compare] WHERE game_win_count = 120

FORCE_LINEAR
- plan used: LINEAR_SCAN
- matched rows: 421
- average: X.XXX ms (1000회)

FORCE_WIN_INDEX
- plan used: BPTREE_WIN_LOOKUP
- matched rows: 421
- average: X.XXX ms (1000회)

speedup: XX.XX배

[Compare] WHERE id = 500000

FORCE_LINEAR
- plan used: LINEAR_SCAN
- matched rows: 1
- average: X.XXX ms (1000회)

FORCE_ID_INDEX
- plan used: BPTREE_ID_LOOKUP
- matched rows: 1
- average: X.XXX ms (1000회)

speedup: XX.XX배
========================================================
```

---

## 6. 함수 주석 규칙

새로 추가하거나 크게 수정한 함수에는 반드시 함수 상단 블록 주석을 단다.

주석에는 아래 4가지를 포함한다.

1. 이 함수가 하는 일
2. 입력과 출력
3. 핵심 흐름
4. 주의할 점 또는 개념 포인트

## 6.1 주석 스타일

원칙:

- 사소한 한 줄마다 주석 달지 않는다.
- 함수명만 봐도 알 수 있는 내용을 반복하지 않는다.
- 주석은 "무엇"보다 "왜"를 설명한다.
- split, promote, cache invalidate, next id 복구, duplicate 처리처럼 헷갈리는 지점에만 인라인 주석을 단다.

## 6.2 반드시 주석이 필요한 함수군

- `bptree_create`
- `bptree_create_node`
- `bptree_find_leaf`
- `bptree_search`
- `bptree_insert`
- `bptree_insert_into_leaf`
- `bptree_split_leaf`
- `bptree_insert_into_parent`
- `bptree_split_internal`
- `bptree_destroy`
- next id meta 읽기, 복구, 저장 함수
- `game_win_count` offset list append 함수
- executor plan 분기 함수
- benchmark 측정 loop

## 6.3 함수 주석 템플릿

```c
/*
 * 함수명: bptree_search
 * ------------------------------------------------------------
 * 기능:
 *   key에 해당하는 값을 B+ 트리에서 찾아 반환한다.
 *
 * 핵심 흐름:
 *   1. root에서 시작하여 leaf node까지 내려간다.
 *   2. leaf node의 keys[]에서 해당 key를 찾는다.
 *   3. 찾으면 values[i], 못 찾으면 NULL을 반환한다.
 *
 * 개념:
 *   - B+ 트리 검색은 root -> leaf 경로를 따른다.
 *   - WHERE id = ? 또는 WHERE game_win_count = ?에서 호출된다.
 *
 * 매개변수:
 *   - tree: B+ 트리 포인터
 *   - key: 검색할 key
 *
 * 반환값:
 *   - 해당 key의 value 포인터
 *   - 없으면 NULL
 */
```

## 6.4 주석으로 반드시 설명할 이유

- 왜 leaf overflow 시 split 하는지
- 왜 오른쪽 leaf의 첫 key를 부모로 올리는지
- 왜 internal split에서는 중간 key를 promote하는지
- 왜 duplicate `id`는 허용하지 않는지
- 왜 duplicate `game_win_count`는 tree duplicate insert가 아니라 value list append로 처리하는지
- 왜 DELETE 시 tree 수정 대신 cache invalidate를 선택하는지

---

## 7. README 작성 의무

`README.md`는 사용법 문서이면서 AI 작업 추적 문서 역할도 해야 한다.

반드시 아래 섹션을 추가하고, 작업 단계가 끝날 때마다 업데이트한다.

필수 섹션:

1. 프로젝트 개요
2. 이번 과제 목표
3. 플레이어 전적 테이블 스키마
4. B+ 트리 설계 요약
   - `id` tree
   - `game_win_count` tree
5. 실행 계획 분기 기준
   - `WHERE id = ?` -> B+ 트리
   - `WHERE game_win_count = ?` -> B+ 트리
   - 그 외 -> 선형 탐색
6. auto id 관리 방식
7. benchmark 방법
8. 테스트 방법
9. AI 작업 로그
10. 최종 benchmark 결과

## 7.1 AI 작업 로그 작성 규칙

매 단계마다 아래 내용을 남긴다.

- 단계 이름
- 이번 단계 목표
- 변경한 파일
- 왜 바꿨는지
- 핵심 구현 내용
- 검증 방법
- 결과
- 남은 이슈

최소 로그 단계:

1. Step 1. base 코드 분석
2. Step 2. B+ 트리 자료구조 추가
3. Step 3. storage auto id 개선
4. Step 4. 2개 인덱스(`id`, `game_win_count`) 연동
5. Step 5. 테스트 추가/수정
6. Step 6. benchmark 추가
7. Step 7. 최종 검증 및 결과 정리

README를 보면 팀원이 AI가 어떤 순서로 무슨 작업을 했는지 바로 알 수 있어야 한다.

---

## 8. 완료 기준

아래를 모두 만족하면 완료로 본다.

1. 기존 SQL Processor가 계속 동작한다.
2. `INSERT` 시 `id` 자동 부여가 된다.
3. auto id는 1,000,000건 삽입에서도 비효율적 full scan을 반복하지 않는다.
4. `SELECT ... WHERE id = ?`는 `id` B+ 트리를 사용한다.
5. `SELECT ... WHERE game_win_count = ?`는 `game_win_count` B+ 트리를 사용한다.
6. `SELECT ... WHERE nickname = ?` 같은 non-index 조회는 선형 탐색을 사용한다.
7. B+ 트리 단위 테스트가 통과한다.
8. `id` / `game_win_count` index manager 테스트가 통과한다.
9. 기존 테스트가 깨지지 않는다.
10. benchmark 전용 실행 파일이 존재한다.
11. benchmark가 1,000,000건 이상 데이터로 실행 가능하다.
12. README에 작업 로그와 결과가 정리되어 있다.
13. benchmark 전용 실행 파일에서 `EXEC_MODE_FORCE_LINEAR`, `EXEC_MODE_FORCE_ID_INDEX`, `EXEC_MODE_FORCE_WIN_INDEX`를 사용할 수 있다.
14. 데모 시 같은 조건에 대해 선형 탐색과 인덱스 탐색의 결과 개수와 시간 차이를 함께 보여줄 수 있다.

---

## 9. 구현 시 금지 사항

- 범용 DB 엔진처럼 과하게 추상화하지 않는다.
- B+ 트리 delete rebalance까지 욕심내지 않는다.
- `id`, `game_win_count` 외 임의의 컬럼 인덱스를 지원하지 않는다.
- `nickname`, `game_loss_count`, `total_game_count` 조건에 인덱스를 적용하지 않는다.
- benchmark에서 결과 row를 매번 출력하지 않는다.
- README 작업 로그를 마지막에 한 번에 몰아서 쓰지 않는다.
- 일반 SQL 문법에 benchmark 전용 문법을 추가하지 않는다.

---

## 10. 선택 구현 가이드

기본 요구사항 완료 후 여유가 있을 때만 선택적으로 진행한다.  
기본 완료 기준에는 포함하지 않는다.

## 10.1 Range Query

목표:

- `WHERE id >= ? AND id <= ?`
- `WHERE game_win_count >= ? AND game_win_count <= ?`

구현 아이디어:

1. `min_key`가 들어갈 leaf를 찾는다.
2. 해당 leaf부터 key를 순회한다.
3. leaf의 `next`를 따라가며 `max_key` 초과 전까지 offset을 수집한다.

주의:

- 기본 SQL 문법을 늘리지 않는다면 benchmark/debug helper로만 둔다.

## 10.2 DELETE Lazy 처리

목표:

- DELETE 후 cache invalidate 대신 value를 비활성화한다.

구현 아이디어:

- `RowRef`에 tombstone flag를 추가한다.
- `OffsetList` node에도 tombstone 또는 remove 처리를 둔다.

주의:

- 완전한 B+ 트리 delete/rebalance는 구현하지 않는다.
- README에 tombstone 기반 단순화라고 명시한다.

## 10.3 B+ 트리 시각화 / 통계 출력

목표:

- 발표와 디버깅을 위해 tree 구조 또는 통계 출력

예시 함수:

- `bptree_print(BPTree *tree)`
- `bptree_collect_stats(BPTree *tree, BPTreeStats *stats)`

출력 통계:

- tree height
- node count
- leaf count
- key count

## 10.4 ORDER별 성능 비교

목표:

- `BPTREE_ORDER = 4 / 32 / 128 / 256` 별 성능 비교

비교 항목:

- tree build time
- `WHERE id = ?` 평균 lookup
- `WHERE game_win_count = ?` 평균 lookup

해석 포인트:

- ORDER가 너무 작으면 tree height가 커진다.
- ORDER가 너무 크면 node당 메모리 footprint와 이동 비용이 커질 수 있다.

---

## 11. AI 에이전트 최종 구현 요약

기존 SQL 문법은 유지한다.

플레이어 전적 테이블:

```text
id,nickname,game_win_count,game_loss_count,total_game_count
```

필수 구현:

```text
id 자동 증가
id B+ 트리
game_win_count B+ 트리
WHERE id = ? 인덱스 검색
WHERE game_win_count = ? 인덱스 검색
그 외 WHERE 선형 탐색
meta 파일 기반 next_id 관리
1,000,000건 이상 benchmark
README AI 작업 로그 기록
```

최종 한 줄:

```text
기존 SQL Processor에 플레이어 전적 테이블 전용 B+ 트리 2개를 붙이고,
id와 game_win_count 조건만 인덱스로 검색하며,
나머지 조건은 선형 탐색으로 처리해 1,000,000건 benchmark에서 성능 차이를 증명한다.
```

