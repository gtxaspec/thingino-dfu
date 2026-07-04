// Arabic (ar) strings for the web flasher - mirrors i18n-dfu-en.js keys.
// Other languages (i18n-dfu-<lang>.js) mirror these keys; missing ones fall back
// to English, then to the key itself. Keep {placeholders} and HTML tags intact
// when translating. Strings used via data-i18n-html may contain <strong>/<a>/<code>.
I18N.add("ar", {
  // <head> / header
  app_title: "thingino · أداة الفلاش عبر الويب",
  header_full: " أداة Thingino للفلاش عبر الويب لأجهزة Ingenic",
  header_short: " أداة Thingino للفلاش",
  title_mode: "الواجهة الخلفية النشطة",
  title_help_toggle: "تبديل فقاعات المساعدة",
  title_settings: "الإعدادات",
  op_warning: "لا تفصل الجهاز ولا تغادر الصفحة أثناء هذه العملية",

  // device / connect
  title_connect: "الاتصال بجهاز في وضع bootrom عبر USB",
  btn_connect: "توصيل الجهاز",
  windows_help_link: "تستخدم Windows؟ تحتاج إلى برنامج تشغيل أولاً",
  label_device: "الجهاز:",
  label_soc: "SoC:",
  label_stage: "المرحلة:",
  label_vidpid: "VID:PID:",

  // actions
  title_bootstrap: "إقلاع الجهاز إلى موجّه الحرق في U-Boot",
  btn_bootstrap: "تمهيد",
  title_write: "كتابة ملف برنامج ثابت بصيغة .bin إلى ذاكرة فلاش الجهاز",
  btn_write: "كتابة البرنامج الثابت",
  title_read: "قراءة كامل محتوى الفلاش وتنزيله كملف .bin",
  btn_read: "قراءة البرنامج الثابت",
  title_diag: "للقراءة فقط: eFuse، والرقم التسلسلي، وحالة الإقلاع الآمن (bootrom)",
  btn_diag: "معلومات",

  // advanced: custom SPL / U-Boot
  adv_toggle: "خيارات متقدمة: SPL / U-Boot مخصّص",
  adv_desc: "اختياري. قدّم نسختك الخاصة من <strong>SPL</strong> و<strong>U-Boot</strong> الداعمين لوضع DFU لاستخدامهما في <strong>التمهيد</strong> التالي بدلاً من الصور المضمّنة. كلاهما مطلوب، ويُتخطّى اكتشاف الـ SoC.",
  btn_sel_spl: "اختيار SPL",
  btn_sel_uboot: "اختيار U-Boot",
  btn_clear: "مسح",
  custom_spl_bundled: "SPL: مضمّن",
  custom_uboot_bundled: "U-Boot: مضمّن",
  progress_init: "جارٍ التهيئة...",

  // log
  log_title: "السجل",

  // settings
  settings_title: "الإعدادات",
  settings_lang: "اللغة",
  settings_backend: "الواجهة الخلفية للفلاش",
  setting_dfu_label: "<strong>DFU</strong> - وضع DFU عبر U-Boot (الافتراضي)",
  setting_remote_label: "خدمة بعيدة - dfu-remote (HTTP)",
  ph_remote_token: "رمز المصادقة (اختياري)",
  remote_lna_note: "سيطلب Chrome منك مرة واحدة السماح بالوصول إلى الشبكة المحلية.",
  setting_help_label: 'إظهار تلميحات المساعدة: مرّر المؤشر فوق أي عنصر لعرض فقاعة توضيحية (أو استخدم زر <i class="bi bi-question-lg"></i>)',
  setting_debug_label: "تسجيل تصحيح الأخطاء (تشخيصات مفصّلة)",
  btn_cancel: "إلغاء",
  btn_save: "حفظ",

  // diagnostics (Info) dialog
  diag_title: "معلومات الجهاز",
  title_close: "إغلاق",
  btn_copy: "نسخ",
  btn_close: "إغلاق",

  // Windows driver help dialog
  win_title: "إعداد برنامج تشغيل Windows",
  win_intro: "تحتاج Windows إلى تثبيت برنامج تشغيل WinUSB قبل أن تتمكّن أداة الفلاش عبر الويب من التواصل مع الجهاز. اتبع هذه الخطوات:",
  win_step1: "إذا كان برنامج تشغيل USB من الشركة المصنّعة Ingenic مثبّتاً، فأزِله أولاً عبر <strong>إدارة الأجهزة</strong>.",
  win_step2: "وصّل الجهاز في وضع الإقلاع عبر USB.",
  win_step3: 'نزّل وشغّل <a href="https://zadig.akeo.ie/" target="_blank" class="text-warning">Zadig <i class="bi bi-box-arrow-up-right" style="font-size:0.7rem"></i></a>.',
  win_step4: "في Zadig، اختر <strong>Ingenic USB Boot Device</strong> من القائمة المنسدلة.",
  win_step5: "اضبط برنامج التشغيل الهدف على <strong>WinUSB</strong> وانقر <strong>Install</strong> (أو <strong>Replace Driver</strong>).",
  win_step6: "عُد إلى هذه الصفحة وانقر <strong>توصيل الجهاز</strong>.",
  win_important: "<strong>مهم:</strong> برنامج تشغيل الشركة المصنّعة Ingenic (<code>libusb0.sys</code>) غير متوافق ويجب إزالته قبل تثبيت WinUSB عبر Zadig.",
  btn_got_it: "فهمت",

  // footer / misc
  browser_warning: "يتطلّب WebUSB اتصال <strong>HTTPS</strong> (أو localhost) ومتصفّح <strong>Chrome</strong> أو <strong>Edge</strong>.",

  // help balloons (data-help keys)
  help_status_badge: "الحالة الحالية: خامل، أو جارٍ الاتصال، أو جارٍ التمهيد، أو جارٍ الكتابة، أو جارٍ القراءة، أو جاهز، أو خطأ. تُظهر ما تفعله أداة الفلاش في هذه اللحظة.",
  help_mode_indicator: "الواجهة الخلفية النشطة. DFU = الفلاش مباشرةً من هذا المتصفّح عبر WebUSB. الوضع البعيد = التحكّم في خدمة dfu-remote على جهاز آخر. بدّلها من الإعدادات.",
  help_help_button: "وضع المساعدة. أثناء تشغيله، مرّر المؤشر فوق أي عنصر لعرض فقاعة تشرحه. انقر مرة أخرى لإيقافه، وسيبقى متوقّفاً حتى تطلبه مجدّداً.",
  help_settings_button: "الإعدادات: اختر الواجهة الخلفية للفلاش (WebUSB داخل المتصفّح أو خدمة dfu-remote بعيدة) وبدّل تلميحات المساعدة هذه.",
  help_connect: "يتّصل بجهاز في وضع الإقلاع عبر USB (bootrom)، ويظهر بالمعرّف a108:c309. لا شيء هنا؟ على الأرجح أن الجهاز ليس في وضع bootrom بعد (اضغط مطوّلاً على دبّوس الإقلاع أو نفّذ قصراً، ثم شغّل الطاقة).",
  help_bootstrap: "يحمّل U-Boot إلى الجهاز في وضع bootrom عبر USB ليصبح هدفاً للفلاش عبر DFU. نفّذ هذا مرة واحدة، وبعدها يُفعّل زرّا الكتابة/القراءة.",
  help_write: "يكتب ملف برنامج ثابت بصيغة .bin إلى فلاش الجهاز. يجب أن يكون الجهاز قد جرى تمهيده مسبقاً إلى وضع DFU.",
  help_read: "يقرأ محتوى فلاش الجهاز بالكامل إلى ملف .bin يمكنك حفظه. مفيد لأخذ نسخة احتياطية قبل الكتابة.",
  help_diag: "قراءة فقط لبيانات eFuse في الشريحة: SoC، والرقم التسلسلي، وحالة الإقلاع الآمن. لا يغيّر أي شيء. يبقى قابلاً للعرض (مخزّن مؤقتاً) حتى بعد التمهيد.",
  help_advanced: "خيارات متقدمة. قدّم نسختك الخاصة من SPL وU-Boot الداعمين لوضع DFU للإقلاع عبر USB في التمهيد التالي بدلاً من الصور المضمّنة. كلاهما مطلوب، ويُتخطّى اكتشاف الـ SoC.",
  help_sel_spl: "اختر ملف SPL (المرحلة الأولى) بصيغة .bin الخاص بك. يُستخدم مع U-Boot مخصّص في التمهيد التالي بدلاً من المُحمّل المضمّن.",
  help_sel_uboot: "اختر ملف U-Boot بصيغة .bin الخاص بك. يُستخدم مع SPL المخصّص في التمهيد التالي.",
  help_clear_custom: "يمسح اختيار SPL/U-Boot المخصّص ويعود إلى المُحمّلات المضمّنة الخاصة بالـ SoC المكتشَف.",
  help_log: "سجل النشاط: تظهر هنا كل خطوة وعدد البايتات وقيمة SHA-256 وأي أخطاء. راجعه أولاً إذا لم يعمل شيء كما هو متوقّع.",
  help_setting_dfu: "وضع DFU: الفلاش مباشرةً من هذا المتصفّح عبر WebUSB، دون برامج إضافية، لكن يجب أن يكون الجهاز موصولاً بهذا الحاسوب تحديداً (Chrome/Edge فقط).",
  help_setting_remote: "الوضع البعيد: تتواصل هذه الصفحة مع خدمة dfu-remote تعمل على جهاز آخر يكون الجهاز موصولاً به. مفيد للفلاش من هاتف أو جهاز عبر الشبكة.",
  help_remote_url: "عنوان خدمة dfu-remote، مثل http://192.168.1.50:5050. سيطلب Chrome مرة واحدة السماح بالوصول إلى الشبكة المحلية.",
  help_remote_token: "رمز مصادقة اختياري، فقط إذا شُغّلت الخدمة برمزٍ ما. اتركه فارغاً فيما عدا ذلك.",
  help_setting_debug: "تشخيصات مفصّلة في سجل النشاط، تحلّ محلّ معامل ?debug القديم في الرابط. اتركه متوقّفاً ما لم تكن تستكشف مشكلة.",
  help_version: "الشيفرة المصدرية والإصدارات والوثائق على GitHub لمشروع thingino-dfu. احصل على نسخ CLI/الخدمة أو أبلغ عن مشكلة هنا.",
});
