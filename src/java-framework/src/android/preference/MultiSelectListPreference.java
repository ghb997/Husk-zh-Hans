package android.preference;

import android.app.AlertDialog;
import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;
import java.util.HashSet;
import java.util.Set;

/** A preference of several choices from a list, stored as a string set. */
@Deprecated
public class MultiSelectListPreference extends DialogPreference {
    private CharSequence[] mEntries, mEntryValues;
    private final Set<String> mValues = new HashSet<>(), mNew = new HashSet<>();
    private boolean mChanged;

    public MultiSelectListPreference(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (attrs != null) {
            TypedArray a = context.obtainStyledAttributes(attrs, new int[] { android.R.attr.entries, android.R.attr.entryValues });
            mEntries = a.getTextArray(0); mEntryValues = a.getTextArray(1);
            a.recycle();
        }
    }
    public MultiSelectListPreference(Context context, AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public MultiSelectListPreference(Context context, AttributeSet attrs) { this(context, attrs, 0, 0); }
    public MultiSelectListPreference(Context context) { this(context, null, 0, 0); }

    public void setEntries(CharSequence[] e) { mEntries = e; }
    public void setEntries(int r) { setEntries(getContext().getResources().getTextArray(r)); }
    public CharSequence[] getEntries() { return mEntries; }
    public void setEntryValues(CharSequence[] v) { mEntryValues = v; }
    public void setEntryValues(int r) { setEntryValues(getContext().getResources().getTextArray(r)); }
    public CharSequence[] getEntryValues() { return mEntryValues; }
    public void setValues(Set<String> v) { mValues.clear(); mValues.addAll(v); persistStringSet(v); notifyChanged(); }
    public Set<String> getValues() { return mValues; }
    public int findIndexOfValue(String value) {
        if (value != null && mEntryValues != null) for (int i = mEntryValues.length - 1; i >= 0; i--) if (mEntryValues[i].toString().equals(value)) return i;
        return -1;
    }
    @Override protected void onPrepareDialogBuilder(AlertDialog.Builder b) {
        if (mEntries == null || mEntryValues == null) throw new IllegalStateException("MultiSelectListPreference requires an entries array and an entryValues array.");
        boolean[] checked = new boolean[mEntryValues.length];
        for (int i = 0; i < checked.length; i++) checked[i] = mValues.contains(mEntryValues[i].toString());
        mNew.clear(); mNew.addAll(mValues); mChanged = false;
        b.setMultiChoiceItems(mEntries, checked, (d, which, isChecked) -> {
            mChanged |= isChecked ? mNew.add(mEntryValues[which].toString()) : mNew.remove(mEntryValues[which].toString());
        });
    }
    @Override protected void onDialogClosed(boolean positive) {
        if (positive && mChanged) { Set<String> v = new HashSet<>(mNew); if (callChangeListener(v)) setValues(v); }
        mChanged = false;
    }
    @Override protected Object onGetDefaultValue(TypedArray a, int index) {
        CharSequence[] d = a.getTextArray(index);
        Set<String> out = new HashSet<>();
        if (d != null) for (CharSequence c : d) out.add(c.toString());
        return out;
    }
    @SuppressWarnings("unchecked")
    @Override protected void onSetInitialValue(boolean restore, Object def) { setValues(restore ? getPersistedStringSet(mValues) : (Set<String>) def); }
}
