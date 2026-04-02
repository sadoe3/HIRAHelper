# HIRA Helper Agent v3.1

**HIRA Helper Agent**는 병원의 레거시 시스템(Delphi OCS/EMR)과 최신 네트워크 스토리지(NAS/PACS), 그리고 외부 API(심평원 등) 간의 대용량 파일 송수신을 중계하는 초경량/고성능 C++ 마이크로 서비스(Middleware)입니다.

기존 레거시 클라이언트가 처리하기 버거운 대용량 I/O, 폴더 압축/해제, 세션 폴더 생명주기 관리 등의 무거운 작업을 서버(Agent)로 완벽하게 위임하는 **'Fire-and-Forget'** 아키텍처를 구현했습니다.

## Core Architecture & Features

### 1. One-Shot Zip Pipeline (v3.1 핵심)
* 클라이언트가 NAS의 원본 폴더 경로만 던져주면 서버가 백그라운드에서 **[세션 폴더 생성 ➔ 폴더 전체 재귀 복사 ➔ 고속 압축 ➔ 임시 폴더 파기]** 과정을 논스톱으로 한 번에 처리합니다.
* 1000개 이상의 파일을 개별 다운로드하며 발생하는 치명적인 네트워크 통신 오버헤드를 원천 차단하고 단 1회의 호출로 전송 가능한 최종 ZIP 파일 경로를 획득합니다.

### 2. High-Speed 7-Zip Engine
* 수백 MB ~ GB 단위의 DICOM 파일들을 처리하기 위해 독립 실행형 `7-Zip (7za.exe)` 엔진을 아키텍처 레벨에 통합했습니다.
* 윈도우 내장 유틸리티(`tar` 또는 `Compress-Archive`)의 극심한 속도 저하 및 메모리 누수를 극복하고, 멀티코어를 활용한 초고속 병렬 압축 및 해제를 지원합니다.

### 3. Strict 2-Folder Sandbox Architecture
* 모든 파일 입출력은 오직 `Downloads`와 `Uploads` 두 개의 격리된 샌드박스 폴더 내에서만 이루어집니다.
* 클라이언트는 복잡한 절대 경로를 알 필요 없이 '순수 파일명'만으로 통신하며, 시스템 상위 디렉토리를 노리는 경로 탐색 공격(Directory Traversal)을 완벽하게 방어합니다.

### 4. Atomic File Transaction
* 네트워크 단절이나 정전 시 파일 손상(Corruption)을 막기 위해, 임시 파일(`.tmp`)에 먼저 쓰고 기록이 완료되면 빠르게 이름을 변경(Rename)하는 원자적 쓰기(Atomic Write) 기술이 적용되었습니다.

### 5. Background Daemon Cleaner & Async Logging
* **Cleaner:** 메인 스레드와 분리된 독립 데몬(Daemon)이 구동되어, 보존 기간(Retention Days)이 지난 찌꺼기 파일들을 서버 랙 없이 조용히 일괄 삭제(Garbage Collection)합니다.
* **Logging:** `spdlog` 기반의 비동기 스레드 풀(Queue Size: 131,072)을 구축하여, 대규모 트래픽이나 예외 상황(400, 500 응답) 발생 시 병목 없는 정밀한 로그 추적(Error Tracking)을 지원합니다.

---

## Tech Stack

* **Server:** C++17, Crow (Microframework), spdlog, 7-Zip (Standalone)
* **Client Interface:** Delphi XE2 (Indy HTTP, JSON, URL Encoding)
* **Service Management:** NSSM (Non-Sucking Service Manager)
* **Proxy/Security:** IIS Reverse Proxy & URL Rewrite Module

---

## API Endpoints

서버는 내부망(`localhost:8080`)에서 동작하며, 모든 응답은 JSON 포맷으로 반환됩니다. 한글 파일명/폴더명 전송 시 URL Encoding(`UTF-8`)이 필수입니다.

| Method | Endpoint | Description |
| --- | --- | --- |
| `POST` | `/pacs/download` | **[핵심]** NAS 대상 폴더를 서버로 통째로 복사 및 고속 압축하여 최종 ZIP 경로 반환 |
| `POST` | `/pacs/extract` | `Downloads` 내 ZIP 파일을 고속 해제 후 내부 `.dcm` 파일 총 개수/용량(MB) 반환 |
| `POST` | `/pacs/mkdir` | 다중 파일 수동 처리를 위한 타임스탬프 기반 고유 세션 폴더(YYYYMMDDHHMMSS) 할당 |
| `POST` | `/pacs/delete` | `Downloads` 폴더 내 특정 단일 파일 즉시 삭제 |
| `POST` | `/file/upload` | 클라이언트의 파일을 `Uploads` 폴더로 안전하게 전송 (Multipart, Atomic Write) |
| `GET`  | `/file/download` | 서버에 캐싱된 파일을 클라이언트로 바이너리 스트리밍 |
| `POST` | `/file/delete` | `Uploads` 폴더 내 특정 단일 파일 즉시 삭제 |
| `GET/POST`| `/config` | 웹 브라우저 기반의 서버 설정(Config) 조회 및 수정 인터페이스 |

---

## Quick Start (Installation)

본 Agent는 보안 및 안정성을 위해 윈도우 서비스(Windows Service) 형태로 백그라운드에서 구동됩니다.

1. 배포된 `HiraAgent` 폴더를 로컬 PC의 안전한 경로(예: `C:\HiraAgent`)에 위치시킵니다.
2. **중요:** 압축/해제 엔진인 `7za.exe` 파일이 반드시 `HiraHelper.exe`와 같은 폴더(`bin`) 내에 존재해야 합니다.
3. 관리자 권한으로 `install_service.bat`을 실행하여 윈도우 서비스(`HiraHelperService`)로 등록 및 자동 실행합니다.
4. `config.json` 파일을 수정하거나 브라우저에서 `http://localhost:8080/config`에 접속하여 환경에 맞게 NAS IP 및 보존 주기를 세팅합니다.

---

## Client Integration (Delphi Example)

클라이언트(Delphi EMR/OCS) 개발자를 위해 HTTP 통신, 예외 처리, JSON 파싱 로직이 완벽하게 래핑(Wrapping)된 `THiraHelper` 정적 클래스가 제공됩니다.

```pascal
// Example: NAS 폴더를 원샷으로 압축 다운로드하고, HIRA API용 최종 ZIP 경로를 획득
var
  bSuccess: Boolean;
  sReadyZipPath: string;
begin
  // 단 1회의 호출로 [폴더복사 -> 7-Zip 압축 -> 찌꺼기 정리] 완료
  bSuccess := THiraHelper.PrepareZipForHira('PacsShort\2024\Study01', sReadyZipPath);

  if bSuccess then
  begin
    // sReadyZipPath (예: "C:\HiraAgent\Downloads\20260401123045.zip")
    // 획득한 서버 측 물리적 경로를 심평원 에이전트(C-STORE) 전송 JSON에 바로 삽입하여 발송!
  end;
end;
```

*(상세한 델파이 연동 코드는 프로젝트 내 `THiraHelper.pas` 참조)*

---

*Architected for High-Performance & Modern Hospital Information Systems.*