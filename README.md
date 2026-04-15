# 7조 B+트리 인덱스 발표 README

이 브랜치는 발표 준비용 브랜치입니다. README는 코드 설명 문서라기보다, 발표 때 바로 보고 말할 수 있는 **4분 발표 대본 + 데모 순서**로 구성했습니다.

---

## 발표 핵심 요약

- 기존 SQL 처리기는 `SELECT ... WHERE ...` 조건을 선형 탐색으로 처리했습니다.
- 100만 건 데이터에서는 선형 탐색의 `O(n)` 비용이 커집니다.
- 이번 구현에서는 `id`, `game_win_count`에 대해 메모리 기반 B+트리 인덱스를 추가했습니다.
- 현재 B+트리는 디스크에 저장되는 영구 인덱스가 아니라, 프로그램 실행 중 메모리에 만들어지는 인덱스입니다.
- B+트리 value에는 파일 offset이 아니라 `row_index`를 저장합니다.
- `id = ?`처럼 결과가 1건인 조회에서는 B+트리가 유리합니다.
- 조건에 맞는 row가 많은 조회에서는 B+트리가 항상 빠르지는 않습니다.

---

## 4분 발표 대본

안녕하세요. 7조 발표 시작하겠습니다.

저번 주에 구현한 SQL 처리기에서는 `SELECT ... WHERE ...` 조건이 들어오면 테이블을 처음부터 끝까지 확인하는 **선형 탐색 방식**으로 처리했습니다.

이 방식은 데이터가 적을 때는 괜찮지만, 100만 건처럼 데이터가 커지면 조회 시간이 크게 늘어납니다.

그래서 저희는 대용량 조회 성능을 개선하기 위해 **B+트리 인덱스**를 학습하고 구현했습니다.

특히 과제 요구사항에서 `id`를 B+트리 인덱스로 두라고 한 이유에 대해서 공부해봤습니다.

기존처럼 `WHERE id = ?`를 선형 탐색으로 처리하면 최악의 경우 **O(n)** 이 걸리지만, B+트리를 사용하면 트리를 따라 내려가며 key를 찾기 때문에 **O(log n)** 수준으로 줄일 수 있기 때문입니다.

이번 구현은 실제 DBMS처럼 디스크에 저장되는 영구 인덱스가 아니라 **메모리 기반 인덱스**입니다.

즉, `SELECT`가 들어오면 `players.csv`를 메모리에 로드하고, 그 데이터를 기준으로 `id`와 `game_win_count`에 대한 B+트리를 구성합니다.

이때 B+트리에는 파일 offset이 아니라 **row_index**를 저장했습니다.

처음에는 파일의 byte offset을 저장하는 방식도 생각할 수 있지만, 그렇게 하면 B+트리로 위치를 찾은 뒤에도 다시 CSV 파일을 열고 해당 위치로 이동해서 row를 읽어야 합니다.

그래서 현재 구현에서는 CSV를 한 번 메모리에 올린 뒤, B+트리 value에 몇 번째 row인지 나타내는 `row_index`를 저장했습니다.

이렇게 하면 key를 찾은 뒤 파일을 다시 읽는 것이 아니라, 메모리에 올라와 있는 `table->rows[row_index]`에 바로 접근할 수 있습니다.

그리고 저희 조는 `id` 외에도 `game_win_count`에 대한 B+트리 인덱스를 만들었습니다.

그 이유는 고유값 조회뿐 아니라 **중복 key 처리**도 직접 다뤄보고 싶었기 때문입니다.

`id`는 고유값이라서 하나의 key가 하나의 row를 가리키면 됩니다.

반면 `game_win_count`는 같은 승리 횟수를 가진 플레이어가 여러 명 있을 수 있습니다.

그래서 `game_win_count` 인덱스에서는 하나의 key에 여러 `row_index`를 연결 리스트 형태로 저장했습니다.

이제 데모를 진행하겠습니다.

1,000,000건의 데이터를 실제 SQL INSERT 문으로 하나씩 넣는 과정은 시간이 오래 걸리기 때문에, 데모 시간 안에서는 보여드리기 어렵다고 판단했습니다.

그래서 이번 데모에서는 미리 생성해둔 `players.csv` 파일을 사용하겠습니다.

먼저 `WHERE id = 500000` 조건으로 선형 탐색과 B+트리 방식을 비교해보겠습니다.

