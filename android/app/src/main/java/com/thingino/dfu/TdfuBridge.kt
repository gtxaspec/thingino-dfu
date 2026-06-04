package com.thingino.dfu

import android.content.res.AssetManager

/**
 * JNI bridge to libtdfu native code.
 *
 * All native methods operate on a USB file descriptor obtained from
 * Android's UsbDeviceConnection. The fd is passed to libusb_wrap_sys_device()
 * on the native side.
 */
object TdfuBridge {

    init {
        System.loadLibrary("tdfu_jni")
    }

    /** Callback interface for log messages and progress updates from native code. */
    interface NativeCallback {
        fun onLog(message: String)
        fun onProgress(percent: Int, stage: String, message: String)
    }

    /**
     * Set the callback object that receives log and progress messages.
     * Must be called before any operation.
     */
    @JvmStatic
    external fun nativeSetCallback(callback: NativeCallback?)

    /**
     * Detect the SoC connected via USB.
     * @param fd USB device file descriptor from UsbDeviceConnection
     * @return SoC variant name (e.g., "t31x", "t40") or empty string on failure
     */
    @JvmStatic
    external fun nativeDetectSoc(fd: Int): String

    /**
     * Bootstrap a device: load DDR config, SPL, and U-Boot into SDRAM.
     * @param fd USB device file descriptor
     * @param variant SoC variant name (e.g., "t31x")
     * @param firmwareDir Cache directory for extracted firmware files
     * @param assetManager Android AssetManager for reading bundled firmware
     * @return 0 on success, negative error code on failure
     */
    @JvmStatic
    external fun nativeBootstrap(
        fd: Int,
        variant: String,
        firmwareDir: String,
        assetManager: AssetManager,
        useDfu: Boolean
    ): Int

    /**
     * Read firmware from device flash to a file.
     * Handles bootstrap automatically if device is in bootrom stage.
     * @param fd USB device file descriptor
     * @param variant SoC variant name
     * @param outputFile Path to save firmware dump
     * @param firmwareDir Cache directory for extracted firmware files
     * @param assetManager Android AssetManager
     * @return 0 on success, negative error code on failure
     */
    @JvmStatic
    external fun nativeReadFirmware(
        fd: Int,
        variant: String,
        outputFile: String,
        firmwareDir: String,
        assetManager: AssetManager,
        useDfu: Boolean
    ): Int

    /**
     * Write firmware from a file to device flash.
     * Handles bootstrap automatically if device is in bootrom stage.
     * @param fd USB device file descriptor
     * @param variant SoC variant name
     * @param inputFile Path to firmware file to write
     * @param firmwareDir Cache directory for extracted firmware files
     * @param assetManager Android AssetManager
     * @return 0 on success, negative error code on failure
     */
    @JvmStatic
    external fun nativeWriteFirmware(
        fd: Int,
        variant: String,
        inputFile: String,
        firmwareDir: String,
        assetManager: AssetManager,
        useDfu: Boolean
    ): Int

    /**
     * Enable or disable verbose debug logging in libtdfu.
     */
    @JvmStatic
    external fun nativeSetDebug(enabled: Boolean)
}
