# B+ Tree 개발 내용 정리

이 문서는 이번 작업에서 프로젝트가 어떻게 바뀌었는지 초보자 기준으로 이해할 수 있게 정리한 문서다.
핵심은 기존 SQL Processor에 `id`와 `game_win_count` 전용 B+ 트리 인덱스를 붙인 것이다.

---

## 1. 한 줄 요약

기존 프로젝트는 CSV 파일을 처음부터 끝까지 훑으면서 데이터를 찾는 방식이 중심이었다.
이번 개발로 `WHERE id = ?`, `WHERE game_win_count = ?` 조건에서는 B+ 트리 인덱스를 통해 필요한 row의 파일 위치(offset)를 먼저 찾고, 그 위치의 row만 읽을 수 있게 바뀌었다.

---

## 2. 전체 흐름이 어떻게 바뀌었나

### 기존 흐름

```text
SQL 입력
→ tokenizer
→ parser
→ executor
→ storage가 CSV 전체를 읽음
→ 조건에 맞는 row를 하나씩 비교
```

기존 방식은 데이터가 적을 때는 이해하기 쉽고 충분히 빠르다.
하지만 row가 1,000,000개가 되면 `id = 500000` 한 건을 찾기 위해서도 앞에서부터 계속 비교해야 한다.

### 변경된 흐름

```text
SQL 입력
→ tokenizer
→ parser
→ executor
→ WHERE 컬럼 확인
   → id 조건이면 id B+ tree 사용
   → game_win_count 조건이면 win_count B+ tree 사용
   → 그 외 컬럼이면 기존처럼 선형 탐색
→ B+ tree가 offset을 반환
→ storage가 해당 offset의 row를 읽음
```

즉, 모든 조회를 빠르게 만든 것이 아니라, 과제 요구사항에 맞게 `id`, `game_win_count` 두 컬럼만 빠르게 찾도록 했다.

---

## 3. offset과 index 관계

### offset이란?

offset은 CSV 파일 안에서 특정 row가 시작되는 위치다.
예를 들어 파일을 긴 문자열처럼 생각하면, offset은 "몇 번째 글자 위치부터 이 row가 시작되는가"에 가깝다.

```text
파일 내용:
id,nickname,game_win_count,...
1,player_1,10,...
2,player_2,20,...

offset 0  : header 시작 위치
offset 35 : 1번 row 시작 위치
offset 52 : 2번 row 시작 위치
```

정확한 숫자는 row 길이, 콤마, 줄바꿈 문자에 따라 달라진다.
그래서 offset은 사람이 정하는 값이 아니라, 파일에 append하거나 읽을 때 `ftell()` 같은 파일 위치 API로 얻는다.

### index란?

index는 `key -> offset`을 빠르게 찾기 위한 자료구조다.

```text
id index:
1 -> offset 35
2 -> offset 52
3 -> offset 69
```

`WHERE id = 2`가 들어오면:

```text
id 2를 B+ tree에서 검색
→ offset 52 반환
→ CSV 파일의 offset 52 위치로 이동
→ 그 row만 읽음
```

### game_win_count는 왜 리스트가 필요한가?

`id`는 자동 증가 PK라서 중복되지 않는다.

```text
id 1 -> row 하나
id 2 -> row 하나
```

하지만 `game_win_count`는 여러 사람이 같은 승리 횟수를 가질 수 있다.

```text
game_win_count 10 -> player_1, player_5, player_9
```

그래서 `game_win_count` 인덱스는 아래처럼 저장한다.

```text
10 -> [offset 35, offset 120, offset 200]
20 -> [offset 52, offset 310]
```

이번 구현에서는 이 offset 묶음을 `OffsetList`로 관리한다.

---

## 4. B+ 트리가 맡는 역할

B+ 트리는 정렬된 key를 빠르게 찾기 위한 트리 구조다.
이번 프로젝트에서는 실제 row 전체를 트리에 넣지 않고, row가 있는 위치인 offset만 넣는다.

```text
B+ tree key: id 또는 game_win_count
B+ tree value: CSV row offset
```

이렇게 한 이유는 간단하다.

