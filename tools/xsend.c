/* xsend.c — synthetic X11 input injector for genuine-input testing.
 *
 * Finds the game window by title substring, focuses it, and sends real
 * XEvent key presses/releases or pointer motion. GLFW processes these
 * like physical input, so tests exercise the real key-edge and mouse
 * paths (unlike the in-process inject_key seam).
 *
 * Build: see tools/input_test.sh
 * Usage:
 *   xsend <display> <title-substr> key <keysym> [hold_ms]
 *   xsend <display> <title-substr> move <x> <y>
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__STRICT_ANSI__) || !defined(_POSIX_C_SOURCE)
extern int usleep(useconds_t); /* c11 -std=c11 without feature macros */
#endif

static Window find_by_name(Display *d, Window w, const char *substr)
{
    char *name = NULL;
    if (XFetchName(d, w, &name) && name && strstr(name, substr)) {
        XFree(name);
        return w;
    }
    if (name) XFree(name);

    Window root, parent, *children = NULL;
    unsigned int n = 0;
    if (XQueryTree(d, w, &root, &parent, &children, &n) && children) {
        Window found = 0;
        for (unsigned int i = 0; i < n && !found; i++)
            found = find_by_name(d, children[i], substr);
        XFree(children);
        return found;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <display> <title-substr> key <keysym> [hold_ms]\n"
                        "       %s <display> <title-substr> move <x> <y>\n"
                        "       %s <display> <title-substr> click [left|right] [hold_ms]\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }
    Display *d = XOpenDisplay(argv[1]);
    if (!d) {
        fprintf(stderr, "xsend: cannot open display %s\n", argv[1]);
        return 1;
    }
    Window root = DefaultRootWindow(d);
    Window target = find_by_name(d, root, argv[2]);
    if (!target) {
        fprintf(stderr, "xsend: no window matching '%s' on %s\n", argv[2], argv[1]);
        XCloseDisplay(d);
        return 1;
    }

    /* focus the window first (no WM in Xvfb; the game auto-pauses when
     * unfocused) */
    XSetInputFocus(d, target, RevertToParent, CurrentTime);
    XFlush(d);

    int rc = 0;
    if (strcmp(argv[3], "key") == 0 && argc >= 5) {
        KeySym sym = XStringToKeysym(argv[4]);
        if (sym == NoSymbol) {
            fprintf(stderr, "xsend: unknown keysym '%s'\n", argv[4]);
            rc = 1;
        } else {
            long hold = argc >= 6 ? strtol(argv[5], NULL, 10) : 80;
            KeyCode code = XKeysymToKeycode(d, sym);
            XKeyEvent ev;
            memset(&ev, 0, sizeof(ev));
            ev.display = d;
            ev.window = target;
            ev.root = root;
            ev.subwindow = None;
            ev.time = CurrentTime;
            ev.x = 100; ev.y = 100;
            ev.x_root = 100; ev.y_root = 100;
            ev.same_screen = True;
            ev.keycode = code;

            ev.type = KeyPress;
            ev.state = 0;
            XSendEvent(d, target, False, 0, (XEvent *)&ev);
            XFlush(d);
            usleep((useconds_t)(hold * 1000));
            ev.type = KeyRelease;
            ev.state = 0x1; /* ShiftMask-ish: button/key down */
            XSendEvent(d, target, False, 0, (XEvent *)&ev);
            XFlush(d);
            printf("xsend: key %s (code %u) press+%ldms+release\n",
                   argv[4], (unsigned)code, hold);
        }
    } else if (strcmp(argv[3], "click") == 0 && argc >= 5) {
        unsigned int button = (strcmp(argv[4], "right") == 0) ? Button3 : Button1;
        unsigned int mask = (button == Button3) ? Button3Mask : Button1Mask;
        long hold = argc >= 6 ? strtol(argv[5], NULL, 10) : 100;
        XButtonEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.display = d;
        ev.window = target;
        ev.root = root;
        ev.subwindow = None;
        ev.time = CurrentTime;
        ev.x = 640; ev.y = 360;
        ev.x_root = 640; ev.y_root = 360;
        ev.same_screen = True;
        ev.button = button;
        ev.type = ButtonPress;
        ev.state = 0;
        XSendEvent(d, target, False, 0, (XEvent *)&ev);
        XFlush(d);
        usleep((useconds_t)(hold * 1000));
        ev.type = ButtonRelease;
        ev.state = mask;
        XSendEvent(d, target, False, 0, (XEvent *)&ev);
        XFlush(d);
        printf("xsend: click button %u press+%ldms+release\n", button, hold);
    } else if (strcmp(argv[3], "move") == 0 && argc >= 6) {
        XMotionEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = MotionNotify;
        ev.display = d;
        ev.window = target;
        ev.root = root;
        ev.subwindow = None;
        ev.time = CurrentTime;
        ev.x = (int)strtol(argv[4], NULL, 10);
        ev.y = (int)strtol(argv[5], NULL, 10);
        ev.x_root = ev.x;
        ev.y_root = ev.y;
        ev.state = 0x100; /* Button1Mask */
        ev.is_hint = NotifyNormal;
        ev.same_screen = True;
        XSendEvent(d, target, False, 0, (XEvent *)&ev);
        XFlush(d);
        printf("xsend: motion to (%d,%d)\n", ev.x, ev.y);
    } else {
        fprintf(stderr, "xsend: unknown command\n");
        rc = 2;
    }

    XSync(d, False);
    XCloseDisplay(d);
    return rc;
}
