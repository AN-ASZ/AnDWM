/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatchingw
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>
#include <pthread.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include "drw.h"
#include "util.h"
#include <Imlib2.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>
#include <stdbool.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#define SIDEBAR_WIDTH 64
#define SIDEBAR_CLICK_THRESH 5

#define FIFO_PATH "/tmp/dwm-pipn.fifo"
static void
sendn(const char *msg)
{
	int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
	if (fd >= 0) {
		write(fd, msg, strlen(msg));
		write(fd, "\n", 1);
		close(fd);
	}
}




/* tile animation config */
#define TILE_ANIM_STEPS 2   /* more = smoother, smoother */
#define TILE_ANIM_DELAY 500 /* microseconds */

/* clock bar grow animation config */
#define CLOCK_ANIMATE    1     /* 0 disables, bar stays fixed-size */
#define CLOCK_ANIM_EXTRA 280   /* max px the bar grows past text width */
#define PW_MONITOR_SINK  1     /* 1 = system audio (what you hear), 0 = mic */

static volatile int clockanim_running = 0;
static volatile unsigned int clockbar_dynw = 0; /* 0 = use fixed clockwidth() */
static volatile Window clockbar_win = 0;
static volatile int clockbar_cx = 0, clockbar_by = 0, clockbar_bh = 0;
static volatile int clockbar_cw = 0;

static struct pw_thread_loop *sound_loop = NULL;
static struct pw_stream *sound_stream = NULL;
static struct spa_audio_info sound_format;

/* macros */
#define BUTTONMASK (ButtonPressMask | ButtonReleaseMask)
#define CLEANMASK(mask)                                                        \
  (mask & ~(numlockmask | LockMask) &                                          \
   (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask |      \
    Mod5Mask))
#define INTERSECT(x, y, w, h, m)                                               \
  (MAX(0, MIN((x) + (w), (m)->wx + (m)->ww) - MAX((x), (m)->wx)) *             \
   MAX(0, MIN((y) + (h), (m)->wy + (m)->wh) - MAX((y), (m)->wy)))
#define INTERSECTC(x, y, w, h, z)                                              \
  (MAX(0, MIN((x) + (w), (z)->x + (z)->w) - MAX((x), (z)->x)) *                \
   MAX(0, MIN((y) + (h), (z)->y + (z)->h) - MAX((y), (z)->y)))
#define ISVISIBLE(C) ((C->tags & C->mon->tagset[C->mon->seltags]) || C->issticky)
#define HIDDEN(C) ((getstate(C->win) == IconicState))
#define LENGTH(X) (sizeof X / sizeof X[0])
#define MOUSEMASK (BUTTONMASK | PointerMotionMask)
#define WIDTH(X) ((X)->w + 2 * (X)->bw)
#define HEIGHT(X) ((X)->h + 2 * (X)->bw)
#define TAGMASK ((1 << LENGTH(tags)) - 1)
#define TAGSLENGTH (LENGTH(tags))
#define TEXTW(X) (drw_fontset_getwidth(drw, (X)) + lrpad)
#define MAXTABS 50

#define SYSTEM_TRAY_REQUEST_DOCK 0

/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY 0
#define XEMBED_WINDOW_ACTIVATE 1
#define XEMBED_FOCUS_IN 4
#define XEMBED_MODALITY_ON 10

#define XEMBED_MAPPED (1 << 0)
#define XEMBED_WINDOW_ACTIVATE 1
#define XEMBED_WINDOW_DEACTIVATE 2

#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR

/* enums */
enum {
  CurNormal,
  CurResize,
  CurMove,
  CurResizeHorzArrow,
  CurResizeVertArrow,
  CurTopLeft,
  CurTopRight,
  CurBottomLeft,
  CurBottomRight,
  CurLast
}; /* cursor */
enum {
  SchemeNorm,
  SchemeSel,
  SchemeTitle,
  SchemeTag,
  SchemeTag1,
  SchemeTag2,
  SchemeTag3,
  SchemeTag4,
  SchemeTag5,
  SchemeTag6,
  SchemeTag7,
  SchemeTag8,
  SchemeTag9,
  SchemeLayout,
  TabSel,
  TabNorm,
  SchemeBtnPrev,
  SchemeBtnNext,
  SchemeBtnClose
}; /* color schemes */
enum {
  NetSupported,
  NetWMName,
  NetWMIcon,
  NetWMState,
  NetWMCheck,
  NetSystemTray,
  NetSystemTrayOP,
  NetSystemTrayOrientation,
  NetSystemTrayOrientationHorz,
  NetWMFullscreen,
  NetWMStateHidden,
  NetActiveWindow,
  NetWMWindowType,
  NetWMWindowTypeDialog,
  NetWMWindowOpacity,
  NetWMBypassCompositor,
  NetClientList,
  NetClientInfo,
  NetDesktopNames,
  NetDesktopViewport,
  NetNumberOfDesktops,
  NetCurrentDesktop,
  NetNoAnimation,
  NetBarLeft,
  NetLast
}; /* EWMH atoms */
enum { Manager, Xembed, XembedInfo, XLast }; /* Xembed atoms */
enum {
  WMProtocols,
  WMDelete,
  WMState,
  WMTakeFocus,
  WMLast
}; /* default atoms */
enum {
  ClkTagBar,
  ClkTabBar,
  ClkTabPrev,
  ClkTabNext,
  ClkTabClose,
  ClkLtSymbol,
  ClkStatusText,
  ClkWinTitle,
  ClkClientWin,
  ClkRootWin,
  ClkLast
}; /* clicks */

enum showtab_modes {
  showtab_never,
  showtab_auto,
  showtab_nmodes,
  showtab_always
}; /* tab modes */

typedef union {
  int i;
  unsigned int ui;
  float f;
  const void *v;
} Arg;

typedef struct {
  unsigned int click;
  unsigned int mask;
  unsigned int button;
  void (*func)(const Arg *arg);
  const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
  char name[256];
  float mina, maxa;
  float cfact;
  int x, y, w, h;
  int oldx, oldy, oldw, oldh;
  int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
  int bw, oldbw;
  unsigned int tags;
  int isfixed, iscentered, isfloating, isurgent, neverfocus, oldstate,
      isfullscreen;
  int bypass_value;
  int isontop;
  int issticky;     /* sticky to background */
  int stickeystate; /* previous state before being sticky */
  int prevfloating;
  int prevx, prevy;
  int prevw, prevh;
  int wasfloating;
  unsigned int oldtags;
  unsigned int icw, ich;
  unsigned int ic_pw, ic_ph; /* actual picture pixel dimensions after drw_picture_create_resized */
  Picture icon;
  int beingmoved;
  Client *next;
  Client *snext;
  Monitor *mon;
  Window win;
  time_t lastvisible;
  int isautominimized;
  int fshidden;     /* hidden by fullscreen window */
  Client *pipparent; /* parent browser window that spawned this PIP */
};

typedef struct {
  unsigned int mod;
  KeySym keysym;
  void (*func)(const Arg *);
  const Arg arg;
} Key;

typedef struct {
  const char *symbol;
  void (*arrange)(Monitor *);
} Layout;

typedef struct {
  const char *class;
  const char *instance;
  const char *title;
  unsigned int tags;
  int iscentered;
  int isfloating;
  int isontop;
  int monitor;
} Rule;

typedef struct Systray Systray;
struct Systray {
  Window win;
  Client *icons;
};

typedef struct {
  const char **command;
  const char *name;
} Launcher;

typedef struct {
  char *display_name;
  Window win;
  long animation_data;
} AnimationPropertyArgs;

/* function declarations */
static Client *find_pipparent(Client *c);
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h,
                          int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void prepare_workspace_switch(Monitor *m, unsigned int oldtags, unsigned int newtags);
static void checkminimize(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void cyclelayout(const Arg *arg);
static unsigned int clockwidth(void);
static unsigned int kblayoutwidth(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void dragmfact(const Arg *arg);
static void dragcfact(const Arg *arg);
static void drawbar(Monitor *m);
static void drawbars(void);
static int drawstatusbar(Monitor *m, int bh, char *text);
static void drawtab(Monitor *m);
static void drawtabs(void);
static void calcsidebar(Monitor *m);
static void drawsidebar(Monitor *m);
static void drawsidbars(void);
static void showsidbar(Monitor *m);
static void hideosidebar(Monitor *m);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void focuswin(const Arg *arg);
static void focusunderpointer(void);
static Atom getatomprop(Client *c, Atom prop);
static Picture geticonprop(Window w, unsigned int *icw, unsigned int *ich, unsigned int *pw, unsigned int *ph);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static unsigned int getsystraywidth();
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void hide(Client *c);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void forcekillclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static void moveorplace(const Arg *arg);
static Client *nexttiled(Client *c);
static void placemouse(const Arg *arg);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void restart(const Arg *arg);
static Client *recttoclient(int x, int y, int w, int h);
static Monitor *recttomon(int x, int y, int w, int h);
static Client *clientfromwin(Window w);
static void removesystrayicon(Client *i);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizebarwin(Monitor *m);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizerequest(XEvent *e);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static int sendevent(Window w, Atom proto, int m, long d0, long d1, long d2,
                     long d3, long d4);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setclienttagprop(Client *c);
static void sound_monitor_start(void);
static void sound_monitor_stop(void);
static void setnoanimation_async(Client *c, long val);
static void setworkspaceanimation(Monitor *m, int val);
static void setcurrentdesktop(void);
static void setdesktopnames(void);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static int haswindowproperty(Client *c, Atom prop);
static int get_bypass_compositor_value(Client *c);
static int window_has_transparency(Client *c);
static void setwindowopacity(Client *c);
static void clearwindowopacity(Client *c);
static void setlayout(const Arg *arg);
static void setcfact(const Arg *arg);
static void setmfact(const Arg *arg);
static void setnumdesktops(void);
static void setup(void);
static void setviewport(void);
static void seturgent(Client *c, int urg);
static void show(Client *c);
static void showhide(Client *c);
static void showtagpreview(int tag);
static void spawn(const Arg *arg);
static unsigned int statuswidth(char *text);
static void switchtag(void);
static Monitor *systraytomon(Monitor *m);
static void tabmode(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefloating_noarrange(Client *c);
static void togglefullscr(const Arg *arg);
static void togglesticky(Client *c, int fullscreen);
static void togglestickyclient(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void freeicon(Client *c);
static void hidewin(const Arg *arg);
static void restorewin(const Arg *arg);
static void togglewin(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatenumberofdesktops(void);
static void updatecurrentdesktop(unsigned int tagset);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updatepreview(void);
static void updateclientlist(void);
static void updateclock(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatesystray(void);
static void updatesystrayicongeom(Client *i, int w, int h);
static void updatesystrayiconstate(Client *i, XPropertyEvent *ev);
static void updatetitle(Client *c);
static void updateicon(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static void *set_animation_property_thread(void *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static Client *wintosystrayicon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static Window ontopsibling(Monitor *m, Client *exclude);
static void zoom(const Arg *arg);
static void alttab(const Arg *arg);

/* variables */
static Systray *systray = NULL;
static const char broken[] = "broken";
static char stext[1024];
static char clockstr[8] = "00:00";
static char kbstr[16] = "us";
static int screen;
static int sw, sh; /* X display screen geometry width, height */
static int bh;     /* bar height */
static int th = 0; /* tab bar geometry */
static int lrpad;  /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static Time last_ev_time;
static void (*handler[LASTEvent])(XEvent *) = {
    [ButtonPress] = buttonpress,
    [ClientMessage] = clientmessage,
    [ConfigureRequest] = configurerequest,
    [ConfigureNotify] = configurenotify,
    [DestroyNotify] = destroynotify,
    [EnterNotify] = enternotify,
    [Expose] = expose,
    [FocusIn] = focusin,
    [KeyPress] = keypress,
    [KeyRelease] = keypress,
    [MappingNotify] = mappingnotify,
    [MapRequest] = maprequest,
    [MotionNotify] = motionnotify,
    [PropertyNotify] = propertynotify,
    [ResizeRequest] = resizerequest,
    [UnmapNotify] = unmapnotify};
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast];
static int running = 1;
static int xkb_event_base = 0;
static int alt_held = 0;
/* When non-zero, tiled-resizes use animated transitions via
 * resizeclient_animated. You can temporarily disable it to have
 * instant resize via resizeclient. */
static Bool animate_tiled = 1;
static Cur *cursor[CurLast];
static Clr **scheme, clrborder;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

#define hiddenWinStackMax 100
static int hiddenWinStackTop = -1;
static Client *hiddenWinStack[hiddenWinStackMax];

/* configuration, allows nested code to access above variables */
#include "config.h"

typedef struct Pertag Pertag;
struct Monitor {
  char ltsymbol[16];
  float mfact;
  int nmaster;
  int num;
  int by;             /* bar geometry */
  int ty;             /* tab bar geometry */
  int mx, my, mw, mh; /* screen size */
  int wx, wy, ww, wh; /* window area  */
  int gappih;         /* horizontal gap between windows */
  int gappiv;         /* vertical gap between windows */
  int gappoh;         /* horizontal outer gaps */
  int gappov;         /* vertical outer gaps */
  unsigned int borderpx;
  unsigned int seltags;
  unsigned int sellt;
  unsigned int tagset[2];
  unsigned int colorfultag;
  int showbar, showtab;
  int topbar, toptab;
  Client *clients;
  Client *sel;
  Client *stack;
  Monitor *next;
  Window barwin;
  Window bartitlewin;
  Window barcenterwin;
  Window barkbwin;
  Window barrightwin;
  Window tabwin;
  Window tagwin;
  Window sidebarwin;
  int sidebarvisible;
  int sidebarh;
  int sidebarw;
  Pixmap tagmap[LENGTH(tags)];
  int previewshow;
  int ntabs;
  int tab_widths[MAXTABS];
  int tab_btn_w[3];
  const Layout *lt[2];
  Pertag *pertag;
};

#include "movestack.c"
#include "shiftview.c"
#include "vanitygaps.c"

struct Pertag {
  unsigned int curtag, prevtag;          /* current and previous tag */
  int nmasters[LENGTH(tags) + 1];        /* number of windows in master area */
  float mfacts[LENGTH(tags) + 1];        /* mfacts per tag */
  unsigned int sellts[LENGTH(tags) + 1]; /* selected layouts */
  const Layout
      *ltidxs[LENGTH(tags) + 1][2]; /* matrix of tags and layouts indexes  */
  int showbars[LENGTH(tags) + 1];   /* display bar for the current tag */
};

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags {
  char limitexceeded[LENGTH(tags) > 31 ? -1 : 1];
};

/* function implementations */
void applyrules(Client *c) {
  const char *class, *instance;
  unsigned int i;
  const Rule *r;
  Monitor *m;
  XClassHint ch = {NULL, NULL};

  /* rule matching */
  c->iscentered = 0;
  c->isfloating = 0;
  c->isontop = 0;
  c->tags = 0;
  XGetClassHint(dpy, c->win, &ch);
  class = ch.res_class ? ch.res_class : broken;
  instance = ch.res_name ? ch.res_name : broken;

  for (i = 0; i < LENGTH(rules); i++) {
    r = &rules[i];
    if ((!r->title || strstr(c->name, r->title)) &&
        (!r->class || strstr(class, r->class)) &&
        (!r->instance || strstr(instance, r->instance))) {
      c->iscentered = r->iscentered;
      c->isfloating = r->isfloating;
      c->isontop = r->isontop;
      if (c->isontop)
        c->isfloating = 1;
      c->tags |= r->tags;
      for (m = mons; m && m->num != r->monitor; m = m->next)
        ;
      if (m)
        c->mon = m;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);
  c->tags =
      c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact) {
  int baseismin;
  Monitor *m = c->mon;

  if (c->issticky) {
    *w = MAX(1, *w);
    *h = MAX(1, *h);
    return 1;
  }

  /* set minimum possible */
  *w = MAX(1, *w);
  *h = MAX(1, *h);
  if (interact) {
    if (*x > sw)
      *x = sw - WIDTH(c);
    if (*y > sh)
      *y = sh - HEIGHT(c);
    if (*x + *w + 2 * c->bw < 0)
      *x = 0;
    if (*y + *h + 2 * c->bw < 0)
      *y = 0;
  } else {
    if (*x >= m->wx + m->ww)
      *x = m->wx + m->ww - WIDTH(c);
    if (*y >= m->wy + m->wh)
      *y = m->wy + m->wh - HEIGHT(c);
    if (*x + *w + 2 * c->bw <= m->wx)
      *x = m->wx;
    if (*y + *h + 2 * c->bw <= m->wy)
      *y = m->wy;
  }
  if (*h < bh)
    *h = bh;
  if (*w < bh)
    *w = bh;
  if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
    if (!c->hintsvalid)
      updatesizehints(c);
    /* see last two sentences in ICCCM 4.1.2.3 */
    baseismin = c->basew == c->minw && c->baseh == c->minh;
    if (!baseismin) { /* temporarily remove base dimensions */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for aspect limits */
    if (c->mina > 0 && c->maxa > 0) {
      if (c->maxa < (float)*w / *h)
        *w = *h * c->maxa + 0.5;
      else if (c->mina < (float)*h / *w)
        *h = *w * c->mina + 0.5;
    }
    if (baseismin) { /* increment calculation requires this */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for increment value */
    if (c->incw)
      *w -= *w % c->incw;
    if (c->inch)
      *h -= *h % c->inch;
    /* restore base dimensions */
    *w = MAX(*w + c->basew, c->minw);
    *h = MAX(*h + c->baseh, c->minh);
    if (c->maxw)
      *w = MIN(*w, c->maxw);
    if (c->maxh)
      *h = MIN(*h, c->maxh);
  }
  return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void arrange(Monitor *m) {
  if (m)
    showhide(m->stack);
  else
    for (m = mons; m; m = m->next)
      showhide(m->stack);
  if (m) {
    arrangemon(m);
    restack(m);
  } else
    for (m = mons; m; m = m->next)
      arrangemon(m);
  focusunderpointer();
}

void arrangemon(Monitor *m) {
  updatebarpos(m);
  updatesystray();
  XMoveResizeWindow(dpy, m->tabwin, m->wx + m->gappov, m->ty,
                    m->ww - 2 * m->gappov, th);
  XMoveResizeWindow(dpy, m->tagwin, m->wx + m->gappov + tag_preview_x_offset,
                    m->by + (m->topbar ? (bh + m->gappoh + tag_preview_y_offset)
                                       : (-(m->mh / scalepreview) - m->gappoh - tag_preview_y_offset)),
                    m->mw / scalepreview, m->mh / scalepreview);
  strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol) - 1);
  m->ltsymbol[sizeof(m->ltsymbol) - 1] = '\0';
  if (m->lt[m->sellt]->arrange)
    m->lt[m->sellt]->arrange(m);
}

void attach(Client *c) {
  if (new_window_attach_on_end) {
    Client **tmp = &c->mon->clients;
    while (*tmp)
      tmp = &(*tmp)->next;
    *tmp = c;
  } else {
    c->next = c->mon->clients;
    c->mon->clients = c;
  }
}

void attachstack(Client *c) {
  c->snext = c->mon->stack;
  c->mon->stack = c;
}

void buttonpress(XEvent *e) {
  unsigned int i, x, click;
  int clientbinding;
  int loop;
  Arg arg = {0};
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;

  click = ClkRootWin;
  /* focus monitor if necessary */
  if ((m = wintomon(ev->window)) && m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
    focus(NULL);
  }
  if (ev->window == selmon->barwin) {
    if (selmon->previewshow) {
      XUnmapWindow(dpy, selmon->tagwin);
      selmon->previewshow = 0;
    }
    i = x = 0;
    do
      x += TEXTW(tags[i]);
    while (ev->x >= x && ++i < LENGTH(tags));
    if (i < LENGTH(tags)) {
      click = ClkTagBar;
      arg.ui = 1 << i;
      goto execute_handler;
    } else if (ev->x < x + TEXTW(selmon->ltsymbol)) {
      click = ClkLtSymbol;
      goto execute_handler;
    }
    click = ClkWinTitle;
  } else if (ev->window == selmon->bartitlewin) {
    x = 0;
    for (i = 0; i < LENGTH(launchers); i++) {
      x += TEXTW(launchers[i].name);

      if (ev->x < x) {
        Arg a;
        a.v = launchers[i].command;
        spawn(&a);
        return;
      }
    }

    /* the remaining area of the title bar is the app icon + app name */
    click = ClkWinTitle;
  } else if (ev->window == selmon->barrightwin) {
    click = ClkStatusText;
  } else if (ev->window == selmon->barcenterwin) {
    click = ClkRootWin;
  } else if (ev->window == selmon->barkbwin) {
    click = ClkRootWin;
  }

  if (ev->window == selmon->tabwin) {
    i = 0;
    x = 0;
    for (c = selmon->clients; c; c = c->next) {
      if (!ISVISIBLE(c))
        continue;
      x += selmon->tab_widths[i];
      if (ev->x > x)
        ++i;
      else
        break;
      if (i >= m->ntabs)
        break;
    }
    if (c && ev->x <= x) {
      click = ClkTabBar;
      arg.ui = i;
    } else {
      x = selmon->ww - 2 * m->gappov;
      for (loop = 2; loop >= 0; loop--) {
        x -= selmon->tab_btn_w[loop];
        if (ev->x > x)
          break;
      }
      if (ev->x >= x)
        click = ClkTabPrev + loop;
    }
  } else if (enablesidebar && ev->window == selmon->sidebarwin) {
    /* sidebar click - find which app was clicked */
    int count = 0;
    Client *visible[MAXTABS];
    Client *clicked = NULL;
    for (c = selmon->clients; c; c = c->next) {
      if (ISVISIBLE(c) && count < MAXTABS) {
        visible[count++] = c;
      }
    }
    if (count > 0) {
      int itemh = selmon->mh / 3 / count;
      int clicked_idx = ev->y / itemh;
      if (clicked_idx >= 0 && clicked_idx < count) {
        clicked = visible[clicked_idx];
      }
    }
    if (clicked) {
      selmon = clicked->mon;
      focus(clicked);
    }
  } else if ((c = wintoclient(ev->window))) {
    click = ClkClientWin;
    clientbinding = 0;
    for (i = 0; i < LENGTH(buttons); i++)
      if (buttons[i].click == ClkClientWin &&
          buttons[i].button == ev->button &&
          CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state)) {
        clientbinding = 1;
        break;
      }
    XAllowEvents(dpy, clientbinding ? AsyncPointer : ReplayPointer, CurrentTime);
    if (clientbinding) {
      selmon = c->mon;
      selmon->sel = c;
      goto execute_handler;
    }
    focus(c);
    restack(selmon);
  }

execute_handler:

  for (i = 0; i < LENGTH(buttons); i++)
    if (click == buttons[i].click && buttons[i].func &&
        buttons[i].button == ev->button &&
        CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
      buttons[i].func(
          ((click == ClkTagBar || click == ClkTabBar) && buttons[i].arg.i == 0)
              ? &arg
              : &buttons[i].arg);
}

void checkotherwm(void) {
  xerrorxlib = XSetErrorHandler(xerrorstart);
  /* this causes an error if some other window manager is running */
  XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  XSync(dpy, False);
}

void cleanup(void) {
  Arg a = {.ui = ~0};
  Layout foo = {"", NULL};
  Monitor *m;
  size_t i;

  if (CLOCK_ANIMATE)
    sound_monitor_stop();

  view(&a);
  selmon->lt[selmon->sellt] = &foo;
  for (m = mons; m; m = m->next)
    while (m->stack)
      unmanage(m->stack, 0);
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  while (mons)
    cleanupmon(mons);
  if (showsystray) {
    XUnmapWindow(dpy, systray->win);
    XDestroyWindow(dpy, systray->win);
    free(systray);
  }
  for (i = 0; i < CurLast; i++)
    drw_cur_free(drw, cursor[i]);
  for (i = 0; i < LENGTH(colors) + 1; i++)
    free(scheme[i]);
  free(scheme);
  XDestroyWindow(dpy, wmcheckwin);
  drw_free(drw);
  XSync(dpy, False);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void cleanupmon(Monitor *mon) {
  Monitor *m;
  size_t i;

  if (mon == mons)
    mons = mons->next;
  else {
    for (m = mons; m && m->next != mon; m = m->next)
      ;
    m->next = mon->next;
  }
  for (i = 0; i < LENGTH(tags); i++) {
    if (mon->tagmap[i])
      XFreePixmap(dpy, mon->tagmap[i]);
  }
  XUnmapWindow(dpy, mon->barwin);
  XDestroyWindow(dpy, mon->barwin);
  XUnmapWindow(dpy, mon->bartitlewin);
  XDestroyWindow(dpy, mon->bartitlewin);
  XUnmapWindow(dpy, mon->barcenterwin);
  XDestroyWindow(dpy, mon->barcenterwin);
  XUnmapWindow(dpy, mon->barkbwin);
  XDestroyWindow(dpy, mon->barkbwin);
  XUnmapWindow(dpy, mon->barrightwin);
  XDestroyWindow(dpy, mon->barrightwin);
  XUnmapWindow(dpy, mon->tabwin);
  XDestroyWindow(dpy, mon->tabwin);
  XUnmapWindow(dpy, mon->tagwin);
  XDestroyWindow(dpy, mon->tagwin);
  XUnmapWindow(dpy, mon->sidebarwin);
  XDestroyWindow(dpy, mon->sidebarwin);
  free(mon);
}

void clientmessage(XEvent *e) {
  XWindowAttributes wa;
  XSetWindowAttributes swa;
  XClientMessageEvent *cme = &e->xclient;
  Client *c = wintoclient(cme->window);

  if (showsystray && cme->window == systray->win &&
      cme->message_type == netatom[NetSystemTrayOP]) {
    /* add systray icons */
    if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
      if (!(c = (Client *)calloc(1, sizeof(Client))))
        die("fatal: could not malloc() %u bytes\n", sizeof(Client));
      if (!(c->win = cme->data.l[2])) {
        free(c);
        return;
      }
      c->mon = selmon;
      c->next = systray->icons;
      systray->icons = c;
      if (!XGetWindowAttributes(dpy, c->win, &wa)) {
        /* use sane defaults */
        wa.width = bh;
        wa.height = bh;
        wa.border_width = 0;
      }
      c->x = c->oldx = c->y = c->oldy = 0;
      c->w = c->oldw = wa.width;
      c->h = c->oldh = wa.height;
      c->oldbw = wa.border_width;
      c->bw = 0;
      c->isfloating = True;
      /* reuse tags field as mapped status */
      c->tags = 1;
      updatesizehints(c);
      updatesystrayicongeom(c, wa.width, wa.height);
      XAddToSaveSet(dpy, c->win);
      XSelectInput(dpy, c->win,
                   StructureNotifyMask | PropertyChangeMask |
                       ResizeRedirectMask);
      XClassHint ch = {"dwmsystray", "dwmsystray"};
      XSetClassHint(dpy, c->win, &ch);
      XReparentWindow(dpy, c->win, systray->win, 0, 0);
      /* use parents background color */
      swa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
      XChangeWindowAttributes(dpy, c->win, CWBackPixel, &swa);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime,
                XEMBED_EMBEDDED_NOTIFY, 0, systray->win,
                XEMBED_EMBEDDED_VERSION);
      /* FIXME not sure if I have to send these events, too */
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime,
                XEMBED_FOCUS_IN, 0, systray->win, XEMBED_EMBEDDED_VERSION);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime,
                XEMBED_WINDOW_ACTIVATE, 0, systray->win,
                XEMBED_EMBEDDED_VERSION);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime,
                XEMBED_MODALITY_ON, 0, systray->win, XEMBED_EMBEDDED_VERSION);
      XSync(dpy, False);
      resizebarwin(selmon);
      updatesystray();
      setclientstate(c, NormalState);
    }
    return;
  }
  if (cme->message_type == netatom[NetCurrentDesktop]) {
    if (cme->data.l[0] >= 0 && (unsigned long)cme->data.l[0] < LENGTH(tags)) {
      Arg a = {.ui = 1 << cme->data.l[0]};
      view(&a);
    }
    return;
  }
  if (!c)
    return;
  if (cme->message_type == netatom[NetWMState]) {
    if (cme->data.l[1] == netatom[NetWMFullscreen] ||
        cme->data.l[2] == netatom[NetWMFullscreen])
      setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                        || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ &&
                            !c->isfullscreen)));
  } else if (cme->message_type == netatom[NetActiveWindow]) {
    if (c != selmon->sel && !c->isurgent)
      seturgent(c, 1);
  }
}

void configure(Client *c) {
  XConfigureEvent ce;

  ce.type = ConfigureNotify;
  ce.display = dpy;
  ce.event = c->win;
  ce.window = c->win;
  ce.x = c->x;
  ce.y = c->y;
  ce.width = c->w;
  ce.height = c->h;
  ce.border_width = c->bw;
  ce.above = None;
  ce.override_redirect = False;
  XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void configurenotify(XEvent *e) {
  Monitor *m;
  Client *c;
  XConfigureEvent *ev = &e->xconfigure;
  int dirty;

  /* TODO: updategeom handling sucks, needs to be simplified */
  if (ev->window == root) {
    dirty = (sw != ev->width || sh != ev->height);
    sw = ev->width;
    sh = ev->height;
    if (updategeom() || dirty) {
      drw_resize(drw, sw, sh);
      updatebars();
      for (m = mons; m; m = m->next) {
        for (c = m->clients; c; c = c->next)
          if (c->isfullscreen)
            resizeclient(c, m->mx, m->my, m->mw, m->mh);
        resizebarwin(m);
      }
      for (m = mons; m; m = m->next)
        if (m->sidebarvisible)
          drawsidebar(m);
      focus(NULL);
      arrange(NULL);
    }
  }
}

void configurerequest(XEvent *e) {
  Client *c;
  Monitor *m;
  XConfigureRequestEvent *ev = &e->xconfigurerequest;
  XWindowChanges wc;

  if ((c = wintoclient(ev->window))) {
    if (ev->value_mask & CWBorderWidth)
      c->bw = ev->border_width;
    else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
      m = c->mon;
      if (ev->value_mask & CWX) {
        c->oldx = c->x;
        c->x = m->mx + ev->x;
      }
      if (ev->value_mask & CWY) {
        c->oldy = c->y;
        c->y = m->my + ev->y;
      }
      if (ev->value_mask & CWWidth) {
        c->oldw = c->w;
        c->w = ev->width;
      }
      if (ev->value_mask & CWHeight) {
        c->oldh = c->h;
        c->h = ev->height;
      }
      if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
        c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
      if ((c->y + c->h) > m->my + m->mh && c->isfloating)
        c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
      if ((ev->value_mask & (CWX | CWY)) &&
          !(ev->value_mask & (CWWidth | CWHeight)))
        configure(c);
      if (ISVISIBLE(c))
        XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    } else
      configure(c);
  } else {
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
  }
  XSync(dpy, False);
}

Monitor *createmon(void) {
  Monitor *m;
  size_t i;

  m = ecalloc(1, sizeof(Monitor));
  m->tagset[0] = m->tagset[1] = 1;
  m->mfact = mfact;
  m->nmaster = nmaster;
  m->showbar = showbar;
  m->showtab = showtab;
  m->topbar = topbar;
  m->toptab = toptab;
  m->ntabs = 0;
  m->colorfultag = colorfultag ? colorfultag : 0;
  m->gappih = gappih;
  m->gappiv = gappiv;
  m->gappoh = gappoh;
  m->gappov = gappov;
  m->borderpx = borderpx;
  m->lt[0] = &layouts[0];
  m->lt[1] = &layouts[1 % LENGTH(layouts)];
  for (i = 0; i < LENGTH(tags); i++)
    m->tagmap[i] = 0;
  m->previewshow = 0;
  m->sidebarw = SIDEBAR_WIDTH;
  m->sidebarh = 1;
  strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
  m->pertag = ecalloc(1, sizeof(Pertag));
  m->pertag->curtag = m->pertag->prevtag = 1;

  for (i = 0; i <= LENGTH(tags); i++) {
    m->pertag->nmasters[i] = m->nmaster;
    m->pertag->mfacts[i] = m->mfact;

    m->pertag->ltidxs[i][0] = m->lt[0];
    m->pertag->ltidxs[i][1] = m->lt[1];
    m->pertag->sellts[i] = m->sellt;

    m->pertag->showbars[i] = m->showbar;
  }

  return m;
}

void cyclelayout(const Arg *arg) {
  Layout *l;
  for (l = (Layout *)layouts; l != selmon->lt[selmon->sellt]; l++)
    ;
  if (arg->i > 0) {
    if (l->symbol && (l + 1)->symbol)
      setlayout(&((Arg){.v = (l + 1)}));
    else
      setlayout(&((Arg){.v = layouts}));
  } else {
    if (l != layouts && (l - 1)->symbol)
      setlayout(&((Arg){.v = (l - 1)}));
    else
      setlayout(&((Arg){.v = &layouts[LENGTH(layouts) - 2]}));
  }
}

void destroynotify(XEvent *e) {
  Client *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;

  if ((c = wintoclient(ev->window)))
    unmanage(c, 1);
  else if ((c = wintosystrayicon(ev->window))) {
    removesystrayicon(c);
    resizebarwin(selmon);
    updatesystray();
  }
}

void detach(Client *c) {
  Client **tc;


  for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next)
    ;
  *tc = c->next;

}

