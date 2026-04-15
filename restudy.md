# Week 7 SQL Processor 복습 노트

이 문서는 다음 과제인 B+ Tree 인덱스 구현을 하기 전에, 현재 프로젝트를 초보자 기준으로 이해하기 위한 복습 자료입니다.

목표는 코드를 전부 외우는 것이 아닙니다. 아래 네 가지를 이해하면 됩니다.

1. SQL이 입력되면 어떤 순서로 처리되는가
2. CSV 파일을 어떻게 테이블처럼 다루는가
3. row offset, lock, index 같은 개념이 왜 필요한가
4. 현재 인덱스의 한계 때문에 왜 B+ Tree가 필요한가

---

## 1. 이 프로젝트를 한 문장으로 설명하면

이 프로젝트는 C 언어로 만든 작은 SQL 처리기입니다.

사용자가 이런 SQL을 입력하면:

```sql
SELECT name FROM users WHERE age >= 27;
```

프로그램은 이 SQL을 해석해서 `data/users.csv` 같은 CSV 파일을 읽고, 조건에 맞는 결과를 출력합니다.

즉, 진짜 데이터베이스 서버를 만드는 것은 아니고, CSV 파일을 작은 데이터베이스 테이블처럼 다루는 프로그램입니다.

---

## 2. 전체 실행 흐름

SQL 한 문장은 아래 순서로 처리됩니다.

```text
사용자 SQL 입력
  -> main.c
  -> tokenizer.c
  -> parser.c
  -> executor.c
  -> storage.c
  -> index.c
  -> 결과 출력
```

조금 더 자세히 보면:

```text
1. main.c
   SQL 한 문장을 받는다.

2. tokenizer.c
   SQL 문자열을 작은 토큰들로 쪼갠다.

3. parser.c
   토큰 배열을 SqlStatement 구조체로 바꾼다.

4. executor.c
   INSERT, SELECT, DELETE 중 어떤 명령인지 보고 실제 실행한다.

5. storage.c
   CSV 파일을 읽거나 쓴다.

6. index.c
   WHERE 조건 검색을 빠르게 하기 위해 key -> row offset 지도를 만든다.
```

핵심 흐름은 `src/main.c`의 `main_process_sql_statement()`에서 시작합니다.

```c
tokens = tokenizer_tokenize(working_sql, &token_count);
status = parser_parse(tokens, token_count, &statement);
status = executor_execute(&statement);
```

이 세 줄이 프로젝트의 중심입니다.

---

## 3. 파일별 역할

| 파일 | 역할 | 초보자용 설명 |
| --- | --- | --- |
| `src/main.c` | 프로그램 시작점 | SQL을 입력받고 처리 흐름을 시작한다. |
| `src/tokenizer.c` | 토큰화 | SQL 문자열을 단어 조각으로 쪼갠다. |
| `src/parser.c` | 파싱 | 토큰을 보고 INSERT/SELECT/DELETE 구조체로 만든다. |
| `src/executor.c` | 실행 | 파싱된 SQL을 실제로 실행한다. |
| `src/storage.c` | 저장소 | CSV 파일을 테이블처럼 읽고 쓴다. |
| `src/index.c` | 인덱스 | WHERE 검색을 빠르게 하기 위한 임시 인덱스를 만든다. |
| `src/utils.c` | 공통 도구 | 문자열 처리, 비교, 파일 읽기 같은 helper 함수가 있다. |
| `Makefile` | 빌드 설정 | `make`, `make tests`, `make clean` 규칙이 있다. |
| `tests/` | 테스트 | 각 기능이 제대로 동작하는지 확인한다. |

---

## 4. 먼저 알아야 할 기본 개념

## 4.1 Table

Table은 데이터베이스의 표입니다.

이 프로젝트에서는 table이 실제로 CSV 파일 하나입니다.

예를 들어 `users` 테이블은 실제로 아래 파일입니다.

```text
data/users.csv
```

파일 내용은 이런 식입니다.

