package android.preference;

import android.content.Context;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

/** A titled section of a screen: its header row, then its preferences. */
@Deprecated
public class PreferenceCategory extends PreferenceGroup {
    public PreferenceCategory(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) { super(context, attrs, defStyleAttr, defStyleRes); }
    public PreferenceCategory(Context context, AttributeSet attrs, int defStyleAttr) { this(context, attrs, defStyleAttr, 0); }
    public PreferenceCategory(Context context, AttributeSet attrs) { this(context, attrs, 0, 0); }
    public PreferenceCategory(Context context) { this(context, null, 0, 0); }
    @Override protected boolean onPrepareAddPreference(Preference p) {
        if (p instanceof PreferenceCategory) throw new IllegalArgumentException("Cannot add a PreferenceCategory directly to a PreferenceCategory");
        return super.onPrepareAddPreference(p);
    }
    @Override public boolean isEnabled() { return false; }
    @Override public boolean shouldDisableDependents() { return !super.isEnabled(); }
    @Override protected View onCreateView(ViewGroup parent) {
        if (getLayoutResource() != 0) return super.onCreateView(parent);
        float d = getContext().getResources().getDisplayMetrics().density;
        TextView t = new TextView(getContext());
        t.setId(android.R.id.title);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        t.setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL));
        t.setTextColor(themeColor(android.R.attr.colorAccent, 0xFF1A73E8));
        t.setPadding((int) (16 * d), (int) (24 * d), (int) (16 * d), (int) (8 * d));
        return t;
    }
    @Override protected void onBindView(View view) {
        TextView t = view.findViewById(android.R.id.title);
        if (t != null) t.setText(getTitle());
    }
}