void detachstack(Client *c) {
  Client **tc, *t;


  for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext)
    ;
  *tc = c->snext;

  if (c == c->mon->sel) {
    for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext)
      ;
    c->mon->sel = t;
  }

}

Monitor *dirtomon(int dir) {
  Monitor *m = NULL;

  if (dir > 0) {
    if (!(m = selmon->next))
      m = mons;
  } else if (selmon == mons)
    for (m = mons; m->next; m = m->next)
      ;
  else
    for (m = mons; m->next != selmon; m = m->next)
      ;
  return m;
}

unsigned int statuswidth(char *stext) {
  int i, w;
  short isCode = 0;
  char *text;
  char *p;
  size_t len = strlen(stext) + 1;

  if (!(text = (char *)malloc(sizeof(char) * len)))
    die("malloc");
  p = text;
  memcpy(text, stext, len);

  /* compute width of the status text */
  w = 0;
  i = -1;
  while (text[++i]) {
    if (text[i] == '^') {
      if (!isCode) {
        isCode = 1;
        text[i] = '\0';
        w += TEXTW(text) - lrpad;
        text[i] = '^';
        if (text[++i] == 'f')
          w += atoi(text + ++i);
      } else {
        isCode = 0;
        text = text + i + 1;
        i = -1;
      }
    }
  }
  if (!isCode)
    w += TEXTW(text) - lrpad;
  else
    isCode = 0;
  free(p);

  return w + horizpadbar;
}

int drawstatusbar(Monitor *m, int bh, char *stext) {
  int i, w, x, len;
  short isCode = 0;
  char *text;
  char *p;

  len = strlen(stext) + 1;
  if (!(text = (char *)malloc(sizeof(char) * len)))
    die("malloc");
  p = text;
  memcpy(text, stext, len);

  w = statuswidth(stext);
  x = horizpadbar / 2;

  XSetForeground(drw->dpy, drw->gc, clrborder.pixel);
  XFillRectangle(drw->dpy, drw->drawable, drw->gc, 0, 0, w, bh);

  drw_setscheme(drw, scheme[LENGTH(colors)]);
  drw->scheme[ColFg] = scheme[SchemeNorm][ColFg];
  drw->scheme[ColBg] = scheme[SchemeNorm][ColBg];
  drw_rect(drw, 0, borderpx, w, bh, 1, 1);

  /* process status text */
  i = -1;
  while (text[++i]) {
    if (text[i] == '^' && !isCode) {
      isCode = 1;

      text[i] = '\0';
      w = TEXTW(text) - lrpad;
      drw_text(drw, x, borderpx + vertpadbar / 2, w, bh - vertpadbar, 0, text,
               0);

      x += w;

      /* process code */
      while (text[++i] != '^') {
        if (text[i] == 'c') {
          char buf[8];
          memcpy(buf, (char *)text + i + 1, 7);
          buf[7] = '\0';
          drw_clr_create(drw, &drw->scheme[ColFg], buf);
          i += 7;
        } else if (text[i] == 'b') {
          char buf[8];
          memcpy(buf, (char *)text + i + 1, 7);
          buf[7] = '\0';
          drw_clr_create(drw, &drw->scheme[ColBg], buf);
          i += 7;
        } else if (text[i] == 'd') {
          drw->scheme[ColFg] = scheme[SchemeNorm][ColFg];
          drw->scheme[ColBg] = scheme[SchemeNorm][ColBg];
        } else if (text[i] == 'r') {
          int rx = atoi(text + ++i);
          while (text[++i] != ',')
            ;
          int ry = atoi(text + ++i);
          while (text[++i] != ',')
            ;
          int rw = atoi(text + ++i);
          while (text[++i] != ',')
            ;
          int rh = atoi(text + ++i);

          drw_rect(drw, rx + x, ry + borderpx + vertpadbar / 2, rw, rh, 1, 0);
        } else if (text[i] == 'f') {
          x += atoi(text + ++i);
        }
      }

      text = text + i + 1;
      i = -1;
      isCode = 0;
    }
  }

  if (!isCode) {
    w = TEXTW(text) - lrpad;
    drw_text(drw, x, borderpx + vertpadbar / 2, w, bh - vertpadbar, 0, text, 0);
  }

  drw_setscheme(drw, scheme[SchemeNorm]);
  free(p);

  return statuswidth(stext);
}

void dragcfact(const Arg *arg) {
  int prev_x, prev_y, dist_x, dist_y;
  float fact;
  Client *c;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfloating) {
    resizemouse(arg);
    return;
  }
#if !FAKEFULLSCREEN_PATCH
#if FAKEFULLSCREEN_CLIENT_PATCH
  if (c->isfullscreen &&
      !c->fakefullscreen) /* no support resizing fullscreen windows by mouse */
    return;
#else
  if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
    return;
#endif // FAKEFULLSCREEN_CLIENT_PATCH
#endif // !FAKEFULLSCREEN_PATCH
  restack(selmon);

  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
    return;
  XSync(dpy, False);
  setworkspaceanimation(selmon, 1);
  int prev_animate = animate_tiled;
  /* disable animated tiled resize while dragging to use instant resizeclient */
  animate_tiled = 0;
  prev_x = prev_y = -999999;

  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 120))
        continue;
      lasttime = ev.xmotion.time;
      if (prev_x == -999999) {
        prev_x = ev.xmotion.x_root;
        prev_y = ev.xmotion.y_root;
      }

      dist_x = ev.xmotion.x - prev_x;
      dist_y = ev.xmotion.y - prev_y;

      if (abs(dist_x) > abs(dist_y)) {
        fact = (float)4.0 * dist_x / c->mon->ww;
      } else {
        fact = (float)-4.0 * dist_y / c->mon->wh;
      }

      if (fact)
        setcfact(&((Arg){.f = fact}));

      prev_x = ev.xmotion.x;
      prev_y = ev.xmotion.y;
      break;
    }
  } while (ev.type != ButtonRelease);

  XUngrabPointer(dpy, CurrentTime);
  setworkspaceanimation(selmon, 0);
  animate_tiled = prev_animate;
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    ;
}

void dragmfact(const Arg *arg) {
  unsigned int n;
  int py, px;         // pointer coordinates
  int ax, ay, aw, ah; // area position, width and height
  int center = 0, horizontal = 0, mirror = 0, fixed = 0; // layout configuration
  double fact;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  m = selmon;

#if VANITYGAPS_PATCH
  int oh, ov, ih, iv;
  getgaps(m, &oh, &ov, &ih, &iv, &n);
#else
  Client *c;
  for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++)
    ;
#endif // VANITYGAPS_PATCH

  ax = m->wx;
  ay = m->wy;
  ah = m->wh;
  aw = m->ww;

  if (!n)
    return;
#if FLEXTILE_DELUXE_LAYOUT
  else if (m->lt[m->sellt]->arrange == &flextile) {
    int layout = m->ltaxis[LAYOUT];
    if (layout < 0) {
      mirror = 1;
      layout *= -1;
    }
    if (layout > FLOATING_MASTER) {
      layout -= FLOATING_MASTER;
      fixed = 1;
    }

    if (layout == SPLIT_HORIZONTAL || layout == SPLIT_HORIZONTAL_DUAL_STACK)
      horizontal = 1;
    else if (layout == SPLIT_CENTERED_VERTICAL && (fixed || n - m->nmaster > 1))
      center = 1;
    else if (layout == FLOATING_MASTER) {
      center = 1;
      if (aw < ah)
        horizontal = 1;
    } else if (layout == SPLIT_CENTERED_HORIZONTAL) {
      if (fixed || n - m->nmaster > 1)
        center = 1;
      horizontal = 1;
    }
  }
#endif // FLEXTILE_DELUXE_LAYOUT
#if CENTEREDMASTER_LAYOUT
  else if (m->lt[m->sellt]->arrange == &centeredmaster &&
           (fixed || n - m->nmaster > 1))
    center = 1;
#endif // CENTEREDMASTER_LAYOUT
#if CENTEREDFLOATINGMASTER_LAYOUT
  else if (m->lt[m->sellt]->arrange == &centeredfloatingmaster)
    center = 1;
#endif // CENTEREDFLOATINGMASTER_LAYOUT
#if BSTACK_LAYOUT
  else if (m->lt[m->sellt]->arrange == &bstack)
    horizontal = 1;
#endif // BSTACK_LAYOUT
#if BSTACKHORIZ_LAYOUT
  else if (m->lt[m->sellt]->arrange == &bstackhoriz)
    horizontal = 1;
#endif // BSTACKHORIZ_LAYOUT

  /* do not allow mfact to be modified under certain conditions */
  if (!m->lt[m->sellt]->arrange                    // floating layout
      || (!fixed && m->nmaster && n <= m->nmaster) // no master
#if MONOCLE_LAYOUT
      || m->lt[m->sellt]->arrange == &monocle
#endif // MONOCLE_LAYOUT
#if GRIDMODE_LAYOUT
      || m->lt[m->sellt]->arrange == &grid
#endif // GRIDMODE_LAYOUT
#if HORIZGRID_LAYOUT
      || m->lt[m->sellt]->arrange == &horizgrid
#endif // HORIZGRID_LAYOUT
#if GAPPLESSGRID_LAYOUT
      || m->lt[m->sellt]->arrange == &gaplessgrid
#endif // GAPPLESSGRID_LAYOUT
#if NROWGRID_LAYOUT
      || m->lt[m->sellt]->arrange == &nrowgrid
#endif // NROWGRID_LAYOUT
#if FLEXTILE_DELUXE_LAYOUT
      ||
      (m->lt[m->sellt]->arrange == &flextile && m->ltaxis[LAYOUT] == NO_SPLIT)
#endif // FLEXTILE_DELUXE_LAYOUT
  )
    return;

#if VANITYGAPS_PATCH
  ay += oh;
  ax += ov;
  aw -= 2 * ov;
  ah -= 2 * oh;