```csv
id,name,age
1,Alice,30
2,Bob,25
3,Chris,35
```

---

## 4.2 Column

Column은 세로 칸입니다.

위 예시에서는 column이 세 개입니다.

```text
id
name
age
```

CSV 파일의 첫 번째 줄은 column 이름 목록입니다.

```csv
id,name,age
```

이 첫 줄을 header라고 부릅니다.

---

## 4.3 Row

Row는 가로 한 줄입니다.

```csv
2,Bob,25
```

이 한 줄이 row 하나입니다.

각 값은 column에 대응됩니다.

```text
id   = 2
name = Bob
age  = 25
```

---

## 4.4 SQL Statement

Statement는 SQL 한 문장입니다.

예:

```sql
INSERT INTO users (name, age) VALUES ('Alice', 30);
SELECT * FROM users;
DELETE FROM users WHERE name = 'Bob';
```

이 프로젝트는 크게 세 가지 statement를 지원합니다.

| SQL | 의미 |
| --- | --- |
| `INSERT` | row 추가 |
| `SELECT` | row 조회 |
| `DELETE` | row 삭제 |

`UPDATE`, `JOIN`, `ORDER BY`, `GROUP BY`, `AND`, `OR` 같은 기능은 현재 지원하지 않습니다.

---

## 5. Tokenizer 이해하기

Tokenizer는 SQL 문자열을 작은 조각으로 나눕니다.

예를 들어:

```sql
SELECT name FROM users WHERE age >= 27;
```

이 SQL은 대략 이런 토큰으로 나뉩니다.

```text
SELECT
name
FROM
users
WHERE
age
>=
27
;
```

각 토큰에는 타입이 있습니다.

| 토큰 | 타입 |
| --- | --- |
| `SELECT` | `TOKEN_KEYWORD` |
| `name` | `TOKEN_IDENTIFIER` |
| `>=` | `TOKEN_OPERATOR` |
| `27` | `TOKEN_INT_LITERAL` |
| `;` | `TOKEN_SEMICOLON` |

Tokenizer가 필요한 이유는 parser가 문자열 전체를 직접 해석하기 어렵기 때문입니다.

사람도 문장을 이해할 때 단어 단위로 읽습니다. tokenizer는 SQL을 단어 단위로 잘라 parser가 이해하기 쉽게 만드는 단계입니다.

---

## 6. Parser 이해하기

Parser는 토큰 배열을 보고 SQL의 구조를 파악합니다.

예:

```sql
SELECT name FROM users WHERE age >= 27;
```

Parser는 이 문장을 이런 구조로 바꿉니다.

```text
type = SQL_SELECT
table_name = users
columns = name
has_where = 1
where.column = age
where.op = >=
where.value = 27
```

이 결과가 `SqlStatement` 구조체입니다.

초보자 관점에서는 이렇게 이해하면 됩니다.

```text
tokenizer:
  문자열을 조각으로 쪼갠다.

parser:
  조각들을 보고 의미 있는 구조체로 만든다.
```

Parser는 SQL을 실행하지 않습니다. 실행하기 좋은 형태로 정리만 합니다.

---

## 7. Executor 이해하기

Executor는 parser가 만든 `SqlStatement`를 실제로 실행합니다.

```text
SQL_INSERT -> INSERT 실행
SQL_SELECT -> SELECT 실행
SQL_DELETE -> DELETE 실행
```

예를 들어 parser가 이런 결과를 만들었다고 합시다.

```text
type = SQL_SELECT
table_name = users
where.column = age
where.op = >=
where.value = 27
```

executor는 이 정보를 보고:

```text
1. users 테이블을 읽는다.
2. age 컬럼을 찾는다.
3. WHERE age >= 27 조건에 맞는 row를 찾는다.
4. 결과를 표처럼 출력한다.
```

Executor는 직접 CSV를 다루지 않고 `storage.c`에 요청합니다.

