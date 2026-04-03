package com.thingino.cloner

import android.content.Intent
import android.content.SharedPreferences
import android.hardware.usb.UsbDevice
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.text.method.ScrollingMovementMethod
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity(), UsbHelper.DeviceListener, ClonerBridge.NativeCallback {

    companion object {
        private const val TAG = "ThinginoCloner"
        private const val PREFS_NAME = "cloner_prefs"
        private const val PREF_HOST = "remote_host"
        private const val PREF_PORT = "remote_port"
    }

    private lateinit var usbHelper: UsbHelper
    private lateinit var prefs: SharedPreferences

    // UI elements
    private lateinit var statusText: TextView
    private lateinit var socText: TextView
    private lateinit var readButton: Button
    private lateinit var writeButton: Button
    private lateinit var progressBar: ProgressBar
    private lateinit var progressText: TextView
    private lateinit var logScroll: ScrollView
    private lateinit var logText: TextView
    private lateinit var deviceSpinner: Spinner

    // Mode selection UI
    private lateinit var modeRadioGroup: RadioGroup
    private lateinit var remoteInputRow: LinearLayout
    private lateinit var hostInput: EditText
    private lateinit var portInput: EditText
    private lateinit var connectButton: Button

    // State
    private var detectedSoc: String = ""
    private var operationRunning = false
    private var isRemoteMode = false
    private var remoteClient: RemoteClient? = null
    private var remoteDevices: List<RemoteClient.DeviceEntry> = emptyList()
    private var selectedDeviceIndex: Int = 0

    // File picker result
    private val filePickerLauncher = registerForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri != null) {
            startWriteOperation(uri)
        } else {
            appendLog("File selection cancelled\n")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)

        // Display version
        try {
            val versionName = packageManager.getPackageInfo(packageName, 0).versionName
            findViewById<android.widget.TextView>(R.id.versionText)?.text = "v$versionName"
        } catch (_: Exception) {}

        // Initialize UI
        statusText = findViewById(R.id.statusText)
        socText = findViewById(R.id.socText)
        readButton = findViewById(R.id.readButton)
        writeButton = findViewById(R.id.writeButton)
        progressBar = findViewById(R.id.progressBar)
        progressText = findViewById(R.id.progressText)
        logScroll = findViewById(R.id.logScroll)
        logText = findViewById(R.id.logText)
        deviceSpinner = findViewById(R.id.deviceSpinner)

        deviceSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, pos: Int, id: Long) {
                if (isRemoteMode && remoteDevices.isNotEmpty()) {
                    selectedDeviceIndex = pos
                    val dev = remoteDevices[pos]
                    detectedSoc = dev.variantName
                    socText.text = "SoC: ${dev.variantName.uppercase()} (${dev.stageName})"
                }
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }

        // Mode selection UI
        modeRadioGroup = findViewById(R.id.modeRadioGroup)
        remoteInputRow = findViewById(R.id.remoteInputRow)
        hostInput = findViewById(R.id.hostInput)
        portInput = findViewById(R.id.portInput)
        connectButton = findViewById(R.id.connectButton)

        // Restore saved host/port
        hostInput.setText(prefs.getString(PREF_HOST, ""))
        val savedPort = prefs.getInt(PREF_PORT, 0)
        if (savedPort > 0) portInput.setText(savedPort.toString())

        logText.movementMethod = ScrollingMovementMethod()

        readButton.setOnClickListener { startReadOperation() }
        writeButton.setOnClickListener { pickFileForWrite() }

        // Mode toggle
        modeRadioGroup.setOnCheckedChangeListener { _, checkedId ->
            isRemoteMode = checkedId == R.id.radioRemote
            remoteInputRow.visibility = if (isRemoteMode) View.VISIBLE else View.GONE
            deviceSpinner.visibility = View.GONE
            setButtonsEnabled(false)
            detectedSoc = ""
            socText.text = ""

            if (isRemoteMode) {
                updateStatus("Enter daemon host and connect")
                usbHelper.closeDevice()
            } else {
                disconnectRemote()
                updateStatus("No device connected")
                scanForDevices()
            }
        }

        connectButton.setOnClickListener {
            if (remoteClient?.isConnected() == true) {
                disconnectRemote()
            } else {
                connectRemote()
            }
        }

        // Disable buttons until device is connected
        setButtonsEnabled(false)

        // Set up USB
        usbHelper = UsbHelper(this)
        usbHelper.setListener(this)

        // Set up native callback
        ClonerBridge.nativeSetCallback(this)

        appendLog("Thingino Cloner ready.\n")
        appendLog("Connect a device via USB OTG or remote daemon.\n")

        // Check if launched from USB device attach intent
        handleUsbIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleUsbIntent(intent)
    }

    private fun handleUsbIntent(intent: Intent?) {
        if (isRemoteMode) return

        if (intent?.action == "android.hardware.usb.action.USB_DEVICE_ATTACHED") {
            val device = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra("device", UsbDevice::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra("device")
            }
            if (device != null && UsbHelper.isIngenicDevice(device)) {
                appendLog("Device connected via intent: ${usbHelper.getDeviceDescription(device)}\n")
                usbHelper.requestPermission(device)
                return
            }
        }
        scanForDevices()
    }

    override fun onResume() {
        super.onResume()
        usbHelper.register()
        if (!isRemoteMode && !usbHelper.isConnected()) {
            scanForDevices()
        }
    }

    override fun onPause() {
        super.onPause()
        usbHelper.unregister()
    }

    override fun onDestroy() {
        super.onDestroy()
        ClonerBridge.nativeSetCallback(null)
        usbHelper.closeDevice()
        remoteClient?.disconnect()
    }

    private fun scanForDevices() {
        if (isRemoteMode) return
        val device = usbHelper.findDevice()
        if (device != null) {
            updateStatus("Device found, requesting permission...")
            usbHelper.requestPermission(device)
        } else {
            updateStatus("No device connected")
            socText.text = ""
            setButtonsEnabled(false)
        }
    }

    // ======================================================================
    // Remote Mode
    // ======================================================================

    private fun connectRemote() {
        val host = hostInput.text.toString().trim()
        if (host.isEmpty()) {
            appendLog("ERROR: Enter a hostname or IP address\n")
            return
        }
        val port = portInput.text.toString().toIntOrNull() ?: RemoteClient.DEFAULT_PORT

        // Save for next time
        prefs.edit().putString(PREF_HOST, host).putInt(PREF_PORT, port).apply()

        connectButton.isEnabled = false
        appendLog("Connecting to $host:$port...\n")

        lifecycleScope.launch(Dispatchers.IO) {
            val client = RemoteClient(this@MainActivity)
            val connected = client.connect(host, port)

            if (!connected) {
                withContext(Dispatchers.Main) {
                    appendLog("Connection failed\n")
                    updateStatus("Connection failed")
                    connectButton.isEnabled = true
                }
                return@launch
            }

            withContext(Dispatchers.Main) {
                appendLog("Connected to daemon\n")
            }

            val devices = client.discover()

            withContext(Dispatchers.Main) {
                remoteClient = client
                remoteDevices = devices
                selectedDeviceIndex = 0
                connectButton.text = getString(R.string.btn_disconnect)
                connectButton.isEnabled = true

                if (devices.isEmpty()) {
                    updateStatus("Connected — no devices found")
                    socText.text = ""
                    deviceSpinner.visibility = View.GONE
                    appendLog("No Ingenic devices found on daemon\n")
                } else {
                    val dev = devices[0]
                    detectedSoc = dev.variantName
                    updateStatus("Connected — ${devices.size} device(s)")
                    socText.text = "SoC: ${dev.variantName.uppercase()} (${dev.stageName})"
                    setButtonsEnabled(true)
                    appendLog("Found ${devices.size} device(s):\n")
                    devices.forEachIndexed { i, d ->
                        appendLog("  [$i] $d\n")
                    }

                    if (devices.size > 1) {
                        val names = devices.map { it.toString() }
                        deviceSpinner.adapter = ArrayAdapter(
                            this@MainActivity,
                            android.R.layout.simple_spinner_dropdown_item,
                            names
                        )
                        deviceSpinner.visibility = View.VISIBLE
                    } else {
                        deviceSpinner.visibility = View.GONE
                    }
                }
            }
        }
    }

    private fun disconnectRemote() {
        remoteClient?.disconnect()
        remoteClient = null
        remoteDevices = emptyList()
        setButtonsEnabled(false)
        deviceSpinner.visibility = View.GONE
        connectButton.text = getString(R.string.btn_connect)
        connectButton.isEnabled = true
        if (isRemoteMode) {
            updateStatus("Disconnected")
            socText.text = ""
        }
    }

    // ======================================================================
    // UsbHelper.DeviceListener
    // ======================================================================

    override fun onDeviceAttached(device: UsbDevice) {
        if (isRemoteMode) return
        runOnUiThread {
            appendLog("Device attached: ${usbHelper.getDeviceDescription(device)}\n")
            updateStatus("Device attached, requesting permission...")
            usbHelper.requestPermission(device)
        }
    }

    override fun onDeviceDetached() {
        if (isRemoteMode) return
        runOnUiThread {
            appendLog("Device detached\n")
            updateStatus("No device connected")
            socText.text = ""
            detectedSoc = ""
            setButtonsEnabled(false)
        }
    }

    override fun onPermissionGranted(device: UsbDevice) {
        if (isRemoteMode) return
        runOnUiThread {
            appendLog("USB permission granted\n")
            updateStatus("Opening device...")

            val fd = usbHelper.openDevice(device)
            if (fd < 0) {
                appendLog("ERROR: Failed to open USB device\n")
                updateStatus("Failed to open device")
                return@runOnUiThread
            }

            appendLog("Device opened (fd=$fd)\n")
            updateStatus("Connected: ${usbHelper.getDeviceDescription(device)}")
            detectSoc(fd)
        }
    }

    override fun onPermissionDenied(device: UsbDevice) {
        runOnUiThread {
            appendLog("USB permission denied\n")
            updateStatus("Permission denied")
        }
    }

    // ======================================================================
    // ClonerBridge.NativeCallback (called from native or remote thread)
    // ======================================================================

    override fun onLog(message: String) {
        runOnUiThread {
            appendLog(message)
        }
    }

    override fun onProgress(percent: Int, stage: String, message: String) {
        runOnUiThread {
            progressBar.progress = percent
            progressText.text = "[$stage] $message ($percent%)"
        }
    }

    // ======================================================================
    // SoC Detection (local USB only)
    // ======================================================================

    private fun detectSoc(fd: Int) {
        lifecycleScope.launch(Dispatchers.IO) {
            val soc = ClonerBridge.nativeDetectSoc(fd)

            withContext(Dispatchers.Main) {
                if (soc.isNotEmpty()) {
                    detectedSoc = soc
                    socText.text = "SoC: ${soc.uppercase()}"
                    setButtonsEnabled(true)
                    appendLog("SoC detected: ${soc.uppercase()}\n")
                } else {
                    socText.text = "SoC: unknown"
                    detectedSoc = "t31x"
                    setButtonsEnabled(true)
                    appendLog("SoC detection failed, defaulting to T31X\n")
                }
            }
        }
    }

    // ======================================================================
    // Read Operation
    // ======================================================================

    private fun startReadOperation() {
        if (operationRunning) return

        if (isRemoteMode) {
            startRemoteReadOperation()
            return
        }

        val fd = usbHelper.getFileDescriptor()
        if (fd < 0) {
            appendLog("ERROR: No USB device connected\n")
            return
        }

        val timestamp = SimpleDateFormat("yyyyMMdd_HHmm", Locale.US).format(Date())
        val filename = "firmware_${detectedSoc.uppercase()}_${timestamp}.bin"
        val downloadsDir = Environment.getExternalStoragePublicDirectory(
            Environment.DIRECTORY_DOWNLOADS
        )
        downloadsDir.mkdirs()
        val outputFile = File(downloadsDir, filename)

        operationRunning = true
        setButtonsEnabled(false)
        progressBar.visibility = View.VISIBLE
        progressText.visibility = View.VISIBLE
        progressBar.progress = 0

        appendLog("\n--- READ OPERATION ---\n")
        appendLog("Output: ${outputFile.absolutePath}\n")

        val cacheDir = cacheDir.absolutePath
        val assetManager = assets

        lifecycleScope.launch(Dispatchers.IO) {
            val newFd = withContext(Dispatchers.Main) {
                val device = usbHelper.getDevice()
                if (device != null) {
                    usbHelper.closeDevice()
                    usbHelper.openDevice(device)
                } else -1
            }

            if (newFd < 0) {
                withContext(Dispatchers.Main) {
                    appendLog("ERROR: Failed to reopen device\n")
                    finishOperation()
                }
                return@launch
            }

            val result = ClonerBridge.nativeReadFirmware(
                newFd, detectedSoc, outputFile.absolutePath, cacheDir, assetManager
            )

            withContext(Dispatchers.Main) {
                if (result == 0) {
                    appendLog("Read complete: ${outputFile.name}\n")
                    updateStatus("Read complete!")
                } else {
                    appendLog("Read failed (error $result)\n")
                    updateStatus("Read failed")
                }
                finishOperation()
            }
        }
    }

    private fun startRemoteReadOperation() {
        val client = remoteClient ?: return

        operationRunning = true
        setButtonsEnabled(false)
        progressBar.visibility = View.VISIBLE
        progressText.visibility = View.VISIBLE
        progressBar.progress = 0

        val timestamp = SimpleDateFormat("yyyyMMdd_HHmm", Locale.US).format(Date())
        val filename = "firmware_${detectedSoc.uppercase()}_${timestamp}.bin"
        val downloadsDir = Environment.getExternalStoragePublicDirectory(
            Environment.DIRECTORY_DOWNLOADS
        )
        downloadsDir.mkdirs()
        val outputFile = File(downloadsDir, filename)

        appendLog("\n--- REMOTE READ OPERATION ---\n")
        appendLog("Output: ${outputFile.absolutePath}\n")

        lifecycleScope.launch(Dispatchers.IO) {
            val data = client.readFirmware(selectedDeviceIndex, detectedSoc)

            withContext(Dispatchers.Main) {
                if (data != null) {
                    outputFile.writeBytes(data)
                    appendLog("Read complete: ${outputFile.name} (${data.size / 1024} KB)\n")
                    updateStatus("Read complete!")
                } else {
                    appendLog("Read failed\n")
                    updateStatus("Read failed")
                }
                finishOperation()
            }
        }
    }

    // ======================================================================
    // Write Operation
    // ======================================================================

    private fun pickFileForWrite() {
        if (operationRunning) return
        filePickerLauncher.launch("application/octet-stream")
    }

    private fun startWriteOperation(uri: Uri) {
        if (isRemoteMode) {
            startRemoteWriteOperation(uri)
            return
        }

        val fd = usbHelper.getFileDescriptor()
        if (fd < 0) {
            appendLog("ERROR: No USB device connected\n")
            return
        }

        operationRunning = true
        setButtonsEnabled(false)
        progressBar.visibility = View.VISIBLE
        progressText.visibility = View.VISIBLE
        progressBar.progress = 0

        appendLog("\n--- WRITE OPERATION ---\n")
        appendLog("Source: $uri\n")

        val cacheDir = cacheDir.absolutePath
        val assetManager = assets

        lifecycleScope.launch(Dispatchers.IO) {
            val tempFile = File(cacheDir, "firmware_write_temp.bin")
            try {
                contentResolver.openInputStream(uri)?.use { input ->
                    FileOutputStream(tempFile).use { output ->
                        input.copyTo(output, bufferSize = 65536)
                    }
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    appendLog("ERROR: Failed to read file: ${e.message}\n")
                    finishOperation()
                }
                return@launch
            }

            val fileSize = tempFile.length()
            withContext(Dispatchers.Main) {
                appendLog("Firmware file: ${fileSize / 1024} KB\n")
            }

            val newFd = withContext(Dispatchers.Main) {
                val device = usbHelper.getDevice()
                if (device != null) {
                    usbHelper.closeDevice()
                    usbHelper.openDevice(device)
                } else -1
            }

            if (newFd < 0) {
                withContext(Dispatchers.Main) {
                    appendLog("ERROR: Failed to reopen device\n")
                    finishOperation()
                }
                tempFile.delete()
                return@launch
            }

            val result = ClonerBridge.nativeWriteFirmware(
                newFd, detectedSoc, tempFile.absolutePath, cacheDir, assetManager
            )

            tempFile.delete()

            withContext(Dispatchers.Main) {
                if (result == 0) {
                    appendLog("Write complete!\n")
                    updateStatus("Write complete!")
                } else {
                    appendLog("Write failed (error $result)\n")
                    updateStatus("Write failed")
                }
                finishOperation()
            }
        }
    }

    private fun startRemoteWriteOperation(uri: Uri) {
        val client = remoteClient ?: return

        operationRunning = true
        setButtonsEnabled(false)
        progressBar.visibility = View.VISIBLE
        progressText.visibility = View.VISIBLE
        progressBar.progress = 0

        appendLog("\n--- REMOTE WRITE OPERATION ---\n")

        lifecycleScope.launch(Dispatchers.IO) {
            val firmwareData = try {
                contentResolver.openInputStream(uri)?.use { it.readBytes() }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    appendLog("ERROR: Failed to read file: ${e.message}\n")
                    finishOperation()
                }
                return@launch
            }

            if (firmwareData == null) {
                withContext(Dispatchers.Main) {
                    appendLog("ERROR: Could not open file\n")
                    finishOperation()
                }
                return@launch
            }

            withContext(Dispatchers.Main) {
                appendLog("Firmware file: ${firmwareData.size / 1024} KB\n")
            }

            val result = client.writeFirmware(selectedDeviceIndex, detectedSoc, firmwareData)

            withContext(Dispatchers.Main) {
                if (result) {
                    appendLog("Write complete!\n")
                    updateStatus("Write complete!")
                } else {
                    appendLog("Write failed\n")
                    updateStatus("Write failed")
                }
                finishOperation()
            }
        }
    }

    // ======================================================================
    // UI Helpers
    // ======================================================================

    private fun finishOperation() {
        operationRunning = false
        progressBar.visibility = View.GONE
        progressText.visibility = View.GONE
        if (isRemoteMode) {
            if (remoteClient?.isConnected() == true) setButtonsEnabled(true)
        } else {
            if (usbHelper.isConnected()) setButtonsEnabled(true)
        }
    }

    private fun setButtonsEnabled(enabled: Boolean) {
        readButton.isEnabled = enabled
        writeButton.isEnabled = enabled
        readButton.alpha = if (enabled) 1.0f else 0.5f
        writeButton.alpha = if (enabled) 1.0f else 0.5f
    }

    private fun updateStatus(text: String) {
        statusText.text = text
    }

    private fun appendLog(text: String) {
        logText.append(text)
        logScroll.post {
            logScroll.fullScroll(View.FOCUS_DOWN)
        }
    }
}