#endif // VANITYGAPS_PATCH

  if (center) {
    if (horizontal) {
      px = ax + aw / 2;
#if VANITYGAPS_PATCH
      py = ay + ah / 2 + (ah - 2 * ih) * (m->mfact / 2.0) + ih / 2;
#else
      py = ay + ah / 2 + ah * m->mfact / 2.0;
#endif       // VANITYGAPS_PATCH
    } else { // vertical split
#if VANITYGAPS_PATCH
      px = ax + aw / 2 + (aw - 2 * iv) * m->mfact / 2.0 + iv / 2;
#else
      px = ax + aw / 2 + aw * m->mfact / 2.0;
#endif // VANITYGAPS_PATCH
      py = ay + ah / 2;
    }
  } else if (horizontal) {
    px = ax + aw / 2;
    if (mirror)
#if VANITYGAPS_PATCH
      py = ay + (ah - ih) * (1.0 - m->mfact) + ih / 2;
#else
      py = ay + (ah * (1.0 - m->mfact));
#endif // VANITYGAPS_PATCH
    else
#if VANITYGAPS_PATCH
      py = ay + ((ah - ih) * m->mfact) + ih / 2;
#else
      py = ay + (ah * m->mfact);
#endif     // VANITYGAPS_PATCH
  } else { // vertical split
    if (mirror)
#if VANITYGAPS_PATCH
      px = ax + (aw - iv) * (1.0 - m->mfact) + iv / 2;
#else
      px = ax + (aw * m->mfact);
#endif // VANITYGAPS_PATCH
    else
#if VANITYGAPS_PATCH
      px = ax + ((aw - iv) * m->mfact) + iv / 2;
#else
      px = ax + (aw * m->mfact);
#endif // VANITYGAPS_PATCH
    py = ay + ah / 2;
  }

  if (XGrabPointer(
          dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None,
          cursor[horizontal ? CurResizeVertArrow : CurResizeHorzArrow]->cursor,
          CurrentTime) != GrabSuccess)
    return;
  XSync(dpy, False);
  if (m->sel)
    setworkspaceanimation(m, 1);

  int prev_animate = animate_tiled;
  /* disable animated tiled resize while dragging to use instant resizeclient */
  animate_tiled = 0;

  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 40))
        continue;
      if (lasttime != 0) {
        px = ev.xmotion.x;
        py = ev.xmotion.y;
      }
      lasttime = ev.xmotion.time;

#if VANITYGAPS_PATCH
      if (center)
        if (horizontal)
          if (py - ay > ah / 2)
            fact = (double)1.0 -
                   (ay + ah - py - ih / 2) * 2 / (double)(ah - 2 * ih);
          else
            fact = (double)1.0 - (py - ay - ih / 2) * 2 / (double)(ah - 2 * ih);
        else if (px - ax > aw / 2)
          fact =
              (double)1.0 - (ax + aw - px - iv / 2) * 2 / (double)(aw - 2 * iv);
        else
          fact = (double)1.0 - (px - ax - iv / 2) * 2 / (double)(aw - 2 * iv);
      else if (horizontal)
        fact = (double)(py - ay - ih / 2) / (double)(ah - ih);
      else
        fact = (double)(px - ax - iv / 2) / (double)(aw - iv);
#else
      if (center)
        if (horizontal)
          if (py - ay > ah / 2)
            fact = (double)1.0 - (ay + ah - py) * 2 / (double)ah;
          else
            fact = (double)1.0 - (py - ay) * 2 / (double)ah;
        else if (px - ax > aw / 2)
          fact = (double)1.0 - (ax + aw - px) * 2 / (double)aw;
        else
          fact = (double)1.0 - (px - ax) * 2 / (double)aw;
      else if (horizontal)
        fact = (double)(py - ay) / (double)ah;
      else
        fact = (double)(px - ax) / (double)aw;
#endif // VANITYGAPS_PATCH

      if (!center && mirror)
        fact = 1.0 - fact;

      setmfact(&((Arg){.f = 1.0 + fact}));
      px = ev.xmotion.x;
      py = ev.xmotion.y;
      break;
    }
  } while (ev.type != ButtonRelease);

  XUngrabPointer(dpy, CurrentTime);
  if (m->sel)
    setworkspaceanimation(m, 0);
  animate_tiled = prev_animate;
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    ;
}

static unsigned int tagsbarwidth(Monitor *m) {
  unsigned int i, w = 0;
  for (i = 0; i < LENGTH(tags); i++)
    w += TEXTW(tags[i]);
  return borderpx + w + TEXTW(m->ltsymbol);
}

static unsigned int bartitlewidth(Monitor *m, unsigned int budget) {
  unsigned int i, w = 0;
  for (i = 0; i < LENGTH(launchers); i++)
    w += TEXTW(launchers[i].name);
  if (m->sel) {
    w += m->sel->icon ? m->sel->icw + ICONSPACING : 0;
    w += drw_fontset_getwidth(drw, m->sel->name) + lrpad;
  }
  return MIN(w, budget);
}

unsigned int clockwidth(void) {
  return TEXTW(clockstr) - lrpad + 2 * clockpad;
}

void getkblayout(void) {
  /* Read _XKB_RULES_NAMES to get raw layout codes like "us,th" */
  Atom xkbRulesNames = XInternAtom(dpy, "_XKB_RULES_NAMES", True);
  if (xkbRulesNames) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    if (XGetWindowProperty(dpy, root, xkbRulesNames, 0, 1024, False,
                           XA_STRING, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) == Success
        && actual_type == XA_STRING && nitems > 0) {
      /* Prop contains: rules\0model\0layout\0variant\0options (null-separated) */
      int entry = 0;
      char *p = (char *)prop;
      char *layout_start = NULL;
      while (*p && entry < 3) {
        if (entry == 2) {
          layout_start = p;
          break;
        }
        /* skip to next null-terminated string */
        while (*p) p++;
        p++; /* skip null */
        entry++;
      }
      if (layout_start) {
        /* layout_start now points to "us,th" or similar */
        XkbStateRec state;
        if (XkbGetState(dpy, XkbUseCoreKbd, &state) == Success) {
          int group = state.group;
          /* Split layout_start by comma and pick group index */
          char *saveptr;
          char *token;
          int idx = 0;
          char *tmp = strdup(layout_start);
          if (tmp) {
            token = strtok_r(tmp, ",", &saveptr);
            while (token) {
              if (idx == group) {
                size_t len = strlen(token);
                if (len < sizeof(kbstr))
                  strcpy(kbstr, token);
                break;
              }
              token = strtok_r(NULL, ",", &saveptr);
              idx++;
            }
            free(tmp);
          }
        }
      }
      XFree(prop);
    }
  }
  /* Fallback: if kbstr is empty */
  if (kbstr[0] == '\0')
    strcpy(kbstr, "us");
}

unsigned int kblayoutwidth(void) {
  return TEXTW(kbstr) - lrpad + 2 * kblayoutpad;
}

static void barwidths(Monitor *m, unsigned int *tw, unsigned int *tlw,
                      unsigned int *cw, unsigned int *kw, unsigned int *rw) {
  unsigned int avail, barW, stw = 0, stgap = 0;
  if (showsystray && m == systraytomon(m)) {
    stw = getsystraywidth();
    if (stw)
      stgap = bargap;
  }
  barW = (floatbar ? m->ww - 2 * m->gappov : m->ww) - stw;
  *rw = statuswidth(stext);
  clockbar_cw = clockwidth();
  *cw = (clockbar_dynw && m == selmon) ? clockbar_dynw : clockbar_cw;
  *kw = kblayoutwidth();
  *tw = tagsbarwidth(m);
  avail = (barW > *tw + *rw + *cw + *kw + 4 * bargap + stgap)
              ? barW - *tw - *rw - *cw - *kw - 4 * bargap - stgap
              : 0;
  *tlw = borderpx + bartitlewidth(m, avail > borderpx ? avail - borderpx : 0);
  if (*tlw > avail)
    *tlw = avail;
}

void drawbar(Monitor *m) {
  int y = borderpx;
  int bh_n = bh - borderpx * 2;
  int boxs = drw->fonts->h / 9;
  int boxw = drw->fonts->h / 6 + 2;
  int x, w;
  unsigned int i, occ = 0, urg = 0;
  unsigned int tw, tlw, cw, kw, rw;
  Client *c;

  if (!m->showbar) {
    if (m == selmon)
      clockbar_win = 0;
    return;
  }

  for (c = m->clients; c; c = c->next) {
    occ |= (c->issticky ? c->oldtags : c->tags);
    if (c->isurgent)
      urg |= (c->issticky ? c->oldtags : c->tags);
  }

  barwidths(m, &tw, &tlw, &cw, &kw, &rw);
  resizebarwin(m);

  XSetForeground(drw->dpy, drw->gc, clrborder.pixel);
  XFillRectangle(drw->dpy, drw->drawable, drw->gc, 0, 0, tw, bh);

  /* tags bar: tags + layout indicator, fixed size */
  x = borderpx;
  for (i = 0; i < LENGTH(tags); i++) {
    w = TEXTW(tags[i]);
    drw_setscheme(
        drw, scheme[occ & 1 << i ? (m->colorfultag ? tagschemes[i] : SchemeSel)
                                 : SchemeTag]);
    drw_text(drw, x, y, w, bh_n, lrpad / 2, tags[i], urg & 1 << i);
    if (ulineall ||
        m->tagset[m->seltags] &
            1 << i) /* if there are conflicts, just move these lines directly
                       underneath both 'drw_setscheme' and 'drw_text' :) */
      drw_rect(drw, x + ulinepad, bh_n - ulinestroke - ulinevoffset,
               w - (ulinepad * 2), ulinestroke, 1, 0);
    x += w;
  }
  w = TEXTW(m->ltsymbol);
  drw_setscheme(drw, scheme[SchemeLayout]);
  drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);
  drw_map(drw, m->barwin, 0, 0, tw, bh);

  /* title bar: arch logo + app icon + app name, dynamic size */
  XSetForeground(drw->dpy, drw->gc, clrborder.pixel);
  XFillRectangle(drw->dpy, drw->drawable, drw->gc, 0, 0, tlw, bh);
  x = borderpx;
  for (i = 0; i < LENGTH(launchers); i++) {
    w = TEXTW(launchers[i].name);
    drw_setscheme(drw, scheme[SchemeLayout]);
    drw_text(drw, x, 0, w, bh, lrpad / 2, launchers[i].name, 0);
    x += w;
  }
  if (m->sel) {
    w = tlw - x;
    if (w > 0) {
      drw_setscheme(drw, scheme[m == selmon ? SchemeTitle : SchemeNorm]);
      drw_text(drw, x, 0, w, bh,
               lrpad / 2 + (m->sel->icon ? m->sel->icw + ICONSPACING : 0),
               m->sel->name, 0);
      if (m->sel->icon)
        drw_pic(drw, x + lrpad / 2, (bh - m->sel->ich) / 2, m->sel->icw,
                m->sel->ich, m->sel->icon);
      if (m->sel->isfloating)
        drw_rect(drw, x + boxs, boxs, boxw, boxw, m->sel->isfixed, 0);
    }
  }
  drw_map(drw, m->bartitlewin, 0, 0, tlw, bh);

  /* center bar: animated 24h clock (only on focused monitor) */
  unsigned int dcw = (clockbar_dynw && m == selmon) ? clockbar_dynw : cw;
  unsigned int tcw = clockbar_cw;
  XSetForeground(drw->dpy, drw->gc, clrborder.pixel);
  XFillRectangle(drw->dpy, drw->drawable, drw->gc, 0, 0, dcw, bh);
  drw_setscheme(drw, scheme[m == selmon ? SchemeTitle : SchemeNorm]);
  drw_rect(drw, 0, borderpx, dcw, bh_n, 1, 1);
  drw_text(drw, dcw > tcw ? (int)((dcw - tcw) / 2) : 0,
           borderpx + vertpadbar / 2, tcw, bh - vertpadbar, clockpad, clockstr,
           0);
  drw_map(drw, m->barcenterwin, 0, 0, dcw, bh);

  /* center bar: keyboard layout */
  XSetForeground(drw->dpy, drw->gc, clrborder.pixel);
  XFillRectangle(drw->dpy, drw->drawable, drw->gc, 0, 0, kw, bh);
  drw_setscheme(drw, scheme[m == selmon ? SchemeTitle : SchemeNorm]);
  drw_rect(drw, 0, borderpx, kw, bh_n, 1, 1);
  drw_text(drw, 0, borderpx + vertpadbar / 2, kw, bh - vertpadbar, kblayoutpad,
           kbstr, 0);
  drw_map(drw, m->barkbwin, 0, 0, kw, bh);

  /* right bar: xsetroot status text */
  drawstatusbar(m, bh_n, stext);
  drw_map(drw, m->barrightwin, 0, 0, rw, bh);
}

static uint32_t prealpha(uint32_t p) {
  uint8_t a = p >> 24u;
  uint32_t rb = (a * (p & 0xFF00FFu)) >> 8u;
  uint32_t g = (a * (p & 0x00FF00u)) >> 8u;
  return (rb & 0xFF00FFu) | (g & 0x00FF00u) | (a << 24u);
}

Picture geticonprop(Window win, unsigned int *picw, unsigned int *pich, unsigned int *pw, unsigned int *ph) {
  int format;
  unsigned long n, extra, *p = NULL;
  Atom real;

  if (XGetWindowProperty(dpy, win, netatom[NetWMIcon], 0L, LONG_MAX, False,
                         AnyPropertyType, &real, &format, &n, &extra,
                         (unsigned char **)&p) != Success)
    return None;
  if (n == 0 || format != 32) {
    XFree(p);
    return None;
  }

  unsigned long *bstp = NULL;
  uint32_t w, h, sz;
  {
    unsigned long *i;
    const unsigned long *end = p + n;
    uint32_t bstd = UINT32_MAX, d, m;
    for (i = p; i < end - 1; i += sz) {
      if ((w = *i++) >= 16384 || (h = *i++) >= 16384) {
        XFree(p);
        return None;
      }
      if ((sz = w * h) > end - i)
        break;
      if ((m = w > h ? w : h) >= ICONSIZE && (d = m - ICONSIZE) < bstd) {
        bstd = d;
        bstp = i;
      }
    }
    if (!bstp) {
      for (i = p; i < end - 1; i += sz) {
        if ((w = *i++) >= 16384 || (h = *i++) >= 16384) {
          XFree(p);
          return None;
        }
        if ((sz = w * h) > end - i)
          break;
        if ((d = ICONSIZE - (w > h ? w : h)) < bstd) {
          bstd = d;
          bstp = i;
        }
      }
    }
    if (!bstp) {
      XFree(p);
      return None;
    }
  }

  if ((w = *(bstp - 2)) == 0 || (h = *(bstp - 1)) == 0) {
    XFree(p);
    return None;
  }

  uint32_t icw, ich;
  if (w <= h) {
    ich = ICONSIZE;
    icw = w * ICONSIZE / h;
    if (icw == 0)
      icw = 1;
  } else {
    icw = ICONSIZE;
    ich = h * ICONSIZE / w;
    if (ich == 0)
      ich = 1;
  }
  *picw = icw;
  *pich = ich;

  uint32_t i, *bstp32 = (uint32_t *)bstp;
  for (sz = w * h, i = 0; i < sz; ++i)
    bstp32[i] = prealpha(bstp[i]);

  Picture ret = drw_picture_create_resized(drw, (char *)bstp, w, h, icw, ich);
  if (ret) {
    if (w <= 2 * icw && h <= 2 * ich) {
      *pw = w; *ph = h; /* XRender path: picture pixmap at original source size */
    } else {
      *pw = icw; *ph = ich; /* Imlib2 path: picture pixmap at target size */
    }
  } else {
    *pw = 0; *ph = 0;
  }
  XFree(p);

  return ret;
}

void drawbars(void) {
  Monitor *m;

  for (m = mons; m; m = m->next)
    drawbar(m);
}

void drawtabs(void) {
  Monitor *m;

  for (m = mons; m; m = m->next)
    drawtab(m);
}

static int cmpint(const void *p1, const void *p2) {
  /* The actual arguments to this function are "pointers to
     pointers to char", but strcmp(3) arguments are "pointers
     to char", hence the following cast plus dereference */
  return *((int *)p1) > *(int *)p2;
}

void drawtab(Monitor *m) {
  Client *c;
  int i;
  char *btn_prev = "";
  char *btn_next = "";
  char *btn_close = " ";
  int buttons_w = 0;
  int sorted_label_widths[MAXTABS];
  int tot_width = 0;
  int maxsize = bh;
  int x = 0;
  int w = 0;
  int mw = floatbar ? m->ww - 2 * m->gappov : m->ww;
  buttons_w += TEXTW(btn_prev) - lrpad + horizpadtabo;
  buttons_w += TEXTW(btn_next) - lrpad + horizpadtabo;
  buttons_w += TEXTW(btn_close) - lrpad + horizpadtabo;
  tot_width = buttons_w;

  /* Calculates number of labels and their width */
  m->ntabs = 0;
  for (c = m->clients; c; c = c->next) {
    if (!ISVISIBLE(c))
      continue;
    m->tab_widths[m->ntabs] =
        MIN(TEXTW(c->name) - lrpad + horizpadtabi + horizpadtabo, 250);
    tot_width += m->tab_widths[m->ntabs];
    ++m->ntabs;
    if (m->ntabs >= MAXTABS)
      break;
  }

  if (tot_width >
      mw) { // not enough space to display the labels, they need to be truncated
    memcpy(sorted_label_widths, m->tab_widths, sizeof(int) * m->ntabs);
    qsort(sorted_label_widths, m->ntabs, sizeof(int), cmpint);
    for (i = 0; i < m->ntabs; ++i) {
      if (tot_width + (m->ntabs - i) * sorted_label_widths[i] > mw)
        break;
      tot_width += sorted_label_widths[i];
    }
    maxsize = (mw - tot_width) / (m->ntabs - i);
    maxsize = (m->ww - tot_width) / (m->ntabs - i);
  } else {
    maxsize = mw;
  }
  i = 0;

  /* cleans window */
  drw_setscheme(drw, scheme[TabNorm]);
  drw_rect(drw, 0, 0, mw, th, 1, 1);

  for (c = m->clients; c; c = c->next) {
    if (!ISVISIBLE(c))
      continue;
    if (i >= m->ntabs)
      break;
    if (m->tab_widths[i] > maxsize)
      m->tab_widths[i] = maxsize;
    w = m->tab_widths[i];
    drw_setscheme(drw, scheme[(c == m->sel) ? TabSel : TabNorm]);
    drw_text(drw, x + horizpadtabo / 2, vertpadbar / 2, w - horizpadtabo,
             th - vertpadbar, horizpadtabi / 2, c->name, 0);
    x += w;
    ++i;
  }

  w = mw - buttons_w - x;
  x += w;
  drw_setscheme(drw, scheme[SchemeBtnPrev]);
  w = TEXTW(btn_prev) - lrpad + horizpadtabo;
  m->tab_btn_w[0] = w;
  drw_text(drw, x + horizpadtabo / 2, vertpadbar / 2, w, th - vertpadbar, 0,
           btn_prev, 0);
  x += w;
  drw_setscheme(drw, scheme[SchemeBtnNext]);
  w = TEXTW(btn_next) - lrpad + horizpadtabo;
  m->tab_btn_w[1] = w;
  drw_text(drw, x + horizpadtabo / 2, vertpadbar / 2, w, th - vertpadbar, 0,
           btn_next, 0);
  x += w;
  drw_setscheme(drw, scheme[SchemeBtnClose]);
  w = TEXTW(btn_close) - lrpad + horizpadtabo;
  m->tab_btn_w[2] = w;
  drw_text(drw, x + horizpadtabo / 2, vertpadbar / 2, w, th - vertpadbar, 0,
           btn_close, 0);
  x += w;

  drw_map(drw, m->tabwin, 0, 0, m->ww, th);
}

void calcsidebar(Monitor *m) {
  Client *c;
  int cnt = 0;
  for (c = m->clients; c; c = c->next)
    if (ISVISIBLE(c) && !HIDDEN(c))
      cnt++;

  if (cnt == 0) {
    m->sidebarh = m->mh / 3;
    m->sidebarw = SIDEBAR_WIDTH;
    return;
  }

  int base;
  if (cnt > 7) {
    base = 30;
    m->sidebarw = 48;
  } else if (cnt > 3) {
    base = 42;
    m->sidebarw = SIDEBAR_WIDTH;
  } else {
    base = 48;
    m->sidebarw = SIDEBAR_WIDTH;
  }

  m->sidebarh = cnt * base + 8;
  if (m->sidebarh > m->mh * 2 / 5)
    m->sidebarh = m->mh * 2 / 5;
  if (m->sidebarh < 50)
    m->sidebarh = 50;
}

void drawsidebar(Monitor *m) {
  if (!m->sidebarvisible)
    return;

  Client *c;
  int count = 0;
  Client *visible[MAXTABS];

  for (c = m->clients; c; c = c->next) {
    if (ISVISIBLE(c) && !HIDDEN(c)) {
      visible[count++] = c;
      if (count >= MAXTABS)
        break;
    }
  }

  if (!count) {
    XUnmapWindow(dpy, m->sidebarwin);
    m->sidebarvisible = 0;
    return;
  }

  calcsidebar(m);
  int sh = m->sidebarh;
  int sw = m->sidebarw;

  /* reposition window to centered y */
  int sidebary = m->my + (m->mh - sh) / 2;
  XMoveResizeWindow(dpy, m->sidebarwin, m->mx + m->gappov * 2, sidebary, sw, sh);

  int margin = 6;
  int pad = 4;
  int content_h = sh - 2 * margin;
  int itemh = (content_h - pad * (count - 1)) / count;

  /* find current workspace tag color index */
  int tagcolor_idx = SchemeTag1;
  for (int t = 0; t < LENGTH(tags); t++) {
    if (m->tagset[m->seltags] & (1 << t)) {
      tagcolor_idx = tagschemes[t];
      break;
    }
  }

  /* clear background */
  drw_setscheme(drw, scheme[SchemeNorm]);
  drw_rect(drw, 0, 0, sw, sh, 1, 1);

  for (int i = 0; i < count; i++) {
    c = visible[i];
    int y = margin + i * (itemh + pad);

    /* focus indicator — only for current focus, small indicator */
    if (c == m->sel) {
      drw_setscheme(drw, scheme[tagcolor_idx]);
      drw_rect(drw, 2, y + itemh / 2 - 5, 3, 10, 1, 0);
    }

    /* draw app icon if available */
    if (c->icon && c->icw > 0 && c->ic_pw > 0) {
      int icon_h = itemh - pad;
      if (icon_h > sw - 2 * margin) icon_h = sw - 2 * margin;
      if (icon_h < 16) icon_h = 16;
      int icon_y = y + (itemh - icon_h) / 2;
      int icon_x = (sw - icon_h) / 2;
      /* set sidebar-scale transform */
      XTransform xf;
      xf.matrix[0][0] = (c->ic_pw << 16) / icon_h;
      xf.matrix[0][1] = 0; xf.matrix[0][2] = 0;
      xf.matrix[1][0] = 0; xf.matrix[1][1] = (c->ic_ph << 16) / icon_h;
      xf.matrix[1][2] = 0;
      xf.matrix[2][0] = 0; xf.matrix[2][1] = 0; xf.matrix[2][2] = 65536;
      XRenderSetPictureTransform(drw->dpy, c->icon, &xf);
      drw_pic(drw, icon_x, icon_y, icon_h, icon_h, c->icon);
      /* restore original transform */
      xf.matrix[0][0] = (c->ic_pw << 16) / c->icw;
      xf.matrix[1][1] = (c->ic_ph << 16) / c->ich;
      XRenderSetPictureTransform(drw->dpy, c->icon, &xf);
    } else {
      /* fallback */
      drw_setscheme(drw, scheme[c == m->sel ? SchemeSel : SchemeNorm]);
      drw_rect(drw, 4, y + 2, itemh - 4, itemh - 4, 1, 0);
    }
  }

  drw_map(drw, m->sidebarwin, 0, 0, sw, sh);
}