[데모 1: id 조회 비교]

`id`는 고유값이기 때문에 B+트리 인덱스의 효과가 가장 잘 드러납니다.

선형 탐색은 앞에서부터 row를 하나씩 확인해야 하지만, B+트리는 key를 기준으로 leaf node까지 내려가서 원하는 row를 찾습니다.

다음으로 `WHERE game_win_count > 120` 조건을 보겠습니다.

[데모 2: game_win_count 범위 조회 비교]

이 경우에는 조건에 맞는 row가 많기 때문에, 성능이 비슷하게 나오거나 오히려 B+트리가 느릴 수도 있습니다.

여기서 확인한 핵심은 **B+트리가 항상 무조건 유리한 것은 아니라는 점**입니다.

`WHERE id = ?`처럼 결과가 거의 한 건인 단건 조회에서는 B+트리가 매우 유리했습니다.

하지만 `game_win_count`처럼 중복값이 많고 조건에 맞는 row가 많아지면, 결국 읽어와야 하는 데이터 자체가 많아져서 성능 차이가 줄어들 수 있었습니다.

즉 B+트리는 조회 조건에 따라 큰 이점을 줄 수 있지만, 모든 상황에서 무조건 유리한 것은 아니라는 점을 확인할 수 있었습니다.

이상으로 발표 마치겠습니다. 감사합니다.

---

## 데모 준비

Docker 이미지를 먼저 빌드합니다.

```powershell
docker build -t mini-sql-btree:test .
```

데모는 `--summary-only` 옵션을 사용합니다. 이 옵션은 결과 표를 생략하고 실행 계획, 결과 행 수, 검사 행 수, 소요 시간만 보여줘서 발표 화면이 깔끔해집니다.

---

## 데모 1. id 조회 비교

### 1-1. 선형 탐색

```powershell
docker run -it --rm -v "C:\Users\KJ\Workspace\mini_sql_btree:/app" -w /app mini-sql-btree:test /bin/sh -lc "make && ./sql_processor --summary-only --force-linear"
```

REPL이 뜨면 입력합니다.

```sql
SELECT * FROM players WHERE id = 500000;
```

발표 멘트:

```text
지금은 force-linear 옵션을 줬기 때문에 id 조건이어도 B+트리를 쓰지 않고 선형 탐색으로 조회합니다.
선형 탐색은 원하는 id를 찾기 위해 row를 앞에서부터 순서대로 확인합니다.
```

### 1-2. B+트리 id 인덱스

```powershell
docker run -it --rm -v "C:\Users\KJ\Workspace\mini_sql_btree:/app" -w /app mini-sql-btree:test /bin/sh -lc "make && ./sql_processor --summary-only --force-id-index"
```

REPL이 뜨면 같은 SQL을 입력합니다.

```sql
SELECT * FROM players WHERE id = 500000;
```

발표 멘트:

```text
이번에는 force-id-index 옵션으로 id B+트리 인덱스를 사용했습니다.
같은 SQL이지만 실행 계획이 ID B+트리 조회로 바뀐 것을 볼 수 있습니다.
```

참고: 첫 B+트리 조회는 CSV 로드와 인덱스 생성 시간이 포함되어 느릴 수 있습니다. 같은 프로그램 안에서 같은 SQL을 한 번 더 실행하면 이미 만들어진 인덱스를 재사용해서 더 빠르게 나옵니다.

---

## 데모 2. game_win_count 범위 조회 비교

### 2-1. 선형 탐색

```powershell
docker run -it --rm -v "C:\Users\KJ\Workspace\mini_sql_btree:/app" -w /app mini-sql-btree:test /bin/sh -lc "make && ./sql_processor --summary-only --force-linear"
```

REPL이 뜨면 입력합니다.

```sql
SELECT * FROM players WHERE game_win_count > 120;
```

발표 멘트:

```text
이번 조건은 game_win_count가 120보다 큰 row를 모두 찾는 범위 조회입니다.
조건에 해당하는 row가 많으면 선형 탐색도 단순히 한 번 훑으면서 결과를 모으기 때문에 생각보다 나쁘지 않을 수 있습니다.
```

### 2-2. B+트리 win_count 인덱스

```powershell
docker run -it --rm -v "C:\Users\KJ\Workspace\mini_sql_btree:/app" -w /app mini-sql-btree:test /bin/sh -lc "make && ./sql_processor --summary-only --force-win-index"
```

