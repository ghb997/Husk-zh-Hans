package android.preference;

import android.content.Context;
import android.content.res.TypedArray;
import android.text.TextUtils;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.LinearLayout;

/** A preference whose text is edited in a dialog. */
@Deprecated
public class EditTextPreference extends DialogPreference {
    private final EditText mEditText;
    private String mText;
    private boolean mTextSet;

    public EditTextPreference(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        mEditText = new EditText(context, attrs);
        mEditText.setId(android.R.id.edit);
        mEditText.setEnabled(true);
    }
    public EditTextPreference(Context context, AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public EditTextPreference(Context context, AttributeSet attrs) { this(context, attrs, 0, 0); }
    public EditTextPreference(Context context) { this(context, null, 0, 0); }

    public void setText(String text) {
        boolean wasBlocking = shouldDisableDependents();
        boolean changed = !TextUtils.equals(mText, text);
        if (changed || !mTextSet) {
            mText = text; mTextSet = true;
            persistString(text);
            if (changed) { boolean blocking = shouldDisableDependents(); if (blocking != wasBlocking) notifyDependencyChange(blocking); notifyChanged(); }
        }
    }
    public String getText() { return mText; }
    public EditText getEditText() { return mEditText; }
    @Override protected View onCreateDialogView() {
        View v = super.onCreateDialogView();
        if (v != null) return v;
        float d = getContext().getResources().getDisplayMetrics().density;
        LinearLayout l = new LinearLayout(getContext());
        l.setPadding((int) (24 * d), (int) (8 * d), (int) (24 * d), 0);
        return l;
    }
    @Override protected void onBindDialogView(View view) {
        super.onBindDialogView(view);
        mEditText.setText(getText());
        if (mEditText.getParent() instanceof ViewGroup) ((ViewGroup) mEditText.getParent()).removeView(mEditText);
        onAddEditTextToDialogView(view, mEditText);
    }
    protected void onAddEditTextToDialogView(View dialogView, EditText editText) {
        ViewGroup container = dialogView.findViewById(android.R.id.edit) instanceof ViewGroup ? (ViewGroup) dialogView.findViewById(android.R.id.edit) : dialogView instanceof ViewGroup ? (ViewGroup) dialogView : null;
        if (container != null) container.addView(editText, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
    }
    @Override protected void onDialogClosed(boolean positive) {
        if (!positive) return;
        String v = mEditText.getText().toString();
        if (callChangeListener(v)) setText(v);
    }
    @Override protected Object onGetDefaultValue(TypedArray a, int index) { return a.getString(index); }
    @Override protected void onSetInitialValue(boolean restore, Object def) { setText(restore ? getPersistedString(mText) : (String) def); }
    @Override public boolean shouldDisableDependents() { return TextUtils.isEmpty(mText) || super.shouldDisableDependents(); }
}