void drawsidbars(void) {
  Monitor *m;
  for (m = mons; m; m = m->next)
    drawsidebar(m);
}

void showsidbar(Monitor *m) {
  Client *c;
  int count = 0;
  /* only show if there are visible apps */
  for (c = m->clients; c; c = c->next)
    if (ISVISIBLE(c) && !HIDDEN(c)) {
      count++;
      break;
    }
  if (!count) {
    hideosidebar(m);
    return;
  }
  if (!m->sidebarvisible) {
    m->sidebarvisible = 1;
    XMapWindow(dpy, m->sidebarwin);
    XRaiseWindow(dpy, m->sidebarwin);
    drawsidebar(m);
  }
}

void hideosidebar(Monitor *m) {
  if (m->sidebarvisible) {
    m->sidebarvisible = 0;
    XUnmapWindow(dpy, m->sidebarwin);
  }
}

void alttab(const Arg *arg) {
  static int alttab_idx = 0;
  static int alttab_count = 0;
  static Client *alttab_list[MAXTABS];
  int forward = arg->i;
  Client *c;

  /* build candidate list */
  alttab_count = 0;
  for (c = selmon->clients; c; c = c->next)
    if (ISVISIBLE(c) && !HIDDEN(c))
      alttab_list[alttab_count++] = c;

  if (alttab_count == 0)
    return;

  alttab_idx = (alttab_idx + forward + alttab_count) % alttab_count;
  focus(alttab_list[alttab_idx]);
  showsidbar(selmon);
  drawsidebar(selmon);
}

void enternotify(XEvent *e) {
  Client *c;
  Monitor *m;
  XCrossingEvent *ev = &e->xcrossing;

  if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) &&
      ev->window != root)
    return;
  c = wintoclient(ev->window);
  m = c ? c->mon : wintomon(ev->window);
  if (m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
  } else if (!c || c == selmon->sel)
    return;
  focus(c);
}

void expose(XEvent *e) {
  Monitor *m;
  XExposeEvent *ev = &e->xexpose;

  if (ev->count == 0 && (m = wintomon(ev->window))) {
    drawbar(m);
    if (m == selmon)
      updatesystray();
  }
}

void write_fullscreen(int value) {
  FILE *f = fopen("/tmp/isfullscreen", "w");
  if (!f)
    return;

  fprintf(f, "%d", value); // write 0 or 1
  fclose(f);
}

static bool fullscreen_st = false;

static void updatefullscreenstatus(Client *c) {
  bool isfullscreen = c && c->isfullscreen && !c->issticky;

  if (fullscreen_st == isfullscreen)
    return;

  write_fullscreen(isfullscreen);
  fullscreen_st = isfullscreen;
}

void focus(Client *c) {
  if (!c || (!ISVISIBLE(c) || HIDDEN(c)))
    for (c = selmon->stack; c && (!ISVISIBLE(c) || HIDDEN(c) || c->issticky); c = c->snext)
      ;
  if (selmon->sel && selmon->sel != c)
    unfocus(selmon->sel, 0);
  if (c) {
    if (c->mon != selmon)
      selmon = c->mon;
    if (c->isurgent)
      seturgent(c, 0);
    detachstack(c);
    attachstack(c);
    grabbuttons(c, 1);
    XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
    setfocus(c);
  } else {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
  selmon->sel = c;

  updatefullscreenstatus(c);

  drawbars();
  drawtabs();
  drawsidbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent *e) {
  XFocusChangeEvent *ev = &e->xfocus;

  if (selmon->sel && ev->window != selmon->sel->win)
    setfocus(selmon->sel);
}

void focusmon(const Arg *arg) {
  Monitor *m;

  if (!mons->next)
    return;
  if ((m = dirtomon(arg->i)) == selmon)
    return;
  unfocus(selmon->sel, 0);
  selmon = m;
  focus(NULL);
}

void focusstack(const Arg *arg) {
  Client *c = NULL, *i;

  if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
    return;
  if (arg->i > 0) {
    for (c = selmon->sel->next;
         c && (!ISVISIBLE(c) || HIDDEN(c) || c->issticky); c = c->next)
      ;
    if (!c)
      for (c = selmon->clients;
           c && (!ISVISIBLE(c) || HIDDEN(c) || c->issticky); c = c->next)
        ;
  } else {
    for (i = selmon->clients; i != selmon->sel; i = i->next)
      if (ISVISIBLE(i) && !HIDDEN(i) && !i->issticky)
        c = i;
    if (!c)
      for (; i; i = i->next)
        if (ISVISIBLE(i) && !HIDDEN(i) && !i->issticky)
          c = i;
  }
  if (c) {
    focus(c);
    restack(selmon);
    XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
                 c->w / 2, c->h / 2);
    XSync(dpy, False);
  }
}

void focuswin(const Arg *arg) {
  int iwin = arg->i;
  Client *c = NULL;
  for (c = selmon->clients; c && (iwin || !ISVISIBLE(c)); c = c->next) {
    if (ISVISIBLE(c) && !c->issticky)
      --iwin;
  };
  if (c) {
    focus(c);
    restack(selmon);
  }
}

Atom getatomprop(Client *c, Atom prop) {
  int di;
  unsigned long dl;
  unsigned char *p = NULL;
  Atom da, atom = None;
  /* FIXME getatomprop should return the number of items and a pointer to
   * the stored data instead of this workaround */
  Atom req = XA_ATOM;
  if (prop == xatom[XembedInfo])
    req = xatom[XembedInfo];

  if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, req, &da,
                         &di, &dl, &dl, &p) == Success &&
      p) {
    atom = *(Atom *)p;
    if (da == xatom[XembedInfo] && dl == 2)
      atom = ((Atom *)p)[1];
    XFree(p);
  }
  return atom;
}

int getrootptr(int *x, int *y) {
  int di;
  unsigned int dui;
  Window dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

/* Resolve a managed client from an arbitrary descendant window (true window
 * under the pointer). recttoclient() only considers tiled clients, which
 * desynchronised border/focus from the actual top window when floats or
 * stacked tiled clients overlapped. */
static Client *clientfromwin(Window w) {
  Client *c;
  Window parent, rootret, *children = NULL;
  unsigned int n;

  while (w && w != root) {
    if ((c = wintoclient(w)))
      return c;
    children = NULL;
    if (!XQueryTree(dpy, w, &rootret, &parent, &children, &n))
      break;
    if (children)
      XFree(children);
    w = parent;
  }
  return NULL;
}

void focusunderpointer(void) {
  int x, y;
  int di;
  unsigned int dui;
  Window rootret, child;
  Client *c;
  Monitor *m;

  if (!XQueryPointer(dpy, root, &rootret, &child, &x, &y, &di, &di, &dui))
    return;
  if ((m = recttomon(x, y, 1, 1)) && m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
  }
  c = clientfromwin(child);
  if (!c)
    c = recttoclient(x, y, 1, 1);
  focus(c);
}

long getstate(Window w) {
  int format;
  long result = -1;
  unsigned char *p = NULL;
  unsigned long n, extra;
  Atom real;

  if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False,
                         wmatom[WMState], &real, &format, &n, &extra,
                         (unsigned char **)&p) != Success)
    return -1;
  if (n != 0)
    result = *p;
  XFree(p);
  return result;
}

unsigned int getsystraywidth() {
  unsigned int w = 0;
  Client *i;
  if (showsystray && systray)
    for (i = systray->icons; i; w += i->w + systrayspacing, i = i->next)
      ;
  return w ? w + systrayspacing : 0;
}

int gettextprop(Window w, Atom atom, char *text, unsigned int size) {
  char **list = NULL;
  int n;
  XTextProperty name;

  if (!text || size == 0)
    return 0;
  text[0] = '\0';
  if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
    return 0;
  if (name.encoding == XA_STRING) {
    strncpy(text, (char *)name.value, size - 1);
  } else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success &&
             n > 0 && *list) {
    strncpy(text, *list, size - 1);
    XFreeStringList(list);
  }
  text[size - 1] = '\0';
  XFree(name.value);
  return 1;
}

void grabbuttons(Client *c, int focused) {
  updatenumlockmask();
  {
    unsigned int i, j;
    unsigned int modifiers[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    if (!focused)
      XGrabButton(dpy, AnyButton, AnyModifier, c->win, False, BUTTONMASK,
                  GrabModeAsync, GrabModeAsync, None, None);
    for (i = 0; i < LENGTH(buttons); i++)
      if (buttons[i].click == ClkClientWin)
        for (j = 0; j < LENGTH(modifiers); j++)
          XGrabButton(dpy, buttons[i].button, buttons[i].mask | modifiers[j],
                      c->win, False, BUTTONMASK, GrabModeAsync, GrabModeAsync,
                      None, None);
  }
}

void grabkeys(void) {
  updatenumlockmask();
  {
    unsigned int i, j, k;
    unsigned int modifiers[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};
    int start, end, skip;
    KeySym *syms;
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    XDisplayKeycodes(dpy, &start, &end);
    syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
    if (!syms)
      return;
    for (k = start; k <= end; k++)
      for (i = 0; i < LENGTH(keys); i++)
        /* skip modifier codes, we do that ourselves */
        if (keys[i].keysym == syms[(k - start) * skip])
          for (j = 0; j < LENGTH(modifiers); j++)
            XGrabKey(dpy, k, keys[i].mod | modifiers[j], root, True,
                     GrabModeAsync, GrabModeAsync);
    XFree(syms);
  }
}

void freeicon(Client *c) {
  if (c->icon) {
    XRenderFreePicture(dpy, c->icon);
    c->icon = None;
  }
}

void hide(Client *c) {
  if (!c || HIDDEN(c))
    return;

  setclientstate(c, IconicState);
  XUnmapWindow(dpy, c->win);
  XSync(dpy, False);

  if (ISVISIBLE(c)) {
    focus(NULL);
    arrange(c->mon);
  }
}

void incnmaster(const Arg *arg) {
  selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag] =
      MAX(selmon->nmaster + arg->i, 0);
  arrange(selmon);
}

#ifdef XINERAMA
static int isuniquegeom(XineramaScreenInfo *unique, size_t n,
                        XineramaScreenInfo *info) {
  while (n--)
    if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org &&
        unique[n].width == info->width && unique[n].height == info->height)
      return 0;
  return 1;
}
#endif /* XINERAMA */

void keypress(XEvent *e) {
  unsigned int i;
  KeySym keysym;
  XKeyEvent *ev;

  ev = &e->xkey;
  keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);

  /* only KeyPress triggers normal keybindings (avoid double-fire on KeyRelease) */
  if (ev->type != KeyPress)
    return;

  for (i = 0; i < LENGTH(keys); i++)
    if (keysym == keys[i].keysym &&
        CLEANMASK(keys[i].mod) == CLEANMASK(ev->state) && keys[i].func)
      keys[i].func(&(keys[i].arg));
}

int countwindows(void) {
  Client *c;
  int count = 0;

  for (c = selmon->clients; c; c = c->next) {
    if (ISVISIBLE(c)) // only count windows visible on current tag
      count++;
  }

  return count;
}

int picom_state = 0;
const char *class_name; // Renamed to avoid confusion with C++ 'class' keyword
static Display *cached_dpy = NULL;
static Window cached_win = 0;
static int cached_valid = 0;

int haswindowproperty(Client *c, Atom prop) {
  Atom actual;
  int format;
  unsigned long n, extra;
  unsigned char *data = NULL;
  int exists = 0;

  if (!c || !c->win || prop == None)
    return 0;

  if (XGetWindowProperty(dpy, c->win, prop, 0L, 0L, False, AnyPropertyType,
                         &actual, &format, &n, &extra, &data) == Success)
    exists = actual != None;

  if (data)
    XFree(data);

  return exists;
}

int get_bypass_compositor_value(Client *c) {
  Atom actual;
  int format;
  unsigned long n, extra;
  unsigned char *data = NULL;

  if (!c || !c->win || netatom[NetWMBypassCompositor] == None)
    return 0;

  if (XGetWindowProperty(dpy, c->win, netatom[NetWMBypassCompositor],
                          0L, 1L, False, XA_CARDINAL,
                          &actual, &format, &n, &extra, &data) == Success
      && actual != None && data) {
    unsigned long val = *(unsigned long *)data;
    XFree(data);
    return val;
  }
  if (data)
    XFree(data);
  return 0;
}

void
set_bypass_compositor(Client *c, unsigned long val)
{
  if (!c || !c->win || netatom[NetWMBypassCompositor] == None)
    return;
  XChangeProperty(dpy, c->win, netatom[NetWMBypassCompositor], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&val, 1);
}

int window_has_transparency(Client *c) {
  if (!c || !c->win)
    return 0;
  if (get_bypass_compositor_value(c) == 2)
    return 1;
  if (transparent_fullscreen_classes[0]) {
    XClassHint ch = {NULL, NULL};
    if (XGetClassHint(dpy, c->win, &ch) && ch.res_class) {
      for (int i = 0; transparent_fullscreen_classes[i]; i++) {
        if (strcmp(ch.res_class, transparent_fullscreen_classes[i]) == 0) {
          XFree(ch.res_class);
          if (ch.res_name) XFree(ch.res_name);
          return 1;
        }
      }
    }
    if (ch.res_class) XFree(ch.res_class);
    if (ch.res_name) XFree(ch.res_name);
  }
  return 0;
}

void setwindowopacity(Client *c) {
  unsigned long value = 0xffffffffUL;

  if (!c || !c->win || netatom[NetWMWindowOpacity] == None ||
      haswindowproperty(c, netatom[NetWMBypassCompositor]))
    return;

  XChangeProperty(dpy, c->win, netatom[NetWMWindowOpacity], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&value, 1);
}

void clearwindowopacity(Client *c) {
  if (!c || !c->win || netatom[NetWMWindowOpacity] == None)
    return;

  XDeleteProperty(dpy, c->win, netatom[NetWMWindowOpacity]);
}

void togglepicom(const Arg *arg) {
  if (netatom[NetWMWindowOpacity] == None)
    return;

  if (picom_state == 0) {
    Client *c = selmon->sel;
    if (!c || !c->win)
      return;

    // Cache values
    cached_dpy = dpy;
    cached_win = c->win;
    cached_valid = 1;

    setwindowopacity(c);

    picom_state = 1;

  } else {
    if (!cached_valid)
      return;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(cached_dpy, cached_win, &wa)) {
      cached_valid = 0;
      return;
    }

    XDeleteProperty(cached_dpy, cached_win, netatom[NetWMWindowOpacity]);

    // Reset cache (important!)
    cached_valid = 0;
    cached_win = 0;
    cached_dpy = NULL;

    picom_state = 0;
  }
}

// Example of fullscreen function
/*
void togglefullscr(const Arg *arg) {
  if (selmon->sel)
    setfullscreen(selmon->sel, !selmon->sel->isfullscreen);
}
*/

//    unsigned stick_state;
//    void togglesticky(const Arg *arg){
//      if (!selmon || (!selmon->sel && !stickywin))
//        return;
//
//      if (!c)
//        return;
//
//      if (c->isfullscreen)
//        return;
//
//      if (!stickywin){
//        stickywin = c;
//        // Save state
//        stickywin->oldtags = c->tags;
//        // Visible on all tags
//        stickywin->tags = TAGMASK;
//        // Keep sticky windows tiled/managed, no fullscreen toggle on error
//        path setfullscreen(stickywin, 0); stickywin->issticky = 1;
//        stick_state=1;
//
//        // Move sticky window to bottom of stack so it does not cover others
//        detachstack(stickywin);
//        attachstack(stickywin);
//        XLowerWindow(dpy, stickywin->win);
//      } else {
//        stickywin->tags = stickywin->oldtags ? stickywin->oldtags :
//        selmon->tagset[selmon->seltags]; setfullscreen(stickywin, 0);
//        stickywin->issticky = 0;
//        stick_state=0;
//
//        stickywin = NULL; /* assignment, not comparison */
//        arrange(selmon);
//      }
//    }
// Global enviroment to store client name
static Client *stickywin = NULL;
unsigned stick_state;
void togglesticky(Client *c, int fullscreen) {
  if (!c || c->isfullscreen)
    return;

  /* 1. If a DIFFERENT window is already sticky, unstick it first */
  if (stickywin && stickywin != c) {
    stickywin->isfloating = stickywin->wasfloating;
    stickywin->tags = stickywin->oldtags;
    stickywin->issticky = 0;

    /* Restore original geometry */
    resizeclient(stickywin, stickywin->prevx, stickywin->prevy,
                 stickywin->prevw, stickywin->prevh);

    /* Remove EWMH Fullscreen state */
    XChangeProperty(dpy, stickywin->win, netatom[NetWMState], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)0, 0);
    clearwindowopacity(stickywin);

    detachstack(stickywin);
    attachstack(stickywin);
    stickywin->fshidden = 0;
    XRaiseWindow(dpy, stickywin->win);

    stickywin = NULL;
    /* After unsticking the old one, we continue below to stick the new one */
  }

  /* 2. Toggle the current window 'c' */
  if (!stickywin) {
    /* Make it Sticky */
    stickywin = c;
    stickywin->issticky = 1;
    stickywin->wasfloating = stickywin->isfloating;

    /* Save state */
    stickywin->oldtags = stickywin->tags;
    stickywin->prevx = stickywin->x;
    stickywin->prevy = stickywin->y;
    stickywin->prevw = stickywin->w;
    stickywin->prevh = stickywin->h;

    stickywin->isfloating = 1;
    stickywin->tags = TAGMASK;

    /* Set EWMH state to Fullscreen so other windows don't overlap it easily */
    XChangeProperty(dpy, stickywin->win, netatom[NetWMState], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&netatom[NetWMFullscreen],
                    1);
    setwindowopacity(stickywin);

    /* Resize to fill the monitor */
    resizeclient(stickywin, stickywin->mon->mx - borderpx, stickywin->mon->my - borderpx,
                 stickywin->mon->mw, stickywin->mon->mh);

    detachstack(stickywin);
    attachstack(stickywin);
    XLowerWindow(dpy, stickywin->win);
  } else {
    /* 1. Reset EWMH first */
    XChangeProperty(dpy, stickywin->win, netatom[NetWMState], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)0, 0);
    clearwindowopacity(stickywin);

    /* 2. Restore the floating status */
    stickywin->isfloating = stickywin->wasfloating;
    stickywin->tags = stickywin->oldtags;
    stickywin->issticky = 0;
    stickywin->isfullscreen = 0;

    /* 3. Explicitly move the client's variables back to old state */
    stickywin->x = stickywin->prevx;
    stickywin->y = stickywin->prevy;
    stickywin->w = stickywin->prevw;
    stickywin->h = stickywin->prevh;

    /* 4. Now call resize to tell X11 where to put it */
    resizeclient(stickywin, stickywin->x, stickywin->y, stickywin->w,
                 stickywin->h);

    detachstack(stickywin);
    attachstack(stickywin);
    stickywin->fshidden = 0;
    XRaiseWindow(dpy, stickywin->win);

    stickywin = NULL;
  }

  /* Finalize layout */
  focus(NULL);
  if (c->mon)
    arrange(c->mon);
}

void togglestickyclient(const Arg *arg) {
  /* if sticky window exists, always just toggle it off */
  if (stickywin) {
    togglesticky(stickywin, stickywin->isfullscreen);
    return;
  }

  /* if no sticky window, make current focused window sticky */
  Client *c = selmon->sel;
  if (!c)
    return;

  togglesticky(c, c->isfullscreen);
}

