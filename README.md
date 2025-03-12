### Original Repository
This project is a modification of [ghost-userspace](https://github.com/google/ghost-userspace).  
Many thanks to the original author for their fantastic work!

Original repository link: [GitHub Link](https://github.com/google/ghost-userspace)

### 실행 환경 구축 가이드
- https://earthy-door-ce5.notion.site/Ghost-by-996861ed21dc41c592325fd0ec6ebb2f
### Test Demonstration Videos on YouTube
<a href="https://youtu.be/3K4QTmFbsxM" target="_blank">
  <img src="https://img.shields.io/badge/YouTube-Test1-FF0000?logo=youtube&logoColor=white" alt="Test1" style="width: 150px;"/>
</a>
<a href="https://youtu.be/_bYUfDNG5EQ" target="_blank">
  <img src="https://img.shields.io/badge/YouTube-Test2-FF0000?logo=youtube&logoColor=white" alt="Test2" style="width: 150px;"/>
</a>

### Members
|Jae-hyeong|Sung-woon|byeong-hyeon|
|:-:|:-:|:-:|
|<img src="https://github.com/user-attachments/assets/ef510219-2286-4048-9810-505ca2b5d5e6" alt="jae-hyeong" width="100" height="100">|<img src="https://github.com/user-attachments/assets/b9478ab7-b1b6-4313-bbc8-38e195364dde" alt="dingwoonee" width="100" height="100">|<img src="https://github.com/user-attachments/assets/d75b48a4-c3bb-4280-be3c-e0fbf4ea8464" alt="dingwoonee" width="100" height="100">|
|[JaehyeongIm](https://github.com/JaehyeongIm)|[DingWoonee](https://github.com/DingWoonee)|[bangbang444](https://github.com/bangbang444)|

### Technologies Used
![Badge](https://img.shields.io/badge/ghOSt%20framework-gray.svg?style=flat-square) <img src="https://img.shields.io/badge/C++-00599C?style=flat-square&logo=C%2B%2B&logoColor=white"/> <img src="https://img.shields.io/badge/Ubuntu-E95420?style=flat-square&logo=Ubuntu&logoColor=white"/>

# 1 서론

운영체제의 자원 관리 시스템은 여러 부분으로 나뉩니다. 그중에서도 중요한 역할을 하는 것이 스케줄러입니다. 스케줄러는 CPU와 같은 제한된 자원을 여러 프로그램이나 작업 간에 효율적으로 분배하는 역할을 합니다. 특히, 스케줄러는 각 작업(task)이 언제, 어느 정도의 자원을 사용할 수 있을지를 결정하는 핵심적인 시스템입니다.

처음에는 ULE(Ultra Low Energy) 스케줄러를 구현해보고자 했습니다. ULE는 FreeBSD 운영체제에서 사용되는 스케줄러로, 다양한 상황에서 효율적인 자원 분배를 목표로 설계되었습니다. 하지만, ghOSt 프레임워크에서 이미 ULE가 구현되어 있다는 사실을 알게 되면서, 새로운 스케줄러를 탐구해보기로 했습니다. 결과적으로, 저희는 O(1) 스케줄러를 선택하게 되었습니다.

O(1) 스케줄러의 특징은 작업의 수와 무관하게 O(1)의 시간 복잡도로 스케줄링이 이루어진다는 점입니다. 즉, 다음에 실행할 작업을 결정하는 데 걸리는 시간이 일정하며, 많은 작업을 효율적으로 처리할 수 있는 구조입니다. 과거 리눅스 운영체제에서도 사용되었으나, 현재는 공정성을 중시하는 CFS(Completely Fair Scheduler)로 대체되었습니다. 이번 구현을 통해 O(1) 스케줄러와 CFS의 성능을 비교하고, 리눅스 자원 관리 시스템의 발전 과정을 직접 경험해 보고자 했습니다.

저희는 스케줄러 구현을 위해 Google에서 개발한 ghOSt 프레임워크를 활용했습니다. ghOSt는 사용자 영역에서 스케줄링 알고리즘을 정의하고 적용할 수 있게 해주어, 커널 레벨에서 복잡한 프로그래밍 없이 고급 스케줄링 기법을 실험할 수 있는 유연한 환경을 제공합니다. 이러한 이유로 O(1) 스케줄러의 구현과 실험에 접근이 훨씬 용이했고, 이를 통해 유의미한 성능 테스트를 수행할 수 있었습니다.

# 2 프로젝트 목표

저희의 목표는 Ghost를 사용하여 안정적으로 동작하는 O(1) 스케줄러를 구현하고, 테스트를 통해 O(1) 스케줄러와 CFS를 비교·분석하는 것입니다.

이를 위해 세운 세부적인 목표는 다음과 같습니다.

1. ghOSt 프레임워크를 위한 환경 구축
2. ghOSt API 분석
3. ghOSt API를 통해 간단한 스케줄러 구현하기
4. ghOSt API를 통한 O(1) 스케줄러 구현하기
5. ghOSt에서 구현한 O(1) 스케줄러와 CFS 성능 비교 테스트

# 3 프로젝트 진행

저희 팀은 학기 초에는 리눅스와 ghOSt를 사용하기 위한 환경을 구축하는 데 대부분의 시간을 할애했습니다. ghOSt를 공부하기 이전에 ghOSt를 리눅스 커널에 설치했고, ghOSt의 Fifo 예제를 참고해서 round-robin을 구현하는 시도를 했습니다.

O(1) 스케줄러를 구현할 때는 각자의 방식으로 O(1)을 구현하면서 새로 알게된 사항들을 공유했습니다. 하지만 각자 구현이 다르다 보니 프로젝트 진행에 장애가 많았습니다. 그래서 교수님의 피드백을 기반으로 각자 지금까지 구현한 것을 취합해서 설계부터 다시 진행하였습니다.

## 3.1 ghOSt API 실습

저희 팀은 ghOSt에 대해 전반적으로 이해하기 위해 논문을 재검토하였고, 본격적으로 ghOSt를 사용해서 스케줄러를 구현하기 전 ghOSt API를 학습하기 위 기초가 되는 코드인 비선점형 스케줄러인 FIFO 스케줄러를 분석했습니다.

이후, 각자 라운드 로빈 스케줄러를 구현하면서 선점형 스케줄러의 기본 개념을 습득하고, 구현해본 경험들을 가지고 토론하며 ghOSt API에 대한 이해를 점진적으로 높여갔습니다.

## 3.2 설계 및 구현

각자 구현하면서 깨달은 것과 교수님의 피드백을 토대로, 팀원 모두가 납득 가능한 방향으로 O(1) 스케줄러를 어떻게 구현할 지 결정하였습니다.

설계에 앞서, 먼저 요구사항을 정의했습니다. 그리고 요구사항을 충족하기 위해 각 클래스와 클래스의 멤버를 어떻게 구현할지 결정하였습니다.

3.2.2절은 각 클래스의 역할에 대한 설명이고, 3.2.3절은 구체적으로 요구사항을 어떻게 해결하여 구현했는지에 대한 내용입니다. 또한 3.2.4절에서는 구현하면서 생긴 의문과 깨달은 답을 작성했습니다.

### 3.2.1 요구사항

- Run Queue는 Active Queue(활성큐)와 Expired Queue(만료큐)가 있어야 하고, Run Queue는 코어마다 있어야 한다.
- 각 task는 time slice를 받고, time slice 이상 CPU를 사용한 후에는 Expired Queue로 들어가야 한다.
- 다음 실행할 task를 Run Queue에서 선택할 때, Active Queue에 task가 없고, Expired Queue에 task가 있다면 Active Queue와 Expired Queue는 Swap되어야 한다.
- task가 cpu에서 실행되다가 time slice를 다 소모하기 전에 선점 당한 경우, 남은 time slice를 유지해야 한다. 그리고 다시 cpu를 할당 받을 때 그 남은 time slice부터 남은 시간을 계산해야 한다.
- 각 작업을 위해 Run Queue에 접근할 때 Race Condition을 방지하기 위해 Run Queue의 lock을 잡아야 한다.

### 3.2.2 클래스 다이어그램

<img src="https://github.com/user-attachments/assets/3fa210cb-4366-4589-8f33-633b954f96e7" alt="class-diagram" width="70%">


- **O1TaskState**

  <img src="https://github.com/user-attachments/assets/c68ae374-67ac-4323-b077-fcab12c2b040" alt="class-diagram" width="70%">

    
    task의 상태를 표현하고, 실행 과정에서 올바른 흐름으로 실행되는지 체크하기 위해 각 task는 자신의 상태를 가지고 있습니다. O1TaskState는 task의 상태를 정의하고 있는 enum class입니다.
    
    kBlocked는 task가 block되어 실행이 불가능한 상태가 되어 run queue에서 빠진 상태입니다.
    
    kQueued는 실행 가능 상태로, Run queue에 있는 상태입니다. Active queue와 Expired queue에 관계 없이 어느 queue에 있든 kQueued 상태를 가집니다.
    
    KOnCpu는 현재 CPU를 할당받고 실행중인 것을 의미합니다.
    
    kRunnable은 kBlocked또는 kOnCpu에서 kQueue로 전환되는 중간, 그리고 kQueued에서 kOnCpu로 전환되는 중간 상태를 의미합니다. 스케줄링이 올바르게 진행되고 있는지 확인하는 용도로 사용될 수 있습니다.
    
- **O1Task**
    
    PCB 역할을 하는 구조체 입니다. Task 하나의 정보를 담고 있습니다. 각 task는 remaining_time을 가지고 있습니다. 또한 remaining_time을 업데이트하기 위해 runtime_at_last_pick 필드도 가지고 있습니다. 이 필드를 통해서 이전 업데이트부터 현재까지 지난 시간을 계산하고, 이를 remaining_time에 뺍니다.
    
- **O1Rq**
    
    Run Queue를 나타내는 클래스입니다. O1Rq 인스턴스를 각 코어(CpuState 구조체)마다 가지고 있습니다.
    
- **CpuState**
    
    각 Cpu를 나타내는 구조체입니다. Cpu를 현재 할당받아 실행 중인 task와 Run Queue, 현재 실행 중인 task의 선점 가능 여부에 대한 정보를 담고 있습니다.
    
- **O1Scheduler**
    
    스케줄링 정책 로직이 있는 클래스입니다. 실제 스케줄링을 수행하는 클래스입니다. 커널의 메시지에 따라 수행할 함수들을 담고 있습니다.
    

### 3.2.3 구현 (요구사항 해결)

- **Run Queue는 Active Queue(활성큐)와 Expired Queue(만료큐)가 있어야 하고, Run Queue는 코어마다 있어야 한다.**
    
    ```cpp
    // O1Rq 클래스 코드의 일부분입니다.
    // Active Queue와 Expired Queue를 구한하기 위해 std::deque를 두 개 두었습니다.
    private:
      mutable absl::Mutex mu_;
      std::deque<OoneTask*> aq_ ABSL_GUARDED_BY(mu_);
      std::deque<OoneTask*> eq_ ABSL_GUARDED_BY(mu_);
    ```
    
    → O1Rq 인스턴스 2개로 두 개의 큐를 구현하는 방식과, O1Rq 인스턴스 하나에 큐를 두 개를 담게하는 방식이 있었습니다. 큐 관리를 하나의 O1Rq 인스턴스에서 하는 것이 구현에 용이하여 O1Rq 인스턴스에서 두 개의 큐를 모두 담고 있는 방식으로 구현하였습니다.
    
    또한 CpuState 구조체에서 이 Run Queue를 하나씩 담고 있어서 각 코어는 Run Queue를 가지고 있습니다.
    
- **각 task는 time slice를 받고, time slice 이상 CPU를 사용한 후에는 Expired Queue로 들어가야 한다.**
    
    ```cpp
    // 다음은 O1Task 구조체 코드의 일부분입니다.
    struct O1Task : public Task<> {
      explicit O1Task(Gtid O1_task_gtid, ghost_sw_info sw_info)
          : Task<>(O1_task_gtid, sw_info) {}
      ...
      void SetRemainingTime() {
        remaining_time = absl::Nanoseconds(10000000); // 10ms
      }
    
      bool UpdateRemainingTime(bool isOff);
    
      void SetRuntimeAtLastPick() {
        runtime_at_last_pick = absl::Now();
      }
    
      // 마지막 remaining_time 업데이트한 시간
      absl::Time runtime_at_last_pick;
      // 남은 cpu 실행 시간
      absl::Duration remaining_time;
    };
    ```
    
    → time slice를 할당하기 위해 O1Task 구조체에 remaining_time 자료형을 사용하였습니다. 또한 정확한 시간 계산을 하기 위해 absl::Duration을 사용하였습니다.
    
    또한 UpdateRaminingTime 함수를 통해 CpuTick 함수와 TaskOffCpu 함수에서 remaining_time을 업데이트하게 했습니다.
    
    ```cpp
    // 다음은 CpuTick 함수에서 호출하는 CheckPreemptTick 함수의 일부입니다.
    void O1Scheduler::CheckPreemptTick(const Cpu& cpu)
      ABSL_NO_THREAD_SAFETY_ANALYSIS {
      ...
        if (cs->current->UpdateRemainingTime(/*isTaskOffCpu=*/false)) {
          cs->preempt_curr = true;
        }
      }
    }
    ```
    
    → 매 tick마다 remaining_time을 갱신하고, remaining_time이 0 이하이면 CpuState의 preempt_curr 필드를 true로 체크했습니다. 이게 true이면 Schedule 함수에서 다음 실행할 task를 결정할 때 현재 Cpu를 할당 받은 task를 무조건 Run Queue로 내립니다.
    
    ```cpp
    // 다음은 스케줄링 로직이 있는 O1Schedule 함수의 일부입니다.
    void O1Scheduler::O1Schedule(const Cpu& cpu, BarrierToken agent_barrier,
                                     bool prio_boost) {
      CpuState* cs = cpu_state(cpu);
      O1Task* next = nullptr;
      if (cs->preempt_curr) {
        O1Task* prev = cs->current;
        if (prev) {
         TaskOffCpu(cs->current, /*blocked=*/false, /*from_switchto=*/false);
         cs->run_queue.Enqueue(prev);
        }
        cs->preempt_curr = false;
    ...
    }
    ```
    
    → 커널의 메시지를 처리한 후에 O1Schedule 함수를 호출하게 되는데, 이때 현재 코어의 CpuState 구조체의 preempt_curr이 true로 체크되어 있으면 현재 CPU를 할당받은 task를 Expired Queue로 넣고 새로운 Task에 CPU를 할당합니다.
    
- **다음 실행할 task를 Run Queue에서 선택할 때, Active Queue에 task가 없고, Expired Queue에 task가 있다면 Active Queue와 Expired Queue는 Swap되어야 한다.**
    
    ```cpp
    void O1Rq::Swap() {
      std::swap(aq_, eq_);
    }
    
    // Dequeue 함수의 일부입니다.
    O1Task* O1Rq::Dequeue() {
      absl::MutexLock lock(&mu_);
      if (aq_.empty()) {
        if (eq_.empty()) {
          return nullptr;
        } else {
          Swap(
    ...
    }
    ```
    
    → O1Schedule 함수에서 다음 task를 가져와야할 때 Dequeue 함수를 호출해서 다음 실행할 task를 반환받습니다. 이때, aq_(Active Queue)가 비어있고 eq_(Expired Queue)가 비어있지 않을 때 두 큐를 Swap합니다.
    
- **task가 cpu에서 실행되다가 time slice를 다 소모하기 전에 선점 당한 경우, 남은 time slice를 유지해야 한다. 그리고 다시 cpu를 할당 받을 때 그 남은 time slice부터 남은 시간을 계산해야 한다.**
    
    →  task가 block되어서 run queue에서 제거된 후에 다시 실행 가능 상태가 되어 run queue로 온 경우에 이전 remaining_time이 유지되는 것은 `GHOST_DPRINT`를 통해 경험적으로 확인하였습니다. 이를 경험적으로 뿐만 아니라 실제 코드를 체크하고, C++ 문법을 확인하여 remaining_time이 유지될 수 있는 이유를 파악하였습니다.
    
    ```cpp
    // TaskAllocator 코드의 일부입니다.
    template <typename TaskType>
    class SingleThreadMallocTaskAllocator : public TaskAllocator<TaskType>{
    	TaskType* GetTask(Gtid gtid) override;
    	void FreeTask(TaskType* task);
    	...
    	absl::flat_hash_map<int64_t, TaskType*> task_map_;
    }
    ```
    
    → `template <typename TaskType>`을 통해 임의의 타입을 정의할 수 있습니다. TaskAllocator의 task_map_ 필드는 hash_map을 통해 이 타입의 포인터를 저장하고 있습니다.
    
    이 타입을 실제 사용하는 곳의 코드는 O1Scheduler 클래스의 코드로, O1Task* 타입으로 사용합니다.
    
    C++에서는 컴파일 시간에 typename으로 적은 TaskType의 자료형을 결정하기 때문에 TaskAllocator에서의 task_map_ 필드는 O1Task 인스턴스의 포인터를 저장하게 됩니다. 따라서 O1Task의 remaining_time은 유지될 수 있습니다.
    
- **Run Queue에 접근할 때 Race Condition을 방지하기 위해 Run Queue의 lock을 잡아야 한다.**
    
    ```cpp
    // O1Rq 클래스 코드의 일부분입니다.
    private:
      mutable absl::Mutex mu_;
      std::deque<OoneTask*> aq_ ABSL_GUARDED_BY(mu_);
      std::deque<OoneTask*> eq_ ABSL_GUARDED_BY(mu_);
    ```
    
    → absl::Mutex 변수를 선언하고 aq_와 eq_를 관리하게 했습니다. 그리고 run queue에 대해 lock을 잡아야 하는 곳에서 `absl::MutexLock lock(&mu_);`을 사용하여 rung queue의 lock을 잡았습니다.
    

### 3.2.4 핵심 논의

- **블락된 프로세스의 타임 슬라이스가 어떻게 유지되는가?**
    
    클래스에 `template <typename TaskType>`을 붙이면 자바의 제네릭처럼 클래스에서 사용할 임의의 타입을 정의할 수 있습니다. 자바와의 차이점은 C++는 컴파일 시간에 이를 결정합니다.
    
    전체 Task를 관리하는 TaskAllocator 클래스가 있는데, 여기에서 정의한 타입(TaskType)의 포인터를 hash_map으로 관리합니다.
    
    커널에서 메시지가 오면, TaskAllocator의 GetTask같은 함수를 통해 저장된 객체의 포인터를 반환 받고, 적절한 루틴을 호출합니다. 위의 코드에서는 `TaskRunnable`을 호출하고 있습니다. 호출된 TaskRunnable 함수에서는 O1Task로 해당 포인터를 접근하기 때문에 상속된 클래스인 O1Task로 활용이 가능합니다.
    
    C++에서 **상속된 클래스의 필드는 기반 클래스(Base Class)에 정의된 메모리 공간에서 유지**되므로, 상속받은 클래스의 필드도 같은 메모리 구조에서 직접 접근할 수 있습니다.
    
- **CPU를 할당받은 스레드가 왜 계속 선점으로 CPU 할당이 해제되는가?**
    
    Task들이 Cpu를 할당받고 곧 선점 되어 CPU가 할당 해제 되는 경우가 많았습니다. 이는 커널 메시지를 처리하는 Agent 스레드와 작업 스레드를 한 코어에서 실행하기 때문에 Agent의 일을 처리하기 위해 CPU가 선점되는 것이었습니다. FIFO와 CFS 스케줄러에서도 동일한 현상이 일어나는 것을 확인하였고 이는 정상동작에 해당합니다.
    
- **작업의 타임슬라이스가 왜 잘 줄어들지 않는가?**
    
    저희가 처음 구현하였을 때는 타임 슬라이스가 거의 줄어들지 않는 것을 로그를 통해 발견했었습니다. ghOSt 동작 방식은 메시지에 대한 이벤트 기반으로 작동하는 것인데 저희는 Tick에 대한 이벤트에 대해서만 타임 슬라이스를 업데이트를 했었습니다. 
    
    타임 슬라이스를 업데이트는 Blocked나 Preempted 등과 같은 경우에도 해야 했습니다.  그래서 Blocked이나 Preempted 등의 경우에도 적절히 타임 슬라이스를 업데이트되게 하였습니다.
    
    그 결과 타임 슬라이스가 적절히 감소되며 주어진 시간이 만료된 작업들은 만료 큐에 잘 들어가게 되었고, 큐에 대한 swap도 잘 일어나게 되었습니다.
    

# 4 발생한 문제 및 해결

Ghost를 활용한 O(1) 스케줄러 구현 프로젝트를 진행하면서 다양한 문제를 마주했습니다. 이를 해결하기 위해 구글링, OS 관련 자료 학습, 교수님 상담, Ghost 팀과의 소통 등 여러 방식을 통해 문제를 해결했습니다. 그 과정에서 겪은 문제와 해결 방안을 다음과 같이 정리했습니다.

## 4.1 환경 설정

### 4.1.1 Ghost 커널 설정

Ghost에서 제공하는 커널을 사용하여 리눅스를 부팅하고 Ghost userspace 코드를 실행하는 과정에서 많은 문제를 겪었습니다. 처음에는 Ghost 커널 리포지터리에서 코드를 클론하여 부팅을 시도했으나, 여러 가지 버그에 직면했습니다. 이 과정에서, Ghost 측에서 제공하는 공식 installer를 사용하는 것이 안정적이라는 것을 깨달았고, installer를 사용해 커널을 설치한 후 문제를 해결할 수 있었습니다.

### 4.1.2 가상 머신 설정

초기에 가상 머신을 설정할 때 하드디스크를 20GB로 설정했지만, 용량 부족으로 인해 여러 오류가 발생했습니다. 나중에 Ghost 팀에서 하드디스크 용량으로 100GB를 권장하는 것을 확인하고, 하드디스크를 110GB로 재설정하여 문제를 해결했습니다.

### 4.1.3 컴파일 도구 Bazel 설정

Ghost에서는 Bazel이라는 컴파일 도구를 사용합니다. 하지만 최신 버전의 Bazel이 Python 2를 지원하지 않아 컴파일 중 버그가 발생했습니다. 이에 Bazel의 최신 버전 대신, Python 2를 지원하는 5.4.0 버전으로 다운그레이드하여 컴파일을 완료할 수 있었습니다.

### 4.1.4 부팅 옵션 설정

Ghost 커널을 설치한 후, 이 커널 이미지로 부팅하는 과정에서 어려움을 겪었습니다. GRUB 부트로더 옵션을 수정하여 다운로드받은 커널 이미지로 부팅해야 한다는 사실을 몰랐기 때문입니다. 이후 GRUB 설정을 수정하여 커널 이미지로 성공적으로 부팅할 수 있었습니다.

### 4.1.5 파일 시스템 마운트 오류

Ghost 파일 시스템인 ghostfs가 설치되지 않아 Ghost 관련 헤더 파일을 참조할 수 없었습니다. 이를 해결하기 위해 고스트 파일 시스템인 ghostfs를 명시적으로 마운트하고, 가상 파일 시스템을 확인하여 문제를 해결했습니다.

### 4.1.6 가상환경 내에서의 작업

팀원들의 컴퓨터 사양이 좋지 않아 VirtualBox 가상환경에서 코드를 작업하는 데 상당한 어려움을 겪었습니다. 메모리 용량을 최적화하고 부하를 줄이기 위한 여러 방법을 시도했지만, 여전히 프로그램 실행 시 큰 부담이 있었습니다. 이 문제를 해결하기 위해 로컬 환경에서 코드를 작성한 후, GitHub을 통해 가상환경과 연동하는 방식으로 프로젝트를 진행했습니다.

## 4.2 구현

### 4.2.1 작업 남은 시간 관리 오류

작업이 CPU에서 처리되는 동안 남은 시간이 줄어들지 않는 문제를 발견했습니다. 문제의 원인은 작업이 taskoff 상태로 전환될 때 시간을 업데이트하지 않았기 때문이었습니다. 이를 해결하기 위해 작업이 CPU를 반환할 때 남은 시간을 업데이트하도록 수정했고, 이후 시간이 정상적으로 줄어드는 것을 확인했습니다.

### 4.2.2 CpuTick 활용

초기에는 Agent가 스케줄링을 호출할 때마다 남은 시간을 검사하는 방식으로 구현했으나, 주기적으로 시간을 검사하지 못해 O(1) 스케줄러의 의도에 맞지 않았습니다. 이를 해결하기 위해 타이머 인터럽트가 발생할 때마다 스레드의 남은 시간을 검사하고, 선점 여부를 결정하는 방식으로 수정했습니다.

### 4.2.3 선점 발생

스레드가 실행되는 도중 계속해서 선점되는 문제가 발생했습니다. 원인을 파악하기 위해 Ghost 팀에 문의한 결과, Ghost 코어에 리눅스 태스크가 들어오거나 타이머 틱이 발생할 때 스레드가 선점된다는 것을 알게 되었습니다. 이를 바탕으로 ghost 선점에 대한 원리를 알게되었습니다.

### 4.2.4 우선순위 플래그 문제 (prio_boost)

우선순위를 고려하는 우선순위 플래그를 사용하여 스케줄링 함수를 구현했는데, 이로 인해 CPU가 유휴 상태에 빠져 작업을 하지 않는 문제가 발생했습니다. 결국 우선순위 관련 기능이 필요하지 않다고 판단하여, 우선순위 기능을 제거한 후 문제를 해결했습니다.

### 4.2.5 태스크 블락 및 깨어나기 처리

Ghost에서 제공하는 기본 테스트 중 하나인 심플 테스트 에서, 스레드가 sleep 함수를 호출하며 블락 상태로 전환되는 상황을 처음에는 CPU 할당 해제 오류로 잘못 이해했습니다. 하지만 이후 sleep 함수의 기본 동작을 파악하고, 블락된 스레드가 어떻게 처리되는지 학습했습니다. 또한, 블락된 태스크가 깨어나는 과정을 정확히 이해하지 못했으나, Ghost에서 TaskMap이라는 자료구조가 블락된 태스크를 관리하고, 해당 태스크가 깨어날 때 메시지를 수신해 스케줄링 큐로 다시 넣는 것을 확인했습니다.

### 4.2.6 Swap 및 만료큐

O(1) 스케줄러는 할당 시간이 만료된 작업을 만료 큐에 삽입해야 합니다. 처음에는 스케줄링 함수에서 선점 플래그를 확인한 후 만료 큐에 작업을 넣는 방식으로 구현했으나, 시간이 만료되지 않은 경우에도 선점이 발생하는 일이 많아 작업들이 적절하게 만료 큐에 삽입되지 않았습니다. 이를 해결하기 위해 스케줄 함수뿐만 아니라 CPU에 할당되는 모든 작업에 대해 남은 실행 시간을 확인하고, 시간이 만료된 작업만 만료 큐로 삽입하는 로직을 추가하여 문제를 해결했습니다.

## 4.3 주제선정

처음에 ULE라는 스케줄러를 Ghost로 구현하는게 계획이었으나 이미 깃허브에 구현된 리포지터리가 있는 것을 확인하고 주제를 변경했습니다. 자료조사와 교수님과 상의하여 Ghost를 사용하여 O(1) 스케줄러를 구현하는것으로 계획을 수정하였습니다.

# 5 테스트

저희가 구현한 O(1) 스케줄러의 테스트는 기능 테스트와 성능 테스트로 나누어 진행했습니다. 이를 통해 스케줄러의 정확성과 성능을 검증하고, CFS(Completely Fair Scheduler)와의 비교 분석을 진행했습니다.

## 5.1 테스트 환경

- **호스트 컴퓨터**
    
    운영체제: 윈도우 11
    
    프로세서: 12th Gen Intel(R) Core(TM) i7-1260P   2.10 GHz
    
    메모리: 16.0GB
    
    저장소: 1024GB
    
    시스템 :	64비트 운영 체제, x64 기반 프로세서
    
- **가상머신 (Virtual Box)**
    
    운영체제: Ubuntu 5.11 (64bit)
    
    프로세서: 8
    
    메모리: 4096MB
    
    저장소: 100GB
    

## 5.2 기능 테스트

O(1) 스케줄러의 주요 기능들이 예상대로 동작하는지 확인하기 위해 유닛 테스트를 진행했습니다. 테스트 환경은 10ms 동안 CPU에서 Spin을 돌고, 10초 동안 sleep한 후 다시 10ms 동안 Spin을 도는 테스트 파일을 기반으로 구성되었습니다. 이를 통해 스케줄러의 작업 생성, 상태 변화, 선점, 종료 등의 동작을 검증했습니다.

### 5.2.1 작업 생성 및 초기화

Task 생성 시에 Task의 수 만큼 TaskNew가 호출되었고, AssignCpu와 Migrate로 CPU 할당까지 정상적으로 진행되었습니다. 또한 kRunnable 상태인 Task만 Migrate되었습니다.

### 5.2.2 작업 CPU 할당

- **모든 Task에서 Migrate가 호출되는가?**
![image](https://github.com/user-attachments/assets/ac8e0127-57d6-43c1-a448-c06dc6eac400)

모든 Task가 TaskNew로 생성되는 즉시 Migrate가 호출되었습니다.
    
- **Migrate의 매개 변수로 넘어온 모든 task가 `kRunnable`상태를 가지는가?**

  ![image](https://github.com/user-attachments/assets/f9cca483-e4ca-4309-a421-968c6ed6e290)

  Google의 glob 라이브러리의 CHECK_EQ를 통해 런타임에 task의 run_state가 `kRunnable`인지 확인합니다. `kRunnable`이 아니라면 런타임에 오류를 발생시키고, 프로그램은 즉시 종료됩니다.
    
    - **테스트 결과**
    
      테스트가 정상적으로 완료되고, `CHECK_EQ(task->run_state, O1TAskState::kRunnable);`에서 오류가 발생하지 않았습니다. 이는 Migrate로 온 모든 task가 `kRunnable`상태를 가지는 것을 의미합니다.
        
- **Migrate의 매개변수로 넘어온 모든 task가 cpu를 배정 받기 전인가?**
    
  Migrate를 하기위해서는 Migrate 이전에 반드시 AssignCpu() 함수를 호출해서 cpu를 받지만, 아직 배정되지 않은 상태여야 합니다. 실제 배정은 Migrate 코드에서 이루어져야 합니다.  
  ![image](https://github.com/user-attachments/assets/7b173acd-0a91-4ad6-8580-96813a4ad3ee)

  - **테스트 결과**
    
    테스트가 정상적으로 완료되고, `CHECK_EQ(task->cpu, -1);`에서 오류가 발생하지 않았습니다. 이는 Migrate로 온 모든 task가 cpu를 배정 받기 전임을 의미합니다.
        

### 5.2.3 **작업 선점**
![image](https://github.com/user-attachments/assets/109cc2f2-98d5-42e0-9f11-4151765c4cba)

작업이 선점될 때 CPU 할당을 해제하고, 활성 큐에서 대기하다가 다시 CPU를 할당받는 과정을 테스트했습니다. 스케줄러는 일정한 시간 동안 CPU를 할당하고,  선점이 발생할때 선점된 작업은 활성 큐에서 다시 대기하는 로직이 올바르게 동작하는 것을 확인했습니다

### 5.2.4 작업 상태 변화 (kRunnable**, kQueued, kOnCpu, kQueued)**

![image](https://github.com/user-attachments/assets/171b6654-84f2-47a3-b204-fd4ad3b96900)

Task들이 **`kOnCpu→kQueue→KRunnable`**와 **`kOnCpu → kBlocked → kRunnable → kOnCpu`**로 상태가 잘 전이되는 모습을 확인했습니다.
또한 Task를 CPU에 올렸을 때만 타임슬라이스가 감소되는 모습을 확인했습니다. 그리고 상태 변화에 대한 검증은 아래와 같이 CHECK_EQ 함수를 통해 수행하였습니다.

![image](https://github.com/user-attachments/assets/d2f2ac49-3bcd-42ab-ade8-3bfad33e8fd7)

### 5.2.5 타이머 인터럽트

다음은 cpuTick이 발생할 때 타임슬라이스의 변화 모습입니다.
![image](https://github.com/user-attachments/assets/09d9aede-9027-4eb9-95f3-68c0d1fd75a9)

Task가 cpuTick이 발생할 때마다 타임 슬라이스가 잘 감소하는 모습을 보이며
![image](https://github.com/user-attachments/assets/711859a6-86c8-43b7-8a56-ca2dfd6a9db7)

또한 타임슬라이스가 만료되고 다시 타임슬라이스가 할당된 후에도 cpuTick이 발생할 때마다 타임슬라이스가 잘 감소되었습니다.
![image](https://github.com/user-attachments/assets/06d8819a-f23d-4add-bfd9-0363bc2b4486)

타임 슬라이스가 만료되면 다음 Task를 가져왔습니다.

### 5.2.6 Swap

- **Swap될 때 활성 큐의 크기는 0이고, 만료 큐의 크기는 1 이상인가?**

  ![image](https://github.com/user-attachments/assets/e651698b-0971-4ce3-b4d2-b7b0dc27fc83)
  Swap이 발생하는 모든 경우에서 활성 큐의 크기는 0이었고, 만료 큐의 크기는 1 이상이었습니다.
    

### 5.2.7 작업 종료
생성된 스레드가 정상적으로 종료되는지 테스트했습니다. 실제 테스트에서 600개의 스레드가 `TaskDead` 상태로 변환되며, 모든 스레드가 정상적으로 종료된 것을 확인했습니다
![image](https://github.com/user-attachments/assets/135f5b8a-5d35-42f2-a28b-69f044991de4)


## 5.3 성능테스트
O(1) 스케줄러에 대한 성능테스트를 논문을 참고하여 진행했습니다. 논문에 존재하던 공정성 테스트, rocksDB 테스트를 진행했습니다. 

### 5.3.1 fairness test (공정성 테스트)
- **테스트 설명**
  
  공정성 테스트는 여러 스레드가 동일한 작업을 수행할 때, 각 스레드에게 CPU 시간이 얼마나 공평하게 분배되는지 평가하기 위한 실험입니다. 각 스레드는 동일한 수치 적분과 삼각함수 연산을 통해 복잡한 계산 작업을 수행하며, 스레드 실행 시간이 측정됩니다. 이를 통해 스케줄러가 CPU 자원을 공정하게 분배했는지 여부를 확인할 수 있습니다.
    
  테스트는 모든 스레드가 동시에 시작하도록 설계되었으며, 스레드들의 실행 시간은 시작과 종료 시간을 기록하여 계산됩니다. 각 스레드의 실행 시간이 모두 기록된 후, 평균 실행 시간과 표준 편차를 계산하여 스레드 간 실행 시간의 균등성을 분석합니다. 표준 편차는 스레드 간의 실행 시간 차이를 나타내며, 공정한 스케줄링을 할수록 표준 편차가 작아집니다.
    
- **논문에서의 결과**
  ![image](https://github.com/user-attachments/assets/2bd87b29-5a48-4692-892f-bc04f23881dc)
  논문에서는 O(1)으로 했을 경우가 CFS보다 실행 시간이 더 넓게 분산되어 나왔습니다. 즉, CFS가 O(1)보다 더 공정하다는 결과를 보였습니다.
  (C.S. Wong, I.K.T. Tan, R.D. Kumari, J.W. Lam. (2008). Fairness and interactive performance of O(1) and CFS Linux kernel schedulers. ****https://ieeexplore.ieee.org/document/4631872)
    
- **테스트 결과**
  
  아래는 ghOSt로 구현된 CFS 스케줄러로 fairness test를 진행한 결과입니다.
  ![image](https://github.com/user-attachments/assets/0e50a25c-9650-43f8-b581-16790d3ec4ca)
  ![image](https://github.com/user-attachments/assets/d0850495-3e21-4400-8e44-cfb5871649b9) 
  ![image](https://github.com/user-attachments/assets/08eb9ee2-8060-450d-8980-b63bd8fb63b0)

  아래는 ghOSt로 구현한 O(1) 스케줄러로 fairness test를 진행한 결과 입니다.
  ![image](https://github.com/user-attachments/assets/d606e3a4-7cfb-4235-8578-6916d72ff564)  
  ![image](https://github.com/user-attachments/assets/781bcf56-c6e8-42bb-b282-6fdf07c59d92)
  ![image](https://github.com/user-attachments/assets/e68e88ee-84de-411a-84be-bc3bea33c405)

    
  저희가 진행한 테스트에서는 O(1)의 실행시간 자체가 더 길었고, 실행 시간의 표준 편차도 O(1)이 더 컸습니다. 실행 시간이 더 길기 때문에 이것이 표준편차에 영향을 줄 수 있습니다. 그래서 평균 실행 시간 대비 표준 편차를 비교해 보았습니다.

  CFS의 평균 실행 시간 대비 표준 편차: 0.198 / 0.162 / 0.258
  O(1) 평균 실행 시간 대비 표준 편차: 0.394 / 0.454 / 0.469
  
  평균 실행 시간 대비 표준 편차를 비교해 보았을 때 O(1)의 표준편차가 CFS보다 더 컸고, 이는 CFS가 O(1)보다 공정하다는 것을 의미합니다.
    

### 5.3.2 rocksdb test

Ghost 논문에서 제시한 RocksDB 툴을 사용하여 O(1) 스케줄러와 CFS 스케줄러의 처리량(throughput)을 비교했습니다. 테스트 조건으로는 처리량 1000, 4개의 코어, 실행 시간 20초를 설정했습니다.

CFS
![image](https://github.com/user-attachments/assets/1b397a27-3632-4012-b481-f6827a08fcfc)


O(1)
![image](https://github.com/user-attachments/assets/3c6d2214-5bd3-42b9-9b9d-4108b062ccc1)


결과 분석 : O(1) 스케줄러는 99% 의 작업까지 완료하는 데 CFS보다 약간 더 빠르지만, 전체 작업을 완료하는 데는 CFS가 더 빠른 결과를 보였습니다. 이 결과를 바탕으로 전체적인 성능에서는 CFS가 더 우수하다고 평가할 수 있습니다.

(Jack Tiger Humphries. (2021). ghOSt: Fast & Flexible User-Space Delegation of Linux Scheduling. https://dl.acm.org/doi/10.1145/3477132.3483542)
