package android.preference;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CheckBox;

/** A preference with a check box at the end of its row. */
@Deprecated
public class CheckBoxPreference extends TwoStatePreference {
    public CheckBoxPreference(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) { super(context, attrs, defStyleAttr, defStyleRes); }
    public CheckBoxPreference(Context context, AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public CheckBoxPreference(Context context, AttributeSet attrs) { this(context, attrs, 0, 0); }
    public CheckBoxPreference(Context context) { this(context, null, 0, 0); }
    @Override protected View onCreateView(ViewGroup parent) {
        View v = super.onCreateView(parent);
        ViewGroup frame = v.findViewById(android.R.id.widget_frame);
        if (frame != null && v.findViewById(android.R.id.checkbox) == null) { CheckBox c = new CheckBox(getContext()); c.setId(android.R.id.checkbox); frame.addView(c); }
        return v;
    }
    @Override protected void onBindView(View view) { super.onBindView(view); bindButton(view.findViewById(android.R.id.checkbox)); }
}
