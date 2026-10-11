/* A Mac window for the Unity runtime: the same launcher the app uses, drawing into a CAMetalLayer the way
 * the phone does -- the path the off-screen harness (unity-test.c) never takes. Mouse = one finger. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>
#include "husk-tl-unity-app.h"

void tl_log_line(const char *fmt, ...)
{
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    static struct timespec t0;
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    pthread_mutex_lock(&m);
    if (!t0.tv_sec) t0 = t;
    fprintf(stderr, "[%7.3f] ", (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9);
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap);
    pthread_mutex_unlock(&m);
}

static NSString *g_apk, *g_ca;

@interface GameView : NSView
@end
@implementation GameView {
    BOOL launched;
}
- (CALayer *)makeBackingLayer { CAMetalLayer *l = [CAMetalLayer layer]; l.pixelFormat = MTLPixelFormatBGRA8Unorm; l.framebufferOnly = YES; return l; }
- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)viewDidMoveToWindow
{
    self.wantsLayer = YES;
    if (self.window) [self layoutIfLaunch];
}
- (void)layout { [super layout]; [self layoutIfLaunch]; }
- (void)layoutIfLaunch
{
    if (launched || !self.window || self.bounds.size.width < 1) return;
    CGFloat scale = self.window.backingScaleFactor;
    CAMetalLayer *l = (CAMetalLayer *)self.layer;
    l.contentsScale = scale;
    int w = (int)(self.bounds.size.width * scale), h = (int)(self.bounds.size.height * scale);
    l.drawableSize = CGSizeMake(w, h);
    launched = YES;
    NSString *data = [NSTemporaryDirectory() stringByAppendingPathComponent:@"husk-unity-window-data"];
    [[NSFileManager defaultManager] createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:nil];
    const char *egl = getenv("TL_ANGLE_EGL") ? getenv("TL_ANGLE_EGL") : "/Users/davi/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/Frameworks/Chromium Embedded Framework.framework/Versions/A/Libraries/libEGL.dylib";
    fprintf(stderr, "window: launching at %dx%d\n", w, h);
    if (!husk_unity_launch(g_apk.UTF8String, data.UTF8String, (__bridge void *)l, w, h, egl, g_ca.UTF8String)) fprintf(stderr, "window: launch refused\n");
}
- (void)touch:(int)phase event:(NSEvent *)e
{
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    CGFloat s = self.window.backingScaleFactor;
    husk_unity_touch(phase, 0, (float)(p.x * s), (float)(p.y * s));
}
- (void)mouseDown:(NSEvent *)e { [self touch:0 event:e]; }
- (void)mouseDragged:(NSEvent *)e { [self touch:1 event:e]; }
- (void)mouseUp:(NSEvent *)e { [self touch:2 event:e]; }
- (void)keyDown:(NSEvent *)e
{
    /* arrow keys swipe, the way the game is played: from the middle of the screen */
    float cx = (float)(self.bounds.size.width * self.window.backingScaleFactor / 2), cy = (float)(self.bounds.size.height * self.window.backingScaleFactor / 2);
    float d = 140 * (float)self.window.backingScaleFactor;
    float dx = 0, dy = 0;
    switch (e.keyCode) { case 123: dx = -d; break; case 124: dx = d; break; case 126: dy = -d; break; case 125: dy = d; break; default: return; }
    husk_unity_touch(0, 0, cx, cy);
    for (int i = 1; i <= 6; i++) { usleep(12000); husk_unity_touch(1, 0, cx + dx * i / 6, cy + dy * i / 6); }
    husk_unity_touch(2, 0, cx + dx, cy + dy);
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)n
{
    NSRect r = NSMakeRect(200, 200, 393, 852);
    NSWindow *w = [[NSWindow alloc] initWithContentRect:r styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    w.title = @"Husk Unity";
    GameView *v = [[GameView alloc] initWithFrame:r];
    w.contentView = v;
    [w makeKeyAndOrderFront:nil];
    [w makeFirstResponder:v];
    [NSApp activateIgnoringOtherApps:YES];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
@end

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 3) { fprintf(stderr, "usage: %s <apk> <cacert.pem>\n", argv[0]); return 2; }
        g_apk = [NSString stringWithUTF8String:argv[1]];
        g_ca = [NSString stringWithUTF8String:argv[2]];
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate *d = [AppDelegate new];
        app.delegate = d;
        [app run];
    }
    return 0;
}
