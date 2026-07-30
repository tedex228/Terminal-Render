#include "capture/capturer_x11.h"
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
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
    root = RootWindow(display, screen);
    return true;
}

static Window getTopLevelWindow(Display* dpy, Window w) {
    Window root_w = RootWindow(dpy, DefaultScreen(dpy));
    Window result = w;
    while (True) {
        Window r, parent;
        Window* children = nullptr;
        unsigned int nchildren;
        if (!XQueryTree(dpy, result, &r, &parent, &children, &nchildren))
            break;
        if (children) XFree(children);
        if (!parent || parent == root_w)
            break;
        result = parent;
    }
    return result;
}

static bool checkWmState(Display* dpy, Window w) {
    Atom wm_state = XInternAtom(dpy, "WM_STATE", True);
    if (wm_state == None) return false;

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* data = nullptr;
    int status = XGetWindowProperty(dpy, w, wm_state, 0, 0, False,
                                    AnyPropertyType, &actual_type,
                                    &actual_format, &nitems, &bytes_after, &data);
    if (data) XFree(data);
    return (status == Success && actual_type != None);
}

static Window findClientWindow(Display* dpy, Window top) {
    // Check if this window itself has WM_STATE
    if (checkWmState(dpy, top)) return top;

    // Recursively search children
    Window root_ret, parent;
    Window* children = nullptr;
    unsigned int nchildren;
    if (!XQueryTree(dpy, top, &root_ret, &parent, &children, &nchildren))
        return None;

    Window result = None;
    for (unsigned i = 0; i < nchildren; ++i) {
        result = findClientWindow(dpy, children[i]);
        if (result) break;
    }

    if (children) XFree(children);
    return result;
}

static Window findAnyWindow(Display* dpy) {
    Window root = RootWindow(dpy, DefaultScreen(dpy));

    // Method A: _NET_CLIENT_LIST
    Atom client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    if (client_list != None) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* data = nullptr;

        if (XGetWindowProperty(dpy, root, client_list, 0, ~0L, False,
                               XA_WINDOW, &actual_type, &actual_format,
                               &nitems, &bytes_after, &data) == Success
            && actual_type == XA_WINDOW && nitems > 0) {
            Window* windows = (Window*)data;
            for (unsigned long i = 0; i < nitems; ++i) {
                XWindowAttributes attr;
                if (XGetWindowAttributes(dpy, windows[i], &attr)
                    && attr.map_state == IsViewable
                    && attr.width > 100 && attr.height > 100) {
                    Window result = windows[i];
                    XFree(data);
                    return result;
                }
            }
            XFree(data);
        }
    }

    // Method B: recursively search all root children for WM_STATE
    Window root_ret, parent;
    Window* children = nullptr;
    unsigned int nchildren;
    if (!XQueryTree(dpy, root, &root_ret, &parent, &children, &nchildren))
        return None;

    Window result = None;
    for (unsigned i = 0; i < nchildren; ++i) {
        Window client = findClientWindow(dpy, children[i]);
        if (client) {
            XWindowAttributes attr;
            if (XGetWindowAttributes(dpy, client, &attr)
                && attr.map_state == IsViewable
                && attr.width > 100 && attr.height > 100) {
                result = client;
                break;
            }
        }
    }

    if (children) XFree(children);
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

    // Method 1: Try XGrabPointer (click to select)
    Cursor cursor = XCreateFontCursor(display, XC_crosshair);

    int ret = XGrabPointer(display, root, False,
                           ButtonPressMask,
                           GrabModeAsync, GrabModeAsync,
                           root, cursor, CurrentTime);

    if (ret == GrabSuccess) {
        fprintf(stderr, "Click on a window to capture...\n");

        XEvent event;
        XNextEvent(display, &event);

        XUngrabPointer(display, CurrentTime);
        XFreeCursor(display, cursor);

        Window clicked = event.xbutton.subwindow;
        if (!clicked) clicked = event.xbutton.window;
        target_window = getTopLevelWindow(display, clicked);
    } else {
        XFreeCursor(display, cursor);

        // Method 2: Auto-detect a suitable window
        fprintf(stderr, "XGrabPointer failed (%d), scanning for windows...\n", ret);

        target_window = findAnyWindow(display);

        if (!target_window) {
            fprintf(stderr, "error: no suitable window found\n");
            return false;
        }
    }

    // Get dimensions
    XWindowAttributes attr;
    XGetWindowAttributes(display, target_window, &attr);
    src_w = attr.width;
    src_h = attr.height;

    if (src_w <= 1 || src_h <= 1) {
        fprintf(stderr, "error: bad window size %dx%d for 0x%lx\n",
                src_w, src_h, target_window);
        return false;
    }

    fprintf(stderr, "Selected window: 0x%lx (%dx%d)\n", target_window, src_w, src_h);

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