void killclient(const Arg *arg) {
  if (!selmon->sel)
    return;
  if (!sendevent(selmon->sel->win, wmatom[WMDelete], NoEventMask,
                 wmatom[WMDelete], CurrentTime, 0, 0, 0)) {
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XSetCloseDownMode(dpy, DestroyAll);
    XKillClient(dpy, selmon->sel->win);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
}

void forcekillclient(const Arg *arg) {
  if (!selmon->sel || selmon->sel->issticky)
    return;

  XGrabServer(dpy);
  XSetErrorHandler(xerrordummy);
  XSetCloseDownMode(dpy, DestroyAll);
  XKillClient(dpy, selmon->sel->win);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  XUngrabServer(dpy);
}

void manage(Window w, XWindowAttributes *wa) {
  Client *c, *t = NULL;
  Window trans = None;
  XWindowChanges wc;

  c = ecalloc(1, sizeof(Client));
  c->win = w;
  /* geometry */
  c->x = c->oldx = wa->x;
  c->y = c->oldy = wa->y;
  c->w = c->oldw = wa->width;
  c->h = c->oldh = wa->height;
  c->oldbw = wa->border_width;
  c->cfact = 1.0;
  c->bypass_value = get_bypass_compositor_value(c);

  updateicon(c);
  updatetitle(c);

  if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
    c->mon = t->mon;
    c->tags = t->tags;
  } else {
    c->mon = selmon;
    applyrules(c);
  }

  if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
    c->x = c->mon->wx + c->mon->ww - WIDTH(c);
  if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
    c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
  c->x = MAX(c->x, c->mon->wx);
  c->y = MAX(c->y, c->mon->wy);
  c->bw = c->mon->borderpx;

  wc.border_width = c->bw;
  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
  configure(c); /* propagates border_width, if size doesn't change */
  updatewindowtype(c);
  updatesizehints(c);
  updatewmhints(c);
  {
    int format;
    unsigned long *data, n, extra;
    Monitor *m;
    Atom atom;
    if (XGetWindowProperty(dpy, c->win, netatom[NetClientInfo], 0L, 2L, False,
                           XA_CARDINAL, &atom, &format, &n, &extra,
                           (unsigned char **)&data) == Success &&
        n == 2) {
      c->tags = *data;
      for (m = mons; m; m = m->next) {
        if (m->num == *(data + 1)) {
          c->mon = m;
          break;
        }
      }
    }
    if (n > 0)
      XFree(data);
  }
  setclienttagprop(c);

  if (c->iscentered) {
    c->x = c->mon->mx + (c->mon->mw - WIDTH(c)) / 2;
    c->y = c->mon->my + (c->mon->mh - HEIGHT(c)) / 2;
  }
  XSelectInput(dpy, w,
               EnterWindowMask | FocusChangeMask | PropertyChangeMask |
                   StructureNotifyMask);
  grabbuttons(c, 0);
  if (!c->isfloating)
    c->isfloating = c->oldstate = trans != None || c->isfixed;
  if (c->isfloating)
    XRaiseWindow(dpy, c->win);
  attach(c);
  attachstack(c);
  XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
                  PropModeAppend, (unsigned char *)&(c->win), 1);
  XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w,
                    c->h); /* some windows require this */
  if (!HIDDEN(c))
    setclientstate(c, NormalState);
  if (c->mon == selmon)
    unfocus(selmon->sel, 0);
  c->mon->sel = c;
  updatetitle(c);
  if (strstr(c->name, "Picture in picture") ||
      strstr(c->name, "Picture-in-Picture")) {
    c->pipparent = find_pipparent(c);
    togglesticky(c, 0);
  }
  else
    arrange(c->mon);

  if (c->bypass_value == 1 && !c->isfullscreen)
    set_bypass_compositor(c, 0);

  if (!HIDDEN(c))
    XMapWindow(dpy, c->win);
  focus(NULL);
}

void mappingnotify(XEvent *e) {
  XMappingEvent *ev = &e->xmapping;

  XRefreshKeyboardMapping(ev);
  if (ev->request == MappingKeyboard)
    grabkeys();
}

void maprequest(XEvent *e) {
  static XWindowAttributes wa;
  XMapRequestEvent *ev = &e->xmaprequest;
  Client *i;
  if ((i = wintosystrayicon(ev->window))) {
    sendevent(i->win, netatom[Xembed], StructureNotifyMask, CurrentTime,
              XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
    resizebarwin(selmon);
    updatesystray();
  }

  if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
    return;
  if (!wintoclient(ev->window))
    manage(ev->window, &wa);
}

void monocle(Monitor *m) {
  unsigned int n = 0;

  Client *c;

  for (c = m->clients; c; c = c->next)
    if (ISVISIBLE(c))
      n++;

  if (n > 0) /* override layout symbol */
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);

  int newx, newy, neww, newh;

  for (c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
    newx = m->wx + m->gappov - c->bw;
    newy = m->wy + m->gappoh - c->bw;
    neww = m->ww - 2 * (m->gappov + c->bw);
    newh = m->wh - 2 * (m->gappoh + c->bw);

    applysizehints(c, &newx, &newy, &neww, &newh, 0);

    if (neww < m->ww)
      newx = m->wx + (m->ww - (neww + 2 * c->bw)) / 2;

    if (newh < m->wh)
      newy = m->wy + (m->wh - (newh + 2 * c->bw)) / 2;

    resize(c, newx, newy, neww, newh, 0);
  }
}

void motionnotify(XEvent *e) {
  unsigned int i, x;
  static Monitor *mon = NULL;
  Monitor *m;
  XMotionEvent *ev = &e->xmotion;

  if (ev->window == selmon->barwin) {
    i = x = 0;
    do
      x += TEXTW(tags[i]);
    while (ev->x >= x && ++i < LENGTH(tags));
    if (i < LENGTH(tags)) {
      if ((i + 1) != selmon->previewshow &&
          !(selmon->tagset[selmon->seltags] & 1 << i)) {
        selmon->previewshow = i + 1;
        showtagpreview(i);
      } else if (selmon->tagset[selmon->seltags] & 1 << i) {
        selmon->previewshow = 0;
        showtagpreview(0);
      }
    } else if (selmon->previewshow != 0) {
      selmon->previewshow = 0;
      showtagpreview(0);
    }
  } else if (selmon->previewshow != 0) {
    selmon->previewshow = 0;
    showtagpreview(0);
  }

  if (ev->window != root) {
    return;
  }
  if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
    unfocus(selmon->sel, 1);
    selmon = m;
    if (alt_held && enablesidebar) {
      hideosidebar(mon);
      showsidbar(selmon);
    }
    focus(NULL);
  }
  mon = m;
}

void updateicon(Client *c) {
  freeicon(c);
  c->icon = geticonprop(c->win, &c->icw, &c->ich, &c->ic_pw, &c->ic_ph);
}

void moveorplace(const Arg *arg) {
  if ((!selmon->lt[selmon->sellt]->arrange ||
       (selmon->sel && selmon->sel->isfloating)))
    movemouse(arg);
  else
    placemouse(arg);
}

static void *
set_animation_property_thread(void *arg)
{
  AnimationPropertyArgs *args = arg;
  Display *thread_dpy;
  Atom atom;

  if ((thread_dpy = XOpenDisplay(args->display_name))) {
    atom = XInternAtom(thread_dpy, "_NO_ANIMATION", False);
    XChangeProperty(thread_dpy, args->win, atom, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&args->animation_data, 1);
    XCloseDisplay(thread_dpy);
  }

  free(args->display_name);
  free(args);
  return NULL;
}

static void
setnoanimation_async(Client *c, long val)
{
  AnimationPropertyArgs *args;
  pthread_t thread;
  const char *display_name;

  if (!c || !c->win)
    return;

  if (!(args = malloc(sizeof *args)))
    return;

  display_name = DisplayString(dpy);
  args->display_name = display_name ? strdup(display_name) : NULL;
  args->win = c->win;
  args->animation_data = val;

  if (display_name && !args->display_name) {
    free(args);
    return;
  }

  if (pthread_create(&thread, NULL, set_animation_property_thread, args) == 0)
    pthread_detach(thread);
  else {
    free(args->display_name);
    free(args);
  }
}

static void on_stream_param_changed(void *userdata, uint32_t id,
                                   const struct spa_pod *param) {
  if (param == NULL || id != SPA_PARAM_Format)
    return;
  if (spa_format_parse(param, &sound_format.media_type,
                       &sound_format.media_subtype) < 0)
    return;
  if (sound_format.media_type != SPA_MEDIA_TYPE_audio ||
      sound_format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
    return;
  spa_format_audio_raw_parse(param, &sound_format.info.raw);
}

static void on_process(void *userdata) {
  struct pw_buffer *b;
  struct spa_buffer *buf;
  float *samples, max;
  uint32_t n, n_channels, n_samples;
  unsigned int min, maxw, new_width;

  if (!(b = pw_stream_dequeue_buffer(sound_stream)))
    return;
  buf = b->buffer;
  if (!buf->datas[0].data) {
    pw_stream_queue_buffer(sound_stream, b);
    return;
  }

  n_channels = sound_format.info.raw.channels;
  if (!n_channels) {
    pw_stream_queue_buffer(sound_stream, b);
    return;
  }
  n_samples = buf->datas[0].chunk->size / sizeof(float);

  /* peak volume across all channels, raw like main.c */
  samples = buf->datas[0].data;
  max = 0.0f;
  for (n = 0; n < n_samples; n++)
    if (fabsf(samples[n]) > max)
      max = fabsf(samples[n]);

  if (max > 1.0f)
    max = 1.0f;

  min = clockbar_cw;
  maxw = clockbar_cw + CLOCK_ANIM_EXTRA;
  new_width = min + (unsigned int)(max * (maxw - min));
  if (new_width < min)
    new_width = min;
  if (new_width > maxw)
    new_width = maxw;
  clockbar_dynw = new_width;

  /* resize keeping the bar centered, like main.c */
  if (clockbar_win) {
    XResizeWindow(dpy, clockbar_win, new_width, clockbar_bh);
    XMoveWindow(dpy, clockbar_win, clockbar_cx - (int)(new_width / 2),
                clockbar_by);
    XFlush(dpy);
  }

  pw_stream_queue_buffer(sound_stream, b);
}

static const struct pw_stream_events sound_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .param_changed = on_stream_param_changed,
    .process = on_process,
};

static void sound_monitor_start(void) {
  const struct spa_pod *params[1];
  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  struct pw_properties *props;

  clockanim_running = 1;
  pw_init(NULL, NULL);
  if (!(sound_loop = pw_thread_loop_new("dwm-sound-bar", NULL))) {
    pw_deinit();
    clockanim_running = 0;
    return;
  }

  props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                            PW_KEY_MEDIA_CATEGORY, "Capture",
                            PW_KEY_MEDIA_ROLE, "Music", NULL);
  if (!props) {
    pw_thread_loop_destroy(sound_loop);
    pw_deinit();
    clockanim_running = 0;
    return;
  }
  if (PW_MONITOR_SINK)
    pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");

  sound_stream = pw_stream_new_simple(pw_thread_loop_get_loop(sound_loop),
                                      "dwm-sound-bar", props,
                                      &sound_stream_events, NULL);
  if (!sound_stream) {
    pw_thread_loop_destroy(sound_loop);
    pw_deinit();
    clockanim_running = 0;
    return;
  }

  params[0] = spa_format_audio_raw_build(
      &b, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32));

  pw_stream_connect(sound_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS,
                    params, 1);

  pw_thread_loop_start(sound_loop);
}

static void sound_monitor_stop(void) {
  if (sound_loop) {
    pw_thread_loop_stop(sound_loop);
    pw_stream_destroy(sound_stream);
    pw_thread_loop_destroy(sound_loop);
    sound_stream = NULL;
    sound_loop = NULL;
  }
  if (clockanim_running) {
    pw_deinit();
    clockanim_running = 0;
  }
}

void movemouse(const Arg *arg) {
  int x, y, nx, ny;
  int rel_x, rel_y; // offset from window corner to mouse
  int lastw = 0, lasth = 0;
  Client *c;
  Monitor *m;
  XEvent ev;
  int was_tiled = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen || c->issticky) /* no support moving fullscreen or sticky windows by mouse */
    return;
  if (!c->isfloating && selmon->lt[selmon->sellt]->arrange) {
    was_tiled = 1;
    togglefloating_noarrange(c);
  }
  restack(selmon);
  XSync(dpy, False); // Ensure floating state and restack are processed
  lastw = c->w;
  lasth = c->h;

  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, cursor[CurMove]->cursor, last_ev_time) != GrabSuccess)
    return;
  setnoanimation_async(c, 1);
  if (!getrootptr(&x, &y))
    return;
  /* Calculate offset from window top-left to mouse position */
  rel_x = x - c->x;
  rel_y = y - c->y;

  /* Move window so mouse is at same relative position at drag start */
  nx = x - rel_x;
  ny = y - rel_y;
  resize(c, nx, ny, c->w, c->h, 1);
  XFlush(dpy);
  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      while (XCheckTypedEvent(dpy, MotionNotify, &ev))
        ;

      // If the window size changed (e.g. due to layout), keep mouse at same
      // relative position
      if (c->w != lastw || c->h != lasth) {
        // Clamp rel_x/rel_y to new window size
        if (rel_x > c->w)
          rel_x = c->w / 2;
        if (rel_y > c->h)
          rel_y = c->h / 2;
        nx = ev.xmotion.x - rel_x;
        ny = ev.xmotion.y - rel_y;
        resize(c, nx, ny, c->w, c->h, 1);
        lastw = c->w;
        lasth = c->h;
        break;
      }

      nx = ev.xmotion.x - rel_x;
      ny = ev.xmotion.y - rel_y;
       m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1);
      if (m && m != selmon)
        selmon = m;
      if (abs(selmon->wx - nx) < snap)
        nx = selmon->wx;
      else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
        nx = selmon->wx + selmon->ww - WIDTH(c);
      if (abs(selmon->wy - ny) < snap)
        ny = selmon->wy;
      else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
        ny = selmon->wy + selmon->wh - HEIGHT(c);
      // Always keep floating while dragging; never toggle floating here
      int neww = c->w, newh = c->h;
      resize(c, nx, ny, neww, newh, 1);
      break;
    }
  } while (ev.type != ButtonRelease);

  XUngrabPointer(dpy, CurrentTime);
  XSync(dpy, False);
  setnoanimation_async(c, 0);
  /*
   * Decide the destination monitor from the mouse cursor on release.
   * Check BEFORE arrange to prevent applysizehints() from clamping
   * the window position to the wrong monitor's window area.
   */
  if ((m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1)) &&
      m != c->mon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }

  // If window was originally tiled, return to tiled after drag
  if (was_tiled && c->isfloating) {
    togglefloating(NULL);
  } else {
    arrange(c->mon);
  }
}

Client *nexttiled(Client *c) {
  for (; c && (c->isfloating || (!ISVISIBLE(c) || HIDDEN(c))); c = c->next)
    ;
  return c;
}

void placemouse(const Arg *arg) {
  int x, y, px, py, ocx, ocy, nx = -9999, ny = -9999, freemove = 0;
  Client *c, *r = NULL, *at, *prevr;
  Monitor *m;
  XEvent ev;
  XWindowAttributes wa;
  int attachmode, prevattachmode;
  attachmode = prevattachmode = -1;

  if (!(c = selmon->sel) ||
      !c->mon->lt[c->mon->sellt]->arrange) /* no support for placemouse when
                                              floating layout is used */
    return;
  if (c->isfullscreen || c->issticky) /* no support placing fullscreen or sticky windows by mouse */
    return;
  restack(selmon);
  prevr = c;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
    return;
  XSync(dpy, False);
  setnoanimation_async(c, 1);

  c->isfloating = 0;
  c->beingmoved = 1;

  XGetWindowAttributes(dpy, c->win, &wa);
  ocx = wa.x;
  ocy = wa.y;

  if (!getrootptr(&x, &y))
    return;

  XFlush(dpy);
  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      while (XCheckTypedEvent(dpy, MotionNotify, &ev))
        ;

      nx = ocx + (ev.xmotion.x - x);
      ny = ocy + (ev.xmotion.y - y);

      if (!freemove && (abs(nx - ocx) > snap || abs(ny - ocy) > snap))
        freemove = 1;

      if (freemove)
        XMoveWindow(dpy, c->win, nx, ny);

      if ((m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1)) && m != selmon)
        selmon = m;

      if (arg->i ==
          1) { // tiled position is relative to the client window center point
        px = nx + wa.width / 2;
        py = ny + wa.height / 2;
      } else { // tiled position is relative to the mouse cursor
        px = ev.xmotion.x;
        py = ev.xmotion.y;
      }

      r = recttoclient(px, py, 1, 1);

      if (!r || r == c)
        break;

      attachmode = 0; // below
      if (((float)(r->y + r->h - py) / r->h) >
          ((float)(r->x + r->w - px) / r->w)) {
        if (abs(r->y - py) < r->h / 2)
          attachmode = 1; // above
      } else if (abs(r->x - px) < r->w / 2)
        attachmode = 1; // above

      if ((r && r != prevr) || (attachmode != prevattachmode)) {
        detachstack(c);
        detach(c);
        if (c->mon != r->mon) {
          arrangemon(c->mon);
          c->tags = r->mon->tagset[r->mon->seltags];
        }

        c->mon = r->mon;
        r->mon->sel = r;

        if (attachmode) {
          if (r == r->mon->clients)
            attach(c);
          else {
            for (at = r->mon->clients; at->next != r; at = at->next)
              ;
            c->next = at->next;
            at->next = c;
          }
        } else {
          c->next = r->next;
          r->next = c;
        }

        attachstack(c);
        arrangemon(r->mon);
        prevr = r;
        prevattachmode = attachmode;
      }
      break;
    }
  } while (ev.type != ButtonRelease);
  XUngrabPointer(dpy, CurrentTime);
  setnoanimation_async(c, 0);

  if ((m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1)) && m != c->mon) {
    detach(c);
    detachstack(c);
    arrangemon(c->mon);
    c->mon = m;
    c->tags = m->tagset[m->seltags];
    attach(c);
    attachstack(c);
    selmon = m;
  }

  focus(c);
  c->beingmoved = 0;

  if (nx != -9999)
    resize(c, nx, ny, c->w, c->h, 0);
  arrangemon(c->mon);
}

void pop(Client *c) {
  detach(c);
  attach(c);
  focus(c);
  arrange(c->mon);
}

void propertynotify(XEvent *e) {
  Client *c;
  Window trans;
  XPropertyEvent *ev = &e->xproperty;

  if ((c = wintosystrayicon(ev->window))) {
    if (ev->atom == XA_WM_NORMAL_HINTS) {
      updatesizehints(c);
      updatesystrayicongeom(c, c->w, c->h);
    } else
      updatesystrayiconstate(c, ev);
    resizebarwin(selmon);
    updatesystray();
  }
  if ((ev->window == root) && (ev->atom == XA_WM_NAME))
    updatestatus();
  else if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = wintoclient(ev->window))) {
    switch (ev->atom) {
    default:
      break;
    case XA_WM_TRANSIENT_FOR:
      if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
          (c->isfloating = (wintoclient(trans)) != NULL))
        arrange(c->mon);
      break;
    case XA_WM_NORMAL_HINTS:
      c->hintsvalid = 0;
      break;
    case XA_WM_HINTS:
      updatewmhints(c);
      drawbars();
      drawtabs();
      break;
    }
    if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
      updatetitle(c);
      if (c == c->mon->sel)
        drawbar(c->mon);
      drawtab(c->mon);
    }

    else if (ev->atom == netatom[NetWMIcon]) {
      updateicon(c);
      if (c == c->mon->sel)
        drawbar(c->mon);
    }

    if (ev->atom == netatom[NetWMWindowType])
      updatewindowtype(c);
  }
}

void restart(const Arg *arg) { running = 0; }

Client *recttoclient(int x, int y, int w, int h) {
  Client *c, *r = NULL;
  int a, area = 0;

  for (c = nexttiled(selmon->clients); c; c = nexttiled(c->next)) {
    if ((a = INTERSECTC(x, y, w, h, c)) > area) {
      area = a;
      r = c;
    }
  }
  return r;
}

Monitor *recttomon(int x, int y, int w, int h) {
  Monitor *m, *r = selmon;
  int a, area = 0;

  for (m = mons; m; m = m->next)
    if ((a = INTERSECT(x, y, w, h, m)) > area) {
      area = a;
      r = m;
    }
  return r;
}

void removesystrayicon(Client *i) {
  Client **ii;

  if (!showsystray || !i)
    return;
  for (ii = &systray->icons; *ii && *ii != i; ii = &(*ii)->next)
    ;
  if (ii)
    *ii = i->next;
  free(i);
}

void resize(Client *c, int x, int y, int w, int h, int interact) {
  if (!c)
    return;

  if (applysizehints(c, &x, &y, &w, &h, interact)) {
     // if (!c->isfloating) {
     //   if (animate_tiled)
     //     resizeclient_animated(c, x, y, w,h); // this function make animation relly play of windows move
     //   else
     //     resizeclient(c, x, y, w, h); // instant for tiled
     // } else
      resizeclient(c, x, y, w, h); // instant for floating
  }
}

