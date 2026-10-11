package android.preference;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.res.TypedArray;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.TextView;

/** A preference that is set in a dialog it opens when tapped. */
@Deprecated
public abstract class DialogPreference extends Preference implements DialogInterface.OnClickListener, DialogInterface.OnDismissListener {
    private CharSequence mDialogTitle, mDialogMessage, mPositive, mNegative;
    private Drawable mDialogIcon;
    private int mDialogLayout;
    private Dialog mDialog;
    private int mWhich;

    public DialogPreference(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (attrs != null) {
            int[] at = { android.R.attr.dialogTitle, android.R.attr.dialogMessage, android.R.attr.dialogIcon, android.R.attr.positiveButtonText,
                         android.R.attr.negativeButtonText, android.R.attr.dialogLayout };
            int[] sorted = at.clone(); java.util.Arrays.sort(sorted);
            TypedArray a = context.obtainStyledAttributes(attrs, sorted);
            mDialogTitle = a.getText(java.util.Arrays.binarySearch(sorted, android.R.attr.dialogTitle));
            mDialogMessage = a.getText(java.util.Arrays.binarySearch(sorted, android.R.attr.dialogMessage));
            mDialogIcon = a.getDrawable(java.util.Arrays.binarySearch(sorted, android.R.attr.dialogIcon));
            mPositive = a.getText(java.util.Arrays.binarySearch(sorted, android.R.attr.positiveButtonText));
            mNegative = a.getText(java.util.Arrays.binarySearch(sorted, android.R.attr.negativeButtonText));
            mDialogLayout = a.getResourceId(java.util.Arrays.binarySearch(sorted, android.R.attr.dialogLayout), 0);
            a.recycle();
        }
        if (mDialogTitle == null) mDialogTitle = getTitle();
        if (mPositive == null) mPositive = context.getText(android.R.string.ok);
        if (mNegative == null) mNegative = context.getText(android.R.string.cancel);
    }
    public DialogPreference(Context context, AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public DialogPreference(Context context, AttributeSet attrs) { this(context, attrs, 0, 0); }

    public void setDialogTitle(CharSequence t) { mDialogTitle = t; }
    public void setDialogTitle(int r) { setDialogTitle(getContext().getText(r)); }
    public CharSequence getDialogTitle() { return mDialogTitle; }
    public void setDialogMessage(CharSequence m) { mDialogMessage = m; }
    public void setDialogMessage(int r) { setDialogMessage(getContext().getText(r)); }
    public CharSequence getDialogMessage() { return mDialogMessage; }
    public void setDialogIcon(Drawable d) { mDialogIcon = d; }
    public void setDialogIcon(int r) { mDialogIcon = getContext().getDrawable(r); }
    public Drawable getDialogIcon() { return mDialogIcon; }
    public void setPositiveButtonText(CharSequence t) { mPositive = t; }
    public void setPositiveButtonText(int r) { setPositiveButtonText(getContext().getText(r)); }
    public CharSequence getPositiveButtonText() { return mPositive; }
    public void setNegativeButtonText(CharSequence t) { mNegative = t; }
    public void setNegativeButtonText(int r) { setNegativeButtonText(getContext().getText(r)); }
    public CharSequence getNegativeButtonText() { return mNegative; }
    public void setDialogLayoutResource(int r) { mDialogLayout = r; }
    public int getDialogLayoutResource() { return mDialogLayout; }
    public Dialog getDialog() { return mDialog; }

    @Override protected void onClick() { if (mDialog == null || !mDialog.isShowing()) showDialog(null); }
    protected void onPrepareDialogBuilder(AlertDialog.Builder builder) {}
    protected View onCreateDialogView() { return mDialogLayout != 0 ? LayoutInflater.from(getContext()).inflate(mDialogLayout, null) : null; }
    protected void onBindDialogView(View view) {
        View m = view.findViewById(android.R.id.message);
        if (m instanceof TextView) { ((TextView) m).setText(mDialogMessage); m.setVisibility(mDialogMessage != null ? View.VISIBLE : View.GONE); }
    }
    protected void showDialog(android.os.Bundle state) {
        mWhich = DialogInterface.BUTTON_NEGATIVE;
        AlertDialog.Builder b = new AlertDialog.Builder(getContext()).setTitle(mDialogTitle).setIcon(mDialogIcon)
            .setPositiveButton(mPositive, this).setNegativeButton(mNegative, this);
        View content = onCreateDialogView();
        if (content != null) { onBindDialogView(content); b.setView(content); } else b.setMessage(mDialogMessage);
        onPrepareDialogBuilder(b);
        mDialog = b.create();
        mDialog.setOnDismissListener(this);
        mDialog.show();
    }
    public void onClick(DialogInterface dialog, int which) { mWhich = which; }
    public void onDismiss(DialogInterface dialog) { mDialog = null; onDialogClosed(mWhich == DialogInterface.BUTTON_POSITIVE); }
    protected void onDialogClosed(boolean positiveResult) {}
    public void onActivityDestroy() { if (mDialog != null && mDialog.isShowing()) mDialog.dismiss(); }
}
