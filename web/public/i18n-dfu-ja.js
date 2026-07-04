// Japanese (ja) strings for the web flasher - mirrors i18n-dfu-en.js keys.
I18N.add("ja", {
  // <head> / header
  app_title: "thingino · Web フラッシャー",
  header_full: " Ingenic 向け Thingino Web フラッシャー",
  header_short: " Thingino フラッシャー",
  title_mode: "使用中のバックエンド",
  title_help_toggle: "ヘルプ吹き出しの表示切り替え",
  title_settings: "設定",
  op_warning: "この操作の実行中は、デバイスを取り外したりページを移動したりしないでください",

  // device / connect
  title_connect: "USB bootrom モードのデバイスに接続します",
  btn_connect: "デバイスに接続",
  windows_help_link: "Windows をお使いですか？まずドライバーが必要です",
  label_device: "デバイス:",
  label_soc: "SoC:",
  label_stage: "ステージ:",
  label_vidpid: "VID:PID:",

  // actions
  title_bootstrap: "デバイスを U-Boot の書き込みプロンプトまで起動します",
  btn_bootstrap: "ブートストラップ",
  title_write: "ファームウェアの .bin ファイルをデバイスのフラッシュメモリに書き込みます",
  btn_write: "ファームウェアを書き込む",
  title_read: "フラッシュの全内容を読み出し、.bin ファイルとしてダウンロードします",
  btn_read: "ファームウェアを読み出す",
  title_diag: "読み取り専用: eFuse、シリアル、セキュアブート状態 (bootrom)",
  btn_diag: "情報",

  // advanced: custom SPL / U-Boot
  adv_toggle: "詳細設定: カスタム SPL / U-Boot",
  adv_desc: "任意設定です。同梱のイメージの代わりに、次回の<strong>ブートストラップ</strong>で使用する DFU 対応の<strong>SPL</strong>と<strong>U-Boot</strong>をご自身で指定します。両方とも必須で、SoC の自動検出は行われません。",
  btn_sel_spl: "SPL を選択",
  btn_sel_uboot: "U-Boot を選択",
  btn_clear: "クリア",
  custom_spl_bundled: "SPL: 同梱",
  custom_uboot_bundled: "U-Boot: 同梱",
  progress_init: "初期化中...",

  // log
  log_title: "ログ",

  // settings
  settings_title: "設定",
  settings_lang: "言語",
  settings_backend: "書き込みバックエンド",
  setting_dfu_label: "<strong>DFU</strong> - U-Boot DFU モード (デフォルト)",
  setting_remote_label: "リモートデーモン - dfu-remote (HTTP)",
  ph_remote_token: "認証トークン (任意)",
  remote_lna_note: "Chrome がローカルネットワークアクセスの許可を一度確認します。",
  setting_help_label: 'ヘルプヒントを表示: 任意のコントロールにマウスを合わせると吹き出しが表示されます (または <i class="bi bi-question-lg"></i> ボタンを使用)',
  setting_debug_label: "デバッグログ (詳細な診断情報)",
  btn_cancel: "キャンセル",
  btn_save: "保存",

  // diagnostics (Info) dialog
  diag_title: "デバイス情報",
  title_close: "閉じる",
  btn_copy: "コピー",
  btn_close: "閉じる",

  // Windows driver help dialog
  win_title: "Windows ドライバーのセットアップ",
  win_intro: "Web フラッシャーがデバイスと通信するには、事前に Windows に WinUSB ドライバーをインストールする必要があります。以下の手順に従ってください:",
  win_step1: "Ingenic ベンダー製の USB ドライバーがインストールされている場合は、まず<strong>デバイス マネージャー</strong>から削除してください。",
  win_step2: "デバイスを USB ブートモードで接続します。",
  win_step3: '<a href="https://zadig.akeo.ie/" target="_blank" class="text-warning">Zadig <i class="bi bi-box-arrow-up-right" style="font-size:0.7rem"></i></a> をダウンロードして実行します。',
  win_step4: "Zadig で、ドロップダウンから <strong>Ingenic USB Boot Device</strong> を選択します。",
  win_step5: "ターゲットドライバーを <strong>WinUSB</strong> に設定し、<strong>Install</strong> (または <strong>Replace Driver</strong>) をクリックします。",
  win_step6: "このページに戻り、<strong>デバイスに接続</strong>をクリックします。",
  win_important: "<strong>重要:</strong> Ingenic ベンダー製ドライバー (<code>libusb0.sys</code>) には互換性がないため、Zadig で WinUSB をインストールする前に削除する必要があります。",
  btn_got_it: "了解",

  // footer / misc
  browser_warning: "WebUSB には <strong>HTTPS</strong> (または localhost) と <strong>Chrome</strong> または <strong>Edge</strong> が必要です。",

  // help balloons (data-help keys)
  help_status_badge: "現在のステータス: Idle (待機中)、Connecting (接続中)、Bootstrapping (ブートストラップ中)、Writing (書き込み中)、Reading (読み出し中)、Ready (準備完了)、Error (エラー) のいずれか。フラッシャーが現在行っている動作を示します。",
  help_mode_indicator: "使用中のバックエンドです。DFU = WebUSB 経由でこのブラウザーから直接書き込みます。リモート = 別のマシンで動作する dfu-remote デーモンを操作します。切り替えは設定で行います。",
  help_help_button: "ヘルプモードです。オンの間は、任意のコントロールにマウスを合わせると説明の吹き出しが表示されます。もう一度クリックするとオフになり、再びオンにするまでオフのままになります。",
  help_settings_button: "設定: 書き込みバックエンド (ブラウザー内の WebUSB またはリモートの dfu-remote デーモン) を選択し、これらのヘルプヒントの表示を切り替えます。",
  help_connect: "USB ブート (bootrom) モードのデバイスに接続します。デバイスは a108:c309 として認識されます。何も表示されませんか？おそらくデバイスがまだ bootrom モードになっていません (ブートピンを押し続ける / ショートさせてから電源を入れてください)。",
  help_bootstrap: "bootrom モードのデバイスに USB 経由で U-Boot を読み込み、DFU 書き込みの対象にします。一度実行すれば、その後は書き込み/読み出しが有効になります。",
  help_write: "ファームウェアの .bin をデバイスのフラッシュに書き込みます。デバイスは事前に DFU モードへブートストラップされている必要があります。",
  help_read: "デバイスのフラッシュ全体を読み出し、保存可能な .bin ファイルにします。書き込み前のバックアップに便利です。",
  help_diag: "チップの eFuse を読み取り専用で読み出します: SoC、シリアル、セキュアブート状態。何も変更しません。ブートストラップ後もキャッシュされ、引き続き表示できます。",
  help_advanced: "詳細設定です。同梱のイメージの代わりに、次回のブートストラップで USB ブートに使用する DFU 対応の SPL と U-Boot をご自身で指定します。両方とも必須で、SoC の自動検出は行われません。",
  help_sel_spl: "ご自身の SPL (stage1) .bin を選択します。同梱のローダーの代わりに、次回のブートストラップでカスタム U-Boot とともに使用されます。",
  help_sel_uboot: "ご自身の U-Boot .bin を選択します。次回のブートストラップでカスタム SPL とともに使用されます。",
  help_clear_custom: "カスタム SPL/U-Boot の選択をクリアし、検出された SoC 用の同梱ローダーに戻します。",
  help_log: "アクティビティログ: すべての手順、バイト数、SHA-256、エラーがここに表示されます。想定どおりに動作しない場合は、まずここを確認してください。",
  help_setting_dfu: "DFU モード: 追加のソフトウェアなしで、WebUSB 経由でこのブラウザーから直接書き込みます。ただし、デバイスはこのコンピューターに接続されている必要があります (Chrome/Edge のみ)。",
  help_setting_remote: "リモートモード: このページは、デバイスが接続された別のマシンで動作する dfu-remote デーモンと通信します。スマートフォンやネットワーク越しの別の機器から書き込む場合に便利です。",
  help_remote_url: "dfu-remote デーモンのアドレスです (例: http://192.168.1.50:5050)。Chrome がローカルネットワークアクセスの許可を一度確認します。",
  help_remote_token: "任意の認証トークンです。デーモンがトークン付きで起動された場合にのみ入力します。それ以外の場合は空欄のままにしてください。",
  help_setting_debug: "アクティビティログに詳細な診断情報を出力します。以前の ?debug URL フラグの代わりです。トラブルシューティング時以外はオフのままにしてください。",
  help_version: "thingino-dfu プロジェクトのソースコード、リリース、ドキュメントは GitHub にあります。CLI/デーモンのビルドの入手や問題の報告はこちらから行えます。",
});