void resizebarwin(Monitor *m) {
  unsigned int tw, tlw, cw, kw, rw, stw = 0, stgap = 0;
  int lx, rx, cx, kx, titlex, title_right, span, center_total;

  if (showsystray && m == systraytomon(m)) {
    stw = getsystraywidth();
    if (stw)
      stgap = bargap;
  }

  if (!m->showbar) {
    XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, 1, bh);
    if (m->bartitlewin)
      XMoveResizeWindow(dpy, m->bartitlewin, m->wx, m->by, 1, bh);
    if (m->barcenterwin)
      XMoveResizeWindow(dpy, m->barcenterwin, m->wx, m->by, 1, bh);
    if (m->barkbwin)
      XMoveResizeWindow(dpy, m->barkbwin, m->wx, m->by, 1, bh);
    if (m->barrightwin)
      XMoveResizeWindow(dpy, m->barrightwin, m->wx, m->by, 1, bh);
    if (m == selmon)
      clockbar_win = 0;
    return;
  }

  barwidths(m, &tw, &tlw, &cw, &kw, &rw);
  {
    unsigned int dcw = (clockbar_dynw && m == selmon) ? clockbar_dynw : cw;

    lx = floatbar ? m->wx + m->gappov : m->wx;
    rx = lx + (int)(floatbar ? m->ww - 2 * m->gappov : m->ww) - (int)stw -
         (int)rw - (int)stgap;

    XMoveResizeWindow(dpy, m->barwin, lx, m->by, tw, bh);

    titlex = lx + (int)tw + bargap;
    title_right = titlex + (int)tlw;
    if (m->bartitlewin) {
      XMoveResizeWindow(dpy, m->bartitlewin, titlex, m->by, tlw, bh);
      XMapWindow(dpy, m->bartitlewin);
    }

    span = rx - title_right;
    center_total = (int)dcw + bargap + (int)kw;
    if (span >= center_total) {
      cx = title_right + (span - center_total) / 2;
      kx = cx + (int)dcw + bargap;
      if (m->barcenterwin) {
        XMoveResizeWindow(dpy, m->barcenterwin, cx, m->by, dcw, bh);
        XMapWindow(dpy, m->barcenterwin);
      }
      if (m->barkbwin) {
        XMoveResizeWindow(dpy, m->barkbwin, kx, m->by, kw, bh);
        XMapWindow(dpy, m->barkbwin);
      }
    } else if (span >= (int)dcw) {
      cx = title_right + (span - (int)dcw) / 2;
      if (m->barcenterwin) {
        XMoveResizeWindow(dpy, m->barcenterwin, cx, m->by, dcw, bh);
        XMapWindow(dpy, m->barcenterwin);
      }
      if (m->barkbwin)
        XUnmapWindow(dpy, m->barkbwin);
    } else {
      cx = 0;
      if (m->barcenterwin)
        XUnmapWindow(dpy, m->barcenterwin);
      if (m->barkbwin)
        XUnmapWindow(dpy, m->barkbwin);
    }

    if (m == selmon) {
      clockbar_win = (span >= (int)dcw) ? m->barcenterwin : 0;
      clockbar_cx = cx + (int)(dcw / 2);
      clockbar_by = m->by;
      clockbar_bh = bh;
    }
  }

  if (m->barrightwin) {
    XMoveResizeWindow(dpy, m->barrightwin, rx, m->by, rw, bh);
    XMapWindow(dpy, m->barrightwin);
  }
}

void resizeclient(Client *c, int x, int y, int w, int h) {
  XWindowChanges wc;

  c->oldw = c->w;
  c->w = wc.width = w;
  c->oldh = c->h;
  c->h = wc.height = h;
  c->oldx = c->x;
  c->x = wc.x = x;
  c->oldy = c->y;
  c->y = wc.y = y;

  if (c->beingmoved)
    return;

  wc.border_width = c->bw;
  XConfigureWindow(dpy, c->win, CWX | CWY | CWWidth | CWHeight | CWBorderWidth,
                   &wc);
  if (!c->isfullscreen && !c->isontop) {
    Window s = ontopsibling(c->mon, c);
    if (s) {
      XWindowChanges wcs;
      wcs.sibling = s;
      wcs.stack_mode = Below;
      XConfigureWindow(dpy, c->win, CWSibling | CWStackMode, &wcs);
    }
  }
  configure(c);
  XSync(dpy, False);
}

static Window ontopsibling(Monitor *m, Client *exclude) {
  Client *c;
  Client *lowest = NULL;
  if (!m)
    return None;
  for (c = m->stack; c; c = c->snext)
    if (c != exclude && c->isontop && !c->issticky && !c->isfullscreen &&
        ISVISIBLE(c))
      if (!lowest || c->isontop < lowest->isontop)
        lowest = c;
  return lowest ? lowest->win : None;
}

void resizemouse(const Arg *arg) {
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;
  int ocx, ocy, ocw, och, nw, nh;
  int mx, my;
  int last_quadrant = -1;
  int quadrant;

  if (!(c = selmon->sel) || c->isfullscreen || c->issticky)
    return;

  restack(selmon);

  /* original position and size */
  ocx = c->x;
  ocy = c->y;
  ocw = c->w;
  och = c->h;

  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
    return;
  XSync(dpy, False);
  setnoanimation_async(c, 1);

  if (!getrootptr(&mx, &my))
    return;

  XFlush(dpy);
  do {
    XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
    switch (ev.type) {
    case ConfigureRequest:
    case Expose:
    case MapRequest:
      handler[ev.type](&ev);
      break;
    case MotionNotify:
      if ((ev.xmotion.time - lasttime) <= (1000 / 60))
        continue;
      lasttime = ev.xmotion.time;

      /* determine resize quadrant based on initial grab point */
      if (mx < ocx + ocw / 2 && my < ocy + och / 2)
        quadrant = CurTopLeft;
      else if (mx >= ocx + ocw / 2 && my < ocy + och / 2)
        quadrant = CurTopRight;
      else if (mx < ocx + ocw / 2 && my >= ocy + och / 2)
        quadrant = CurBottomLeft;
      else
        quadrant = CurBottomRight;

      if (quadrant != last_quadrant) {
        XChangeActivePointerGrab(dpy, MOUSEMASK, cursor[quadrant]->cursor, CurrentTime);
        last_quadrant = quadrant;
      }

      /* width/height based on mouse movement */
      nw = MAX(ocw + (ev.xmotion.x - mx) * (mx < ocx + ocw / 2 ? -1 : 1),
               c->minw);
      nh = MAX(och + (ev.xmotion.y - my) * (my < ocy + och / 2 ? -1 : 1),
               c->minh);

      /* calculate new window position */
      int nx = (mx < ocx + ocw / 2) ? ocx + (ocw - nw) : ocx;
      int ny = (my < ocy + och / 2) ? ocy + (och - nh) : ocy;

      if (!c->isfloating && selmon->lt[selmon->sellt]->arrange &&
          (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
        togglefloating(NULL);

      if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
        resize(c, nx, ny, nw, nh, 1);

      break;
    }
  } while (ev.type != ButtonRelease);

  XUngrabPointer(dpy, CurrentTime);
  setnoanimation_async(c, 0);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    ;

  if ((m = recttomon(c->x + WIDTH(c) / 2, c->y + HEIGHT(c) / 2, 1, 1)) &&
      m != c->mon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

void resizerequest(XEvent *e) {
  XResizeRequestEvent *ev = &e->xresizerequest;
  Client *i;

  if ((i = wintosystrayicon(ev->window))) {
    updatesystrayicongeom(i, ev->width, ev->height);
    resizebarwin(selmon);
    updatesystray();
  }
}

void restack(Monitor *m) {
  Client *c;
  XEvent ev;
  XWindowChanges wc;

  drawbar(m);
  drawtab(m);
  drawsidebar(m);

  if (!m)
    return;

  if (stickywin) {
    XLowerWindow(dpy, stickywin->win);
  }

  /* Also lower any other sticky windows just in case */
  for (c = m->stack; c; c = c->snext)
    if (c->issticky && c != stickywin && ISVISIBLE(c))
      XLowerWindow(dpy, c->win);

  if (m->sel && !m->sel->issticky && !m->sel->isontop && (m->sel->isfloating || !m->lt[m->sellt]->arrange))
    XRaiseWindow(dpy, m->sel->win);
  if (m->lt[m->sellt]->arrange) {
    wc.stack_mode = Below;
    wc.sibling = m->barwin;
    for (c = m->stack; c; c = c->snext)
      if (!c->isfloating && !c->issticky && ISVISIBLE(c)) {
        XConfigureWindow(dpy, c->win, CWSibling | CWStackMode, &wc);
        wc.sibling = c->win;
      }
  }
  {
    Client *ontop_clients[256];
    int ontop_count = 0;
    for (c = m->stack; c; c = c->snext)
      if (c->isontop && !c->issticky && !c->isfullscreen && ISVISIBLE(c))
        ontop_clients[ontop_count++] = c;
    for (int i = 1; i < ontop_count; i++) {
      Client *key = ontop_clients[i];
      int j = i - 1;
      while (j >= 0 && ontop_clients[j]->isontop < key->isontop) {
        ontop_clients[j + 1] = ontop_clients[j];
        j--;
      }
      ontop_clients[j + 1] = key;
    }
    Window isontop_sibling = None;
    for (int i = 0; i < ontop_count; i++) {
      c = ontop_clients[i];
      if (isontop_sibling == None) {
        XRaiseWindow(dpy, c->win);
      } else {
        wc.sibling = isontop_sibling;
        wc.stack_mode = Below;
        XConfigureWindow(dpy, c->win, CWSibling | CWStackMode, &wc);
      }
      isontop_sibling = c->win;
    }
  }
  XSync(dpy, False);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
    ;
  if (m && m->sidebarvisible)
    XRaiseWindow(dpy, m->sidebarwin);
}void checkminimize(void) {
  Monitor *m;
  Client *c;
  time_t now = time(NULL);

  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      if (c != m->sel && !ISVISIBLE(c) && !HIDDEN(c) && !c->issticky && c->lastvisible > 0)
        if (now - c->lastvisible >= 1) {
          hide(c);
          c->isautominimized = 1;
        }
}
void prepare_workspace_switch(Monitor *m, unsigned int oldtags, unsigned int newtags) {
  Client *c;
  int changed = 0;

  /* Set windows leaving the view to IconicState */
  for (c = m->clients; c; c = c->next) {
    if ((c->tags & oldtags) && !(c->tags & newtags) && !c->issticky && !HIDDEN(c)) {
      setclientstate(c, IconicState);
      XUnmapWindow(dpy, c->win);
      c->isautominimized = 1;
      if (c->lastvisible == 0)
        c->lastvisible = time(NULL);
      changed = 1;
    }
  }

  /* Set windows entering the view to NormalState */
  for (c = m->clients; c; c = c->next) {
    if ((c->tags & newtags) && !c->issticky && HIDDEN(c) && c->isautominimized) {
      XMapWindow(dpy, c->win);
      setclientstate(c, NormalState);
      c->isautominimized = 0;
      c->lastvisible = 0;
      changed = 1;
    }
  }

  if (!changed)
    return;

  /* Wait for the X server to confirm all state changes */

  int all_done, attempts = 0;
  do {
    all_done = 1;
    for (c = m->clients; c; c = c->next) {
      if ((c->tags & newtags) && !c->issticky && getstate(c->win) == IconicState && c->isautominimized) {
        all_done = 0;
        break;
      }
      if ((c->tags & oldtags) && !(c->tags & newtags) && !c->issticky && getstate(c->win) == NormalState) {
        all_done = 0;
        break;
      }
    }
    if (!all_done)
      XSync(dpy, False);
  } while (!all_done && ++attempts < 200);
}

void run(void) {
  XEvent ev;
  int xfd = ConnectionNumber(dpy);
  int n;
  struct pollfd pfd = {.fd = xfd, .events = POLLIN};

  /* main event loop */
  XSync(dpy, False);
  while (running) {
    n = poll(&pfd, 1, 1000);
    while (XPending(dpy)) {
      XNextEvent(dpy, &ev);

      /* Xkb StateNotify for Alt sidebar tracking */
      if (xkb_event_base && ev.type == xkb_event_base) {
        XkbEvent *xkbev = (XkbEvent *)&ev;
        if (xkbev->any.xkb_type == XkbStateNotify) {
          int new_alt = (xkbev->state.mods & Mod1Mask) != 0;
          if (enablesidebar && selmon) {
            if (new_alt && !alt_held)
              showsidbar(selmon);
            else if (!new_alt && alt_held)
              hideosidebar(selmon);
          }
          alt_held = new_alt;
          getkblayout();
          drawbars();
        }
        continue;
      }

      if (handler[ev.type])
        handler[ev.type](&ev); /* call handler */
    }
    if (n == 0)
      checkminimize();
    updateclock();
  }
}

void updateclock(void) {
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  char buf[8];

  if (!tm)
    return;
  snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
  if (strcmp(buf, clockstr) == 0)
    return;
  strcpy(clockstr, buf);
  drawbars();
}

void scan(void) {
  unsigned int i, num;
  Window d1, d2, *wins = NULL;
  XWindowAttributes wa;

  if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
    for (i = 0; i < num; i++) {
      if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect ||
          XGetTransientForHint(dpy, wins[i], &d1))
        continue;
      if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
        manage(wins[i], &wa);
    }
    for (i = 0; i < num; i++) { /* now the transients */
      if (!XGetWindowAttributes(dpy, wins[i], &wa))
        continue;
      if (XGetTransientForHint(dpy, wins[i], &d1) &&
          (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
        manage(wins[i], &wa);
    }
    if (wins)
      XFree(wins);
  }
}

void sendmon(Client *c, Monitor *m) {
  if (c->mon == m)
    return;
  unfocus(c, 1);
  detach(c);
  detachstack(c);
  c->mon = m;
  c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
  attach(c);
  attachstack(c);
  setclienttagprop(c);
  focus(NULL);
  arrange(NULL);
}

void setclientstate(Client *c, long state) {
  long data[] = {state, None};
  Atom atoms[32];
  int natoms = 0;
  Atom da;
  int di;
  unsigned long nitems, extra;
  unsigned char *p = NULL;

  if (XGetWindowProperty(dpy, c->win, netatom[NetWMState], 0L, 32, False,
                         XA_ATOM, &da, &di, &nitems, &extra,
                         &p) == Success && p) {
    Atom *existing = (Atom *)p;
    for (unsigned long i = 0; i < nitems && natoms < 32; i++)
      if (existing[i] != netatom[NetWMStateHidden])
        atoms[natoms++] = existing[i];
    XFree(p);
  }

  if (state == IconicState)
    atoms[natoms++] = netatom[NetWMStateHidden];

  XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)atoms, natoms);

  XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
                  PropModeReplace, (unsigned char *)data, 2);
}

void setcurrentdesktop(void) {
  long data[] = {0};
  XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)data, 1);
}
void setdesktopnames(void) {
  XTextProperty text;
  Xutf8TextListToTextProperty(dpy, tags, TAGSLENGTH, XUTF8StringStyle, &text);
  XSetTextProperty(dpy, root, &text, netatom[NetDesktopNames]);
}

int sendevent(Window w, Atom proto, int mask, long d0, long d1, long d2,
              long d3, long d4) {
  int n;
  Atom *protocols, mt;
  int exists = 0;
  XEvent ev;

  if (proto == wmatom[WMTakeFocus] || proto == wmatom[WMDelete]) {
    mt = wmatom[WMProtocols];
    if (XGetWMProtocols(dpy, w, &protocols, &n)) {
      while (!exists && n--)
        exists = protocols[n] == proto;
      XFree(protocols);
    }
  } else {
    exists = True;
    mt = proto;
  }
  if (exists) {
    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = mt;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = d0;
    ev.xclient.data.l[1] = d1;
    ev.xclient.data.l[2] = d2;
    ev.xclient.data.l[3] = d3;
    ev.xclient.data.l[4] = d4;
    XSendEvent(dpy, w, False, mask, &ev);
  }
  return exists;
}

void setnumdesktops(void) {
  long data[] = {TAGSLENGTH};
  XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)data, 1);
}

/*
 * cgwrite_focused — GPU VRAM boost for focused window via cgroup
 * Mirrors dwm-dmemcg-boost.sh logic: reads /proc/PID/cgroup, enables
 * subtree_control recursively, and writes to dmem.low
 */
#define CGWRITE_BIN "/usr/local/bin/cgwrite"
#define CGWRITE_DRM_SYSFS "/sys/class/drm"
#define CGWRITE_DEFAULT_BOOST_SIZE "4294967296" /* 4 GiB */
#define CGWRITE_DEFAULT_DRM_RESOURCE 0

static int cgwrite_drm_resource = CGWRITE_DEFAULT_DRM_RESOURCE;
static char cgwrite_boost_size[64] = CGWRITE_DEFAULT_BOOST_SIZE;

static int cgwrite_read_uint64_file(const char *path,
                                    unsigned long long *value) {
  FILE *f;
  unsigned long long v;

  if (!path || !value)
    return 0;

  f = fopen(path, "r");
  if (!f)
    return 0;

  if (fscanf(f, "%llu", &v) != 1) {
    fclose(f);
    return 0;
  }

  fclose(f);
  *value = v;
  return 1;
}

static int cgwrite_read_text_file(const char *path, char *buf,
                                  size_t bufsize) {
  FILE *f;

  if (!path || !buf || bufsize == 0)
    return 0;

  f = fopen(path, "r");
  if (!f)
    return 0;

  if (!fgets(buf, bufsize, f)) {
    fclose(f);
    return 0;
  }

  fclose(f);
  return 1;
}

static int cgwrite_parse_card_name(const char *name, int *card,
                                   const char **suffix) {
  const char *p;
  char *end;
  long n;

  if (!name || strncmp(name, "card", 4) != 0 ||
      !isdigit((unsigned char)name[4]))
    return 0;

  p = name + 4;
  errno = 0;
  n = strtol(p, &end, 10);
  if (errno != 0 || n < 0 || n > 255)
    return 0;

  if (card)
    *card = (int)n;
  if (suffix)
    *suffix = end;

  return 1;
}

static int cgwrite_find_connected_display_card(void) {
  DIR *dir;
  struct dirent *ent;
  int best = -1;

  dir = opendir(CGWRITE_DRM_SYSFS);
  if (!dir)
    return -1;

  while ((ent = readdir(dir))) {
    const char *suffix;
    char status_path[512];
    char status[64];
    int card;

    if (!cgwrite_parse_card_name(ent->d_name, &card, &suffix) || !suffix ||
        suffix[0] != '-')
      continue;

    snprintf(status_path, sizeof(status_path), "%s/%s/status",
             CGWRITE_DRM_SYSFS, ent->d_name);
    if (!cgwrite_read_text_file(status_path, status, sizeof(status)))
      continue;

    if (strncmp(status, "connected", strlen("connected")) == 0 &&
        (best < 0 || card < best))
      best = card;
  }

  closedir(dir);
  return best;
}

static int cgwrite_find_boot_vga_card(void) {
  DIR *dir;
  struct dirent *ent;
  int best = -1;

  dir = opendir(CGWRITE_DRM_SYSFS);
  if (!dir)
    return -1;

  while ((ent = readdir(dir))) {
    const char *suffix;
    char boot_vga_path[512];
    unsigned long long boot_vga;
    int card;

    if (!cgwrite_parse_card_name(ent->d_name, &card, &suffix) || !suffix ||
        suffix[0] != '\0')
      continue;

    snprintf(boot_vga_path, sizeof(boot_vga_path), "%s/%s/device/boot_vga",
             CGWRITE_DRM_SYSFS, ent->d_name);
    if (cgwrite_read_uint64_file(boot_vga_path, &boot_vga) && boot_vga == 1 &&
        (best < 0 || card < best))
      best = card;
  }

  closedir(dir);
  return best;
}

static int cgwrite_find_first_card(void) {
  DIR *dir;
  struct dirent *ent;
  int best = -1;

  dir = opendir(CGWRITE_DRM_SYSFS);
  if (!dir)
    return -1;

  while ((ent = readdir(dir))) {
    const char *suffix;
    int card;

    if (!cgwrite_parse_card_name(ent->d_name, &card, &suffix) || !suffix ||
        suffix[0] != '\0')
      continue;

    if (best < 0 || card < best)
      best = card;
  }

  closedir(dir);
  return best;
}

static int cgwrite_read_card_vram_total(int card, unsigned long long *bytes) {
  static const char *files[] = {
      "mem_info_vram_total",
      "mem_info_vis_vram_total",
      "local_memory_size",
  };
  char path[512];
  size_t i;

  if (card < 0 || !bytes)
    return 0;

  for (i = 0; i < LENGTH(files); i++) {
    snprintf(path, sizeof(path), "%s/card%d/device/%s", CGWRITE_DRM_SYSFS,
             card, files[i]);
    if (cgwrite_read_uint64_file(path, bytes) && *bytes > 0)
      return 1;
  }

  return 0;
}

static void cgwrite_detect_drm_boost(void) {
  unsigned long long bytes;
  int card;

  cgwrite_drm_resource = CGWRITE_DEFAULT_DRM_RESOURCE;
  snprintf(cgwrite_boost_size, sizeof(cgwrite_boost_size), "%s",
           CGWRITE_DEFAULT_BOOST_SIZE);

  card = cgwrite_find_connected_display_card();
  if (card < 0)
    card = cgwrite_find_boot_vga_card();
  if (card < 0)
    card = cgwrite_find_first_card();

  if (card < 0)
    return;

  cgwrite_drm_resource = card;
  if (cgwrite_read_card_vram_total(card, &bytes))
    snprintf(cgwrite_boost_size, sizeof(cgwrite_boost_size), "%llu", bytes);
}

pid_t getwindowpid(Window win) {
  Atom atom, type;
  int format;
  unsigned long nitems, bytes;
  unsigned char *prop;
  pid_t pid = -1;

  atom = XInternAtom(dpy, "_NET_WM_PID", False);
  if (atom == None)
    return -1;

  if (XGetWindowProperty(dpy, win, atom, 0L, 1L, False, XA_CARDINAL, &type,
                          &format, &nitems, &bytes, &prop) == Success &&
      prop != NULL) {
    pid = *(pid_t *)prop;
    XFree(prop);
  }
  return pid;
}

static Window get_win_prop(Window w, const char *atom_name) {
  Atom atom, type;
  int format;
  unsigned long nitems, bytes;
  unsigned char *prop;
  Window result = None;

  atom = XInternAtom(dpy, atom_name, False);
  if (atom == None)
    return None;

  if (XGetWindowProperty(dpy, w, atom, 0L, 1L, False, XA_WINDOW, &type,
                          &format, &nitems, &bytes, &prop) == Success &&
      prop != NULL) {
    result = *(Window *)prop;
    XFree(prop);
  }
  return result;
}

static unsigned long get_user_time(Window win) {
  Atom atom, type;
  int format;
  unsigned long nitems, bytes;
  unsigned char *prop;
  unsigned long time_val = 0;

  atom = XInternAtom(dpy, "_NET_WM_USER_TIME", False);
  if (atom == None)
    return 0;

  if (XGetWindowProperty(dpy, win, atom, 0L, 1L, False, XA_CARDINAL, &type,
                          &format, &nitems, &bytes, &prop) == Success &&
      prop != NULL) {
    time_val = *(unsigned long *)prop;
    XFree(prop);
  }
  return time_val;
}

