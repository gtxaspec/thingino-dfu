// Dutch (nl) strings for the web flasher - mirrors i18n-dfu-en.js keys.
// Other languages (i18n-dfu-<lang>.js) mirror these keys; missing ones fall back
// to English, then to the key itself. Keep {placeholders} and HTML tags intact
// when translating. Strings used via data-i18n-html may contain <strong>/<a>/<code>.
I18N.add("nl", {
  // <head> / header
  app_title: "thingino · Webflasher",
  header_full: " Thingino Webflasher voor Ingenic",
  header_short: " Thingino Flasher",
  title_mode: "Actieve backend",
  title_help_toggle: "Hulpballonnen in-/uitschakelen",
  title_settings: "Instellingen",
  op_warning: "Koppel het apparaat niet los en verlaat deze pagina niet tijdens deze bewerking",

  // device / connect
  title_connect: "Verbind met een apparaat in USB-bootrom-modus",
  btn_connect: "Apparaat verbinden",
  windows_help_link: "Windows? Je hebt eerst een stuurprogramma nodig",
  label_device: "Apparaat:",
  label_soc: "SoC:",
  label_stage: "Fase:",
  label_vidpid: "VID:PID:",

  // actions
  title_bootstrap: "Start een apparaat op naar de U-Boot-burnerprompt",
  btn_bootstrap: "Bootstrappen",
  title_write: "Schrijf een firmware-.bin-bestand naar het flashgeheugen van het apparaat",
  btn_write: "Firmware schrijven",
  title_read: "Lees de volledige flashinhoud en download deze als .bin-bestand",
  btn_read: "Firmware lezen",
  title_diag: "Alleen-lezen: eFuse, serienummer, secure-boot-status (bootrom)",
  btn_diag: "Info",

  // advanced: custom SPL / U-Boot
  adv_toggle: "Geavanceerd: aangepaste SPL / U-Boot",
  adv_desc: "Optioneel. Lever je eigen DFU-geschikte <strong>SPL</strong> en <strong>U-Boot</strong> aan voor de volgende <strong>Bootstrap</strong>, in plaats van de meegeleverde images. Beide zijn vereist; SoC-detectie wordt overgeslagen.",
  btn_sel_spl: "SPL selecteren",
  btn_sel_uboot: "U-Boot selecteren",
  btn_clear: "Wissen",
  custom_spl_bundled: "SPL: meegeleverd",
  custom_uboot_bundled: "U-Boot: meegeleverd",
  progress_init: "Initialiseren...",

  // log
  log_title: "Logboek",

  // settings
  settings_title: "Instellingen",
  settings_lang: "Taal",
  settings_backend: "Flash-backend",
  setting_dfu_label: "<strong>DFU</strong> - U-Boot DFU-modus (standaard)",
  setting_remote_label: "Externe daemon - dfu-remote (HTTP)",
  ph_remote_token: "authenticatietoken (optioneel)",
  remote_lna_note: "Chrome vraagt eenmalig om toegang tot het lokale netwerk toe te staan.",
  setting_help_label: 'Hulptips tonen: beweeg de muis over een besturingselement voor een ballon (of gebruik de knop <i class="bi bi-question-lg"></i>)',
  setting_verify_label: "Verificatie na het schrijven (teruglezen en vergelijken)",
  setting_debug_label: "Foutopsporingslogboek (uitgebreide diagnostiek)",
  btn_cancel: "Annuleren",
  btn_save: "Opslaan",

  // diagnostics (Info) dialog
  diag_title: "Apparaatinfo",
  title_close: "Sluiten",
  btn_copy: "Kopiëren",
  btn_close: "Sluiten",

  // Windows driver help dialog
  win_title: "Windows-stuurprogramma instellen",
  win_intro: "Windows heeft een WinUSB-stuurprogramma nodig voordat de webflasher met het apparaat kan communiceren. Volg deze stappen:",
  win_step1: "Als het USB-stuurprogramma van de Ingenic-fabrikant is geïnstalleerd, verwijder het dan eerst via <strong>Apparaatbeheer</strong>.",
  win_step2: "Sluit het apparaat aan in USB-bootmodus.",
  win_step3: 'Download en start <a href="https://zadig.akeo.ie/" target="_blank" class="text-warning">Zadig <i class="bi bi-box-arrow-up-right" style="font-size:0.7rem"></i></a>.',
  win_step4: "Selecteer in Zadig <strong>Ingenic USB Boot Device</strong> in de vervolgkeuzelijst.",
  win_step5: "Stel het doelstuurprogramma in op <strong>WinUSB</strong> en klik op <strong>Install</strong> (of <strong>Replace Driver</strong>).",
  win_step6: "Ga terug naar deze pagina en klik op <strong>Apparaat verbinden</strong>.",
  win_important: "<strong>Belangrijk:</strong> Het stuurprogramma van de Ingenic-fabrikant (<code>libusb0.sys</code>) is niet compatibel en moet worden verwijderd voordat je WinUSB via Zadig installeert.",
  btn_got_it: "Begrepen",

  // footer / misc
  browser_warning: "WebUSB vereist <strong>HTTPS</strong> (of localhost) en <strong>Chrome</strong> of <strong>Edge</strong>.",

  // help balloons (data-help keys)
  help_status_badge: "Huidige status: Inactief, Verbinden, Bootstrappen, Schrijven, Lezen, Gereed of Fout. Het houdt bij wat de flasher op dit moment doet.",
  help_mode_indicator: "Actieve backend. DFU = rechtstreeks flashen vanuit deze browser via WebUSB. Extern = een dfu-remote-daemon op een andere machine aansturen. Wijzig dit bij Instellingen.",
  help_help_button: "Hulpmodus. Wanneer deze aanstaat, beweeg je de muis over een besturingselement voor een ballon met uitleg. Klik nogmaals om het uit te schakelen; het blijft uit totdat je erom vraagt.",
  help_settings_button: "Instellingen: kies de flash-backend (WebUSB in de browser of een externe dfu-remote-daemon) en schakel deze hulptips in of uit.",
  help_connect: "Maakt verbinding met een apparaat dat in USB-boot-modus (bootrom) staat; het wordt herkend als a108:c309. Niets te zien? Dan staat het apparaat waarschijnlijk nog niet in bootrom (houd de boot-pin ingedrukt / sluit kort en schakel het dan in).",
  help_bootstrap: "Laadt U-Boot via USB op het bootrom-apparaat zodat het een DFU-flashdoel wordt. Doe dit één keer; daarna lichten Schrijven/Lezen op.",
  help_write: "Schrijft een firmware-.bin naar de flash van het apparaat. Het apparaat moet al met een bootstrap in DFU-modus zijn gezet.",
  help_read: "Leest de volledige flash van het apparaat terug naar een .bin-bestand dat je kunt opslaan. Handig voor back-ups voordat je schrijft.",
  help_diag: "Alleen-lezen uitlezing van de eFuse van de chip: SoC, serienummer en secure-boot-status. Verandert niets. Blijft zichtbaar (in cache), ook nadat je de bootstrap hebt uitgevoerd.",
  help_advanced: "Geavanceerd. Lever je eigen DFU-geschikte SPL en U-Boot aan om te USB-booten bij de volgende Bootstrap in plaats van de meegeleverde images. Beide zijn vereist en SoC-detectie wordt overgeslagen.",
  help_sel_spl: "Kies je eigen SPL-.bin (stage1). Wordt samen met een aangepaste U-Boot gebruikt bij de volgende Bootstrap, in plaats van de meegeleverde loader.",
  help_sel_uboot: "Kies je eigen U-Boot-.bin. Wordt samen met de aangepaste SPL gebruikt bij de volgende Bootstrap.",
  help_clear_custom: "Wist de selectie van aangepaste SPL/U-Boot en keert terug naar de meegeleverde loaders voor de gedetecteerde SoC.",
  help_log: "Activiteitenlogboek: elke stap, byteaantal, SHA-256 en eventuele fouten komen hier terecht. Controleer dit eerst als iets zich niet gedraagt zoals verwacht.",
  help_setting_dfu: "DFU-modus: rechtstreeks flashen vanuit deze browser via WebUSB, geen extra software, maar het apparaat moet op DEZE computer zijn aangesloten (alleen Chrome/Edge).",
  help_setting_remote: "Externe modus: deze pagina communiceert met een dfu-remote-daemon die draait op een andere machine waarop het apparaat is aangesloten. Handig om te flashen vanaf een telefoon of een apparaat elders in het netwerk.",
  help_remote_url: "Het adres van de dfu-remote-daemon, bijv. http://192.168.1.50:5050. Chrome vraagt eenmalig om toegang tot het lokale netwerk toe te staan.",
  help_remote_token: "Optioneel authenticatietoken, alleen als de daemon met een token is gestart. Laat het anders leeg.",
  help_setting_verify: "Leest na het schrijven de volledige flash terug en vergelijkt die met het bestand. Ontdekt zo een mislukte schrijfactie, maar het flashen duurt daardoor ongeveer twee keer zo lang. Staat standaard uit.",
  help_setting_debug: "Uitgebreide diagnostiek in het activiteitenlogboek; vervangt de oude ?debug-URL-vlag. Laat uit tenzij je problemen oplost.",
  help_version: "Broncode, releases en documentatie op GitHub voor het thingino-dfu-project. Download hier de CLI-/daemon-builds of dien een issue in.",
});
