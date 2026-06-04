# Thingino DFU proguard rules
# Keep JNI methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep the TdfuBridge callback interface
-keep class com.thingino.dfu.TdfuBridge$NativeCallback { *; }
-keep class com.thingino.dfu.TdfuBridge { *; }