- row 전체를 메모리에 계속 복사하지 않아도 된다.
- CSV 저장 방식은 유지할 수 있다.
- index는 "어디에 있는지만" 빠르게 알려주면 된다.

---

## 5. 개발된 주요 파일

### `src/bptree.h`, `src/bptree.c`

B+ 트리의 핵심 자료구조와 알고리즘을 새로 추가했다.

추가된 핵심 기능:

- 빈 B+ 트리 생성
- key 검색
- key-value 삽입
- leaf node split
- internal node split
- root split
- 트리 메모리 해제

초보자 관점에서 중요한 점:

- B+ 트리는 `id` 전용이 아니다.
- `long long key`와 `void *value`를 받기 때문에 `id` 인덱스와 `game_win_count` 인덱스가 같은 트리 코어를 재사용한다.
- 단, 트리 코어는 중복 key를 허용하지 않는다.
- `game_win_count` 중복 처리는 `index.c`에서 `OffsetList`로 해결한다.

---

### `src/index.h`, `src/index.c`

기존 범용 인메모리 인덱스 대신, 이번 과제에 맞는 player 전용 index manager로 바꿨다.

관리하는 인덱스:

```text
id_tree        : id -> RowRef(offset)
win_tree       : game_win_count -> OffsetList(offset 여러 개)
```

추가된 핵심 기능:

- 테이블 전체를 읽어서 `id` B+ 트리 만들기
- 테이블 전체를 읽어서 `game_win_count` B+ 트리 만들기
- `id`로 offset 찾기
- `game_win_count`로 offset 목록 찾기
- insert된 row를 인덱스에 반영할 수 있는 함수 제공
- 인덱스 메모리 해제

초보자 관점에서 중요한 점:

- `id`는 중복되면 안 되므로 duplicate insert를 실패 처리한다.
- `game_win_count`는 중복될 수 있으므로 같은 key에 offset을 계속 붙인다.

---

### `src/storage.h`, `src/storage.c`

CSV 저장소 계층을 player schema에 맞게 확장했다.

기준 스키마:

```csv
id,nickname,game_win_count,game_loss_count,total_game_count
```

추가된 핵심 기능:

- `id` 자동 부여
- `data/<table>.meta` 파일 기반 `next_id` 관리
- `total_game_count = game_win_count + game_loss_count` 자동 계산
- insert 후 executor가 사용할 수 있도록 `StorageInsertResult` 반환
- insert된 row의 `file_offset` 반환

초보자 관점에서 중요한 점:

- 예전처럼 매 INSERT마다 CSV 전체를 읽어서 다음 id를 찾으면 1,000,000건에서 너무 느리다.
- 그래서 다음에 쓸 id를 `.meta` 파일에 저장한다.
- offset은 row를 파일에 쓰기 직전의 파일 위치를 기준으로 기록한다.

---

### `src/executor.h`, `src/executor.c`

SQL 실행 계층에 B+ 트리 사용 판단 로직을 추가했다.

실행 계획:

```text
WHERE id = 숫자
→ BPTREE_ID_LOOKUP

WHERE game_win_count = 숫자
→ BPTREE_WIN_LOOKUP

그 외 WHERE
→ LINEAR_SCAN

WHERE 없음
→ FULL_SCAN
```

추가된 핵심 기능:

- table별 player index cache
- `id` 조건일 때 B+ 트리 조회
- `game_win_count` 조건일 때 B+ 트리 조회
- benchmark용 silent mode
- benchmark용 강제 실행 모드
- 실행 결과 통계 `ExecStats`

초보자 관점에서 중요한 점:

- SQL 문법은 바꾸지 않았다.
- executor가 WHERE 컬럼명을 보고 내부 실행 방식을 고른다.
- DELETE 후에는 B+ 트리를 직접 고치지 않고 cache를 무효화한다.
- 다음 조회 때 다시 index를 만들기 때문에 구현이 단순하다.

---

## 6. 테스트가 추가된 부분

### `tests/test_bptree.c`

B+ 트리 자체가 맞게 동작하는지 확인한다.

검증 내용:

- 빈 트리 검색
- 순차 insert/search
- 역순 insert/search
- 존재하지 않는 key 검색 실패
- 중복 key insert 실패

### `tests/test_index.c`