Executor는 직접 인덱스 자료구조를 만들지 않고 `index.c`에 요청합니다.

즉, executor는 전체 작업의 지휘자 역할입니다.

---

## 8. Storage 이해하기

Storage는 CSV 파일을 실제로 읽고 쓰는 부분입니다.

테이블 이름이 `users`이면 파일은 다음 위치에 저장됩니다.

```text
data/users.csv
```

INSERT를 실행하면 CSV 파일에 row가 추가됩니다.

```sql
INSERT INTO users (name, age) VALUES ('Alice', 30);
```

결과 CSV:

```csv
id,name,age
1,Alice,30
```

여기서 `id`는 사용자가 넣지 않아도 자동으로 붙을 수 있습니다.

Storage가 담당하는 일:

| 함수 | 역할 |
| --- | --- |
| `storage_insert()` | CSV에 row 추가 |
| `storage_delete()` | 조건에 맞는 row 삭제 |
| `storage_load_table()` | CSV 전체를 메모리로 읽기 |
| `storage_read_row_at_offset()` | 특정 offset 위치의 row 하나만 읽기 |
| `storage_get_columns()` | CSV header 읽기 |

---

## 9. Row Offset 이해하기

## 9.1 Offset이란

Offset은 파일 안에서 어떤 위치를 가리키는 숫자입니다.

CSV 파일이 이렇게 있다고 합시다.

```csv
id,name,age
1,Alice,30
2,Bob,25
3,Chris,35
```

컴퓨터는 이 파일을 사실상 긴 문자열처럼 봅니다.

```text
id,name,age\n1,Alice,30\n2,Bob,25\n3,Chris,35\n
```

각 row가 파일 안에서 시작하는 위치가 있습니다.

대략 이런 느낌입니다.

```text
0  -> id,name,age
12 -> 1,Alice,30
23 -> 2,Bob,25
32 -> 3,Chris,35
```

여기서 Bob row의 offset은 `23`이라고 볼 수 있습니다.

정확한 숫자는 줄바꿈 문자나 문자열 길이에 따라 달라질 수 있습니다. 중요한 것은 숫자 자체가 아니라 개념입니다.

```text
row offset = 파일 안에서 해당 row가 시작하는 위치
```

---

## 9.2 Offset이 왜 필요한가

offset이 없으면 특정 row를 찾기 위해 파일 처음부터 끝까지 계속 읽어야 합니다.

예:

```sql
SELECT * FROM users WHERE name = 'Bob';
```

offset이 없다면:

```text
1. 첫 row 읽기
2. Alice인지 확인
3. 두 번째 row 읽기
4. Bob인지 확인
5. 찾음
```

데이터가 3개면 괜찮습니다.

하지만 row가 1,000,000개라면 매번 처음부터 읽는 것은 너무 느립니다.

offset이 있으면:

```text
1. 인덱스에서 Bob의 offset을 찾는다.
2. 파일에서 그 위치로 바로 이동한다.
3. Bob row만 읽는다.
```

C에서는 이런 함수가 사용됩니다.

```c
ftell(fp)
```

현재 파일 위치를 알려줍니다.

```c
fseek(fp, offset, SEEK_SET)
```

파일의 특정 위치로 이동합니다.

이 프로젝트의 인덱스는 row 전체를 저장하지 않고 offset을 저장합니다.

---

## 10. Lock 이해하기

## 10.1 Lock이란

Lock은 파일을 동시에 읽고 쓰다가 데이터가 깨지는 것을 막기 위한 잠금장치입니다.

예를 들어 두 작업이 동시에 일어난다고 합시다.

```text
A: users.csv를 읽는 중
B: users.csv에 새 row를 쓰는 중
```

B가 쓰는 도중에 A가 읽으면 A는 이상한 중간 상태를 볼 수 있습니다.

그래서 파일을 읽거나 쓸 때 lock을 겁니다.

---

## 10.2 Shared Lock

Shared lock은 공유 락입니다.

읽기 작업에 사용됩니다.

