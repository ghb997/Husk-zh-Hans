package android.preference;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.TypedArray;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.Set;

/** The platform's settings item (android.preference, which apps' own settings screens and game launchers still use): a key in the
 *  app's SharedPreferences, shown as a row with a title, a summary and a widget at its end. */
@Deprecated
public class Preference implements Comparable<Preference> {
    public static final int DEFAULT_ORDER = Integer.MAX_VALUE;

    public interface OnPreferenceChangeListener { boolean onPreferenceChange(Preference preference, Object newValue); }
    public interface OnPreferenceClickListener { boolean onPreferenceClick(Preference preference); }

    private final Context mContext;
    PreferenceManager mManager;
    PreferenceGroup mParent;
    private String mKey, mDependencyKey, mFragment;
    private CharSequence mTitle, mSummary;
    private int mTitleRes, mIconRes, mOrder = DEFAULT_ORDER, mLayoutRes, mWidgetLayoutRes;
    private Drawable mIcon;
    private Intent mIntent;
    private Object mDefaultValue;
    private boolean mEnabled = true, mSelectable = true, mPersistent = true, mDependencyMet = true, mParentDependencyMet = true, mShouldDisableView = true;
    private boolean mVisible = true, mIconSpaceReserved, mSingleLineTitle = true;
    private OnPreferenceChangeListener mChangeListener;
    private OnPreferenceClickListener mClickListener;
    private Bundle mExtras;
    private final ArrayList<Preference> mDependents = new ArrayList<>();

    static final int[] ATTRS = { android.R.attr.icon, android.R.attr.key, android.R.attr.title, android.R.attr.summary, android.R.attr.order,
        android.R.attr.fragment, android.R.attr.layout, android.R.attr.widgetLayout, android.R.attr.enabled, android.R.attr.selectable,
        android.R.attr.dependency, android.R.attr.persistent, android.R.attr.defaultValue, android.R.attr.shouldDisableView };