static int is_browser_window(Client *c) {
  if (!c->name[0] || c->name[0] == '\0')
    return 0;
  if (strstr(c->name, "Firefox") || strstr(c->name, "firefox"))
    return 1;
  if (strstr(c->name, "Zen Browser") || strstr(c->name, "zen-browser"))
    return 1;
  if (strstr(c->name, "chrome") || strstr(c->name, "Chrome") ||
      strstr(c->name, "Chromium") || strstr(c->name, "Chromium-browser") ||
      strstr(c->name, "brave") || strstr(c->name, "Microsoft Edge") ||
      strstr(c->name, "microsoft-edge") || strstr(c->name, "edge") ||
      strstr(c->name, "vivaldi") || strstr(c->name, "opera") ||
      strstr(c->name, "waterfox") || strstr(c->name, "librewolf") ||
      strstr(c->name, "pale-moon") || strstr(c->name, "org.mozilla"))
    return 1;
  return 0;
}

Client *find_pipparent(Client *c) {
  if (!c || c->win == None)
    return NULL;

  Monitor *m;
  Client *it;
  unsigned long highest_time = 0;
  Client *best_parent = NULL;

  Window pip_user_time_window = get_win_prop(c->win, "_NET_WM_USER_TIME_WINDOW");
  if (pip_user_time_window != None && pip_user_time_window != root) {
    Window parent = pip_user_time_window;
    Window child;
    int depth = 0;

    while (parent && parent != root && depth < 20) {
      Client *client = wintoclient(parent);
      if (client) {
        pid_t client_pid = getwindowpid(client->win);
        pid_t pip_pid = getwindowpid(c->win);
        if (client_pid == pip_pid && client_pid > 0 && client != c) {
          return client;
        }
      }

      if (XGetTransientForHint(dpy, parent, &child) && child == pip_user_time_window) {
        Window dummy;
        Window *children = NULL;
        unsigned int nchildren = 0;

        if (XQueryTree(dpy, parent, &dummy, &parent, &children, &nchildren)) {
          if (children)
            XFree(children);
          if (!parent || parent == root)
            break;
        } else {
          break;
        }
      } else {
        parent = None;
      }
      depth++;
    }
  }

  pid_t pip_pid = getwindowpid(c->win);
  if (pip_pid <= 0)
    return NULL;

  for (m = mons; m; m = m->next) {
    for (it = m->clients; it; it = it->next) {
      if (!it->win || it == c || it->pipparent)
        continue;

      pid_t client_pid = getwindowpid(it->win);
      if (client_pid != pip_pid)
        continue;

      if (is_browser_window(it)) {

        unsigned long current_time = get_user_time(it->win);

        if (current_time > highest_time) {
          highest_time = current_time;
          best_parent = it;
        }
      }
    }
  }

  return best_parent;
}

void cgwrite_focused(Window win) {
  pid_t pid, child;
  char pid_str[32];
  char drm_resource_str[16];
  char *argv[] = {CGWRITE_BIN, pid_str, drm_resource_str, cgwrite_boost_size,
                  NULL};

  if (!win || win == None)
    return;

  pid = getwindowpid(win);
  if (pid <= 0)
    return;

  snprintf(pid_str, sizeof(pid_str), "%d", (int)pid);
  snprintf(drm_resource_str, sizeof(drm_resource_str), "%d",
           cgwrite_drm_resource);

  child = fork();
  if (child < 0) {
    /* fork failed — nothing we can do without logging infrastructure here */
    return;
  }

  if (child == 0) {
    /* Child: exec the setuid binary */
    execv(CGWRITE_BIN, argv);
    /* execv only returns on failure */
    _exit(1);
  }

  /* Parent: reap child so it doesn't become a zombie */
  waitpid(child, NULL, 0);
}

void setfocus(Client *c) {
  if (!c->neverfocus) {
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&(c->win), 1);
  } else {
    /* InputHint false: do not focus the client window, but avoid leaving the
     * previous X focus target so keys still go there while dwm shows sel on
     * the neverfocus window (e.g. after arrange → focusunderpointer). */
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
  sendevent(c->win, wmatom[WMTakeFocus], NoEventMask, wmatom[WMTakeFocus],
            CurrentTime, 0, 0, 0);

  /* Apply cgroup settings to focused window */
  cgwrite_focused(c->win);
}

void setfullscreen(Client *c, int fullscreen) {
  if (fullscreen && !c->isfullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&netatom[NetWMFullscreen],
                    1);
    c->isfullscreen = 1;
    c->oldstate = c->isfloating;
    c->oldbw = c->bw;
    c->bw = 0;
    c->isfloating = 1;
    resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
    setwindowopacity(c);
    if (c->bypass_value == 1)
      set_bypass_compositor(c, c->bypass_value);
    XRaiseWindow(dpy, c->win);

    /* hide all lower windows; preserve sticky windows if window has transparency */
    int has_transparency = window_has_transparency(c);
    Client *it;
    for (it = c->mon->stack; it; it = it->snext) {
      if (it == c || !ISVISIBLE(it) || HIDDEN(it))
        continue;
      if (has_transparency && stickywin && it->issticky)
        continue;
      setclientstate(it, IconicState);
      XUnmapWindow(dpy, it->win);
      it->fshidden = 1;
      it->isautominimized = 0;
    }
  } else if (!fullscreen && c->isfullscreen) {
    if (c->bypass_value == 1) {
      set_bypass_compositor(c, 0);
      XSync(dpy, False);
    }
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)0, 0);
    clearwindowopacity(c);
    c->isfullscreen = 0;
    c->isfloating = c->oldstate;
    c->bw = c->oldbw;
    c->x = c->oldx;
    c->y = c->oldy;
    c->w = c->oldw;
    c->h = c->oldh;
    resizeclient(c, c->x, c->y, c->w, c->h);

    /* restore windows hidden by fullscreen */
    Client *it;
    for (it = c->mon->stack; it; it = it->snext) {
      if (it->fshidden) {
        it->fshidden = 0;
        XMapWindow(dpy, it->win);
        setclientstate(it, NormalState);
        it->isautominimized = 0;
      }
    }
    arrange(c->mon);
  }

  if (c == selmon->sel || fullscreen_st)
    updatefullscreenstatus(selmon->sel);
}

void setlayout(const Arg *arg) {
  if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
    selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
  if (arg && arg->v)
    selmon->lt[selmon->sellt] =
        selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] =
            (Layout *)arg->v;
  strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol,
          sizeof(selmon->ltsymbol) - 1);
  selmon->ltsymbol[sizeof(selmon->ltsymbol) - 1] = '\0';
  if (selmon->sel)
    arrange(selmon);
  else
    drawbar(selmon);
}

void setcfact(const Arg *arg) {
  float f;
  Client *c;

  c = selmon->sel;

  if (!arg || !c || !selmon->lt[selmon->sellt]->arrange)
    return;
  if (!arg->f)
    f = 1.0;
  else if (arg->f > 4.0) // set fact absolutely
    f = arg->f - 4.0;
  else
    f = arg->f + c->cfact;
  if (f < 0.25)
    f = 0.25;
  else if (f > 4.0)
    f = 4.0;
  Bool prev_animate = animate_tiled;
  animate_tiled = 0;
  animate_tiled = prev_animate;
  c->cfact = f;
  arrange(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg) {
  float f;

  if (!arg || !selmon->lt[selmon->sellt]->arrange)
    return;
  f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
  if (f < 0.05 || f > 0.95)
    return;
  Bool prev_animate = animate_tiled;
  animate_tiled = 0;
  selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag] = f;
  arrange(selmon);
  animate_tiled = prev_animate;
}

void setup(void) {
  int i;
  XSetWindowAttributes wa;
  Atom utf8string;
  struct sigaction sa;
  /* do not transform children into zombies when they terminate */
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
  sa.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &sa, NULL);

  /* clean up any zombies (inherited from .xinitrc etc) immediately */
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;

  cgwrite_detect_drm_boost();

  /* init screen */
  screen = DefaultScreen(dpy);
  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  root = RootWindow(dpy, screen);
  drw = drw_create(dpy, screen, root, sw, sh);
  if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
    die("no fonts could be loaded.");
  lrpad = drw->fonts->h;
  bh = drw->fonts->h + 2 + vertpadbar + borderpx * 2;
  th = vertpadtab;
  // bh_n = vertpadtab;
  updategeom();
  /* init atoms */
  utf8string = XInternAtom(dpy, "UTF8_STRING", False);
  wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
  wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
  wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
  netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
  netatom[NetSystemTray] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
  netatom[NetSystemTrayOP] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
  netatom[NetSystemTrayOrientation] =
      XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
  netatom[NetSystemTrayOrientationHorz] =
      XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION_HORZ", False);
  netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
  netatom[NetWMIcon] = XInternAtom(dpy, "_NET_WM_ICON", False);
  netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
  netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
  netatom[NetWMFullscreen] =
      XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  netatom[NetWMStateHidden] =
      XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
  netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  netatom[NetWMWindowTypeDialog] =
      XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  netatom[NetWMWindowOpacity] =
      XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
  netatom[NetWMBypassCompositor] =
      XInternAtom(dpy, "_NET_WM_BYPASS_COMPOSITOR", False);
  netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
  xatom[Manager] = XInternAtom(dpy, "MANAGER", False);
  xatom[Xembed] = XInternAtom(dpy, "_XEMBED", False);
  xatom[XembedInfo] = XInternAtom(dpy, "_XEMBED_INFO", False);
  netatom[NetDesktopViewport] =
      XInternAtom(dpy, "_NET_DESKTOP_VIEWPORT", False);
  netatom[NetNumberOfDesktops] =
      XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
  netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
  netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
  netatom[NetClientInfo] = XInternAtom(dpy, "_NET_CLIENT_INFO", False);
  netatom[NetNoAnimation] = XInternAtom(dpy, "_NO_ANIMATION", False);
  netatom[NetBarLeft] = XInternAtom(dpy, "_BAR_LEFT", False);
  /* init cursors */
  cursor[CurNormal] = drw_cur_create(drw, "left_ptr");
  cursor[CurResize] = drw_cur_create(drw, "sizing");
  cursor[CurMove] = drw_cur_create(drw, "fleur");
  cursor[CurResizeHorzArrow] = drw_cur_create(drw, "sb_h_double_arrow");
  cursor[CurResizeVertArrow] = drw_cur_create(drw, "sb_v_double_arrow");
  cursor[CurTopLeft] = drw_cur_create(drw, "top_left_corner");
  cursor[CurTopRight] = drw_cur_create(drw, "top_right_corner");
  cursor[CurBottomLeft] = drw_cur_create(drw, "bottom_left_corner");
  cursor[CurBottomRight] = drw_cur_create(drw, "bottom_right_corner");
  /* init appearance */
  scheme = ecalloc(LENGTH(colors) + 1, sizeof(Clr *));
  scheme[LENGTH(colors)] = drw_scm_create(drw, colors[0], 3);
  for (i = 0; i < LENGTH(colors); i++)
    scheme[i] = drw_scm_create(drw, colors[i], 3);
  drw_clr_create(drw, &clrborder, col_borderbar);
  /* init system tray */
  updatesystray();
  /* init bars */
  updatebars();
  updatestatus();
  updatebarpos(selmon);
  updatepreview();
  updatenumberofdesktops();
  updatecurrentdesktop(selmon->tagset[selmon->seltags]);
  /* supporting window for NetWMCheck */
  wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)&wmcheckwin, 1);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
                  PropModeReplace, (unsigned char *)"dwm", 3);
  XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)&wmcheckwin, 1);
  XChangeProperty(dpy, root, netatom[NetWMName], utf8string, 8,
                  PropModeReplace, (unsigned char *)"dwm", 3);
  /* EWMH support per view */
  XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)netatom, NetLast);
  setnumdesktops();
  setcurrentdesktop();
  setdesktopnames();
  setviewport();
  XDeleteProperty(dpy, root, netatom[NetClientList]);
  XDeleteProperty(dpy, root, netatom[NetClientInfo]);
  /* select events */
  wa.cursor = cursor[CurNormal]->cursor;
  wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                  ButtonPressMask | PointerMotionMask | EnterWindowMask |
                  LeaveWindowMask | StructureNotifyMask | PropertyChangeMask |
                  FocusChangeMask;
  XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
  grabkeys();
  /* Xkb StateNotify for Alt sidebar tracking */
  if (XkbQueryExtension(dpy, NULL, &xkb_event_base, NULL, NULL, NULL)) {
    XkbSelectEvents(dpy, XkbUseCoreKbd, XkbStateNotifyMask, XkbStateNotifyMask);
    getkblayout();
  }
  focus(NULL);
}
void setviewport(void) {
  long data[] = {0, 0};
  XChangeProperty(dpy, root, netatom[NetDesktopViewport], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)data, 2);
}

void seturgent(Client *c, int urg) {
  XWMHints *wmh;

  c->isurgent = urg;
  if (!(wmh = XGetWMHints(dpy, c->win)))
    return;
  wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
  XSetWMHints(dpy, c->win, wmh);
  XFree(wmh);
}

void show(Client *c) {
  if (!c || !HIDDEN(c))
    return;

  XMapWindow(dpy, c->win);
  setclientstate(c, NormalState);
  c->isautominimized = 0;
  c->fshidden = 0;
  arrange(c->mon);
}

void showhide(Client *c) {
  if (!c)
    return;
  if (ISVISIBLE(c)) {
    c->lastvisible = 0;
    /* show clients top down */
    if (HIDDEN(c) && c->isautominimized) {
      XMapWindow(dpy, c->win);
      setclientstate(c, NormalState);
      c->isautominimized = 0;
    }
    XMoveWindow(dpy, c->win, c->x, c->y);
    if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) &&
        !c->isfullscreen)
      resize(c, c->x, c->y, c->w, c->h, 0);
    showhide(c->snext);
  } else {
    if (c->lastvisible == 0)
      c->lastvisible = time(NULL);
    /* hide clients bottom up */
    showhide(c->snext);
    setclientstate(c, IconicState);
    c->isautominimized = 1;
    XUnmapWindow(dpy, c->win);
  }
}

void showtagpreview(int tag) {
  if (!selmon->previewshow || !tag_preview) {
    XUnmapWindow(dpy, selmon->tagwin);
    return;
  }

  if (selmon->tagmap[tag]) {
    XSetWindowBackgroundPixmap(dpy, selmon->tagwin, selmon->tagmap[tag]);
    XCopyArea(dpy, selmon->tagmap[tag], selmon->tagwin, drw->gc, 0, 0,
              selmon->mw / scalepreview, selmon->mh / scalepreview, 0, 0);
    XSync(dpy, False);
    XMapWindow(dpy, selmon->tagwin);
  } else
    XUnmapWindow(dpy, selmon->tagwin);
}

void spawn(const Arg *arg) {
  struct sigaction sa;
  if (fork() == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    setsid();
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &sa, NULL);
    execvp(((char **)arg->v)[0], (char **)arg->v);
    die("dwm: execvp '%s' failed:", ((char **)arg->v)[0]);
  }
}

void setclienttagprop(Client *c) {
  long data[] = {(long)c->tags, (long)c->mon->num};
  XChangeProperty(dpy, c->win, netatom[NetClientInfo], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)data, 2);
}

static void setworkspaceanimation(Monitor *m, int val) {
  Client *c;
  long data = val;
  for (c = m->clients; c; c = c->next)
    if (ISVISIBLE(c))
      XChangeProperty(dpy, c->win, netatom[NetNoAnimation], XA_CARDINAL, 32,
                      PropModeReplace, (unsigned char *)&data, 1);
}

void switchtag(void) {
  int i;
  unsigned int occ = 0;
  Client *c;
  Imlib_Image image;

  for (c = selmon->clients; c; c = c->next)
    occ |= c->tags;
  for (i = 0; i < LENGTH(tags); i++) {
    if (selmon->tagset[selmon->seltags] & 1 << i) {
      if (selmon->tagmap[i] != 0) {
        XFreePixmap(dpy, selmon->tagmap[i]);
        selmon->tagmap[i] = 0;
      }
      if (occ & 1 << i && tag_preview) {
        image = imlib_create_image(sw, sh);
        imlib_context_set_image(image);
        imlib_context_set_display(dpy);
        imlib_context_set_visual(DefaultVisual(dpy, screen));
        imlib_context_set_drawable(RootWindow(dpy, screen));
        imlib_copy_drawable_to_image(0, selmon->mx, selmon->my, selmon->mw,
                                     selmon->mh, 0, 0, 1);
        selmon->tagmap[i] =
            XCreatePixmap(dpy, selmon->tagwin, selmon->mw / scalepreview,
                          selmon->mh / scalepreview, DefaultDepth(dpy, screen));
        imlib_context_set_drawable(selmon->tagmap[i]);
        imlib_render_image_part_on_drawable_at_size(
            0, 0, selmon->mw, selmon->mh, 0, 0, selmon->mw / scalepreview,
            selmon->mh / scalepreview);
        imlib_free_image();
      }
    }
  }
}

void tabmode(const Arg *arg) {
  if (arg && arg->i >= 0)
    selmon->showtab = arg->ui % showtab_nmodes;
  else
    selmon->showtab = (selmon->showtab + 1) % showtab_nmodes;
  arrange(selmon);
}

void tag(const Arg *arg) {
  Client *c;
  if (selmon->sel && arg->ui & TAGMASK) {
    c = selmon->sel;
    selmon->sel->tags = arg->ui & TAGMASK;
    setclienttagprop(c);
    focus(NULL);
    arrange(selmon);
  }
}

void tagmon(const Arg *arg) {
  if (!selmon->sel || !mons->next)
    return;
  sendmon(selmon->sel, dirtomon(arg->i));
}

void togglebar(const Arg *arg) {
  selmon->showbar = selmon->pertag->showbars[selmon->pertag->curtag] =
      !selmon->showbar;
  updatebarpos(selmon);
  resizebarwin(selmon);
  if (showsystray) {
    XWindowChanges wc;
    if (!selmon->showbar)
      wc.y = -bh;
    else if (selmon->showbar) {
      wc.y = selmon->gappoh;
      if (!selmon->topbar)
        wc.y = selmon->mh - bh + selmon->gappoh;
    }
    XConfigureWindow(dpy, systray->win, CWY, &wc);
  }
  arrange(selmon);
}

void togglefloating_noarrange(Client *c) {
  if (!c || c->isfullscreen || c->issticky)
    return;
  c->isfloating = !c->isfloating || c->isfixed;
  if (c->isfloating)
    resize(c, c->x, c->y, c->w, c->h, 0);
}

void togglefloating(const Arg *arg) {
  if (!selmon->sel)
    return;
  if (selmon->sel->isfullscreen || selmon->sel->issticky) /* no support for fullscreen or sticky windows */
    return;
  selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
  if (selmon->sel->isfloating)
    resize(selmon->sel, selmon->sel->x, selmon->sel->y, selmon->sel->w,
           selmon->sel->h, 0);
  arrange(selmon);
}

void togglefullscr(const Arg *arg) {
  if (!selmon->sel)
    return;

  XEvent e = {0};
  e.type = ClientMessage;
  e.xclient.window = selmon->sel->win;
  e.xclient.message_type = netatom[NetWMState];
  e.xclient.format = 32;
  e.xclient.data.l[0] = 2;  /* _NET_WM_STATE_TOGGLE */
  e.xclient.data.l[1] = netatom[NetWMFullscreen];
  e.xclient.data.l[2] = 0;
  XSendEvent(dpy, root, False, SubstructureRedirectMask, &e);

  /* MAY BE NEXT TIME ( need someone to implement this )
    Add auto close pip when enter fullscreen and reopen when exit fullscreen
    - 1st run close pip if exists when entering fullscreen
      - save piprante to reopen when exiting fullscreen
    - 2nd run reopen pip if it was closed when exiting fullscreen
      - add fake focus to parent to trigger focus event, this will fake browser to allow spawn pip again
  */
}


void toggletag(const Arg *arg) {
  unsigned int newtags;

  if (!selmon->sel)
    return;
  newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
  if (newtags) {
    selmon->sel->tags = newtags;
    setclienttagprop(selmon->sel);
    focus(NULL);
    updatecurrentdesktop(selmon->tagset[selmon->seltags]);
    arrange(selmon);
  }
}

void toggleview(const Arg *arg) {
  unsigned int newtagset =
      selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);
  int i;

  if (newtagset) {
    prepare_workspace_switch(selmon, selmon->tagset[selmon->seltags], newtagset);
    switchtag();
    selmon->tagset[selmon->seltags] = newtagset;

    if (newtagset == ~0) {
      selmon->pertag->prevtag = selmon->pertag->curtag;
      selmon->pertag->curtag = 0;
    }

    /* test if the user did not select the same tag */
    if (!(newtagset & 1 << (selmon->pertag->curtag - 1))) {
      selmon->pertag->prevtag = selmon->pertag->curtag;
      for (i = 0; !(newtagset & 1 << i); i++)
        ;
      selmon->pertag->curtag = i + 1;
    }

    /* apply settings for this view */
    selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
    selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
    selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
    selmon->lt[selmon->sellt] =
        selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
    selmon->lt[selmon->sellt ^ 1] =
        selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt ^ 1];

    if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
      togglebar(NULL);

    updatecurrentdesktop(selmon->tagset[selmon->seltags]);
    focus(NULL);
    arrange(selmon);
  }
}

void hidewin(const Arg *arg) {
  if (!selmon->sel)
    return;
  Client *c = (Client *)selmon->sel;
  hide(c);
  hiddenWinStack[++hiddenWinStackTop] = c;
}


void restorewin(const Arg *arg) {
  int i = hiddenWinStackTop;
  while (i > -1) {
    if (HIDDEN(hiddenWinStack[i]) &&
        hiddenWinStack[i]->tags == selmon->tagset[selmon->seltags]) {
      show(hiddenWinStack[i]);
      focus(hiddenWinStack[i]);
      restack(selmon);
      for (int j = i; j < hiddenWinStackTop; ++j) {
        hiddenWinStack[j] = hiddenWinStack[j + 1];
      }
      --hiddenWinStackTop;
      return;
    }
    --i;
  }
}


