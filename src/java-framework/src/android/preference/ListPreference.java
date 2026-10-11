package android.preference;

import android.app.AlertDialog;
import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

/** A preference chosen from a list (entries shown, entryValues stored); its summary can show the choice (%s). */
@Deprecated
public class ListPreference extends DialogPreference {
    private CharSequence[] mEntries, mEntryValues;
    private String mValue, mSummary;
    private boolean mValueSet;
    private int mClicked = -1;

    public ListPreference(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (attrs != null) {
            TypedArray a = context.obtainStyledAttributes(attrs, new int[] { android.R.attr.entries, android.R.attr.entryValues });
            mEntries = a.getTextArray(0); mEntryValues = a.getTextArray(1);
            a.recycle();
            TypedArray b = context.obtainStyledAttributes(attrs, new int[] { android.R.attr.summary });
            mSummary = b.getString(0);
            b.recycle();
        }
    }
    public ListPreference(Context context, AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public ListPreference(Context context, AttributeSet attrs) { this(context, attrs, 0, 0); }
    public ListPreference(Context context) { this(context, null, 0, 0); }

    public void setEntries(CharSequence[] e) { mEntries = e; }
    public void setEntries(int r) { setEntries(getContext().getResources().getTextArray(r)); }
    public CharSequence[] getEntries() { return mEntries; }
    public void setEntryValues(CharSequence[] v) { mEntryValues = v; }
    public void setEntryValues(int r) { setEntryValues(getContext().getResources().getTextArray(r)); }
    public CharSequence[] getEntryValues() { return mEntryValues; }
    public void setValue(String value) {
        boolean changed = !android.text.TextUtils.equals(mValue, value);
        if (changed || !mValueSet) { mValue = value; mValueSet = true; persistString(value); if (changed) notifyChanged(); }
    }
    public String getValue() { return mValue; }
    public void setValueIndex(int i) { if (mEntryValues != null) setValue(mEntryValues[i].toString()); }
    public int findIndexOfValue(String value) {
        if (value != null && mEntryValues != null) for (int i = mEntryValues.length - 1; i >= 0; i--) if (mEntryValues[i].toString().equals(value)) return i;
        return -1;
    }
    public CharSequence getEntry() { int i = findIndexOfValue(mValue); return i >= 0 && mEntries != null && i < mEntries.length ? mEntries[i] : null; }
    @Override public void setSummary(CharSequence s) { super.setSummary(s); mSummary = s == null ? null : s.toString(); }
    @Override public CharSequence getSummary() {
        CharSequence e = getEntry();
        if (mSummary == null) return super.getSummary();
        return String.format(mSummary, e == null ? "" : e);
    }
    @Override protected void onPrepareDialogBuilder(AlertDialog.Builder b) {
        if (mEntries == null || mEntryValues == null) throw new IllegalStateException("ListPreference requires an entries array and an entryValues array.");
        mClicked = findIndexOfValue(mValue);
        b.setSingleChoiceItems(mEntries, mClicked, (d, which) -> {
            mClicked = which;
            onClick(d, android.content.DialogInterface.BUTTON_POSITIVE);
            d.dismiss();
        });
        b.setPositiveButton(null, null);
    }
    @Override protected void onDialogClosed(boolean positive) {
        if (positive && mClicked >= 0 && mEntryValues != null) {
            String v = mEntryValues[mClicked].toString();
            if (callChangeListener(v)) setValue(v);
        }
    }
    @Override protected Object onGetDefaultValue(TypedArray a, int index) { return a.getString(index); }
    @Override protected void onSetInitialValue(boolean restore, Object def) { setValue(restore ? getPersistedString(mValue) : (String) def); }
}
