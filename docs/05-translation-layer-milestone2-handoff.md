# Translation Layer: Milestone 2 (Dalvik / DEX Execution) Handoff

## Summary for Claude

We have implemented Dalvik (DEX) bytecode execution and core Android framework shims to run Flappy Bird (`FlappyBird_64bit.apk`) natively on iOS/macOS without running Android under QEMU.

Frame 0 successfully renders pixel-perfect into the 540x960 ARGB8888 framebuffer (title screen with `(c) .GEARS 2013`, city skyline, ground, and logo).

The user reported:
> "shows a single frame of it then just turns white, it might just be loading, if it is then its too slow"

We investigated both issues directly on device logs pulled via `xcrun devicectl` and with isolated macOS test harnesses. Here is the exact diagnosis and what needs to be done.

---

## 1. Issue 1: Performance / Extreme Slowness

### Root Cause
In `src/translation-layer/husk-tl-dex.c`:
`tl_dex_invoke` was calling `dex_log("  pc=%d op=0x%02x", pc, opcode)` on every opcode, which executed `dlsym(RTLD_DEFAULT, "tl_log_line")`, acquired a `pthread_mutex`, reallocated log buffers, and printed to stderr. A single frame generated 19,000+ log lines, slowing execution by 10,000x and flooding SwiftUI with megabytes of log text.

### Progress
- In commit `b1aa706`, we removed all per-opcode and per-invoke `dex_log` lines from `husk-tl-dex.c`.
- **Caution**: Do **NOT** eagerly run `<clinit>` across all 9,182 classes in the APK at startup. The APK contains hundreds of Google Play Services and AndroidX classes with circular dependencies or unshimmed native hooks. Class initialization must be **on-demand (lazy)**.

---

## 2. Issue 2: Why Frame 2 Turns White

### Root Cause
1. At Frame 0, the title screen is drawn with cyan sky (`fb[0] = 0xffcac04e`).
2. At Frame 2, `fb[0]` becomes `0xffffffff` (solid white) because `canvas_drawRect(0, 0, 540, 960, paint)` is executed with a white paint overlay.
3. This white rectangle is the game's death/flash overlay: field `aQ` of class `c` represents the flash alpha (`0..255`). In Frame 2, `aQ` jumped from `0` to `255`!
4. `aQ` jumped to `255` inside `c.a` (the physics tick called by `c.doFrame`) because `this.a` (the game state enum `com/flappybird/recreation/d`) was `NULL` (`0x00000000`).
5. When `this.a` is `NULL`, `this.a.ordinal()` fails/returns 0 or hits the default branch, which `c.a` treats as game-over/death, triggering the full-screen white flash overlay (`aQ = 255`).
6. Why was `this.a` NULL? Because `this.a` is assigned via `sget-object` from `Lcom/flappybird/recreation/d;->a`. But `d.<clinit>` was never executed, so all static fields (`d.a`, `d.b`, `d.c`...) remained `NULL`.

---

## 3. Solution / Immediate Next Steps

### Step 1: Implement On-Demand `<clinit>`
In `src/translation-layer/husk-tl-dex.c`:
Add a helper to initialize a class on first access:

```c
static void dex_ensure_class_initialized(tl_dex_context *ctx, tl_dex_class *clazz)
{
    if (!clazz || clazz->initialized) return;
    clazz->initialized = true; /* Guard against recursion */
    tl_dex_method *clinit = tl_dex_find_method(clazz, "<clinit>", "V");
    if (!clinit) clinit = tl_dex_find_method(clazz, "<clinit>", NULL);
    if (clinit) {
        tl_dex_invoke(ctx, clinit, NULL, 0, NULL);
    }
}
```

Call `dex_ensure_class_initialized(ctx, f->clazz);` in:
- `sget` / `sput` (opcodes `0x60` .. `0x6d`) when accessing static fields.
- `new-instance` (opcode `0x22`).
- `invoke-static` (opcode `0x71`, `0x77`).

### Step 2: Verify Flappy Bird State Transition
In `c.doFrame`:
- Check that `this.a` is properly set to `d.a` (or whichever title/ready state enum constant it starts in).
- Make sure `android.graphics.Rect.intersects(Rect, Rect)` and `Rect.set(...)` in `src/translation-layer/husk-tl-framework.c` are shimmed (currently logged as `invoke ?->intersects (native=0x0)`).
- When `intersects` was unshimmed, collision detection may have erroneously evaluated to a collision with pipes or the ground.

### Step 3: Test on macOS with `dex-test.c`
Run:
```bash
clang -O2 tools/dex-test.c src/translation-layer/husk-tl-dex.c src/translation-layer/husk-tl-framework.c src/translation-layer/husk-tl-zip.c -Isrc/translation-layer -lz -framework CoreGraphics -framework ImageIO -framework CoreFoundation -o /tmp/dex-test && /tmp/dex-test
```
Verify that `fb[0]` stays cyan (`0xffcac04e`) across all 60 frames and does NOT turn white (`0xffffffff`).

### Step 4: Package and Commit
- Run `bash scripts/package_ipa.sh` to package `~/Desktop/Husk.ipa`.
- Commit directly to `origin/main` without co-author tags.