    public Preference(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        mContext = context;
        if (attrs == null) return;
        TypedArray a = context.obtainStyledAttributes(attrs, sorted(ATTRS), defStyleAttr, defStyleRes);
        try {
            mIconRes = a.getResourceId(idx(android.R.attr.icon), 0);
            mKey = a.getString(idx(android.R.attr.key));
            mTitleRes = a.getResourceId(idx(android.R.attr.title), 0);
            mTitle = a.getText(idx(android.R.attr.title));
            mSummary = a.getText(idx(android.R.attr.summary));
            mOrder = a.getInt(idx(android.R.attr.order), mOrder);
            mFragment = a.getString(idx(android.R.attr.fragment));
            mLayoutRes = a.getResourceId(idx(android.R.attr.layout), 0);
            mWidgetLayoutRes = a.getResourceId(idx(android.R.attr.widgetLayout), 0);
            mEnabled = a.getBoolean(idx(android.R.attr.enabled), true);
            mSelectable = a.getBoolean(idx(android.R.attr.selectable), true);
            mDependencyKey = a.getString(idx(android.R.attr.dependency));
            mPersistent = a.getBoolean(idx(android.R.attr.persistent), true);
            mShouldDisableView = a.getBoolean(idx(android.R.attr.shouldDisableView), true);
            if (a.hasValue(idx(android.R.attr.defaultValue))) mDefaultValue = onGetDefaultValue(a, idx(android.R.attr.defaultValue));
        } finally { a.recycle(); }
    }
    public Preference(Context context, AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public Preference(Context context, AttributeSet attrs) { this(context, attrs, 0, 0); }
    public Preference(Context context) { this(context, null, 0, 0); }

    /* obtainStyledAttributes wants the ids sorted; the array's index of an attribute in that order */
    private static int[] sSorted;
    static synchronized int[] sorted(int[] attrs) { if (sSorted == null) { sSorted = attrs.clone(); java.util.Arrays.sort(sSorted); } return sSorted; }
    static int idx(int attr) { return java.util.Arrays.binarySearch(sorted(ATTRS), attr); }

    protected Object onGetDefaultValue(TypedArray a, int index) { return null; }

    public Context getContext() { return mContext; }
    public PreferenceManager getPreferenceManager() { return mManager; }
    public SharedPreferences getSharedPreferences() { return mManager != null ? mManager.getSharedPreferences() : PreferenceManager.getDefaultSharedPreferences(mContext); }
    public SharedPreferences.Editor getEditor() { return getSharedPreferences().edit(); }
    public boolean shouldCommit() { return true; }
    public PreferenceDataStore getPreferenceDataStore() { return null; }
    public void setPreferenceDataStore(PreferenceDataStore s) {}

    public void setKey(String key) { mKey = key; }
    public String getKey() { return mKey; }
    public boolean hasKey() { return mKey != null && !mKey.isEmpty(); }
    public void setTitle(CharSequence t) { mTitle = t; mTitleRes = 0; notifyChanged(); }
    public void setTitle(int r) { mTitleRes = r; mTitle = mContext.getText(r); notifyChanged(); }
    public int getTitleRes() { return mTitleRes; }
    public CharSequence getTitle() { return mTitle; }
    public void setSummary(CharSequence s) { mSummary = s; notifyChanged(); }
    public void setSummary(int r) { setSummary(mContext.getText(r)); }
    public CharSequence getSummary() { return mSummary; }
    public void setIcon(Drawable d) { mIcon = d; notifyChanged(); }
    public void setIcon(int r) { mIconRes = r; mIcon = null; notifyChanged(); }
    public Drawable getIcon() { if (mIcon == null && mIconRes != 0) mIcon = mContext.getDrawable(mIconRes); return mIcon; }
    public void setOrder(int o) { mOrder = o; }
    public int getOrder() { return mOrder; }
    public void setFragment(String f) { mFragment = f; }
    public String getFragment() { return mFragment; }
    public void setIntent(Intent i) { mIntent = i; }
    public Intent getIntent() { return mIntent; }
    public Bundle getExtras() { if (mExtras == null) mExtras = new Bundle(); return mExtras; }
    public Bundle peekExtras() { return mExtras; }
    public void setLayoutResource(int r) { mLayoutRes = r; }
    public int getLayoutResource() { return mLayoutRes; }
    public void setWidgetLayoutResource(int r) { mWidgetLayoutRes = r; }
    public int getWidgetLayoutResource() { return mWidgetLayoutRes; }
    public void setEnabled(boolean e) { if (mEnabled != e) { mEnabled = e; notifyDependencyChange(shouldDisableDependents()); notifyChanged(); } }
    public boolean isEnabled() { return mEnabled && mDependencyMet && mParentDependencyMet; }
    public void setSelectable(boolean s) { mSelectable = s; notifyChanged(); }
    public boolean isSelectable() { return mSelectable; }
    public void setShouldDisableView(boolean b) { mShouldDisableView = b; notifyChanged(); }
    public boolean getShouldDisableView() { return mShouldDisableView; }
    public void setPersistent(boolean p) { mPersistent = p; }
    public boolean isPersistent() { return mPersistent; }
    public void setDefaultValue(Object v) { mDefaultValue = v; }
    public void setDependency(String key) { unregisterDependency(); mDependencyKey = key; registerDependency(); }
    public String getDependency() { return mDependencyKey; }
    public void setVisible(boolean v) { mVisible = v; notifyHierarchyChanged(); }
    public boolean isVisible() { return mVisible; }
    public void setIconSpaceReserved(boolean r) { mIconSpaceReserved = r; }
    public boolean isIconSpaceReserved() { return mIconSpaceReserved; }
    public void setSingleLineTitle(boolean s) { mSingleLineTitle = s; }
    public boolean isSingleLineTitle() { return mSingleLineTitle; }
    public void setOnPreferenceChangeListener(OnPreferenceChangeListener l) { mChangeListener = l; }
    public OnPreferenceChangeListener getOnPreferenceChangeListener() { return mChangeListener; }
    public void setOnPreferenceClickListener(OnPreferenceClickListener l) { mClickListener = l; }
    public OnPreferenceClickListener getOnPreferenceClickListener() { return mClickListener; }
    public PreferenceGroup getParent() { return mParent; }
    public int compareTo(Preference o) {
        if (mOrder != o.mOrder) return Integer.compare(mOrder, o.mOrder);
        CharSequence a = mTitle, b = o.mTitle;
        if (a == b) return 0; if (a == null) return 1; if (b == null) return -1;
        return a.toString().compareToIgnoreCase(b.toString());
    }

    /* ---- values, kept in the SharedPreferences under the key */
    protected boolean shouldPersist() { return mManager != null && mPersistent && hasKey(); }
    public boolean callChangeListener(Object v) { return mChangeListener == null || mChangeListener.onPreferenceChange(this, v); }
    private void commit(SharedPreferences.Editor e) { e.apply(); }
    protected boolean persistString(String v) { if (!shouldPersist()) return false; commit(getEditor().putString(mKey, v)); return true; }
    protected String getPersistedString(String d) { return shouldPersist() ? getSharedPreferences().getString(mKey, d) : d; }
    public boolean persistStringSet(Set<String> v) { if (!shouldPersist()) return false; commit(getEditor().putStringSet(mKey, v)); return true; }
    public Set<String> getPersistedStringSet(Set<String> d) { return shouldPersist() ? getSharedPreferences().getStringSet(mKey, d) : d; }
    protected boolean persistInt(int v) { if (!shouldPersist()) return false; commit(getEditor().putInt(mKey, v)); return true; }
    protected int getPersistedInt(int d) { return shouldPersist() ? getSharedPreferences().getInt(mKey, d) : d; }
    protected boolean persistFloat(float v) { if (!shouldPersist()) return false; commit(getEditor().putFloat(mKey, v)); return true; }
    protected float getPersistedFloat(float d) { return shouldPersist() ? getSharedPreferences().getFloat(mKey, d) : d; }
    protected boolean persistLong(long v) { if (!shouldPersist()) return false; commit(getEditor().putLong(mKey, v)); return true; }
    protected long getPersistedLong(long d) { return shouldPersist() ? getSharedPreferences().getLong(mKey, d) : d; }
    protected boolean persistBoolean(boolean v) { if (!shouldPersist()) return false; commit(getEditor().putBoolean(mKey, v)); return true; }
    protected boolean getPersistedBoolean(boolean d) { return shouldPersist() ? getSharedPreferences().getBoolean(mKey, d) : d; }

    /** Its value from the preferences, or the default it was given (written there then), when it joins a screen. */
    protected void onSetInitialValue(boolean restorePersistedValue, Object defaultValue) {}
    protected void onAttachedToHierarchy(PreferenceManager m) {
        mManager = m;
        boolean has = shouldPersist() && getSharedPreferences().contains(mKey);
        if (has) onSetInitialValue(true, null);
        else if (mDefaultValue != null) onSetInitialValue(false, mDefaultValue);
    }
    protected void onAttachedToActivity() { registerDependency(); }
    protected void onPrepareForRemoval() { unregisterDependency(); }

    /* ---- dependencies: a preference disabled while the one it depends on is off */
    private void registerDependency() {
        if (mDependencyKey == null || mManager == null) return;
        Preference p = mManager.findPreference(mDependencyKey);
        if (p != null) { p.mDependents.add(this); onDependencyChanged(p, p.shouldDisableDependents()); }
    }
    private void unregisterDependency() {
        if (mDependencyKey == null || mManager == null) return;
        Preference p = mManager.findPreference(mDependencyKey);
        if (p != null) p.mDependents.remove(this);
    }
    public void notifyDependencyChange(boolean disableDependents) { for (Preference d : new ArrayList<>(mDependents)) d.onDependencyChanged(this, disableDependents); }
    public void onDependencyChanged(Preference dependency, boolean disableDependent) {
        if (mDependencyMet == disableDependent) { mDependencyMet = !disableDependent; notifyDependencyChange(shouldDisableDependents()); notifyChanged(); }
    }
    public void onParentChanged(Preference parent, boolean disableChild) {
        if (mParentDependencyMet == disableChild) { mParentDependencyMet = !disableChild; notifyDependencyChange(shouldDisableDependents()); notifyChanged(); }
    }
    public boolean shouldDisableDependents() { return !isEnabled(); }

    /* ---- its row */
    private Runnable mOnChange;
    void huskSetOnChange(Runnable r) { mOnChange = r; }
    protected void notifyChanged() { PreferenceScreen.huskRefresh(this); }
    protected void notifyHierarchyChanged() { PreferenceScreen.huskRefresh(this); }

    public View getView(View convertView, ViewGroup parent) {
        View v = convertView != null && convertView.getTag() == getClass() ? convertView : onCreateView(parent);
        v.setTag(getClass());
        onBindView(v);
        return v;
    }
    protected View onCreateView(ViewGroup parent) {
        if (mLayoutRes != 0) {
            View v = LayoutInflater.from(mContext).inflate(mLayoutRes, parent, false);
            ViewGroup frame = v.findViewById(android.R.id.widget_frame);
            if (frame != null) { if (mWidgetLayoutRes != 0) LayoutInflater.from(mContext).inflate(mWidgetLayoutRes, frame); else frame.setVisibility(View.GONE); }
            return v;
        }
        float d = mContext.getResources().getDisplayMetrics().density;
        LinearLayout row = new LinearLayout(mContext);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinimumHeight((int) (64 * d));
        row.setPadding((int) (16 * d), (int) (12 * d), (int) (16 * d), (int) (12 * d));
        ImageView icon = new ImageView(mContext);
        icon.setId(android.R.id.icon);
        LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams((int) (24 * d), (int) (24 * d));
        ilp.setMarginEnd((int) (32 * d));
        row.addView(icon, ilp);
        LinearLayout texts = new LinearLayout(mContext);
        texts.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(mContext);
        title.setId(android.R.id.title);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        title.setTextColor(themeColor(android.R.attr.textColorPrimary, 0xDE000000));
        texts.addView(title);
        TextView summary = new TextView(mContext);
        summary.setId(android.R.id.summary);
        summary.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        summary.setTextColor(themeColor(android.R.attr.textColorSecondary, 0x8A000000));
        texts.addView(summary);
        row.addView(texts, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        LinearLayout frame = new LinearLayout(mContext);
        frame.setId(android.R.id.widget_frame);
        frame.setGravity(Gravity.CENTER_VERTICAL);
        if (mWidgetLayoutRes != 0) LayoutInflater.from(mContext).inflate(mWidgetLayoutRes, frame);
        row.addView(frame, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
    }
    int themeColor(int attr, int fallback) {
        TypedValue v = new TypedValue();
        if (!mContext.getTheme().resolveAttribute(attr, v, true)) return fallback;
        if (v.type >= TypedValue.TYPE_FIRST_COLOR_INT && v.type <= TypedValue.TYPE_LAST_COLOR_INT) return v.data;
        if (v.resourceId != 0) try { return mContext.getColorStateList(v.resourceId).getDefaultColor(); } catch (RuntimeException e) { }
        return fallback;
    }
    protected void onBindView(View view) {
        TextView title = view.findViewById(android.R.id.title);
        if (title != null) { CharSequence t = getTitle(); title.setText(t); title.setVisibility(t == null || t.length() == 0 ? View.GONE : View.VISIBLE); title.setSingleLine(mSingleLineTitle); }
        TextView summary = view.findViewById(android.R.id.summary);
        if (summary != null) { CharSequence s = getSummary(); summary.setText(s); summary.setVisibility(s == null || s.length() == 0 ? View.GONE : View.VISIBLE); }
        View icon = view.findViewById(android.R.id.icon);
        if (icon instanceof ImageView) { Drawable d = getIcon(); ((ImageView) icon).setImageDrawable(d); icon.setVisibility(d != null ? View.VISIBLE : mIconSpaceReserved ? View.INVISIBLE : View.GONE); }
        if (mShouldDisableView) setEnabledDeep(view, isEnabled());
    }
    private static void setEnabledDeep(View v, boolean e) {
        v.setEnabled(e);
        if (v instanceof ViewGroup) for (int i = 0; i < ((ViewGroup) v).getChildCount(); i++) setEnabledDeep(((ViewGroup) v).getChildAt(i), e);
    }

    /* ---- a tap on its row */
    protected void onClick() {}
    public void performClick(PreferenceScreen screen) {
        if (!isEnabled()) return;
        onClick();
        if (mClickListener != null && mClickListener.onPreferenceClick(this)) return;
        if (mManager != null && mManager.huskTreeClick(screen, this)) return;
        if (mIntent != null) mContext.startActivity(mIntent);
    }

    protected android.os.Parcelable onSaveInstanceState() { return android.view.AbsSavedState.EMPTY_STATE; }
    protected void onRestoreInstanceState(android.os.Parcelable state) {}
    public void saveHierarchyState(Bundle container) {}
    public void restoreHierarchyState(Bundle container) {}
    @Override public String toString() { return String.valueOf(getTitle()) + " " + String.valueOf(getSummary()); }

    public static class BaseSavedState extends android.view.AbsSavedState {
        public BaseSavedState(android.os.Parcel source) { super(source); }
        public BaseSavedState(android.os.Parcelable superState) { super(superState); }
        // ---- generated by tools/compat/fillmembers.py (BaseSavedState): the platform's members this class does not write (signatures only)
        public static android.os.Parcelable.Creator CREATOR;
        // ---- end of generated members (BaseSavedState)
    }
    // ---- generated by tools/compat/fillmembers.py: the platform's members this class does not write (signatures only)
    private final java.util.HashMap<String, Object> huskFill = new java.util.HashMap<>();
    protected android.preference.Preference findPreferenceInHierarchy(java.lang.String p0) { return this; }
    public boolean isRecycleEnabled() { return (huskFill.get("RecycleEnabled") instanceof Boolean ? (Boolean) huskFill.get("RecycleEnabled") : false); }
    public boolean onKey(android.view.View p0, int p1, android.view.KeyEvent p2) { return false; }
    public void setRecycleEnabled(boolean p0) { huskFill.put("RecycleEnabled", Boolean.valueOf(p0)); }
    // ---- end of generated members
}