```text
SELECT = 읽기 = shared lock
```

공유 락은 여러 명이 동시에 잡을 수 있습니다.

왜냐하면 읽기만 하는 작업끼리는 서로 데이터를 망가뜨리지 않기 때문입니다.

```text
A: SELECT 중
B: SELECT 중
C: SELECT 중

가능
```

---

## 10.3 Exclusive Lock

Exclusive lock은 배타 락입니다.

쓰기 작업에 사용됩니다.

```text
INSERT = 쓰기 = exclusive lock
DELETE = 쓰기 = exclusive lock
```

배타 락은 한 명만 잡을 수 있습니다.

```text
A: INSERT 중
B: SELECT 하려 함
C: DELETE 하려 함

B와 C는 기다려야 함
```

쓰기 중에 다른 작업이 끼어들면 파일이 깨지거나 이상한 상태를 읽을 수 있기 때문입니다.

---

## 11. Index 이해하기

## 11.1 Index란

Index는 검색을 빠르게 하기 위한 지도입니다.

책에서 특정 단어가 몇 페이지에 있는지 찾을 때 색인을 보는 것과 비슷합니다.

CSV 파일이 이렇게 있다고 합시다.

```csv
id,name,age
1,Alice,30
2,Bob,25
3,Chris,35
```

`age` 컬럼에 대한 인덱스는 이런 느낌입니다.

```text
25 -> Bob row offset
30 -> Alice row offset
35 -> Chris row offset
```

그러면 이 SQL을 실행할 때:

```sql
SELECT * FROM users WHERE age = 25;
```

전체 CSV를 처음부터 끝까지 읽지 않고:

```text
1. age 인덱스에서 25 검색
2. Bob row offset 획득
3. offset 위치로 이동
4. Bob row 읽기
```

이렇게 처리할 수 있습니다.

---

## 11.2 현재 프로젝트의 인덱스

현재 `src/index.c`는 B+ Tree가 아닙니다.

현재는 두 종류의 임시 메모리 인덱스를 만듭니다.

| 조건 | 현재 방식 |
| --- | --- |
| `=` | Hash index |
| `>`, `>=`, `<`, `<=`, `!=` | 정렬된 배열 기반 range index |

즉:

```sql
WHERE name = 'Bob'
```

같은 조건은 hash index를 씁니다.

```sql
WHERE age >= 27
```

같은 조건은 정렬된 배열을 씁니다.

---

## 12. 현재 SELECT WHERE 실행 흐름

예를 들어:

```sql
SELECT name FROM users WHERE age >= 27;
```

현재 프로젝트는 대략 이렇게 동작합니다.

```text
1. executor가 SELECT 문장을 받는다.
2. storage가 users.csv 전체를 메모리로 읽는다.
3. age 컬럼 위치를 찾는다.
4. index.c가 age 기준 인덱스를 만든다.
5. 인덱스에서 age >= 27에 맞는 row offset 목록을 찾는다.
6. storage_read_row_at_offset()으로 해당 row만 다시 읽는다.
7. name 컬럼만 출력한다.
```

여기서 핵심은:

```text
인덱스의 결과 = row 자체가 아니라 row offset 목록
```

입니다.

---

## 13. Cache 이해하기

Cache는 한 번 계산한 결과를 잠시 저장해두고 다시 쓰는 것입니다.

이 프로젝트에는 몇 가지 cache가 있습니다.

| cache | 위치 | 의미 |
| --- | --- | --- |
| tokenizer cache | `tokenizer.c` | 같은 SQL 문자열을 다시 토큰화하지 않음 |
| table cache | `executor.c` | 같은 테이블을 반복해서 CSV에서 읽지 않음 |
| index cache | `executor.c` | 같은 테이블, 같은 컬럼의 인덱스를 다시 만들지 않음 |

예를 들어 같은 SELECT를 두 번 실행하면:

```sql
SELECT name FROM users WHERE age >= 27;
SELECT name FROM users WHERE age >= 27;
```

