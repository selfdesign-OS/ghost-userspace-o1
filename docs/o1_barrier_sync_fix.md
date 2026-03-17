# O1 Scheduler: 커널-유저스페이스 배리어 동기화 버그 수정 보고서

## 환경

### 하드웨어 및 OS

- 베어메탈 리눅스 5.11 (VirtualBox에서 변경 — 가상화 오버헤드로 인한 타이밍 변수 제거 목적)
- CPU 고립 설정 (커널 파라미터):
  ```
  isolcpus=nohz,domain,4-5
  nohz_full=4-5
  rcu_nocbs=4-5
  irq_affinity=0-3,6-15
  ```
  4, 5번 코어를 OS 스케줄러, 인터럽트, RCU 처리에서 완전히 격리

### 스케줄러

- O1 per-CPU ghost scheduler
- ghost_cpus: 4, 5번 코어 (고립된 코어에 에이전트 고정)

### 테스트 설계

`SimpleExpMany(1000)` hang 현상의 재현성이 없어 분석이 어려웠다.
원인을 단계적으로 좁히기 위해 선점이 전혀 발생하지 않는 최소 조건의
테스트(`o1_test`)를 먼저 작성해 hang 여부를 확인했다.

- `ShortTaskCompletesWithoutPreemption` — 단일 스레드, 1ms 작업 (타임슬라이스 10ms 미만)
- `MultipleShortTasksCompleteWithoutPreemption` — 10개 스레드, 1ms 작업

선점 변수를 제거하고 환경 변수(가상화, 인터럽트, OS 스케줄러 간섭)를 최소화한
상태에서도 hang이 재현됨을 확인하여 스케줄러 내부 로직 문제로 범위를 좁혔다.

---

## 문제

### 증상

- 다중 스레드 환경에서 `t->Join()`이 반환되지 않는 hang 발생
- **비결정적** — 동일 코드로 실행해도 통과/실패가 불규칙 (두 테스트 공통)
- `--verbose=1` 로깅 활성화 시 통과하는 경우가 많아지는 하이젠버그 현상
- 동일 조건에서 FIFO per-CPU 스케줄러는 통과, O1 스케줄러만 hang

### hang의 실제 형태

hang이 발생했을 때 프로세스가 멈춘 것이 아니라, 커밋 실패(`state=-2147483642`)가
무한히 증가하며 테스트가 종료되지 않는 형태였다.

`state=-2147483642`는 `GHOST_TXN_AGENT_STALE`(에이전트 배리어 동기화 오류)로,
에이전트가 커널의 현재 시퀀스 번호보다 낡은 배리어로 커밋을 시도할 때 반환된다.

**`simple_exp` 실행 방법:**
```bash
sudo bazel run o1_agent -- --verbose=1 log.txt 2>&1
sudo bazel run simple_exp
grep -c "commit failed (state=-2147483642)" log.txt
```

| 조건 | 관측 결과 |
|------|-----------|
| `boosted_priority=false` | 커밋 실패가 10만 건을 넘어 계속 증가, `SimpleExpMany` 종료되지 않음 |
| `boosted_priority=true`  | 커밋 실패 4,531건에서 고정, `SimpleExpMany` 정상 종료 |

**`o1_test` 실행 방법:**
```bash
sudo bazel run o1_test -- --verbose=1 log.txt 2>&1
grep -c "commit failed (state=-2147483642)" log.txt
```

| 조건 | 관측 결과 |
|------|-----------|
| `boosted_priority=false` | 커밋 실패가 10,000건을 넘어 계속 증가, 테스트 종료되지 않음 |
| `boosted_priority=true`  | 커밋 실패 7~9건에서 고정, `o1_test` 정상 종료 |

두 테스트 모두 `boosted_priority=false`일 때 hang이 **항상** 발생하지는 않았다.
타이밍에 따라 정상 종료되는 경우도 있었으며, 이는 비결정적 동작임을 의미한다.

### 원인

Ghost 스케줄러는 에이전트가 커널에 태스크 스케줄링을 요청(트랜잭션 커밋)할 때
`agent_barrier`를 함께 제출한다. 커널은 이 값이 현재 시퀀스 번호와 일치할 때만
커밋을 승인하며, 낡은 배리어로 요청하면 `GHOST_TXN_AGENT_STALE`을 반환한다.

