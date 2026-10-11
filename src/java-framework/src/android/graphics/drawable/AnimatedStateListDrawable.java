package android.graphics.drawable;

/** A StateListDrawable whose state changes would animate; Husk switches straight to the new state's drawable. */
public class AnimatedStateListDrawable extends StateListDrawable {
    public AnimatedStateListDrawable() { super(); }
    public void addState(int[] stateSet, Drawable d, int id) { addState(stateSet, d); }
    public <T extends Drawable & Animatable> void addTransition(int fromId, int toId, T transition, boolean reversible) {}
}
