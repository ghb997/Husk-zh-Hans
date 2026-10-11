package android.preference;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;
import android.view.View;
import android.widget.Checkable;
import android.widget.CompoundButton;
import android.widget.TextView;

/** A preference that is on or off (a check box, a switch), with a summary for each. */
@Deprecated
public abstract class TwoStatePreference extends Preference {
    private boolean mChecked, mCheckedSet, mDisableDependentsState;
    private CharSequence mSummaryOn, mSummaryOff;

    public TwoStatePreference(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (attrs != null) {
            TypedArray a = context.obtainStyledAttributes(attrs, new int[] { android.R.attr.summaryOn, android.R.attr.summaryOff, android.R.attr.disableDependentsState });
            mSummaryOn = a.getText(0); mSummaryOff = a.getText(1); mDisableDependentsState = a.getBoolean(2, false);
            a.recycle();
        }
    }
    public TwoStatePreference(Context context, AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public TwoStatePreference(Context context, AttributeSet attrs) { this(context, attrs, 0, 0); }
    public TwoStatePreference(Context context) { this(context, null, 0, 0); }

    @Override protected void onClick() {
        super.onClick();
        boolean v = !isChecked();
        if (callChangeListener(v)) setChecked(v);
    }
    public void setChecked(boolean checked) {
        boolean changed = mChecked != checked;
        if (changed || !mCheckedSet) {
            mChecked = checked; mCheckedSet = true;
            persistBoolean(checked);
            if (changed) { notifyDependencyChange(shouldDisableDependents()); notifyChanged(); }
        }
    }
    public boolean isChecked() { return mChecked; }
    @Override public boolean shouldDisableDependents() { boolean off = mDisableDependentsState ? mChecked : !mChecked; return off || super.shouldDisableDependents(); }
    public void setSummaryOn(CharSequence s) { mSummaryOn = s; if (isChecked()) notifyChanged(); }
    public void setSummaryOn(int r) { setSummaryOn(getContext().getText(r)); }
    public CharSequence getSummaryOn() { return mSummaryOn; }
    public void setSummaryOff(CharSequence s) { mSummaryOff = s; if (!isChecked()) notifyChanged(); }
    public void setSummaryOff(int r) { setSummaryOff(getContext().getText(r)); }
    public CharSequence getSummaryOff() { return mSummaryOff; }
    public boolean getDisableDependentsState() { return mDisableDependentsState; }
    public void setDisableDependentsState(boolean d) { mDisableDependentsState = d; }
    @Override protected Object onGetDefaultValue(TypedArray a, int index) { return a.getBoolean(index, false); }
    @Override protected void onSetInitialValue(boolean restore, Object def) { setChecked(restore ? getPersistedBoolean(mChecked) : (Boolean) def); }
    @Override public CharSequence getSummary() {
        if (mChecked && mSummaryOn != null) return mSummaryOn;
        if (!mChecked && mSummaryOff != null) return mSummaryOff;
        return super.getSummary();
    }
    /** The widget in the row shows the state; the row takes the taps. */
    void bindButton(View button) {
        if (button instanceof CompoundButton) { ((CompoundButton) button).setOnCheckedChangeListener(null); }
        if (button instanceof Checkable) ((Checkable) button).setChecked(mChecked);
        if (button != null) { button.setClickable(false); button.setFocusable(false); }
    }
}
