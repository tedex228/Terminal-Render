#include "capture/capturer_x11.h"
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool X11Capturer::init() {
    display = XOpenDisplay(nullptr);
    if (!display) {
        fprintf(stderr, "error: cannot open X display\n");
        return false;
    }
    screen = DefaultScreen(display);
    return true;
}

static Window getTopLevelWindow(Display* dpy, Window w) {
    Window root_ret = RootWindow(dpy, DefaultScreen(dpy));
    Window result = w;
    while (true) {
        Window root, parent;
        Window* children = nullptr;
        unsigned int nchildren;
        if (!XQueryTree(dpy, result, &root, &parent, &children, &nchildren))
            break;
        if (children) XFree(children);
        if (!parent || parent == root_ret)
            break;
        result = parent;
    }
    return result;
}

bool X11Capturer::setupShmImage() {
    if (image) {
        XShmDetach(display, &shm_info);
        shmdt(shm_info.shmaddr);
        XDestroyImage(image);
        image = nullptr;
    }

    image = XShmCreateImage(display, DefaultVisual(display, screen),
                            DefaultDepth(display, screen), ZPixmap,
                            nullptr, &shm_info, src_w, src_h);
    if (!image) {
        fprintf(stderr, "error: XShmCreateImage failed\n");
        return false;
    }

    shm_info.shmid = shmget(IPC_PRIVATE, image->bytes_per_line * image->height,
                            IPC_CREAT | 0777);
    if (shm_info.shmid < 0) {
        fprintf(stderr, "error: shmget failed\n");
        XDestroyImage(image);
        image = nullptr;
        return false;
    }

    shm_info.shmaddr = (char*)shmat(shm_info.shmid, nullptr, 0);
    image->data = shm_info.shmaddr;
    shm_info.readOnly = False;

    if (!XShmAttach(display, &shm_info)) {
        fprintf(stderr, "error: XShmAttach failed\n");
        shmdt(shm_info.shmaddr);
        XDestroyImage(image);
        image = nullptr;
        return false;
    }

    XSync(display, False);
    shmctl(shm_info.shmid, IPC_RMID, nullptr);
    return true;
}

bool X11Capturer::selectWindow() {
    if (!display) return false;

    Window root = RootWindow(display, screen);

    // Method 1: Try XGrabPointer (click to select)
    Cursor cursor = XCreateFontCursor(display, XC_crosshair);

    int ret = XGrabPointer(display, root, False,
                           ButtonPressMask,
                           GrabModeAsync, GrabModeAsync,
                           root, cursor, CurrentTime);

    if (ret == GrabSuccess) {
        fprintf(stderr, "Click on a window to capture...\n");
        fflush(stderr);

        XEvent event;
        XNextEvent(display, &event);

        XUngrabPointer(display, CurrentTime);
        XFreeCursor(display, cursor);

        Window clicked = event.xbutton.subwindow;
        if (!clicked) clicked = event.xbutton.window;
        target_window = getTopLevelWindow(display, clicked);
    } else {
        XFreeCursor(display, cursor);

        // Method 2: Fallback — capture the currently focused window
        fprintf(stderr, "XGrabPointer failed (%d), capturing focused window...\n", ret);
        fflush(stderr);

        Window focus;
        int revert;
        XGetInputFocus(display, &focus, &revert);

        if (!focus || focus == root || focus == PointerRoot) {
            fprintf(stderr, "error: no focused window\n");
            return false;
        }

        target_window = getTopLevelWindow(display, focus);
    }

    // Get dimensions
    XWindowAttributes attr;
    XGetWindowAttributes(display, target_window, &attr);
    src_w = attr.width;
    src_h = attr.height;

    if (src_w <= 0 || src_h <= 0) {
        fprintf(stderr, "error: invalid window size %dx%d\n", src_w, src_h);
        return false;
    }

    fprintf(stderr, "Selected window: 0x%lx (%dx%d)\n", target_window, src_w, src_h);

    // Allocate shared memory
    if (!setupShmImage()) return false;

    return true;
}

Frame X11Capturer::capture() {
    if (!display || !image) return {};

    XShmGetImage(display, target_window, image, 0, 0, AllPlanes);

    Frame frame(src_w, src_h);
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w; ++x) {
            unsigned long pixel = XGetPixel(image, x, y);
            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;
            frame.at(x, y) = (r << 16) | (g << 8) | b;
        }
    }

    return frame;
}

X11Capturer::~X11Capturer() {
    if (display) {
        if (image) {
            XShmDetach(display, &shm_info);
            shmdt(shm_info.shmaddr);
            XDestroyImage(image);
        }
        XCloseDisplay(display);
    }
}
