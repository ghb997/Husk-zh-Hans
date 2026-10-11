package android.preference;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.XmlResourceParser;
import android.util.AttributeSet;
import android.util.Xml;
import java.lang.reflect.Constructor;
import java.util.ArrayList;
import org.xmlpull.v1.XmlPullParser;

/** A settings screen's preferences: the SharedPreferences they live in, their tree (inflated from res/xml), and their defaults. */
@Deprecated
public class PreferenceManager {
    public static final String KEY_HAS_SET_DEFAULT_VALUES = "_has_set_default_values";
    public static final String METADATA_KEY_PREFERENCES = "android.preference";

    public interface OnActivityDestroyListener { void onActivityDestroy(); }
    public interface OnActivityResultListener { boolean onActivityResult(int requestCode, int resultCode, Intent data); }
    public interface OnActivityStopListener { void onActivityStop(); }
    public interface OnPreferenceTreeClickListener { boolean onPreferenceTreeClick(PreferenceScreen screen, Preference preference); }

    private final Context mContext;
    private Activity mActivity;
    private String mName;
    private int mMode;
    private SharedPreferences mPrefs;
    private PreferenceScreen mScreen;
    private PreferenceDataStore mDataStore;
    private OnPreferenceTreeClickListener mTreeClick;
    private final ArrayList<OnActivityStopListener> mStopListeners = new ArrayList<>();
    private boolean mNoCommit;

    public PreferenceManager(Activity activity, int firstRequestCode) { this((Context) activity); mActivity = activity; }
    PreferenceManager(Context context) { mContext = context; mName = getDefaultSharedPreferencesName(context); }
    PreferenceManager() { this((Activity) null, 0); }

    public static SharedPreferences getDefaultSharedPreferences(Context c) { return c.getSharedPreferences(getDefaultSharedPreferencesName(c), Context.MODE_PRIVATE); }
    public static String getDefaultSharedPreferencesName(Context c) { return c.getPackageName() + "_preferences"; }
    public static void setDefaultValues(Context c, int resId, boolean readAgain) { setDefaultValues(c, getDefaultSharedPreferencesName(c), Context.MODE_PRIVATE, resId, readAgain); }
    /** The defaults a preferences file gives, written where no value is yet (once, unless readAgain). */
    public static void setDefaultValues(Context c, String name, int mode, int resId, boolean readAgain) {
        SharedPreferences done = c.getSharedPreferences(KEY_HAS_SET_DEFAULT_VALUES, Context.MODE_PRIVATE);
        if (!readAgain && done.getBoolean(KEY_HAS_SET_DEFAULT_VALUES, false)) return;
        PreferenceManager m = new PreferenceManager(c);
        m.setSharedPreferencesName(name); m.setSharedPreferencesMode(mode);
        m.inflateFromResource(c, resId, null);
        done.edit().putBoolean(KEY_HAS_SET_DEFAULT_VALUES, true).apply();
    }

    public SharedPreferences getSharedPreferences() {
        if (mPrefs == null) mPrefs = (mContext != null ? mContext : android.app.Application.huskGet()).getSharedPreferences(mName, mMode);
        return mPrefs;
    }
    public void setSharedPreferencesName(String name) { mName = name; mPrefs = null; }
    public String getSharedPreferencesName() { return mName; }
    public void setSharedPreferencesMode(int mode) { mMode = mode; mPrefs = null; }
    public int getSharedPreferencesMode() { return mMode; }
    public PreferenceDataStore getPreferenceDataStore() { return mDataStore; }
    public void setPreferenceDataStore(PreferenceDataStore s) { mDataStore = s; }
    public void setStorageDefault() {} public void setStorageDeviceProtected() {} public void setStorageCredentialProtected() {}
    public boolean isStorageDefault() { return true; } public boolean isStorageDeviceProtected() { return false; } public boolean isStorageCredentialProtected() { return false; }