두 번째 실행에서는 이전에 만든 table cache와 index cache를 재사용할 수 있습니다.

하지만 INSERT나 DELETE가 일어나면 테이블 내용이 바뀌므로 cache를 무효화해야 합니다.

```text
INSERT 발생
-> 기존 table cache 무효화
-> 기존 index cache 무효화
```

그렇지 않으면 오래된 데이터로 SELECT 결과를 낼 수 있습니다.

---

## 14. 현재 인덱스 방식의 한계

현재 인덱스는 아주 좋은 학습용 구조입니다.

하지만 데이터가 커지면 한계가 있습니다.

## 14.1 매번 CSV 전체를 읽어야 한다

현재는 인덱스를 만들기 전에 `storage_load_table()`로 CSV 전체를 메모리에 읽습니다.

즉, WHERE 검색을 빠르게 하려는 인덱스가 있어도 처음에는 전체 테이블을 읽어야 합니다.

데이터가 적으면 괜찮습니다.

하지만 row가 1,000,000개라면:

```text
CSV 전체 읽기
-> 메모리에 모든 row 저장
-> 인덱스 만들기
-> 그 다음 검색
```

이 과정 자체가 무겁습니다.

---

## 14.2 인덱스가 영구 저장되지 않는다

현재 인덱스는 메모리에 임시로 만들어집니다.

프로그램이 종료되면 사라집니다.

다시 실행하면 다시 만들어야 합니다.

즉:

```text
프로그램 시작
-> SELECT WHERE 실행
-> CSV 읽기
-> 인덱스 생성
-> 검색
-> 프로그램 종료
-> 인덱스 사라짐
```

큰 데이터에서는 매번 다시 만드는 비용이 큽니다.

---

## 14.3 메모리를 많이 쓴다

현재 방식은 CSV 전체를 메모리에 올리고, 추가로 인덱스도 메모리에 만듭니다.

데이터가 작으면 괜찮지만, 데이터가 커지면 메모리를 많이 사용합니다.

```text
CSV 전체 rows
+ offsets 배열
+ hash index
+ range index
```

이 모든 것이 메모리에 올라갑니다.

---

## 14.4 range query에 더 좋은 구조가 필요하다

Range query는 이런 조건입니다.

```sql
WHERE age >= 27
WHERE age < 50
WHERE name >= 'Kim'
```

현재는 정렬 배열을 사용합니다.

정렬 배열은 검색은 빠를 수 있지만, 삽입과 삭제가 자주 일어나면 유지하기 어렵습니다.

예를 들어 중간에 새 값을 넣으려면 배열 뒤쪽 데이터를 밀어야 할 수 있습니다.

데이터가 많아질수록 비효율적입니다.

---

## 15. 그래서 B+ Tree가 왜 필요한가

B+ Tree는 데이터베이스에서 인덱스를 만들 때 자주 쓰는 자료구조입니다.

필요한 이유는 현재 방식의 한계를 해결하기 위해서입니다.

## 15.1 전체 scan을 줄이기 위해

Full scan은 테이블의 모든 row를 처음부터 끝까지 읽는 것입니다.

```text
Full scan:
row 1 확인
row 2 확인
row 3 확인
...
row N 확인
```

데이터가 많아질수록 느려집니다.

B+ Tree 인덱스가 있으면:

```text
key 검색
-> leaf node에서 offset 찾기
-> 해당 row만 읽기
```

이렇게 할 수 있습니다.

---

## 15.2 디스크 친화적인 구조라서

일반 이진 탐색 트리는 노드 하나에 key가 적게 들어갑니다.

하지만 B+ Tree는 노드 하나에 여러 key를 넣습니다.

예:

```text
[10 | 20 | 30 | 40]
```

이렇게 한 번에 여러 key를 담을 수 있어서 디스크에서 읽어야 하는 횟수를 줄일 수 있습니다.

