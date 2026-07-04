// Korean (ko) strings for the web flasher - mirrors i18n-dfu-en.js keys.
// Other languages (i18n-dfu-<lang>.js) mirror these keys; missing ones fall back
// to English, then to the key itself. Keep {placeholders} and HTML tags intact
// when translating. Strings used via data-i18n-html may contain <strong>/<a>/<code>.
I18N.add("ko", {
  // <head> / header
  app_title: "thingino · 웹 플래셔",
  header_full: " Ingenic용 Thingino 웹 플래셔",
  header_short: " Thingino 플래셔",
  title_mode: "활성 백엔드",
  title_help_toggle: "도움말 풍선 켜기/끄기",
  title_settings: "설정",
  op_warning: "이 작업 중에는 장치를 분리하거나 페이지를 벗어나지 마세요",

  // device / connect
  title_connect: "USB bootrom 모드의 장치에 연결",
  btn_connect: "장치 연결",
  windows_help_link: "Windows 사용자이신가요? 먼저 드라이버가 필요합니다",
  label_device: "장치:",
  label_soc: "SoC:",
  label_stage: "단계:",
  label_vidpid: "VID:PID:",

  // actions
  title_bootstrap: "장치를 U-Boot 버너 프롬프트로 부팅",
  btn_bootstrap: "부트스트랩",
  title_write: "펌웨어 .bin 파일을 장치의 플래시 메모리에 기록",
  btn_write: "펌웨어 기록",
  title_read: "전체 플래시 내용을 읽어 .bin 파일로 다운로드",
  btn_read: "펌웨어 읽기",
  title_diag: "읽기 전용: eFuse, 시리얼, 보안 부팅 상태 (bootrom)",
  btn_diag: "정보",

  // advanced: custom SPL / U-Boot
  adv_toggle: "고급: 사용자 지정 SPL / U-Boot",
  adv_desc: "선택 사항. 번들 이미지 대신 다음 <strong>부트스트랩</strong>에 사용할 DFU 지원 <strong>SPL</strong> 및 <strong>U-Boot</strong>를 직접 제공합니다. 둘 다 필요하며, SoC 감지는 건너뜁니다.",
  btn_sel_spl: "SPL 선택",
  btn_sel_uboot: "U-Boot 선택",
  btn_clear: "지우기",
  custom_spl_bundled: "SPL: 번들",
  custom_uboot_bundled: "U-Boot: 번들",
  progress_init: "초기화 중...",

  // log
  log_title: "로그",

  // settings
  settings_title: "설정",
  settings_lang: "언어",
  settings_backend: "플래싱 백엔드",
  setting_dfu_label: "<strong>DFU</strong> - U-Boot DFU 모드 (기본값)",
  setting_remote_label: "원격 데몬 - dfu-remote (HTTP)",
  ph_remote_token: "인증 토큰 (선택 사항)",
  remote_lna_note: "Chrome에서 로컬 네트워크 접근을 허용할지 한 번 물어봅니다.",
  setting_help_label: '도움말 힌트 표시: 컨트롤 위에 마우스를 올리면 풍선이 나타납니다 (또는 <i class="bi bi-question-lg"></i> 버튼 사용)',
  setting_debug_label: "디버그 로깅 (상세 진단)",
  btn_cancel: "취소",
  btn_save: "저장",

  // diagnostics (Info) dialog
  diag_title: "장치 정보",
  title_close: "닫기",
  btn_copy: "복사",
  btn_close: "닫기",

  // Windows driver help dialog
  win_title: "Windows 드라이버 설정",
  win_intro: "웹 플래셔가 장치와 통신하려면 먼저 Windows에 WinUSB 드라이버를 설치해야 합니다. 다음 단계를 따르세요:",
  win_step1: "Ingenic 공급업체 USB 드라이버가 설치되어 있다면 먼저 <strong>장치 관리자</strong>에서 제거하세요.",
  win_step2: "USB 부팅 모드로 장치를 연결하세요.",
  win_step3: '<a href="https://zadig.akeo.ie/" target="_blank" class="text-warning">Zadig <i class="bi bi-box-arrow-up-right" style="font-size:0.7rem"></i></a>를 다운로드하여 실행하세요.',
  win_step4: "Zadig에서 드롭다운 목록으로부터 <strong>Ingenic USB Boot Device</strong>를 선택하세요.",
  win_step5: "대상 드라이버를 <strong>WinUSB</strong>로 설정하고 <strong>Install</strong>(또는 <strong>Replace Driver</strong>)을 클릭하세요.",
  win_step6: "이 페이지로 돌아와 <strong>장치 연결</strong>을 클릭하세요.",
  win_important: "<strong>중요:</strong> Ingenic 공급업체 드라이버(<code>libusb0.sys</code>)는 호환되지 않으므로 Zadig로 WinUSB를 설치하기 전에 반드시 제거해야 합니다.",
  btn_got_it: "확인",

  // footer / misc
  browser_warning: "WebUSB에는 <strong>HTTPS</strong>(또는 localhost)와 <strong>Chrome</strong> 또는 <strong>Edge</strong>가 필요합니다.",

  // help balloons (data-help keys)
  help_status_badge: "현재 상태: 대기, 연결 중, 부트스트랩 중, 기록 중, 읽는 중, 준비됨 또는 오류. 플래셔가 지금 무엇을 하고 있는지 표시합니다.",
  help_mode_indicator: "활성 백엔드. DFU = 이 브라우저에서 WebUSB로 직접 플래싱. 원격 = 다른 컴퓨터의 dfu-remote 데몬을 제어. 설정에서 전환할 수 있습니다.",
  help_help_button: "도움말 모드. 켜져 있는 동안 컨트롤 위에 마우스를 올리면 설명 풍선이 나타납니다. 다시 클릭하면 꺼지며, 다시 켜기 전까지 꺼진 상태로 유지됩니다.",
  help_settings_button: "설정: 플래싱 백엔드(브라우저 내 WebUSB 또는 원격 dfu-remote 데몬)를 선택하고 이 도움말 힌트를 켜거나 끕니다.",
  help_connect: "USB 부팅(bootrom) 모드에 있는 장치에 연결합니다. a108:c309로 인식됩니다. 아무것도 보이지 않나요? 장치가 아직 bootrom 상태가 아닐 수 있습니다 (부팅 핀을 누르거나 단락시킨 후 전원을 켜세요).",
  help_bootstrap: "USB를 통해 bootrom 장치에 U-Boot를 로드하여 DFU 플래싱 대상으로 만듭니다. 한 번만 수행하면 이후 기록/읽기가 활성화됩니다.",
  help_write: "펌웨어 .bin을 장치의 플래시에 기록합니다. 장치가 이미 DFU 모드로 부트스트랩되어 있어야 합니다.",
  help_read: "장치의 전체 플래시를 저장 가능한 .bin 파일로 읽어옵니다. 기록하기 전 백업에 유용합니다.",
  help_diag: "칩 eFuse의 읽기 전용 정보: SoC, 시리얼, 보안 부팅 상태. 아무것도 변경하지 않습니다. 부트스트랩 후에도 (캐시되어) 계속 볼 수 있습니다.",
  help_advanced: "고급. 번들 이미지 대신 다음 부트스트랩 시 USB 부팅에 사용할 DFU 지원 SPL 및 U-Boot를 직접 제공합니다. 둘 다 필요하며, SoC 감지는 건너뜁니다.",
  help_sel_spl: "사용자 지정 SPL(stage1) .bin을 선택합니다. 번들 로더 대신 사용자 지정 U-Boot와 함께 다음 부트스트랩에 사용됩니다.",
  help_sel_uboot: "사용자 지정 U-Boot .bin을 선택합니다. 사용자 지정 SPL과 함께 다음 부트스트랩에 사용됩니다.",
  help_clear_custom: "사용자 지정 SPL/U-Boot 선택을 지우고 감지된 SoC용 번들 로더로 돌아갑니다.",
  help_log: "활동 로그: 모든 단계, 바이트 수, SHA-256, 오류가 여기에 표시됩니다. 예상대로 동작하지 않으면 먼저 확인하세요.",
  help_setting_dfu: "DFU 모드: 추가 소프트웨어 없이 이 브라우저에서 WebUSB로 직접 플래싱합니다. 단, 장치가 이 컴퓨터에 연결되어 있어야 합니다 (Chrome/Edge 전용).",
  help_setting_remote: "원격 모드: 이 페이지가 장치가 연결된 다른 컴퓨터에서 실행 중인 dfu-remote 데몬과 통신합니다. 휴대폰이나 네트워크 너머의 장비에서 플래싱할 때 유용합니다.",
  help_remote_url: "dfu-remote 데몬의 주소입니다. 예: http://192.168.1.50:5050. Chrome에서 로컬 네트워크 접근을 허용할지 한 번 물어봅니다.",
  help_remote_token: "선택적 인증 토큰으로, 데몬이 토큰과 함께 시작된 경우에만 필요합니다. 그렇지 않으면 비워 두세요.",
  help_setting_debug: "활동 로그에 상세 진단을 표시하며, 기존 ?debug URL 플래그를 대체합니다. 문제를 해결하는 경우가 아니면 꺼 두세요.",
  help_version: "thingino-dfu 프로젝트의 소스, 릴리스, 문서가 GitHub에 있습니다. 여기에서 CLI/데몬 빌드를 받거나 이슈를 등록할 수 있습니다.",
});