player index manager가 맞게 동작하는지 확인한다.

검증 내용:

- `id` 검색 시 offset이 맞는지
- `game_win_count` 중복 key가 offset list로 묶이는지
- 새 row insert 후 두 인덱스가 갱신되는지
- 중복 id가 거부되는지

### `tests/test_executor.c`

SQL 실행 계층에서 올바른 실행 계획이 선택되는지 확인한다.

검증 내용:

- `WHERE id = ?`는 `BPTREE_ID_LOOKUP`
- `WHERE game_win_count = ?`는 `BPTREE_WIN_LOOKUP`
- `WHERE nickname = ?`는 `LINEAR_SCAN`
- DELETE 후 재조회가 정상인지

---

## 7. benchmark가 추가된 부분

### `tests/benchmark_bptree.c`

1,000,000건 데이터를 빠르게 만들고, 같은 조건을 선형 탐색과 B+ 트리 탐색으로 비교할 수 있게 했다.

비교 대상:

- `WHERE game_win_count = 120`
  - 강제 선형 탐색
  - 강제 B+ 트리 탐색
- `WHERE id = 500000`
  - 강제 선형 탐색
  - 강제 B+ 트리 탐색

중요한 점:

- benchmark에서는 SELECT 결과를 화면에 계속 출력하지 않는다.
- 출력 시간이 성능 측정을 방해하지 않도록 silent mode를 사용한다.
- 두 방식의 matched row count가 같은지 확인한다.

---

## 8. 실행 방법

### 빌드

```bash
make
```

### 테스트

```bash
make tests
```

### benchmark

```bash
make benchmark
```

benchmark는 기본적으로 `data/players.csv`와 `data/players.meta`를 생성한다.
데이터가 크기 때문에 실행 시간이 일반 테스트보다 길 수 있다.

### 현재 작업 환경에서의 검증 상태

이 작업을 작성한 현재 Windows PowerShell 환경에서는 `make`, `gcc`, `clang`, `cl`이 직접 설치되어 있지 않았다.
대신 이미 준비되어 있던 Docker 이미지 `jungle-c-dev` 안에서 빌드와 테스트를 실행했다.

검증 결과:

- `make tests`: 14 passed, 0 failed
- `make benchmark ARGS="10000 10"`: 정상 실행
- 기본 1,000,000건 benchmark는 시간이 오래 걸리므로 발표 전에 별도 실행 후 README에 수치를 적으면 된다.

컴파일 도구가 직접 설치된 환경에서는 아래 순서로 다시 확인하면 된다.

```bash
make clean
make tests
make benchmark
```

---

## 9. 이번 구현에서 단순화한 부분

이번 목표는 하루 안에 이해하고 발표 가능한 기본 구현이다.
그래서 일부러 아래 부분은 복잡하게 만들지 않았다.

- B+ 트리 delete / rebalance는 구현하지 않았다.
- DELETE 후에는 index cache를 무효화하고 다음 조회 때 다시 만든다.
- `id`, `game_win_count` 외 컬럼에는 인덱스를 적용하지 않는다.
- SQL 문법은 새로 추가하지 않았다.
- range query는 구현하지 않았다.

이 단순화 덕분에 핵심 설명이 명확해진다.

```text
id 검색이 왜 빠른가?
→ B+ tree에서 offset을 바로 찾기 때문이다.

game_win_count 중복은 어떻게 처리하는가?
→ tree key는 하나만 두고, value에 offset list를 둔다.

나머지 컬럼은 왜 느릴 수 있는가?
→ 인덱스 없이 row를 처음부터 끝까지 비교하기 때문이다.
```

---

## 10. 발표 때 설명하면 좋은 흐름

1. 기존 CSV 선형 탐색의 한계를 설명한다.
2. offset은 "파일 안에서 row가 시작되는 위치"라고 설명한다.
3. index는 "key로 offset을 빨리 찾는 지도"라고 설명한다.
4. `id -> offset`은 한 건만 찾는 unique index라고 설명한다.
5. `game_win_count -> offset list`는 중복 key를 처리하는 secondary index라고 설명한다.
6. 같은 조건을 선형 탐색과 B+ 트리 탐색으로 benchmark해서 속도 차이를 보여준다.
