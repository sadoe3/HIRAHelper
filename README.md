# HIRA Helper

**HIRA Helper Agent**는 병원의 레거시 시스템(Delphi OCS/EMR)과 최신 네트워크 스토리지(NAS/PACS), 그리고 외부 API 간의 대용량 파일 송수신을 중계하는 초경량/고성능 C++ 마이크로 서비스(Middleware)입니다.

기존 레거시 클라이언트가 처리하기 버거운 대용량 I/O, 압축 해제, 파일 생명주기 관리 등의 무거운 작업을 서버(Agent)로 완벽하게 위임하는 **'Fire-and-Forget'** 아키텍처를 구현했습니다.

## Core Architecture & Features

### 1. Strict 2-Folder Sandbox Architecture

* 모든 파일 입출력은 오직 `Downloads`와 `Uploads` 두 개의 격리된 폴더 내에서만 이루어집니다.
* 클라이언트는 복잡한 절대 경로를 알 필요 없이 '순수 파일명'만으로 서버와 통신하며, 경로 탐색 공격(Directory Traversal)을 원천 차단합니다.

### 2. Zero-Dependency Native Zip Extraction

* 무거운 외부 압축 라이브러리(zlib 등) 없이, 윈도우 10/11에 내장된 `tar.exe`를 활용하여 시스템 콜 방식으로 압축을 해제합니다.
* **Unicode Safe:** `std::wstring`과 `_wsystem()`을 적용하여, 한글/특수문자가 포함된 기괴한 파일명이나 경로에서도 인코딩 깨짐 없이 100% 완벽하게 압축을 해제합니다.

### 3. Background Daemon Cleaner

* 메인 API 스레드와 완전히 분리된 독립된 백그라운드 스레드(Daemon)가 구동됩니다.
* 서버 랙(Lag)이나 I/O 병목을 유발하지 않고, 설정된 보존 기간(Retention Days)이 지난 찌꺼기 파일과 빈 폴더들을 조용히 일괄 삭제(Garbage Collection)합니다.

### 4. Atomic File Transaction

* 네트워크 단절이나 정전 시 파일 손상(Corruption)을 막기 위해, 임시 파일(`.tmp`)에 먼저 쓰고 기록이 완료되면 빠르게 이름을 변경(Rename)하는 원자적 쓰기(Atomic Write) 기술이 적용되었습니다.

### 5. High-Performance Async Logging

* `spdlog` 기반의 비동기 로깅 시스템을 구축하여, 대규모 트래픽 발생 시에도 로깅으로 인한 메인 서버의 성능 저하가 없습니다. (일별 롤링 및 용량 제한 적용)

---

## Tech Stack

* **Server:** C++17, Crow (Microframework), spdlog, C++ `std::filesystem`
* **Client Interface:** Delphi XE2 (Indy HTTP, JSON, URL Encoding)
* **Service Management:** NSSM (Non-Sucking Service Manager)
* **Proxy/Security:** IIS Reverse Proxy & URL Rewrite Module

---

## API Endpoints

서버는 내부망(`localhost:8080`)에서 동작하며, 모든 응답은 JSON 포맷으로 반환됩니다. 한글 파일명 전송 시 URL Encoding(`UTF-8`)이 필수입니다.

| Method | Endpoint | Description |
| --- | --- | --- |
| `POST` | `/pacs/download` | NAS의 단일 DICOM 파일을 `Downloads` 폴더로 고속 캐싱 |
| `POST` | `/pacs/delete` | `Downloads` 폴더 내 특정 단일 파일 즉시 삭제 |
| `POST` | `/pacs/extract` | ZIP 파일 압축 해제 (현재 폴더) 및 해당 폴더 내 전체 `.dcm` 파일 누적 개수/용량(MB) 반환 |
| `POST` | `/pacs/mkdir` | 다중 파일(DICOM/ZIP) 처리를 위한 타임스탬프 기반의 고유 전송 세션 폴더 생성 |
| `POST` | `/file/upload` | 클라이언트의 파일을 `Uploads` 폴더로 안전하게 전송 (Multipart, Atomic Write) |
| `GET` | `/file/download` | 서버에 캐싱된 파일을 클라이언트로 바이너리 스트리밍 |
| `POST` | `/file/delete` | `Uploads` 폴더 내 특정 단일 파일 즉시 삭제 |
| `GET/POST` | `/config` | 웹 브라우저 기반의 서버 설정(Config) 조회 및 수정 인터페이스 |

---

## Quick Start (Installation)

본 Agent는 보안상 윈도우 서비스(Windows Service) 형태로 백그라운드에서 구동됩니다.

1. 배포된 `HiraAgent` 폴더를 로컬 PC의 안전한 경로(예: `C:\HiraAgent`)에 위치시킵니다.
2. `installers` 폴더 내의 필수 라이브러리(VC++ Redist, IIS Rewrite/ARR)를 설치합니다.
3. 관리자 권한으로 `install_service.bat`을 실행하여 윈도우 서비스(`HiraHelperService`)로 등록 및 자동 실행합니다.
4. `config.json` 파일을 수정하거나 브라우저에서 `http://localhost:8080/config`에 접속하여 환경에 맞게 NAS IP 및 폴더 경로를 세팅합니다.

---

## Client Integration (Delphi Example)

클라이언트(Delphi EMR/OCS) 개발자를 위해 HTTP 통신, 예외 처리, URL 인코딩 로직이 완벽하게 래핑(Wrapping)된 `THiraHelper` 정적 클래스가 제공됩니다.

```pascal
// Example: 다운로드된 ZIP 파일을 고유 임시 폴더로 압축 해제 지시
var
  sExtractedPath: string;
begin
  // 서버가 응답한 JSON 텍스트 수신 (내부적으로 URL 인코딩 처리됨)
  sJsonResponse := THiraHelper.ExtractZipFile('20260312_12345\test.zip');
  
  // JSON 파싱 후 상태 확인
  // 정상 처리 시 extract_path 를 참조하여 후속 C-STORE 작업 진행
end;

```

*(상세한 델파이 연동 코드는 프로젝트 내 `HiraAPILibrary.pas` 참조)*

---

*Developed & Architected for Modern Hospital Information Systems.*
