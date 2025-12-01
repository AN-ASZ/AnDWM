#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>

std::string clipboard_data = "";
std::vector<unsigned char> image_data = {};
std::string image_type = "";

Atom UTF8;

void respond_to_request(Display* dpy, XSelectionRequestEvent* req) {
    XEvent ev = {0};
    ev.xselection.type = SelectionNotify;
    ev.xselection.display = req->display;
    ev.xselection.requestor = req->requestor;
    ev.xselection.selection = req->selection;
    ev.xselection.target = req->target;
    ev.xselection.time = req->time;
    ev.xselection.property = None;

    Atom TARGETS = XInternAtom(dpy, "TARGETS", False);
    Atom image_png = XInternAtom(dpy, "image/png", False);

    if (req->target == TARGETS) {
        std::vector<Atom> targets = { UTF8, XA_STRING, image_png };
        XChangeProperty(dpy, req->requestor, req->property,
                        XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)targets.data(), targets.size());
        ev.xselection.property = req->property;
    }
    else if (req->target == image_png) {
        if (!image_data.empty()) {
            XChangeProperty(dpy, req->requestor, req->property,
                            image_png, 8, PropModeReplace,
                            image_data.data(), image_data.size());
            ev.xselection.property = req->property;
        }
    }
    else if (req->target == UTF8 || req->target == XA_STRING) {
        Atom format = (req->target == UTF8) ? UTF8 : XA_STRING;
        XChangeProperty(dpy, req->requestor, req->property,
                        format, 8, PropModeReplace,
                        (unsigned char*)clipboard_data.c_str(),
                        clipboard_data.length());
        ev.xselection.property = req->property;
    }

    XSendEvent(dpy, req->requestor, True, 0, &ev);
    XFlush(dpy);
}

// Request clipboard content from current owner
void fetch_clipboard(Display* dpy, Window win) {
    Atom clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    Atom prop = XInternAtom(dpy, "XCLIP_TEMP", False);

    // Try fetching as image first
    Atom image_png = XInternAtom(dpy, "image/png", False);
    XConvertSelection(dpy, clipboard, image_png, prop, win, CurrentTime);
    
    // Also try text in case there's no image
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    XConvertSelection(dpy, clipboard, utf8, prop, win, CurrentTime);
    XFlush(dpy);
}

int main() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { std::cerr << "Cannot open X display\n"; return 1; }

    Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);

    UTF8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom clipboard = XInternAtom(dpy, "CLIPBOARD", False);

    int event_base, error_base;
    if (!XFixesQueryExtension(dpy, &event_base, &error_base)) {
        std::cerr << "XFixes not available\n"; return 1;
    }

    XFixesSelectSelectionInput(dpy, DefaultRootWindow(dpy), clipboard,
                               XFixesSetSelectionOwnerNotifyMask);

    // Request initial clipboard content
    fetch_clipboard(dpy, win);

    XEvent ev;
    bool has_clipboard = false;

    while (true) {
        XNextEvent(dpy, &ev);

        if (ev.type == SelectionRequest) {
            respond_to_request(dpy, &ev.xselectionrequest);
        }
        else if (ev.type == event_base + XFixesSelectionNotify) {
            XFixesSelectionNotifyEvent* se = (XFixesSelectionNotifyEvent*)&ev;
            if (se->selection == clipboard) {
                Atom owner = XGetSelectionOwner(dpy, clipboard);
                // If we don't own it, fetch and claim it
                if (owner != win) {
                    fetch_clipboard(dpy, win);
                }
            }
        }
        else if (ev.type == SelectionNotify) {
            if (ev.xselection.property != None) {
                Atom type;
                int format;
                unsigned long nitems, bytes_after;
                unsigned char* data = nullptr;

                XGetWindowProperty(dpy, win, ev.xselection.property, 0, ~0L, False,
                                   AnyPropertyType, &type, &format,
                                   &nitems, &bytes_after, &data);

                if (data) {
                    Atom image_png = XInternAtom(dpy, "image/png", False);
                    
                    // Check if we received image data
                    if (type == image_png) {
                        image_data.assign(data, data + nitems);
                        image_type = "image/png";
                    } else {
                        // Text data
                        clipboard_data = std::string((char*)data, nitems);
                        image_data.clear();
                        image_type = "";
                        std::cerr << "Clipboard: Text\n";
                    }
                    XFree(data);

                    // Take ownership to persist it
                    Atom clipboard_atom = XInternAtom(dpy, "CLIPBOARD", False);
                    XSetSelectionOwner(dpy, clipboard_atom, win, CurrentTime);
                    XFlush(dpy);
                    has_clipboard = true;
                    
                    // Small delay to ensure ownership is established
                    usleep(10000);
                }
            }
        }
    }
}
