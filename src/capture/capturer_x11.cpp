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

bool X11Capturer::selectWindow() {
    if (!display) return false;

    // Grab pointer and wait for click
    Cursor cursor = XCreateFontCursor(display, XC_crosshair);
    Window root = RootWindow(display, screen);

    XGrabPointer(display, root, False,
                 ButtonPressMask | ButtonReleaseMask,
                 GrabModeSync, GrabModeAsync,
                 root, cursor, CurrentTime);

    fprintf(stderr, "Click on a window to capture...\n");

    XEvent event;
    XAllowEvents(display, SyncPointer, CurrentTime);
    XWindowEvent(display, root, ButtonPressMask, &event);

    XUngrabPointer(display, CurrentTime);
    XFreeCursor(display, cursor);

    Window child;
    int win_x, win_y, root_x, root_y;
    unsigned int mask;
    XQueryPointer(display, event.xbutton.root, &root, &child,
                  &root_x, &root_y, &win_x, &win_y, &mask);

    target_window = child;
    if (!target_window)
        target_window = root;

    fprintf(stderr, "Selected window: 0x%lx\n", target_window);

    // Get window dimensions
    XWindowAttributes attr;
    XGetWindowAttributes(display, target_window, &attr);
    src_w = attr.width;
    src_h = attr.height;

    // Allocate shared memory image
    if (image) {
        XShmDetach(display, &shm_info);
        shmdt(shm_info.shmaddr);
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
        return false;
    }

    shm_info.shmaddr = (char*)shmat(shm_info.shmid, nullptr, 0);
    image->data = shm_info.shmaddr;
    shm_info.readOnly = False;

    if (!XShmAttach(display, &shm_info)) {
        fprintf(stderr, "error: XShmAttach failed\n");
        return false;
    }

    XSync(display, False);
    shmctl(shm_info.shmid, IPC_RMID, nullptr);

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
            XFree(image);
        }
        XCloseDisplay(display);
    }
}