데이터베이스에서는 메모리보다 디스크 접근이 훨씬 느립니다.

그래서 "디스크를 몇 번 읽느냐"가 중요합니다.

B+ Tree는 높이가 낮고, 한 노드에 많은 key를 담을 수 있어서 디스크 기반 인덱스에 적합합니다.

---

## 15.3 range query에 강해서

B+ Tree의 leaf node들은 보통 옆 leaf와 연결되어 있습니다.

예:

```text
[10, 20] -> [25, 30] -> [35, 40] -> [50, 60]
```

그래서 `age >= 30` 같은 range query를 처리할 때:

```text
1. 30이 있는 leaf를 찾는다.
2. 그 leaf부터 오른쪽 leaf로 쭉 이동한다.
3. 조건에 맞는 offset들을 모은다.
```

이 방식이 range query에 좋습니다.

---

## 15.4 삽입과 삭제 후에도 정렬 상태를 유지하기 좋아서

인덱스는 key가 정렬되어 있어야 검색이 빠릅니다.

배열로 정렬 상태를 유지하려면 중간 삽입과 삭제가 비쌀 수 있습니다.

B+ Tree는 node split, merge, redistribution 같은 규칙으로 정렬 상태를 유지합니다.

초보자 단계에서는 이 정도로만 이해하면 됩니다.

```text
B+ Tree는 값을 넣고 지워도 검색하기 좋은 정렬 구조를 유지한다.
```

---

## 16. B+ Tree 인덱스가 저장해야 하는 것

이 프로젝트에서 B+ Tree 인덱스가 저장해야 할 핵심 정보는 보통 이 형태입니다.

```text
key -> row offset
```

예를 들어 `age` 컬럼 인덱스:

```text
25 -> offset of Bob row
30 -> offset of Alice row
35 -> offset of Chris row
```

`name` 컬럼 인덱스:

```text
Alice -> offset of Alice row
Bob   -> offset of Bob row
Chris -> offset of Chris row
```

중요한 점:

```text
B+ Tree leaf node에는 row 전체를 저장하지 않아도 된다.
row를 찾을 수 있는 offset만 저장하면 된다.
```

그 후 실제 row는 `storage_read_row_at_offset()` 같은 함수로 읽으면 됩니다.

---

## 17. 현재 코드에서 B+ Tree와 연결될 가능성이 큰 곳

B+ Tree 과제에서 가장 먼저 볼 파일은 다음 네 개입니다.

| 파일 | 이유 |
| --- | --- |
| `src/index.h` | 현재 인덱스 구조체와 함수 선언이 있다. |
| `src/index.c` | 현재 인덱스 생성, 검색, 해제 로직이 있다. |
| `src/executor.c` | SELECT WHERE에서 인덱스를 호출한다. |
| `src/storage.c` | row offset을 만들고 offset으로 row를 읽는다. |

현재 인덱스 API는 다음과 같습니다.

```c
int index_build(const TableData *table, int column_index, TableIndex *out_index);

int index_query_equals(const TableIndex *index, const char *value,
                       long **offsets, int *count);

int index_query_range(const TableIndex *index, const char *op, const char *value,
                      long **offsets, int *count);

void index_free(TableIndex *index);
```

B+ Tree를 구현한다면 이 함수들의 내부 동작이 바뀌거나, 새로운 B+ Tree 전용 함수가 추가될 가능성이 큽니다.

---

## 18. SELECT WHERE를 B+ Tree 방식으로 상상해보기

현재 방식:

```text
1. CSV 전체 읽기
2. 메모리 인덱스 생성
3. key 검색
4. offset 목록 획득
5. offset으로 row 읽기
```

B+ Tree 방식:

```text
1. B+ Tree 인덱스에서 key 검색
2. offset 목록 획득
3. offset으로 row 읽기
```

차이는 이것입니다.

```text
현재 방식:
  검색하기 전에 CSV 전체를 읽고 인덱스를 다시 만든다.

B+ Tree 방식:
  이미 만들어진 인덱스를 이용해서 필요한 row 위치를 바로 찾는다.
```