REPL이 뜨면 같은 SQL을 입력합니다.

```sql
SELECT * FROM players WHERE game_win_count > 120;
```

발표 멘트:

```text
이번에는 game_win_count B+트리 인덱스를 사용했습니다.
다만 이 조건은 결과 row가 많기 때문에, B+트리로 시작점을 찾더라도 결국 많은 row를 가져와야 합니다.
그래서 B+트리가 항상 압도적으로 빠르지는 않다는 점을 확인할 수 있습니다.
```

---

## 빠른 파일 실행 데모

대화형 입력이 부담스러우면 이미 만들어둔 SQL 파일로 `id` 조회를 바로 비교할 수 있습니다.

선형 탐색:

```powershell
docker run --rm -v "C:\Users\KJ\Workspace\mini_sql_btree:/app" -w /app mini-sql-btree:test /bin/sh -lc "make && ./sql_processor --summary-only --force-linear bench/select_id_500000_twice.sql"
```

B+트리:

```powershell
docker run --rm -v "C:\Users\KJ\Workspace\mini_sql_btree:/app" -w /app mini-sql-btree:test /bin/sh -lc "make && ./sql_processor --summary-only --force-id-index bench/select_id_500000_twice.sql"
```

`twice` 파일은 같은 SELECT를 두 번 실행합니다. 첫 번째 조회와 두 번째 조회의 차이를 보여주기 좋습니다.

---

## 구현 설명용 키워드

발표 중 질문이 나오면 아래 표현을 사용하면 됩니다.

| 질문 | 답변 |
| --- | --- |
| B+트리는 어디에 구현했나요? | `src/bptree.c`, `src/bptree.h`에 구현했습니다. |
| 어떤 컬럼에 인덱스를 만들었나요? | `id`, `game_win_count` 두 컬럼입니다. |
| 왜 `game_win_count`도 했나요? | 중복 key를 가진 secondary index를 다뤄보기 위해서입니다. |
| offset을 저장하나요? | 현재 B+트리 value에는 파일 offset이 아니라 `row_index`를 저장합니다. |
| 왜 row_index로 바꿨나요? | 인덱스 조회 후 CSV 파일을 다시 읽지 않고 `table->rows[row_index]`로 바로 접근하기 위해서입니다. |
| B+트리는 디스크에 남나요? | 아닙니다. 현재 구현은 프로그램 실행 중 메모리에 만드는 인덱스입니다. |
| 첫 조회가 왜 느린가요? | CSV 로드와 B+트리 생성 시간이 포함되기 때문입니다. |
| B+트리가 항상 빠른가요? | 아닙니다. 조건에 맞는 row가 많으면 결국 많은 데이터를 읽어야 해서 선형 탐색과 차이가 줄거나 느릴 수 있습니다. |

---

## 코드에서 볼 부분

발표 후 코드 질문이 나오면 아래 파일을 보면 됩니다.

| 파일 | 역할 |
| --- | --- |
| `src/bptree.h` | B+트리 node 구조체 정의 |
| `src/bptree.c` | search, insert, split 로직 |
| `src/index.h` | `RowRef`, `OffsetNode`, `OffsetList`, `PlayerIndexSet` 정의 |
| `src/index.c` | `id`, `game_win_count` 인덱스 생성 및 조회 |
| `src/executor.c` | SQL 조건에 따라 선형 탐색/B+트리 선택 |
| `src/storage.c` | CSV 로드, players INSERT, meta id 관리 |

---

## 조심해서 말할 부분

아래 표현은 정확히 구분해서 말하는 것이 좋습니다.

잘못 말하기 쉬운 표현:

```text
INSERT할 때 B+트리에 바로 등록합니다.
```

현재 구현 기준으로 더 정확한 표현:

```text
현재 구현은 메모리 기반 인덱스라서 SELECT 시점에 CSV 데이터를 기준으로 B+트리를 구성합니다.
INSERT나 DELETE 후에는 캐시를 무효화하고 다음 조회 때 다시 빌드합니다.
```

잘못 말하기 쉬운 표현:

```text
B+트리 value에 offset을 저장합니다.
```

현재 구현 기준으로 더 정확한 표현:

```text
처음에는 offset 방식도 고려할 수 있지만, 현재 구현에서는 파일 offset 대신 row_index를 저장합니다.
```
