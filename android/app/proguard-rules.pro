# Thingino DFU proguard rules
# Keep JNI methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep the ClonerBridge callback interface
-keep class com.thingino.dfu.ClonerBridge$NativeCallback { *; }
-keep class com.thingino.dfu.ClonerBridge { *; }
