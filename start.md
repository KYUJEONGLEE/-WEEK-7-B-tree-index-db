# 실행 방법 정리

이 문서는 이 프로젝트를 처음 실행하는 사람이 그대로 따라 할 수 있게 만든 실행 가이드다.

현재 프로젝트는 C 기반 SQL Processor이고, 실행 파일 이름은 `sql_processor`다.
SQL을 실행하는 방식은 크게 2가지다.

1. `.sql` 파일을 한 번에 실행하기
2. REPL 모드에서 SQL을 직접 입력하기

---

## 1. 먼저 알아야 할 것

### 이 프로젝트가 하는 일

```text
SQL 입력
-> tokenizer
-> parser
-> executor
-> storage(CSV 파일)
```

데이터는 DB 서버가 아니라 `data/` 폴더 안의 CSV 파일에 저장된다.

예를 들어 `players` 테이블에 INSERT하면 아래 파일들이 생긴다.

```text
data/players.csv
data/players.meta
```

- `players.csv`: 실제 row 데이터 저장
- `players.meta`: 다음에 사용할 자동 증가 id 저장

---

## 2. 권장 실행 방법: Docker 사용

현재 Windows PowerShell 환경에 `make`, `gcc`가 직접 설치되어 있지 않다면 Docker로 실행하는 것이 가장 쉽다.

이 프로젝트에서는 이미 `jungle-c-dev` Docker 이미지로 테스트를 통과했다.

### 2-1. 전체 테스트 실행

PowerShell에서 프로젝트 루트 폴더(`C:\junhee\WEEK7_CODEX`)에서 실행한다.

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev make tests
```

성공하면 대략 아래처럼 나온다.

```text
Results: 14 passed, 0 failed
```

### 2-2. 빌드만 하기

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev make
```

빌드가 성공하면 실행 파일이 생긴다.

```text
sql_processor
```

### 2-3. SQL 파일 실행하기

이미 만들어 둔 B+ Tree 테스트 SQL 파일을 실행하려면 아래 명령을 사용한다.

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev sh -c "make && ./sql_processor tests/test_cases/player_bptree.sql"
```

이 SQL 파일은 아래 흐름을 확인한다.

```sql
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000001', 10, 2);
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000002', 20, 4);
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000003', 10, 3);

SELECT nickname FROM players WHERE id = 2;
SELECT nickname FROM players WHERE game_win_count = 10;
SELECT nickname FROM players WHERE nickname = 'player_000002';
```

의미는 아래와 같다.

- `WHERE id = 2`: id B+ Tree 사용
- `WHERE game_win_count = 10`: game_win_count B+ Tree 사용
- `WHERE nickname = 'player_000002'`: 선형 탐색 사용

### 2-4. REPL 모드로 직접 SQL 입력하기

아래 명령으로 컨테이너 안에 들어간다.

```powershell
docker run --rm -it -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev bash
```

컨테이너 안에서 빌드한다.

```bash
make
```

프로그램을 실행한다.

```bash
./sql_processor
```

그러면 아래처럼 입력창이 뜬다.

```text
SQL>
```

여기에 SQL을 직접 입력하면 된다.

```sql
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000001', 10, 2);
SELECT * FROM players WHERE id = 1;
SELECT * FROM players WHERE game_win_count = 10;
SELECT * FROM players WHERE nickname = 'player_000001';
```

종료하려면 아래 중 하나를 입력한다.

```text
exit
```

또는

```text
quit
```

중요:

- SQL 문장 끝에는 반드시 `;`를 붙인다.
- `exit`, `quit`에는 `;`를 붙이지 않아도 된다.

---

## 3. 로컬에 make/gcc가 설치된 경우

Linux, Mac, WSL, 또는 GCC/Make가 설치된 Windows 환경이라면 Docker 없이 바로 실행할 수 있다.

### 3-1. 빌드

```bash
make
```

### 3-2. 테스트

```bash
make tests
```

### 3-3. SQL 파일 실행

```bash
./sql_processor tests/test_cases/player_bptree.sql
```

Windows PowerShell에서 직접 실행 파일이 `.exe`로 만들어졌다면 아래처럼 실행한다.

```powershell
.\sql_processor.exe tests\test_cases\player_bptree.sql
```

### 3-4. REPL 모드 실행

```bash
./sql_processor
```

---

## 4. 직접 입력해볼 SQL 예시

### 4-1. player row 추가

`id`는 직접 넣지 않는다.
자동으로 1, 2, 3처럼 붙는다.

```sql
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000001', 10, 2);
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000002', 20, 4);
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000003', 10, 3);
```

저장되는 실제 row는 이런 느낌이다.

```csv
id,nickname,game_win_count,game_loss_count,total_game_count
1,player_000001,10,2,12
2,player_000002,20,4,24
3,player_000003,10,3,13
```

`total_game_count`는 직접 넣지 않아도 내부에서 자동 계산된다.

```text
total_game_count = game_win_count + game_loss_count
```

### 4-2. id로 조회하기

```sql
SELECT * FROM players WHERE id = 2;
```

이 조회는 `id` B+ Tree를 사용한다.

흐름:

```text
id 2 검색
-> B+ Tree에서 row offset 찾기
-> CSV 파일의 해당 위치만 읽기
```

### 4-3. game_win_count로 조회하기

```sql
SELECT * FROM players WHERE game_win_count = 10;
```

이 조회는 `game_win_count` B+ Tree를 사용한다.

`game_win_count`는 중복될 수 있으므로 결과가 여러 row일 수 있다.

예:

```text
player_000001 win_count = 10
player_000003 win_count = 10
```

### 4-4. nickname으로 조회하기

```sql
SELECT * FROM players WHERE nickname = 'player_000002';
```

이 조회는 B+ Tree를 사용하지 않는다.
처음 row부터 끝 row까지 하나씩 비교하는 선형 탐색이다.

이유:

```text
이번 구현의 인덱스 대상은 id, game_win_count 두 컬럼뿐이다.
```

### 4-5. 삭제하기

```sql
DELETE FROM players WHERE id = 2;
```

삭제 후에는 기존 B+ Tree cache를 버린다.
그 이유는 CSV 파일이 다시 쓰이면서 row offset이 바뀔 수 있기 때문이다.

다음에 다시 `SELECT ... WHERE id = ?`를 실행하면 B+ Tree를 새로 만든다.

---

## 5. benchmark 실행

benchmark는 B+ Tree 조회와 선형 탐색 조회의 속도 차이를 비교하는 기능이다.

### 5-1. 빠른 확인용 benchmark

처음에는 작은 데이터로 실행하는 것을 추천한다.

Docker:

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev make benchmark ARGS="10000 10"
```