커널이 새 메시지를 포스팅하면 시퀀스 번호를 증가시키고 `BOOST_PRIO` 신호를
에이전트 StatusWord에 세트한다. 이 신호는 에이전트에게 "배리어가 갱신됐으니
LocalYield 후 재동기화하라"는 의미다.

O1 스케줄러는 이 신호를 전달하는 `agent_sw.boosted_priority()`를
태스크의 우선순위 신호로 오인하여 스케줄러 정책상 불필요하다고 판단,
`false`로 하드코딩했다.

```cpp
// 수정 전 — boosted_priority()를 태스크 우선순위로 오인하여 false 하드코딩
O1Schedule(cpu, agent_barrier, false);

// 수정 후
O1Schedule(cpu, agent_barrier, agent_sw.boosted_priority());
```

실제로 `boosted_priority()`는 태스크 우선순위가 아니라 커널이 에이전트에게
보내는 배리어 재동기화 요청 신호다. 이를 무시하면 에이전트는 낡은 배리어로
커밋을 계속 시도하고, 스레드 수가 많아질수록 메시지 발생 빈도가 높아져
배리어가 항상 낡은 상태를 유지하게 된다. 결국 커밋이 영구적으로 실패하며
테스트가 종료되지 않는 상태에 빠진다.

### 비결정적 동작과 하이젠버그 원인

`boosted_priority=false`일 때도 항상 hang이 발생하지는 않는다.
배리어를 읽는 시점과 커밋 시점 사이에 커널이 새 메시지를 포스팅하지 않으면
커밋이 성공하기 때문이다. 이 타이밍은 시스템 부하, CPU 상태에 따라 달라진다.

`--verbose=1` 활성화 시 `fprintf` 시스템 콜이 에이전트 루프를 충분히 늦춰
배리어가 일시적으로 안정되는 효과가 생기며, 이로 인해 통과 확률이 높아지는
하이젠버그 현상이 나타난다.

---

## 디버깅 과정

1. `--verbose=3` 설정으로 전체 스케줄러 로그를 `log.txt`에 출력
2. 커밋 실패 시 `state` 값을 출력하는 printf 문 추가
3. `grep -c "commit failed (state=-2147483642)" log.txt` 로 실패 건수 추적
4. `boosted_priority=false` 시 실패 건수가 무한히 증가함을 확인
5. `-2147483642`를 unsigned 32비트로 변환 → `2147483654 = GHOST_TXN_AGENT_STALE`
   (에이전트 배리어 동기화 오류 — 에이전트가 낡은 시퀀스 번호로 커밋을 시도함)
6. `boosted_priority()`가 태스크 우선순위가 아닌 배리어 재동기화 신호임을 파악하고 수정

---

## 수정

`O1Schedule` 호출 시 `agent_sw.boosted_priority()` 값을 전달하도록 복원.

`prio_boost=true`일 때 에이전트는 커밋 시도 대신 `LocalYield(RTLA_ON_IDLE)`을
호출해 CPU를 반납하고 잠든다. 깨어난 후 `Schedule()`을 재진입해 최신 배리어를
읽고 채널에 쌓인 메시지를 처리한 뒤 커밋을 재시도한다.

---

## 결과

| 테스트 | 조건 | 커밋 실패 건수 | 결과 |
|--------|------|---------------|------|
| `o1_test` | `boosted_priority=false` | 10,000건+ (무한 증가) | hang |
| `o1_test` | `boosted_priority=true` | 7~9건 (고정) | 통과 |
| `SimpleExpMany(1000)` | `boosted_priority=false` | 100,000건+ (무한 증가) | hang |
| `SimpleExpMany(1000)` | `boosted_priority=true` | 4,531건 (고정) | 통과 |

수정 후 남은 소수의 실패 건수는 배리어 읽기와 커밋 사이의 피할 수 없는
race window에서 발생하며, `boosted_priority` 감지로 LocalYield → 재시도 경로가
열려 hang 없이 해소된다.
