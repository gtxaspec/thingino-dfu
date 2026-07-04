// Turkish (tr) strings for the web flasher - mirrors i18n-dfu-en.js keys.
// Other languages (i18n-dfu-<lang>.js) mirror these keys; missing ones fall back
// to English, then to the key itself. Keep {placeholders} and HTML tags intact
// when translating. Strings used via data-i18n-html may contain <strong>/<a>/<code>.
I18N.add("tr", {
  // <head> / header
  app_title: "thingino · Web Flash Aracı",
  header_full: " Ingenic için Thingino Web Flash Aracı",
  header_short: " Thingino Flash Aracı",
  title_mode: "Etkin arka uç",
  title_help_toggle: "Yardım balonlarını aç/kapat",
  title_settings: "Ayarlar",
  op_warning: "Bu işlem sırasında cihazın bağlantısını kesmeyin veya sayfadan ayrılmayın",

  // device / connect
  title_connect: "USB bootrom modundaki bir cihaza bağlanın",
  btn_connect: "Cihaza Bağlan",
  windows_help_link: "Windows mu? Önce bir sürücü gerekiyor",
  label_device: "Cihaz:",
  label_soc: "SoC:",
  label_stage: "Aşama:",
  label_vidpid: "VID:PID:",

  // actions
  title_bootstrap: "Bir cihazı U-Boot yazma komut istemine önyükleyin",
  btn_bootstrap: "Önyükle",
  title_write: "Cihazın flash belleğine bir firmware .bin dosyası yazın",
  btn_write: "Firmware Yaz",
  title_read: "Tüm flash içeriğini okuyup .bin dosyası olarak indirin",
  btn_read: "Firmware Oku",
  title_diag: "Salt okunur: eFuse, seri numarası, güvenli önyükleme durumu (bootrom)",
  btn_diag: "Bilgi",

  // advanced: custom SPL / U-Boot
  adv_toggle: "Gelişmiş: özel SPL / U-Boot",
  adv_desc: "İsteğe bağlı. Birlikte gelen imajlar yerine bir sonraki <strong>Önyükle</strong> işleminde kullanmak üzere kendi DFU uyumlu <strong>SPL</strong> ve <strong>U-Boot</strong> dosyalarınızı sağlayın. Her ikisi de gereklidir; SoC algılama atlanır.",
  btn_sel_spl: "SPL Seç",
  btn_sel_uboot: "U-Boot Seç",
  btn_clear: "Temizle",
  custom_spl_bundled: "SPL: birlikte gelen",
  custom_uboot_bundled: "U-Boot: birlikte gelen",
  progress_init: "Başlatılıyor...",

  // log
  log_title: "Günlük",

  // settings
  settings_title: "Ayarlar",
  settings_lang: "Dil",
  settings_backend: "Flash arka ucu",
  setting_dfu_label: "<strong>DFU</strong> - U-Boot DFU modu (varsayılan)",
  setting_remote_label: "Uzak daemon - dfu-remote (HTTP)",
  ph_remote_token: "kimlik doğrulama belirteci (isteğe bağlı)",
  remote_lna_note: "Chrome, yerel ağ erişimine izin vermeniz için bir kez soracaktır.",
  setting_help_label: 'Yardım ipuçlarını göster: balon görmek için herhangi bir kontrolün üzerine gelin (veya <i class="bi bi-question-lg"></i> düğmesini kullanın)',
  setting_debug_label: "Hata ayıklama günlüğü (ayrıntılı tanılama)",
  btn_cancel: "İptal",
  btn_save: "Kaydet",

  // diagnostics (Info) dialog
  diag_title: "Cihaz Bilgisi",
  title_close: "Kapat",
  btn_copy: "Kopyala",
  btn_close: "Kapat",

  // Windows driver help dialog
  win_title: "Windows Sürücü Kurulumu",
  win_intro: "Web flash aracının cihazla iletişim kurabilmesi için önce Windows'a bir WinUSB sürücüsü kurulmalıdır. Şu adımları izleyin:",
  win_step1: "Ingenic üretici USB sürücüsü kuruluysa, önce <strong>Aygıt Yöneticisi</strong> üzerinden kaldırın.",
  win_step2: "Cihazı USB önyükleme modunda bağlayın.",
  win_step3: '<a href="https://zadig.akeo.ie/" target="_blank" class="text-warning">Zadig <i class="bi bi-box-arrow-up-right" style="font-size:0.7rem"></i></a> uygulamasını indirin ve çalıştırın.',
  win_step4: "Zadig'de açılır menüden <strong>Ingenic USB Boot Device</strong> öğesini seçin.",
  win_step5: "Hedef sürücüyü <strong>WinUSB</strong> olarak ayarlayın ve <strong>Install</strong> (veya <strong>Replace Driver</strong>) düğmesine tıklayın.",
  win_step6: "Bu sayfaya dönün ve <strong>Cihaza Bağlan</strong> düğmesine tıklayın.",
  win_important: "<strong>Önemli:</strong> Ingenic üretici sürücüsü (<code>libusb0.sys</code>) uyumlu değildir ve Zadig ile WinUSB kurulmadan önce kaldırılmalıdır.",
  btn_got_it: "Anladım",

  // footer / misc
  browser_warning: "WebUSB için <strong>HTTPS</strong> (veya localhost) ve <strong>Chrome</strong> ya da <strong>Edge</strong> gerekir.",

  // help balloons (data-help keys)
  help_status_badge: "Geçerli durum: Boşta, Bağlanıyor, Önyükleniyor, Yazılıyor, Okunuyor, Hazır veya Hata. Flash aracının şu anda ne yaptığını gösterir.",
  help_mode_indicator: "Etkin arka uç. DFU = doğrudan bu tarayıcıdan WebUSB üzerinden flash yapar. Uzak = başka bir makinede çalışan bir dfu-remote daemon'ını kullanır. Ayarlar'dan değiştirin.",
  help_help_button: "Yardım modu. Açıkken, açıklayan bir balon görmek için herhangi bir kontrolün üzerine gelin. Kapatmak için tekrar tıklayın; siz isteyene kadar kapalı kalır.",
  help_settings_button: "Ayarlar: flash arka ucunu seçin (tarayıcı içi WebUSB veya uzak bir dfu-remote daemon'ı) ve bu yardım ipuçlarını açıp kapatın.",
  help_connect: "USB-boot (bootrom) modunda bekleyen bir cihaza bağlanır; a108:c309 olarak görünür. Burada bir şey yok mu? Cihaz muhtemelen henüz bootrom modunda değildir (boot pinini basılı tutun / kısa devre yapın, ardından güç verin).",
  help_bootstrap: "U-Boot'u USB üzerinden bootrom cihazına yükler; böylece bir DFU flash hedefi olur. Bunu bir kez yapın; sonrasında Yaz/Oku etkinleşir.",
  help_write: "Cihazın flash belleğine bir firmware .bin dosyası yazar. Cihazın önceden DFU moduna önyüklenmiş olması gerekir.",
  help_read: "Cihazın tüm flash içeriğini kaydedebileceğiniz bir .bin dosyasına geri okur. Yazmadan önce yedek almak için kullanışlıdır.",
  help_diag: "Yonganın eFuse'unun salt okunur dökümü: SoC, seri numarası ve güvenli önyükleme durumu. Hiçbir şeyi değiştirmez. Önyükleme yaptıktan sonra bile görüntülenebilir kalır (önbelleğe alınır).",
  help_advanced: "Gelişmiş. Birlikte gelen imajlar yerine bir sonraki Önyükle işleminde USB-boot için kullanmak üzere kendi DFU uyumlu SPL ve U-Boot dosyalarınızı sağlayın. Her ikisi de gereklidir ve SoC algılama atlanır.",
  help_sel_spl: "Kendi SPL (1. aşama) .bin dosyanızı seçin. Birlikte gelen yükleyici yerine, bir sonraki Önyükle için özel bir U-Boot ile birlikte kullanılır.",
  help_sel_uboot: "Kendi U-Boot .bin dosyanızı seçin. Bir sonraki Önyükle için özel SPL ile birlikte kullanılır.",
  help_clear_custom: "Özel SPL/U-Boot seçimini temizler ve algılanan SoC için birlikte gelen yükleyicilere geri döner.",
  help_log: "Etkinlik günlüğü: her adım, bayt sayısı, SHA-256 ve tüm hatalar burada görünür. Bir şey beklendiği gibi çalışmazsa önce buraya bakın.",
  help_setting_dfu: "DFU modu: doğrudan bu tarayıcıdan WebUSB üzerinden flash yapın, ek yazılım gerekmez, ancak cihazın BU bilgisayara takılı olması gerekir (yalnızca Chrome/Edge).",
  help_setting_remote: "Uzak mod: bu sayfa, cihazın takılı olduğu başka bir makinede çalışan bir dfu-remote daemon'ı ile iletişim kurar. Bir telefondan veya ağ üzerindeki başka bir makineden flash yapmak için idealdir.",
  help_remote_url: "dfu-remote daemon'ının adresi, örn. http://192.168.1.50:5050. Chrome, yerel ağ erişimine izin vermeniz için bir kez soracaktır.",
  help_remote_token: "İsteğe bağlı kimlik doğrulama belirteci; yalnızca daemon bir belirteçle başlatıldıysa gereklidir. Aksi halde boş bırakın.",
  help_setting_debug: "Etkinlik günlüğünde ayrıntılı tanılama; eski ?debug URL bayrağının yerini alır. Sorun gidermiyorsanız kapalı bırakın.",
  help_version: "thingino-dfu projesinin kaynak kodu, sürümleri ve belgeleri GitHub'da. CLI/daemon derlemelerini edinin veya buradan bir sorun bildirin.",
});
