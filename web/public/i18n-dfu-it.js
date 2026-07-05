// Italian (it) strings for the web flasher - mirrors i18n-dfu-en.js keys.
// Other languages (i18n-dfu-<lang>.js) mirror these keys; missing ones fall back
// to English, then to the key itself. Keep {placeholders} and HTML tags intact
// when translating. Strings used via data-i18n-html may contain <strong>/<a>/<code>.
I18N.add("it", {
  // <head> / header
  app_title: "thingino · Web Flasher",
  header_full: " Thingino Web Flasher per Ingenic",
  header_short: " Thingino Flasher",
  title_mode: "Backend attivo",
  title_help_toggle: "Attiva/disattiva i fumetti di aiuto",
  title_settings: "Impostazioni",
  op_warning: "Non scollegare il dispositivo e non uscire dalla pagina durante questa operazione",

  // device / connect
  title_connect: "Collegati a un dispositivo in modalità bootrom USB",
  btn_connect: "Collega dispositivo",
  windows_help_link: "Windows? Prima ti serve un driver",
  label_device: "Dispositivo:",
  label_soc: "SoC:",
  label_stage: "Fase:",
  label_vidpid: "VID:PID:",

  // actions
  title_bootstrap: "Avvia un dispositivo fino al prompt di scrittura di U-Boot",
  btn_bootstrap: "Avvia",
  title_write: "Scrivi un file firmware .bin nella memoria flash del dispositivo",
  btn_write: "Scrivi firmware",
  title_read: "Leggi l'intero contenuto della memoria flash e scaricalo come file .bin",
  btn_read: "Leggi firmware",
  title_diag: "Sola lettura: eFuse, seriale, stato di secure-boot (bootrom)",
  btn_diag: "Info",

  // advanced: custom SPL / U-Boot
  adv_toggle: "Avanzate: SPL / U-Boot personalizzati",
  adv_desc: "Opzionale. Fornisci il tuo <strong>SPL</strong> e <strong>U-Boot</strong> compatibili con DFU da usare per il prossimo <strong>Avvia</strong> invece delle immagini incluse. Entrambi sono obbligatori; il rilevamento del SoC viene saltato.",
  btn_sel_spl: "Seleziona SPL",
  btn_sel_uboot: "Seleziona U-Boot",
  btn_clear: "Cancella",
  custom_spl_bundled: "SPL: incluso",
  custom_uboot_bundled: "U-Boot: incluso",
  progress_init: "Inizializzazione...",

  // log
  log_title: "Log",

  // settings
  settings_title: "Impostazioni",
  settings_lang: "Lingua",
  settings_backend: "Backend di scrittura",
  setting_dfu_label: "<strong>DFU</strong> - modalità DFU di U-Boot (predefinita)",
  setting_remote_label: "Daemon remoto - dfu-remote (HTTP)",
  ph_remote_token: "token di autenticazione (opzionale)",
  remote_lna_note: "Chrome chiederà una volta di consentire l'accesso alla rete locale.",
  setting_help_label: 'Mostra i suggerimenti di aiuto: passa il mouse su qualsiasi controllo per un fumetto (o usa il pulsante <i class="bi bi-question-lg"></i>)',
  setting_verify_label: "Verifica dopo la scrittura (rilettura e confronto)",
  setting_debug_label: "Log di debug (diagnostica dettagliata)",
  btn_cancel: "Annulla",
  btn_save: "Salva",

  // diagnostics (Info) dialog
  diag_title: "Info dispositivo",
  title_close: "Chiudi",
  btn_copy: "Copia",
  btn_close: "Chiudi",

  // Windows driver help dialog
  win_title: "Configurazione driver Windows",
  win_intro: "Windows richiede l'installazione di un driver WinUSB prima che il web flasher possa comunicare con il dispositivo. Segui questi passaggi:",
  win_step1: "Se il driver USB del produttore Ingenic è installato, rimuovilo prima tramite <strong>Gestione dispositivi</strong>.",
  win_step2: "Collega il dispositivo in modalità di avvio USB.",
  win_step3: 'Scarica ed esegui <a href="https://zadig.akeo.ie/" target="_blank" class="text-warning">Zadig <i class="bi bi-box-arrow-up-right" style="font-size:0.7rem"></i></a>.',
  win_step4: "In Zadig, seleziona <strong>Ingenic USB Boot Device</strong> dal menu a discesa.",
  win_step5: "Imposta il driver di destinazione su <strong>WinUSB</strong> e fai clic su <strong>Install</strong> (oppure <strong>Replace Driver</strong>).",
  win_step6: "Torna a questa pagina e fai clic su <strong>Collega dispositivo</strong>.",
  win_important: "<strong>Importante:</strong> il driver del produttore Ingenic (<code>libusb0.sys</code>) non è compatibile e deve essere rimosso prima di installare WinUSB tramite Zadig.",
  btn_got_it: "Ho capito",

  // footer / misc
  browser_warning: "WebUSB richiede <strong>HTTPS</strong> (o localhost) e <strong>Chrome</strong> oppure <strong>Edge</strong>.",

  // help balloons (data-help keys)
  help_status_badge: "Stato attuale: Inattivo, Connessione, Avvio, Scrittura, Lettura, Pronto o Errore. Indica cosa sta facendo il flasher in questo momento.",
  help_mode_indicator: "Backend attivo. DFU = scrittura diretta da questo browser tramite WebUSB. Remoto = controlla un daemon dfu-remote su un'altra macchina. Modificalo nelle Impostazioni.",
  help_help_button: "Modalità aiuto. Mentre è attiva, passa il mouse su qualsiasi controllo per un fumetto che lo spiega. Fai di nuovo clic per disattivarla; rimane disattivata finché non la richiedi di nuovo.",
  help_settings_button: "Impostazioni: scegli il backend di scrittura (WebUSB nel browser o un daemon dfu-remote remoto) e attiva/disattiva questi suggerimenti di aiuto.",
  help_connect: "Si collega a un dispositivo in modalità USB-boot (bootrom); viene enumerato come a108:c309. Non appare nulla? Probabilmente il dispositivo non è ancora in bootrom (tieni premuto il pin di boot o cortocircuitalo, poi accendi).",
  help_bootstrap: "Carica U-Boot sul dispositivo bootrom tramite USB, così diventa un target di scrittura DFU. Fallo una volta; dopodiché Scrivi/Leggi si attivano.",
  help_write: "Scrive un file firmware .bin nella flash del dispositivo. Il dispositivo deve già essere stato avviato in modalità DFU.",
  help_read: "Legge l'intero contenuto della flash del dispositivo in un file .bin che puoi salvare. Utile per i backup prima di scrivere.",
  help_diag: "Lettura di sola lettura dell'eFuse del chip: SoC, seriale e stato di secure-boot. Non modifica nulla. Rimane consultabile (in cache) anche dopo l'avvio.",
  help_advanced: "Avanzate. Fornisci il tuo SPL e U-Boot compatibili con DFU, da caricare via USB al prossimo Avvia, invece delle immagini incluse. Entrambi sono obbligatori e il rilevamento del SoC viene saltato.",
  help_sel_spl: "Scegli il tuo file SPL (stage1) .bin. Viene usato insieme a un U-Boot personalizzato per il prossimo Avvia, al posto del loader incluso.",
  help_sel_uboot: "Scegli il tuo file U-Boot .bin. Viene usato insieme al file SPL personalizzato per il prossimo Avvia.",
  help_clear_custom: "Cancella la selezione di SPL/U-Boot personalizzati e torna ai loader inclusi per il SoC rilevato.",
  help_log: "Log delle attività: ogni passaggio, il conteggio dei byte, lo SHA-256 ed eventuali errori compaiono qui. Controllalo per primo se qualcosa non si comporta come previsto.",
  help_setting_dfu: "Modalità DFU: scrittura diretta da questo browser tramite WebUSB, senza software aggiuntivo, ma il dispositivo deve essere collegato a QUESTO computer (solo Chrome/Edge).",
  help_setting_remote: "Modalità remota: questa pagina comunica con un daemon dfu-remote in esecuzione su un'altra macchina a cui è collegato il dispositivo. Utile per scrivere da uno smartphone o da un'altra macchina sulla rete.",
  help_remote_url: "L'indirizzo del daemon dfu-remote, ad es. http://192.168.1.50:5050. Chrome chiederà una volta di consentire l'accesso alla rete locale.",
  help_remote_token: "Token di autenticazione opzionale, solo se il daemon è stato avviato con uno. Altrimenti lascia il campo vuoto.",
  help_setting_verify: "Dopo la scrittura rilegge l'intera flash e la confronta con il file. Rileva una scrittura difettosa, ma raddoppia all'incirca il tempo di scrittura. Disattivata per impostazione predefinita.",
  help_setting_debug: "Diagnostica dettagliata nel log delle attività; sostituisce il vecchio flag URL ?debug. Lascialo disattivato a meno che tu non stia risolvendo un problema.",
  help_version: "Codice sorgente, release e documentazione su GitHub per il progetto thingino-dfu. Qui puoi scaricare le build CLI/daemon o segnalare un problema.",
});
