// English baseline strings for the web flasher - the single source of keys.
// Other languages (i18n-dfu-<lang>.js) mirror these keys; missing ones fall back
// to English, then to the key itself. Keep {placeholders} and HTML tags intact
// when translating. Strings used via data-i18n-html may contain <strong>/<a>/<code>.
I18N.add("en", {
  // <head> / header
  app_title: "thingino · Web Flasher",
  header_full: " Thingino Web Flasher for Ingenic",
  header_short: " Thingino Flasher",
  title_mode: "Active backend",
  title_help_toggle: "Toggle help balloons",
  title_settings: "Settings",
  op_warning: "Do not disconnect the device or navigate away during this operation",

  // device / connect
  title_connect: "Connect to a device in USB bootrom mode",
  btn_connect: "Connect Device",
  windows_help_link: "Windows? You need a driver first",
  label_device: "Device:",
  label_soc: "SoC:",
  label_stage: "Stage:",
  label_vidpid: "VID:PID:",

  // actions
  title_bootstrap: "Boot a device to the U-Boot burner prompt",
  btn_bootstrap: "Bootstrap",
  title_write: "Write a firmware .bin file to the device's flash memory",
  btn_write: "Write Firmware",
  title_read: "Read the full flash contents and download as a .bin file",
  btn_read: "Read Firmware",
  title_diag: "Read-only: eFuse, serial, secure-boot state (bootrom)",
  btn_diag: "Info",

  // advanced: custom SPL / U-Boot
  adv_toggle: "Advanced: custom SPL / U-Boot",
  adv_desc: "Optional. Supply your own DFU-capable <strong>SPL</strong> and <strong>U-Boot</strong> to use for the next <strong>Bootstrap</strong> instead of the bundled images. Both are required; SoC detection is skipped.",
  btn_sel_spl: "Select SPL",
  btn_sel_uboot: "Select U-Boot",
  btn_clear: "Clear",
  custom_spl_bundled: "SPL: bundled",
  custom_uboot_bundled: "U-Boot: bundled",
  progress_init: "Initializing...",

  // log
  log_title: "Log",

  // settings
  settings_title: "Settings",
  settings_lang: "Language",
  settings_backend: "Flashing backend",
  setting_dfu_label: "<strong>DFU</strong> - U-Boot DFU mode (default)",
  setting_remote_label: "Remote daemon - dfu-remote (HTTP)",
  ph_remote_token: "auth token (optional)",
  remote_lna_note: "Chrome will prompt once to allow local-network access.",
  setting_help_label: 'Show help hints: hover any control for a balloon (or use the <i class="bi bi-question-lg"></i> button)',
  setting_debug_label: "Debug logging (verbose diagnostics)",
  btn_cancel: "Cancel",
  btn_save: "Save",

  // diagnostics (Info) dialog
  diag_title: "Device Info",
  title_close: "Close",
  btn_copy: "Copy",
  btn_close: "Close",

  // Windows driver help dialog
  win_title: "Windows Driver Setup",
  win_intro: "Windows needs a WinUSB driver installed before the web flasher can communicate with the device. Follow these steps:",
  win_step1: "If the Ingenic vendor USB driver is installed, remove it first via <strong>Device Manager</strong>.",
  win_step2: "Connect the device in USB boot mode.",
  win_step3: 'Download and run <a href="https://zadig.akeo.ie/" target="_blank" class="text-warning">Zadig <i class="bi bi-box-arrow-up-right" style="font-size:0.7rem"></i></a>.',
  win_step4: "In Zadig, select <strong>Ingenic USB Boot Device</strong> from the dropdown.",
  win_step5: "Set the target driver to <strong>WinUSB</strong> and click <strong>Install</strong> (or <strong>Replace Driver</strong>).",
  win_step6: "Return to this page and click <strong>Connect Device</strong>.",
  win_important: "<strong>Important:</strong> The Ingenic vendor driver (<code>libusb0.sys</code>) is not compatible and must be removed before installing WinUSB via Zadig.",
  btn_got_it: "Got it",

  // footer / misc
  browser_warning: "WebUSB requires <strong>HTTPS</strong> (or localhost) and <strong>Chrome</strong> or <strong>Edge</strong>.",

  // help balloons (data-help keys)
  help_status_badge: "Current status: Idle, Connecting, Bootstrapping, Writing, Reading, Ready, or Error. It tracks what the flasher is doing right now.",
  help_mode_indicator: "Active backend. DFU = flash straight from this browser over WebUSB. Remote = drive a dfu-remote daemon on another machine. Switch it in Settings.",
  help_help_button: "Help mode. While it's on, hover any control for a balloon explaining it. Click again to turn it off, it stays off until you ask for it.",
  help_settings_button: "Settings: pick the flashing backend (in-browser WebUSB or a remote dfu-remote daemon) and toggle these help hints.",
  help_connect: "Connects to a device sitting in USB-boot (bootrom) mode, it enumerates as a108:c309. Nothing here? The device probably isn't in bootrom yet (hold its boot pin / short, then power on).",
  help_bootstrap: "Loads U-Boot onto the bootrom device over USB so it becomes a DFU flashing target. Do this once; afterward Write/Read light up.",
  help_write: "Writes a firmware .bin to the device's flash. The device must already be bootstrapped into DFU mode.",
  help_read: "Reads the device's full flash back to a .bin file you can save. Handy for backups before you write.",
  help_diag: "Read-only readout of the chip's eFuse: SoC, serial, and secure-boot state. Changes nothing. Stays viewable (cached) even after you bootstrap.",
  help_advanced: "Advanced. Supply your own DFU-capable SPL and U-Boot to USB-boot on the next Bootstrap instead of the bundled images. Both are required, and SoC detection is skipped.",
  help_sel_spl: "Pick your own SPL (stage1) .bin. Used with a custom U-Boot for the next Bootstrap, in place of the bundled loader.",
  help_sel_uboot: "Pick your own U-Boot .bin. Used together with the custom SPL for the next Bootstrap.",
  help_clear_custom: "Clears the custom SPL/U-Boot selection and goes back to the bundled loaders for the detected SoC.",
  help_log: "Activity log: every step, byte count, SHA-256 and any errors land here. Check it first if something doesn't behave as expected.",
  help_setting_dfu: "DFU mode: flash directly from this browser over WebUSB, no extra software, but the device must be plugged into THIS computer (Chrome/Edge only).",
  help_setting_remote: "Remote mode: this page talks to a dfu-remote daemon running on another machine that has the device plugged in. Good for flashing from a phone or a box across the network.",
  help_remote_url: "The dfu-remote daemon's address, e.g. http://192.168.1.50:5050. Chrome will ask once to allow local-network access.",
  help_remote_token: "Optional auth token, only if the daemon was started with one. Leave blank otherwise.",
  help_setting_debug: "Verbose diagnostics in the activity log, replaces the old ?debug URL flag. Leave off unless you're troubleshooting.",
  help_version: "Source, releases and docs on GitHub for the thingino-dfu project. Grab the CLI/daemon builds or file an issue here.",
});
