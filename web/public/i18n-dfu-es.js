// Spanish (es) strings for the web flasher - mirrors i18n-dfu-en.js keys.
I18N.add("es", {
  // <head> / header
  app_title: "thingino · Flasher Web",
  header_full: " Thingino Flasher Web para Ingenic",
  header_short: " Thingino Flasher",
  title_mode: "Backend activo",
  title_help_toggle: "Alternar globos de ayuda",
  title_settings: "Ajustes",
  op_warning: "No desconecte el dispositivo ni salga de la página durante esta operación",

  // device / connect
  title_connect: "Conéctese a un dispositivo en modo bootrom USB",
  btn_connect: "Conectar dispositivo",
  windows_help_link: "¿Windows? Primero necesita un controlador",
  label_device: "Dispositivo:",
  label_soc: "SoC:",
  label_stage: "Etapa:",
  label_vidpid: "VID:PID:",

  // actions
  title_bootstrap: "Arranque un dispositivo hasta el prompt de grabación de U-Boot",
  btn_bootstrap: "Inicializar",
  title_write: "Escriba un archivo de firmware .bin en la memoria flash del dispositivo",
  btn_write: "Escribir firmware",
  title_read: "Lea todo el contenido de la flash y descárguelo como un archivo .bin",
  btn_read: "Leer firmware",
  title_diag: "Solo lectura: eFuse, número de serie, estado de arranque seguro (bootrom)",
  btn_diag: "Información",

  // advanced: custom SPL / U-Boot
  adv_toggle: "Avanzado: SPL / U-Boot personalizados",
  adv_desc: "Opcional. Proporcione su propio <strong>SPL</strong> y <strong>U-Boot</strong> compatibles con DFU para usarlos la próxima vez que pulse <strong>Inicializar</strong>, en lugar de las imágenes incluidas. Ambos son obligatorios; se omite la detección del SoC.",
  btn_sel_spl: "Seleccionar SPL",
  btn_sel_uboot: "Seleccionar U-Boot",
  btn_clear: "Borrar",
  custom_spl_bundled: "SPL: incluido",
  custom_uboot_bundled: "U-Boot: incluido",
  progress_init: "Inicializando...",

  // log
  log_title: "Registro",

  // settings
  settings_title: "Ajustes",
  settings_lang: "Idioma",
  settings_backend: "Backend de grabación",
  setting_dfu_label: "<strong>DFU</strong> - modo DFU de U-Boot (predeterminado)",
  setting_remote_label: "Daemon remoto - dfu-remote (HTTP)",
  ph_remote_token: "token de autenticación (opcional)",
  remote_lna_note: "Chrome le pedirá una vez permiso para acceder a la red local.",
  setting_help_label: 'Mostrar sugerencias de ayuda: pase el cursor sobre cualquier control para ver un globo (o use el botón <i class="bi bi-question-lg"></i>)',
  setting_debug_label: "Registro de depuración (diagnóstico detallado)",
  btn_cancel: "Cancelar",
  btn_save: "Guardar",

  // diagnostics (Info) dialog
  diag_title: "Información del dispositivo",
  title_close: "Cerrar",
  btn_copy: "Copiar",
  btn_close: "Cerrar",

  // Windows driver help dialog
  win_title: "Configuración del controlador de Windows",
  win_intro: "Windows necesita tener instalado un controlador WinUSB antes de que el flasher web pueda comunicarse con el dispositivo. Siga estos pasos:",
  win_step1: "Si el controlador USB del fabricante Ingenic está instalado, quítelo primero desde el <strong>Administrador de dispositivos</strong>.",
  win_step2: "Conecte el dispositivo en modo de arranque USB.",
  win_step3: 'Descargue y ejecute <a href="https://zadig.akeo.ie/" target="_blank" class="text-warning">Zadig <i class="bi bi-box-arrow-up-right" style="font-size:0.7rem"></i></a>.',
  win_step4: "En Zadig, seleccione <strong>Ingenic USB Boot Device</strong> en la lista desplegable.",
  win_step5: "Establezca el controlador de destino en <strong>WinUSB</strong> y haga clic en <strong>Install</strong> (o <strong>Replace Driver</strong>).",
  win_step6: "Vuelva a esta página y haga clic en <strong>Conectar dispositivo</strong>.",
  win_important: "<strong>Importante:</strong> El controlador del fabricante Ingenic (<code>libusb0.sys</code>) no es compatible y debe quitarse antes de instalar WinUSB mediante Zadig.",
  btn_got_it: "Entendido",

  // footer / misc
  browser_warning: "WebUSB requiere <strong>HTTPS</strong> (o localhost) y <strong>Chrome</strong> o <strong>Edge</strong>.",

  // help balloons (data-help keys)
  help_status_badge: "Estado actual: Inactivo, Conectando, Inicializando, Escribiendo, Leyendo, Listo o Error. Indica lo que está haciendo el flasher en este momento.",
  help_mode_indicator: "Backend activo. DFU = graba directamente desde este navegador mediante WebUSB. Remoto = controla un daemon dfu-remote en otra máquina. Cámbielo en Ajustes.",
  help_help_button: "Modo de ayuda. Mientras está activado, pase el cursor sobre cualquier control para ver un globo que lo explica. Haga clic de nuevo para desactivarlo; permanecerá desactivado hasta que lo vuelva a solicitar.",
  help_settings_button: "Ajustes: elija el backend de grabación (WebUSB en el navegador o un daemon dfu-remote remoto) y active o desactive estas sugerencias de ayuda.",
  help_connect: "Se conecta a un dispositivo que está en modo USB-boot (bootrom); se enumera como a108:c309. ¿No aparece nada? Probablemente el dispositivo aún no está en bootrom (mantenga su pin de arranque / cortocircuito y luego enciéndalo).",
  help_bootstrap: "Carga U-Boot en el dispositivo en bootrom a través de USB para que se convierta en un destino de grabación DFU. Hágalo una vez; después se activan Escribir/Leer.",
  help_write: "Escribe un firmware .bin en la flash del dispositivo. El dispositivo ya debe estar inicializado en modo DFU.",
  help_read: "Lee toda la flash del dispositivo y la guarda en un archivo .bin. Útil para hacer copias de seguridad antes de escribir.",
  help_diag: "Consulta de solo lectura del eFuse del chip: SoC, número de serie y estado de arranque seguro. No cambia nada. Permanece visible (en caché) incluso después de inicializar.",
  help_advanced: "Avanzado. Proporcione su propio SPL y U-Boot compatibles con DFU para arrancar por USB la próxima vez que pulse Inicializar, en lugar de las imágenes incluidas. Ambos son obligatorios y se omite la detección del SoC.",
  help_sel_spl: "Elija su propio SPL (stage1) .bin. Se usa junto con un U-Boot personalizado en el próximo Inicializar, en lugar del cargador incluido.",
  help_sel_uboot: "Elija su propio U-Boot .bin. Se usa junto con el SPL personalizado en el próximo Inicializar.",
  help_clear_custom: "Borra la selección de SPL/U-Boot personalizados y vuelve a los cargadores incluidos para el SoC detectado.",
  help_log: "Registro de actividad: cada paso, recuento de bytes, SHA-256 y cualquier error aparecen aquí. Revíselo primero si algo no funciona como se espera.",
  help_setting_dfu: "Modo DFU: graba directamente desde este navegador mediante WebUSB, sin software adicional, pero el dispositivo debe estar conectado a ESTE equipo (solo Chrome/Edge).",
  help_setting_remote: "Modo remoto: esta página se comunica con un daemon dfu-remote que se ejecuta en otra máquina que tiene el dispositivo conectado. Ideal para grabar desde un teléfono o un equipo en otro punto de la red.",
  help_remote_url: "La dirección del daemon dfu-remote, p. ej. http://192.168.1.50:5050. Chrome le pedirá una vez permiso para acceder a la red local.",
  help_remote_token: "Token de autenticación opcional, solo si el daemon se inició con uno. De lo contrario, déjelo en blanco.",
  help_setting_debug: "Diagnóstico detallado en el registro de actividad; reemplaza el antiguo parámetro ?debug de la URL. Déjelo desactivado salvo que esté solucionando problemas.",
  help_version: "Código fuente, versiones y documentación en GitHub del proyecto thingino-dfu. Consiga las compilaciones de la CLI/daemon o informe de un problema aquí.",
});