로컬:

```bash
make benchmark ARGS="10000 10"
```

의미:

```text
10000 = row 10,000개 생성
10    = 같은 조회를 10번 반복
```

### 5-2. 과제 기준 benchmark

과제 기준은 1,000,000건 이상이다.

Docker:

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev make benchmark
```

로컬:

```bash
make benchmark
```

기본값:

```text
rows = 1,000,000
queries = 1,000
```

시간이 오래 걸릴 수 있으니 발표 전에 한 번 돌려서 결과를 README에 적으면 된다.

### 5-3. benchmark 출력 읽는 법

benchmark 출력에는 이런 값들이 나온다.

```text
plan used: 1
matched rows: 1
scanned rows: 10000
average: 0.601900 ms
speedup: 1.94x
```

plan 번호 의미:

```text
0 = FULL_SCAN
1 = LINEAR_SCAN
2 = BPTREE_ID_LOOKUP
3 = BPTREE_WIN_LOOKUP
```

중요하게 볼 값:

- `matched rows`: 조건에 맞은 row 수
- `scanned rows`: 실제로 검사한 row 수
- `average`: 평균 실행 시간
- `speedup`: 선형 탐색보다 몇 배 빨랐는지

예를 들어:

```text
scanned rows: 10000
```

이면 10,000개 row를 전부 봤다는 뜻이다.

```text
scanned rows: 1
```

이면 B+ Tree로 offset을 찾아 거의 바로 row를 읽었다는 뜻이다.

---

## 6. 데이터 초기화하기

실행하다 보면 `data/players.csv`, `data/players.meta`가 생긴다.
처음부터 다시 하고 싶으면 clean을 실행한다.

Docker:

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev make clean
```

로컬:

```bash
make clean
```

clean이 지우는 것:

```text
build/
sql_processor
data/*.csv
data/*.meta
```

---

## 7. 발표 전에 추천 실행 순서

아래 순서대로 하면 발표 준비가 편하다.

### 1단계: 깨끗하게 초기화

```bash
make clean
```

Docker를 쓰면:

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev make clean
```

### 2단계: 전체 테스트 확인

```bash
make tests
```

Docker를 쓰면:

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev make tests
```

기대 결과:

```text
Results: 14 passed, 0 failed
```

### 3단계: SQL 파일 데모

```bash
./sql_processor tests/test_cases/player_bptree.sql
```

Docker를 쓰면:

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev sh -c "make && ./sql_processor tests/test_cases/player_bptree.sql"
```

### 4단계: benchmark 실행

빠른 확인:

```bash
make benchmark ARGS="10000 10"
```

최종 발표용:

```bash
make benchmark
```

---

## 8. 자주 막히는 부분

### `make` 명령어가 없다고 나오는 경우

로컬에 Make가 없는 것이다.
Docker 명령어를 사용하면 된다.

```powershell
docker run --rm -v C:/junhee/WEEK7_CODEX:/app -w /app jungle-c-dev make tests
```

### `gcc`가 없다고 나오는 경우

로컬에 C 컴파일러가 없는 것이다.
역시 Docker 명령어를 사용하면 된다.

### SQL을 입력했는데 실행이 안 되는 경우

SQL 끝에 세미콜론 `;`을 붙였는지 확인한다.

```sql
SELECT * FROM players WHERE id = 1;
```

### id를 직접 넣어야 하는지 헷갈리는 경우

이번 player 테이블에서는 보통 id를 직접 넣지 않는다.

```sql
INSERT INTO players (nickname, game_win_count, game_loss_count) VALUES ('player_000001', 10, 2);
```

그러면 내부에서 자동으로 id를 붙인다.

### B+ Tree가 진짜 쓰이는지 확인하고 싶은 경우

일반 SELECT 출력에는 실행 계획이 직접 표시되지 않는다.
대신 benchmark와 executor 테스트에서 확인한다.

```bash
make tests
make benchmark ARGS="10000 10"
```

benchmark에서 아래 plan이 나오면 B+ Tree를 사용한 것이다.

```text
2 = BPTREE_ID_LOOKUP
3 = BPTREE_WIN_LOOKUP
```

