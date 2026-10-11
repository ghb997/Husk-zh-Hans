package org.chromium.support_lib_glue;

/** Where androidx.webkit looks for the WebView's support-library glue (through WebView.getWebViewClassLoader()). */
public class SupportLibReflectionUtil {
    public static java.lang.reflect.InvocationHandler createWebViewProviderFactory() { return android.webkit.HuskSupportLib.factory(); }
}