---

## 19. 초보자용 핵심 비유

## 19.1 CSV 파일

CSV 파일은 책 본문입니다.

```text
책 본문 = 실제 데이터
```

---

## 19.2 Row

Row는 책의 한 줄입니다.

```text
한 줄 = row 하나
```

---

## 19.3 Offset

Offset은 그 줄이 책의 몇 번째 글자 위치에서 시작하는지입니다.

```text
offset = 책 안에서 줄의 주소
```

---

## 19.4 Index

Index는 책 뒤의 색인입니다.

```text
Bob -> 23번째 위치
Alice -> 12번째 위치
```

---

## 19.5 Lock

Lock은 책을 읽거나 수정할 때 거는 잠금장치입니다.

```text
여러 명이 읽기만 하는 것은 가능
누가 수정 중이면 다른 사람은 기다려야 함
```

---

## 19.6 B+ Tree

B+ Tree는 색인을 빠르게 찾고, 정렬 상태로 유지하기 위한 자료구조입니다.

```text
B+ Tree = 큰 책에서도 빠르게 찾을 수 있는 정렬된 색인 구조
```

---

## 20. 복습 체크리스트

아래 질문에 답할 수 있으면 다음 과제로 넘어갈 준비가 된 것입니다.

1. `users` 테이블은 실제로 어떤 파일인가?
2. `row`와 `column`의 차이는 무엇인가?
3. tokenizer는 SQL을 무엇으로 바꾸는가?
4. parser는 토큰을 무엇으로 바꾸는가?
5. executor는 어떤 역할을 하는가?
6. storage는 왜 필요한가?
7. row offset은 무엇인가?
8. index는 row 자체를 저장하는가, row offset을 저장하는가?
9. shared lock과 exclusive lock은 각각 언제 필요한가?
10. 현재 인덱스는 왜 큰 데이터에서 한계가 있는가?
11. B+ Tree는 왜 range query에 유리한가?
12. B+ Tree leaf node에는 무엇을 저장하면 좋은가?

---

## 21. 코드 읽는 추천 순서

처음부터 모든 코드를 읽으려고 하면 어렵습니다.

아래 순서로 읽는 것을 추천합니다.

```text
1. src/main.c
   SQL 한 문장이 어떻게 처리 흐름에 들어가는지 본다.

2. src/parser.h
   SqlStatement 구조체를 본다.

3. src/parser.c
   SELECT, INSERT, DELETE가 어떻게 구조체로 바뀌는지 본다.

4. src/executor.c
   executor_execute()와 executor_execute_select()를 본다.

5. src/storage.h
   TableData 구조체를 본다.

6. src/storage.c
   storage_load_table(), storage_read_row_at_offset()을 본다.

7. src/index.h
   현재 인덱스 구조체를 본다.

8. src/index.c
   index_build(), index_query_equals(), index_query_range()를 본다.
```

---

## 22. B+ Tree 과제 전 꼭 기억할 결론

이 프로젝트에서 데이터의 실제 저장 위치는 CSV 파일입니다.

인덱스는 데이터를 대신 저장하는 것이 아니라, 데이터를 빠르게 찾기 위한 지도입니다.

그 지도에 적히는 핵심 정보는 이것입니다.

```text
검색 key -> row offset
```

현재 프로젝트는 이 지도를 매번 메모리에 임시로 만드는 방식입니다.

B+ Tree 과제는 이 지도를 더 데이터베이스답게, 더 큰 데이터에서도 빠르게 동작하도록 만드는 작업이라고 이해하면 됩니다.

최종적으로 머릿속에 남겨야 할 그림은 이것입니다.

```text
SQL:
SELECT * FROM users WHERE age = 25;

B+ Tree index:
25 -> offset 23

CSV:
offset 23 위치로 이동
-> 2,Bob,25 읽기

결과:
Bob row 출력
```

