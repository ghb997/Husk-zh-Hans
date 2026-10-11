package android.preference;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Switch;

/** A preference with a switch at the end of its row. */
@Deprecated
public class SwitchPreference extends TwoStatePreference {
    private CharSequence mOn, mOff;
    public SwitchPreference(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        if (attrs != null) {
            TypedArray a = context.obtainStyledAttributes(attrs, new int[] { android.R.attr.switchTextOn, android.R.attr.switchTextOff });
            mOn = a.getText(0); mOff = a.getText(1);
            a.recycle();
        }
    }
    public SwitchPreference(Context context, AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public SwitchPreference(Context context, AttributeSet attrs) { this(context, attrs, 0, 0); }
    public SwitchPreference(Context context) { this(context, null, 0, 0); }
    public void setSwitchTextOn(CharSequence t) { mOn = t; notifyChanged(); }
    public void setSwitchTextOn(int r) { setSwitchTextOn(getContext().getText(r)); }
    public CharSequence getSwitchTextOn() { return mOn; }
    public void setSwitchTextOff(CharSequence t) { mOff = t; notifyChanged(); }
    public void setSwitchTextOff(int r) { setSwitchTextOff(getContext().getText(r)); }
    public CharSequence getSwitchTextOff() { return mOff; }
    @Override protected View onCreateView(ViewGroup parent) {
        View v = super.onCreateView(parent);
        ViewGroup frame = v.findViewById(android.R.id.widget_frame);
        if (frame != null && v.findViewById(android.R.id.switch_widget) == null) { Switch s = new Switch(getContext()); s.setId(android.R.id.switch_widget); frame.addView(s); }
        return v;
    }
    @Override protected void onBindView(View view) { super.onBindView(view); bindButton(view.findViewById(android.R.id.switch_widget)); }
}