    public PreferenceScreen createPreferenceScreen(Context c) {
        PreferenceScreen s = new PreferenceScreen(c, null);
        s.onAttachedToHierarchy(this);
        return s;
    }
    public Preference findPreference(CharSequence key) { return mScreen != null ? mScreen.findPreference(key) : null; }
    PreferenceScreen huskScreen() { return mScreen; }
    boolean huskSetScreen(PreferenceScreen s) { if (s == mScreen) return false; mScreen = s; return true; }
    void huskSetTreeClickListener(OnPreferenceTreeClickListener l) { mTreeClick = l; }
    boolean huskTreeClick(PreferenceScreen screen, Preference p) { return mTreeClick != null && mTreeClick.onPreferenceTreeClick(screen, p); }
    public void registerOnActivityStopListener(OnActivityStopListener l) { if (!mStopListeners.contains(l)) mStopListeners.add(l); }
    public void unregisterOnActivityStopListener(OnActivityStopListener l) { mStopListeners.remove(l); }
    void huskActivityStop() { for (OnActivityStopListener l : new ArrayList<>(mStopListeners)) l.onActivityStop(); }

    /* ---- a preferences file (res/xml): <PreferenceScreen>, <PreferenceCategory>, the platform's preference tags by name, an app's
       own by class name, <intent> and <extra> inside them */
    public PreferenceScreen inflateFromResource(Context c, int resId, PreferenceScreen root) {
        try (XmlResourceParser p = c.getResources().getXml(resId)) {
            AttributeSet attrs = Xml.asAttributeSet(p);
            int t;
            while ((t = p.next()) != XmlPullParser.START_TAG && t != XmlPullParser.END_DOCUMENT) {}
            if (t != XmlPullParser.START_TAG) throw new android.view.InflateException("no preferences in resource " + Integer.toHexString(resId));
            Preference top = root != null ? root : create(c, p.getName(), attrs);
            if (!(top instanceof PreferenceScreen)) throw new android.view.InflateException("a preferences file starts with a PreferenceScreen, not " + p.getName());
            PreferenceScreen screen = (PreferenceScreen) top;
            screen.onAttachedToHierarchy(this);
            children(c, p, attrs, screen);
            return screen;
        } catch (android.view.InflateException e) { throw e; }
        catch (Exception e) { throw new android.view.InflateException("preferences " + Integer.toHexString(resId) + ": " + e, e); }
    }
    private void children(Context c, XmlPullParser p, AttributeSet attrs, Preference parent) throws Exception {
        int depth = p.getDepth(), t;
        while (((t = p.next()) != XmlPullParser.END_TAG || p.getDepth() > depth) && t != XmlPullParser.END_DOCUMENT) {
            if (t != XmlPullParser.START_TAG) continue;
            String name = p.getName();
            if ("intent".equals(name)) { parent.setIntent(intent(p, attrs)); skip(p); continue; }
            if ("extra".equals(name)) {
                String k = attrs.getAttributeValue("http://schemas.android.com/apk/res/android", "name");
                String v = attrs.getAttributeValue("http://schemas.android.com/apk/res/android", "value");
                if (k != null) parent.getExtras().putString(k, v);
                skip(p); continue;
            }
            Preference child = create(c, name, attrs);
            if (parent instanceof PreferenceGroup) ((PreferenceGroup) parent).addItemFromInflater(child);
            else throw new android.view.InflateException(name + " inside " + parent.getClass().getName() + ", which is not a group");
            children(c, p, attrs, child);
        }
    }
    private static void skip(XmlPullParser p) throws Exception {
        int depth = p.getDepth(), t;
        while (((t = p.next()) != XmlPullParser.END_TAG || p.getDepth() > depth) && t != XmlPullParser.END_DOCUMENT) {}
    }
    private static Intent intent(XmlPullParser p, AttributeSet a) {
        String ns = "http://schemas.android.com/apk/res/android";
        Intent i = new Intent(a.getAttributeValue(ns, "action"));
        String pkg = a.getAttributeValue(ns, "targetPackage"), cls = a.getAttributeValue(ns, "targetClass"), data = a.getAttributeValue(ns, "data");
        if (pkg != null && cls != null) i.setClassName(pkg, cls);
        if (data != null) i.setData(android.net.Uri.parse(data));
        return i;
    }
    private static Preference create(Context c, String name, AttributeSet attrs) throws Exception {
        String cls = name.indexOf('.') >= 0 ? name : "android.preference." + name;
        if ("android.preference.PreferenceScreen".equals(cls)) return new PreferenceScreen(c, attrs);
        Class<?> k = Class.forName(cls, true, c.getClassLoader());
        Constructor<?> ctor = k.getConstructor(Context.class, AttributeSet.class);
        return (Preference) ctor.newInstance(c, attrs);
    }
}