void togglewin(const Arg *arg) {
  if (selmon->sel) hidewin(arg);
  else restorewin(arg);
}


void unfocus(Client *c, int setfocus) {
  if (!c)
    return;
  grabbuttons(c, 0);
  XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
  if (setfocus) {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
}

void unmanage(Client *c, int destroyed) {
  Monitor *m = c->mon;
  XWindowChanges wc;

  if (!destroyed && (c->isfullscreen || c->issticky))
    clearwindowopacity(c);

  if (c == selmon->sel || (c->isfullscreen && fullscreen_st))
    updatefullscreenstatus(NULL);

  /* cleanup sticky metadata if the removed client was sticky */
  if (c == stickywin) {
    stickywin = NULL;
    stick_state = 0;
  }

  detach(c);
  detachstack(c);
  freeicon(c);

  if (!destroyed) {
    wc.border_width = c->oldbw;
    XGrabServer(dpy); /* avoid race conditions */
    XSetErrorHandler(xerrordummy);
    XSelectInput(dpy, c->win, NoEventMask);
    XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }

  /* if the dying window was fullscreen, restore any clients it hid */
  if (c->isfullscreen) {
    Client *it;
    for (it = m->stack; it; it = it->snext) {
      if (it->fshidden) {
        it->fshidden = 0;
        XMapWindow(dpy, it->win);
        setclientstate(it, NormalState);
        it->isautominimized = 0;
      }
    }
  }

  if (c->pipparent) {
    c->pipparent->pipparent = NULL;
    c->pipparent = NULL;
  }

  free(c);
  updateclientlist();
  arrange(m);
}

void unmapnotify(XEvent *e) {
  Client *c;
  XUnmapEvent *ev = &e->xunmap;

  if ((c = wintoclient(ev->window))) {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else if (ISVISIBLE(c) && getstate(c->win) != IconicState)
      unmanage(c, 0);
  } else if ((c = wintosystrayicon(ev->window))) {
    /* KLUDGE! sometimes icons occasionally unmap their windows, but do
     * _not_ destroy them. We map those windows back */
    XMapRaised(dpy, c->win);
    updatesystray();
  }
}

void updatebars(void) {
  unsigned int w;
  Monitor *m;
  XSetWindowAttributes wa = {.override_redirect = True,
                             .background_pixmap = ParentRelative,
                             .event_mask = ButtonPressMask | ExposureMask |
                                           PointerMotionMask};

  XClassHint ch = {"dwm", "dwm"};
  for (m = mons; m; m = m->next) {
    if (m->barwin)
      continue;
    w = m->ww;
    if (showsystray && m == systraytomon(m))
      w -= getsystraywidth();
    m->barwin = XCreateWindow(
        dpy, root, m->wx + m->gappov, m->by, w - 2 * m->gappov, bh, 0,
        DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
    XMapRaised(dpy, m->barwin);
    m->bartitlewin = XCreateWindow(
        dpy, root, m->wx, m->by, 1, bh, 0, DefaultDepth(dpy, screen),
        CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    XDefineCursor(dpy, m->bartitlewin, cursor[CurNormal]->cursor);
    XUnmapWindow(dpy, m->bartitlewin);
    m->barcenterwin = XCreateWindow(
        dpy, root, m->wx, m->by, 1, bh, 0, DefaultDepth(dpy, screen),
        CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    XDefineCursor(dpy, m->barcenterwin, cursor[CurNormal]->cursor);
    XUnmapWindow(dpy, m->barcenterwin);
    m->barkbwin = XCreateWindow(
        dpy, root, m->wx, m->by, 1, bh, 0, DefaultDepth(dpy, screen),
        CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    XDefineCursor(dpy, m->barkbwin, cursor[CurNormal]->cursor);
    XUnmapWindow(dpy, m->barkbwin);
    m->barrightwin = XCreateWindow(
        dpy, root, m->wx, m->by, 1, bh, 0, DefaultDepth(dpy, screen),
        CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    XDefineCursor(dpy, m->barrightwin, cursor[CurNormal]->cursor);
    XUnmapWindow(dpy, m->barrightwin);
    m->tabwin = XCreateWindow(
        dpy, root, m->wx + m->gappov, m->ty, m->ww - 2 * m->gappov, th, 0,
        DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    XDefineCursor(dpy, m->tabwin, cursor[CurNormal]->cursor);
    XMapRaised(dpy, m->tabwin);
    /* sidebar window */
    XSetWindowAttributes swa = {.override_redirect = True,
                                .event_mask = ButtonPressMask | EnterWindowMask |
                                              PointerMotionMask};
    m->sidebarwin = XCreateWindow(
        dpy, root, m->mx + m->gappov * 2, m->my + m->mh / 3, SIDEBAR_WIDTH, m->mh / 3, 0,
        DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWEventMask, &swa);
    XChangeProperty(dpy, m->sidebarwin, netatom[NetBarLeft], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&netatom[NetBarLeft], 1);
    /* don't map here — only show when apps exist */
    m->sidebarvisible = 0;
    XSetClassHint(dpy, m->barwin, &ch);
  }
}

void updatepreview(void) {
  Monitor *m;

  XSetWindowAttributes wa = {.override_redirect = True,
                             .background_pixmap = ParentRelative,
                             .event_mask = ButtonPressMask | ExposureMask};
  for (m = mons; m; m = m->next) {
    m->tagwin = XCreateWindow(
        dpy, root, m->wx, m->by + bh, m->mw / scalepreview, m->mh / scalepreview, 0,
        DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
    XDefineCursor(dpy, m->tagwin, cursor[CurNormal]->cursor);
    XMapRaised(dpy, m->tagwin);
    XUnmapWindow(dpy, m->tagwin);
  }
}

void updatebarpos(Monitor *m) {
  Client *c;
  int nvis = 0;

  m->wy = m->my;
  m->wh = m->mh;

  for (c = m->clients; c; c = c->next) {
    if (ISVISIBLE(c))
      ++nvis;
  }

  if (m->showtab == showtab_always ||
      ((m->showtab == showtab_auto) && (nvis > 1) &&
       (m->lt[m->sellt]->arrange == monocle))) {
    m->topbar = !toptab;
    m->wh -=
        th + ((m->topbar == toptab && m->showbar) ? 0 : m->gappoh) - m->gappoh;
    m->ty = m->toptab ? m->wy + ((m->topbar && m->showbar) ? 0 : m->gappoh)
                      : m->wy + m->wh - m->gappoh;
    if (m->toptab)
      m->wy += th + ((m->topbar && m->showbar) ? 0 : m->gappoh) - m->gappoh;
  } else {
    m->ty = -th - m->gappoh;
    m->topbar = topbar;
  }
  if (m->showbar) {
    if (floatbar) {
      m->wh = m->wh - m->gappoh - bh;
      m->by = m->topbar ? m->wy + m->gappoh : m->wy + m->wh;
    } else {
      m->wh = m->wh - bh;
      m->by = m->topbar ? m->wy : m->wy + m->wh;
    }
    if (m->topbar) {
      m->wy += floatbar ? bh + m->gappoh : bh;
    }
  } else
    m->by = -bh - m->gappoh;
  /* resize sidebar */
  if (m->sidebarwin) {
    calcsidebar(m);
    int sidebary = m->my + (m->mh - m->sidebarh) / 2;
    XMoveResizeWindow(dpy, m->sidebarwin, m->mx + m->gappov * 2, sidebary, m->sidebarw, m->sidebarh);
  }
}

void updateclientlist() {
  Client *c;
  Monitor *m;

  XDeleteProperty(dpy, root, netatom[NetClientList]);
  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
                      PropModeAppend, (unsigned char *)&(c->win), 1);
}

void updatenumberofdesktops(void) {
  long data[] = {LENGTH(tags)};
  XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)data, 1);
}

void updatecurrentdesktop(unsigned int tagset) {
  long data[] = {0};
  unsigned int i, mask = 1;

  for (i = 0; i < LENGTH(tags); i++, mask <<= 1) {
    if (tagset & mask) {
      data[0] = i;
      break;
    }
  }

  XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)data, 1);
}

int updategeom(void) {
  int dirty = 0;

#ifdef XINERAMA
  if (XineramaIsActive(dpy)) {
    int i, j, n, nn;
    Client *c;
    Monitor *m;
    XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
    XineramaScreenInfo *unique = NULL;

    for (n = 0, m = mons; m; m = m->next, n++)
      ;
    /* only consider unique geometries as separate screens */
    unique = ecalloc(nn, sizeof(XineramaScreenInfo));
    for (i = 0, j = 0; i < nn; i++)
      if (isuniquegeom(unique, j, &info[i]))
        memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
    XFree(info);
    nn = j;
    /* new monitors if nn > n */
    for (i = n; i < nn; i++) {
      for (m = mons; m && m->next; m = m->next)
        ;
      if (m)
        m->next = createmon();
      else
        mons = createmon();
    }
    for (i = 0, m = mons; i < nn && m; m = m->next, i++)
      if (i >= n || unique[i].x_org != m->mx || unique[i].y_org != m->my ||
          unique[i].width != m->mw || unique[i].height != m->mh) {
        dirty = 1;
        m->num = i;
        m->mx = m->wx = unique[i].x_org;
        m->my = m->wy = unique[i].y_org;
        m->mw = m->ww = unique[i].width;
        m->mh = m->wh = unique[i].height;
        updatebarpos(m);
      }
    /* removed monitors if n > nn */
    for (i = nn; i < n; i++) {
      for (m = mons; m && m->next; m = m->next)
        ;
      while ((c = m->clients)) {
        dirty = 1;
        m->clients = c->next;
        detachstack(c);
        c->mon = mons;
        attach(c);
        attachstack(c);
      }
      if (m == selmon)
        selmon = mons;
      cleanupmon(m);
    }
    free(unique);
  } else
#endif /* XINERAMA */
  {    /* default monitor setup */
    if (!mons)
      mons = createmon();
    if (mons->mw != sw || mons->mh != sh) {
      dirty = 1;
      mons->mw = mons->ww = sw;
      mons->mh = mons->wh = sh;
      updatebarpos(mons);
    }
  }
  if (dirty) {
    selmon = mons;
    selmon = wintomon(root);
  }
  return dirty;
}

void updatenumlockmask(void) {
  unsigned int i, j;
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap = XGetModifierMapping(dpy);
  for (i = 0; i < 8; i++)
    for (j = 0; j < modmap->max_keypermod; j++)
      if (modmap->modifiermap[i * modmap->max_keypermod + j] ==
          XKeysymToKeycode(dpy, XK_Num_Lock))
        numlockmask = (1 << i);
  XFreeModifiermap(modmap);
}

void updatesizehints(Client *c) {
  long msize;
  XSizeHints size;

  if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
    /* size is uninitialized, ensure that size.flags aren't used */
    size.flags = PSize;
  if (size.flags & PBaseSize) {
    c->basew = size.base_width;
    c->baseh = size.base_height;
  } else if (size.flags & PMinSize) {
    c->basew = size.min_width;
    c->baseh = size.min_height;
  } else
    c->basew = c->baseh = 0;
  if (size.flags & PResizeInc) {
    c->incw = size.width_inc;
    c->inch = size.height_inc;
  } else
    c->incw = c->inch = 0;
  if (size.flags & PMaxSize) {
    c->maxw = size.max_width;
    c->maxh = size.max_height;
  } else
    c->maxw = c->maxh = 0;
  if (size.flags & PMinSize) {
    c->minw = size.min_width;
    c->minh = size.min_height;
  } else if (size.flags & PBaseSize) {
    c->minw = size.base_width;
    c->minh = size.base_height;
  } else
    c->minw = c->minh = 0;
  if (size.flags & PAspect) {
    c->mina = (float)size.min_aspect.y / size.min_aspect.x;
    c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
  } else
    c->maxa = c->mina = 0.0;
  c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
  c->hintsvalid = 1;
}

void updatestatus(void) {
  if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
    strcpy(stext, "dwm-" VERSION);
  drawbars();
  updatesystray();
}

void updatesystrayicongeom(Client *i, int w, int h) {
  int rh = bh - vertpadbar;
  if (i) {
    i->h = rh;
    if (w == h)
      i->w = rh;
    else if (h == rh)
      i->w = w;
    else
      i->w = (int)((float)rh * ((float)w / (float)h));
    i->y = i->y + vertpadbar / 2;
    applysizehints(i, &(i->x), &(i->y), &(i->w), &(i->h), False);
    /* force icons into the systray dimensions if they don't want to */
    if (i->h > rh) {
      if (i->w == i->h)
        i->w = rh;
      else
        i->w = (int)((float)rh * ((float)i->w / (float)i->h));
      i->h = rh;
    }
  }
}

void updatesystrayiconstate(Client *i, XPropertyEvent *ev) {
  long flags;
  int code = 0;

  if (!showsystray || !i || ev->atom != xatom[XembedInfo] ||
      !(flags = getatomprop(i, xatom[XembedInfo])))
    return;

  if (flags & XEMBED_MAPPED && !i->tags) {
    i->tags = 1;
    code = XEMBED_WINDOW_ACTIVATE;
    XMapRaised(dpy, i->win);
    setclientstate(i, NormalState);
  } else if (!(flags & XEMBED_MAPPED) && i->tags) {
    i->tags = 0;
    code = XEMBED_WINDOW_DEACTIVATE;
    XUnmapWindow(dpy, i->win);
    setclientstate(i, WithdrawnState);
  } else
    return;
  sendevent(i->win, xatom[Xembed], StructureNotifyMask, CurrentTime, code, 0,
            systray->win, XEMBED_EMBEDDED_VERSION);
}

void updatesystray(void) {
  XSetWindowAttributes wa;
  XWindowChanges wc;
  Client *i;
  Monitor *m = systraytomon(NULL);
  unsigned int x = floatbar ? m->mx + m->mw - m->gappov : m->mx + m->mw;
  unsigned int w = 1;

  if (!showsystray)
    return;
  if (!systray) {
    /* init systray */
    if (!(systray = (Systray *)calloc(1, sizeof(Systray))))
      die("fatal: could not malloc() %u bytes\n", sizeof(Systray));
    systray->win = XCreateSimpleWindow(dpy, root, x, m->by, w, bh, 0, 0,
                                       scheme[SchemeSel][ColBg].pixel);
    wa.event_mask = ButtonPressMask | ExposureMask;
    wa.override_redirect = True;
    wa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
    XSelectInput(dpy, systray->win, SubstructureNotifyMask);
    XChangeProperty(dpy, systray->win, netatom[NetSystemTrayOrientation],
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&netatom[NetSystemTrayOrientationHorz], 1);
    XChangeWindowAttributes(
        dpy, systray->win, CWEventMask | CWOverrideRedirect | CWBackPixel, &wa);
    XUnmapWindow(dpy, systray->win);
    XSetSelectionOwner(dpy, netatom[NetSystemTray], systray->win, CurrentTime);
    if (XGetSelectionOwner(dpy, netatom[NetSystemTray]) == systray->win) {
      sendevent(root, xatom[Manager], StructureNotifyMask, CurrentTime,
                netatom[NetSystemTray], systray->win, 0, 0);
      XSync(dpy, False);
    } else {
      fprintf(stderr, "dwm: unable to obtain system tray.\n");
      free(systray);
      systray = NULL;
      return;
    }
  }
  for (w = 0, i = systray->icons; i; i = i->next) {
    /* make sure the background color stays the same */
    wa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
    XChangeWindowAttributes(dpy, i->win, CWBackPixel, &wa);
    XMapRaised(dpy, i->win);
    w += systrayspacing;
    i->x = w;
    XMoveResizeWindow(dpy, i->win, i->x, vertpadbar / 2, i->w, i->h);
    w += i->w;
    if (i->mon != m)
      i->mon = m;
  }
  if (!w) {
    XUnmapWindow(dpy, systray->win);
    return;
  }
  w += systrayspacing;
  x -= w;
  XMoveResizeWindow(dpy, systray->win, x, m->by, w, bh);
  wc.x = x;
  wc.y = m->by;
  wc.width = w;
  wc.height = bh;
  wc.stack_mode = Above;
  wc.sibling = m->barwin;
  XConfigureWindow(dpy, systray->win,
                   CWX | CWY | CWWidth | CWHeight | CWSibling | CWStackMode,
                   &wc);
  XMapWindow(dpy, systray->win);
  XMapSubwindows(dpy, systray->win);
  /* redraw background */
  XSetForeground(dpy, drw->gc, scheme[SchemeNorm][ColBg].pixel);
  XFillRectangle(dpy, systray->win, drw->gc, 0, 0, w, bh);
  XSync(dpy, False);
}

void updatetitle(Client *c) {
  if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

void updatewindowtype(Client *c) {
  Atom state = getatomprop(c, netatom[NetWMState]);
  Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

  if (state == netatom[NetWMFullscreen])
    setfullscreen(c, 1);
  if (wtype == netatom[NetWMWindowTypeDialog]) {
    c->iscentered = 1;
    c->isfloating = 1;
  }
}

void updatewmhints(Client *c) {
  XWMHints *wmh;

  if ((wmh = XGetWMHints(dpy, c->win))) {
    if (c == selmon->sel && wmh->flags & XUrgencyHint) {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(dpy, c->win, wmh);
    } else
      c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
    if (wmh->flags & InputHint)
      c->neverfocus = !wmh->input;
    else
      c->neverfocus = 0;
    XFree(wmh);
  }
}

void view(const Arg *arg) {
  int i;
  unsigned int tmptag;
  unsigned int newtagset;

  if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
    return;
  
  newtagset = arg->ui & TAGMASK ? arg->ui & TAGMASK : selmon->pertag->prevtag;
  updatecurrentdesktop(newtagset);
  XSync(dpy,False);
  prepare_workspace_switch(selmon, selmon->tagset[selmon->seltags], newtagset);
  switchtag();
  selmon->seltags ^= 1; /* toggle sel tagset */

  if (arg->ui & TAGMASK) {
    selmon->pertag->prevtag = selmon->pertag->curtag;
    selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;

    if (arg->ui == ~0)
      selmon->pertag->curtag = 0;
    else {
      for (i = 0; !(arg->ui & 1 << i); i++)
        ;
      selmon->pertag->curtag = i + 1;
    }
  } else {
    tmptag = selmon->pertag->prevtag;
    selmon->pertag->prevtag = selmon->pertag->curtag;
    selmon->pertag->curtag = tmptag;
  }

  selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
  selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
  selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
  selmon->lt[selmon->sellt] =
      selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
  selmon->lt[selmon->sellt ^ 1] =
      selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt ^ 1];

  if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
    togglebar(NULL);

  focus(NULL);
  arrange(selmon);
}

Client *wintoclient(Window w) {
  Client *c;
  Monitor *m;

  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      if (c->win == w)
        return c;
  return NULL;
}

Client *wintosystrayicon(Window w) {
  Client *i = NULL;

  if (!showsystray || !w)
    return i;
  for (i = systray->icons; i && i->win != w; i = i->next)
    ;
  return i;
}

Monitor *wintomon(Window w) {
  int x, y;
  Client *c;
  Monitor *m;

  if (w == root && getrootptr(&x, &y))
    return recttomon(x, y, 1, 1);
  for (m = mons; m; m = m->next)
    if (w == m->barwin || w == m->bartitlewin || w == m->barcenterwin ||
        w == m->barkbwin || w == m->barrightwin || w == m->tabwin)
      return m;
  if ((c = wintoclient(w)))
    return c->mon;
  return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int xerror(Display *dpy, XErrorEvent *ee) {
  if (ee->error_code == BadWindow ||
      (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) ||
      (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable) ||
      (ee->request_code == X_PolyFillRectangle &&
       ee->error_code == BadDrawable) ||
      (ee->request_code == X_PolySegment && ee->error_code == BadDrawable) ||
      (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) ||
      (ee->request_code == X_GrabButton && ee->error_code == BadAccess) ||
      (ee->request_code == X_GrabKey && ee->error_code == BadAccess) ||
      (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
    return 0;
  fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
          ee->request_code, ee->error_code);
  return xerrorxlib(dpy, ee); /* may call exit */
}

int xerrordummy(Display *dpy, XErrorEvent *ee) { return 0; }

/* Startup Error handler to check if another window manager
 * is already running. */
int xerrorstart(Display *dpy, XErrorEvent *ee) {
  die("dwm: another window manager is already running");
  return -1;
}

Monitor *systraytomon(Monitor *m) {
  Monitor *t;
  int i, n;
  if (!systraypinning) {
    if (!m)
      return selmon;
    return m == selmon ? m : NULL;
  }
  for (n = 1, t = mons; t && t->next; n++, t = t->next)
    ;
  for (i = 1, t = mons; t && t->next && i < systraypinning; i++, t = t->next)
    ;
  if (systraypinningfailfirst && n < systraypinning)
    return mons;
  return t;
}

void zoom(const Arg *arg) {
  Client *c = selmon->sel;

  if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
    return;
  if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
    return;
  pop(c);
}

int main(int argc, char *argv[]) {
  XInitThreads();
  if (argc == 2 && !strcmp("-v", argv[1]))
    die("dwm-" VERSION);
  else if (argc != 1 && strcmp("-s", argv[1]))
    die("usage: dwm [-v]");
  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
    fputs("warning: no locale support\n", stderr);
  if (!(dpy = XOpenDisplay(NULL)))
    die("dwm: cannot open display");
  if (argc > 1 && !strcmp("-s", argv[1])) {
    XStoreName(dpy, RootWindow(dpy, DefaultScreen(dpy)), argv[2]);
    XCloseDisplay(dpy);
    return 0;
  }
  checkotherwm();
  setup();
#ifdef __OpenBSD__
  if (pledge("stdio rpath proc exec", NULL) == -1)
    die("pledge");
#endif /* __OpenBSD__ */
  scan();
  if (CLOCK_ANIMATE)
    sound_monitor_start();
  run();
  cleanup();
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
