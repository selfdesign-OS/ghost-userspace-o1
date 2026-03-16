# ghOSt O(1) 스케줄러

## 개요

ghOSt 환경에서 O(1) 스케줄러를 실행하기 위한 프로젝트다.

이 스케줄러는 기존 O(1) 스케줄러와 다르게 우선순위를 나누지 않는다. per-CPU마다 활성 큐(active queue)와 만료 큐(expired queue) 하나씩을 두고, 라운드로빈으로 동작한다.

> **주의**: 기존 O(1) 스케줄러의 완전한 동작을 기대해서는 안 된다. 현재 기존 O(1)의 동작을 구현하기 위한 추가 작업이 진행 중이다.

## 기술

![Badge](https://img.shields.io/badge/ghOSt%20framework-gray.svg?style=flat-square) <img src="https://img.shields.io/badge/C++-00599C?style=flat-square&logo=C%2B%2B&logoColor=white"/> <img src="https://img.shields.io/badge/Ubuntu-E95420?style=flat-square&logo=Ubuntu&logoColor=white"/>

## 요구 사항

- 베어메탈 리눅스 (버츄얼박스 같은 가상머신은 제한된다)
- OS: Ubuntu 20.04
- 디스크 여유 공간: 70GB 이상

## 설치

```bash
sudo apt update && sudo apt install git make && git clone https://github.com/selfdesign-OS/ghost-userspace-o1 && cd ghost-userspace-o1/ghost-installer && sudo make
```

### 설치 항목

| 항목 | 내용 |
|------|------|
| 패키지 | 커널 빌드 및 ghOSt userspace 빌드에 필요한 apt 패키지 |
| 커널 | `ghost-v5.11` 브랜치 기반 ghOSt 커널 빌드 및 설치 |
| GRUB | 재부팅 시 커널 선택 메뉴 활성화 |
| Bazel | ghOSt userspace 빌드 시스템 |

## 설치 후

1. 재부팅 후 GRUB 메뉴에서 `linux-5.11.0+` 커널 선택
2. 부팅 확인 후 `bazel build` 로 userspace 빌드 실행

## 실행 환경 구축 가이드

https://earthy-door-ce5.notion.site/Ghost-by-996861ed21dc41c592325fd0ec6ebb2f

## 시연 영상

<a href="https://youtu.be/3K4QTmFbsxM" target="_blank">
  <img src="https://img.shields.io/badge/YouTube-Test1-FF0000?logo=youtube&logoColor=white" alt="Test1" style="width: 150px;"/>
</a>
<a href="https://youtu.be/_bYUfDNG5EQ" target="_blank">
  <img src="https://img.shields.io/badge/YouTube-Test2-FF0000?logo=youtube&logoColor=white" alt="Test2" style="width: 150px;"/>
</a>

## 팀 구성

|Jae-hyeong|Sung-woon|byeong-hyeon|
|:-:|:-:|:-:|
|<img src="https://github.com/user-attachments/assets/ef510219-2286-4048-9810-505ca2b5d5e6" alt="jae-hyeong" width="100" height="100">|<img src="https://github.com/user-attachments/assets/b9478ab7-b1b6-4313-bbc8-38e195364dde" alt="dingwoonee" width="100" height="100">|<img src="https://github.com/user-attachments/assets/d75b48a4-c3bb-4280-be3c-e0fbf4ea8464" alt="dingwoonee" width="100" height="100">|
|[JaehyeongIm](https://github.com/JaehyeongIm)|[DingWoonee](https://github.com/DingWoonee)|[bangbang444](https://github.com/bangbang444)|

## Reference

- 유저스페이스: https://github.com/google/ghost-userspace
- 커널: https://github.com/google/ghost-kernel
