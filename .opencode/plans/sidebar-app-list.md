# Sidebar App List - Implementation Plan

## Overview
Add a vertical sidebar on the left side of each monitor showing app icons for windows in the current workspace. The sidebar auto-shows/hides on mouse near left edge or Alt hold, and supports Alt+Tab cycling.

## File: dwm.c

### 1. Add color schemes enum entries (line ~143)
Add 3 more tag color schemes to support up to 9 tags:
```c
  SchemeTag6,
  SchemeTag7,
  SchemeTag8,
  SchemeTag9,
```

### 2. Add struct for sidebar items
Add after the `typedef struct { ... } Button;` block (~line ~214):
```c
/* Sidebar item */
typedef struct {
  Client *c;
  int idx;
} SidebarItem;
```

### 3. Add `sidebarvisible` and `sidebarwin` to Monitor struct (~line ~517)
Add these fields to the Monitor struct:
```c
  Window sidebarwin;       /* sidebar subwindow */
  int sidebarvisible;      /* 0 = hidden, 1 = showing */
```

### 4. Add global sidebar state variables (~line ~482)
Add after `static Window root, wmcheckwin;`:
```c
static AltTabState {
  int active;                 /* Alt key held or Alt+Tab in progress */
  int alttab_idx;             /* index in alttab_cycle[] */
  int alttab_count;           /* number of candidates */
  Client *alttab_cycle[MAXTABS];
  int sidebar_click_idx;      /* sidebar item index for click-to-focus */
} alttab, sidebar_click;
static int sidebar_scroll_offset; /* scroll offset when overflow */
```

### 5. Add sidebar config constants (~line ~47)
Add before `#define FIFO_PATH`:
```c
#define SIDEBAR_WIDTH 28          /* sidebar icon column width */
#define SIDEBAR_ITEM_SIZE 24      /* icon display size */
#define SIDEBAR_MARGIN 4          /* top/bottom margin */
#define SIDEBAR_CLICK_THRESHOLD 5 /* px to register as click vs drag */
#define SIDEBAR_EDGE_TRIGGER 5    /* px from left edge to trigger */
```

### 6. Add function declarations (~line ~399)
Add after `static void toggleview(const Arg *arg);`:
```c
static void drawsidbar(Monitor *m);
static void drawsidbars(void);
static void showsidebar(Monitor *m);
static void hideshowbar(Monitor *m);
static void togglesidebar(const Arg *arg);
static void alttabcycle(const Arg *arg);
static Client *getnextalttab(int forward);
```

### 7. Modify Monitor struct (line ~493-527)
Add to Monitor struct:
```c
Window swin;           /* sidebar subwindow */
int swvisible;         /* sidebar visible flag */
```

### 8. Implement `drawsidebar(Monitor *m)` - draw sidebar windows
After `drawtabs(void)` function (~line ~1801):
```c
void drawsidbar(Monitor *m) {
  if (!m->swvisible) return;
  
  Client *c;
  int n = 0;
  /* count visible clients */
  for (c = m->clients; c; c = c->next)
    if (ISVISIBLE(c)) n++;
  
  if (!n) {
    XUnmapWindow(dpy, m->swin);
    return;
  }
  
  /* position and size sidebar window */
  SidebarItem items[MAXTABS];
  int count = 0;
  for (c = m->clients; c; c = c->next) {
    if (ISVISIBLE(c)) {
      items[count].c = c;
      items[count].idx = count;
      count++;
    }
  }
  
  /* redraw sidebar window background */
  drw_setscheme(drw, scheme[SchemeNorm]);
  drw_rect(drw, 0, 0, SIDEBAR_WIDTH, m->wh, 1, 1);
  
  /* draw each app icon with colored indicator on left */
  int itemh = (m->wh - 2 * SIDEBAR_MARGIN) / count;
  for (int i = 0; i < count; i++) {
    c = items[i].c;
    /* colored indicator bar on left (uses current workspace tag color) */
    int tagcolor = tagschemes[0];
    for (int t = 0; t < LENGTH(tags); t++) {
      if (m->tagset[m->seltags] & (1 << t)) {
        tagcolor = tagschemes[t];
        break;
      }
    }
    drw_setscheme(drw, scheme[tagcolor]);
    drw_rect(drw, 0, SIDEBAR_MARGIN + i * itemh, 2, itemh - 1, 1, 0);
    
    /* draw app icon if available, else just the bar */
    if (c->icon && c->icw > 0) {
      drw_setscheme(drw, scheme[c == m->sel ? SchemeSel : SchemeNorm]);
      drw_pic(drw, 4, SIDEBAR_MARGIN + i * itemh + (itemh - (ICONSIZE > itemh ? itemh : ICONSIZE)) / 2,
              ICONSIZE, ICONSIZE, c->icon);
    } else {
      /* draw title text */
      drw_text(drw, 6, SIDEBAR_MARGIN + i * itemh, SIDEBAR_WIDTH - 8, itemh - 1,
               0, c->name, 0);
    }
  }
  
  drw_map(drw, m->swin, 0, 0, SIDEBAR_WIDTH, m->wh - 2 * SIDEBAR_MARGIN);
}
```

### 9. Implement sidebar show/hide triggered by edge mouse motion
In `motionnotify()` (~line ~1546), detect mouse proximity to left edge:
- Get current monitor from mouse position
- If mouse x - monitor x < SIDEBAR_EDGE_TRIGGER → `showsidebar(m)`
- Else if sidebar visible and mouse moved away → `hideshowbar(m)`

### 10. Add keypress handling for Alt key 
In `keypress()`, detect Alt key press/release:
- On Alt press: `alttab.active = 1; showsidebar(selmon);`
- On Alt release: if not doing alttab cycle, `hideshowbar(selmon);`
- Track Alt via a static variable that checks `ev->xkey.state & Mod1Mask`

### 11. Add button press handling for sidebar click
In `buttonpress()`, add sidebar window click handling:
- If click on `selmon->swin`, calculate which item was clicked
- Find the client at that index and call `focus(client)`

### 12. Implement Alt+Tab cycling
```c
void alttabcycle(const Arg *arg) {
  /* build candidate list of visible clients on selmon */
  alttab.alttab_count = 0;
  for (c = selmon->clients; c; c = c->next)
    if (ISVISIBLE(c) && !HIDDEN(c))
      alttab.alttab_cycle[alttab.alttab_count++] = c;
  
  if (alttab.alttab_count == 0) return;
  
  int forward = arg->i;
  /* update alttab_idx and focus next/prev */
  alttab.alttab_idx = (alttab.alttab_idx + forward + alttab.alttab_count) % alttab.alttab_count;
  focus(alttab.alttab_cycle[alttab.alttab_idx]);
}
```

### 13. Create sidebar window in `updatebars()`
In updatebars(), create sidebar window:
```c
m->swin = XCreateWindow(dpy, root, m->mx, m->wy, SIDEBAR_WIDTH, m->wh,
  0, DefaultDepth(dpy, screen), CopyFromParent,
  DefaultVisual(dpy, screen),
  CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
XMapRaised(dpy, m->swin);
```

### 14. Clean up sidebar window in `cleanupmon()`
```c
XUnmapWindow(dpy, m->swin);
XDestroyWindow(dpy, m->swin);
```

### 15. Draw sidebar after restack
`restack()` already calls `drawbar()` and `drawtab()`. Add `drawsidebar(selmon);`

### 16. Redraw sidebar on focus/title changes
- In `focus()`: add `drawsidbars();`
- After `unmanage()`: sidebar item removed automatically (redrawn next arrange)

### 17. Map sidebar to root with correct event mask
When creating sidebar window, use event mask:
```c
CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
XSelectInput(dpy, m->swin, ButtonPressMask | EnterWindowMask | LeaveWindowMask | PointerMotionMask);
```

## File: config.h

### Add sidebar constants and keybindings
After the existing constants (~line 27):
```c
/* sidebar app list */
static const int enablesidebar = 1;   /* 1 = enabled, 0 = disabled */
static const int sidebarwidth    = 28; /* sidebar width in pixels */
static const int sidebaritemsize = 22; /* app icon size */
static const int sidebaretrigger = 4;  /* px from edge to show sidebar */
```

### Add 9 tag colors for sidebar indicators
In the `colors[]` array:
```c
[SchemeschemeTag6] = { purple,   black,  black },  // tag 6
[SchemeTag7]  = { brown,    black,  black },   // tag 7
[SchemeTag8]  = { cyan,     black,  black },   // tag 8
```

In `tagschemes[]`:
```c
static const int tagschemes[] = {
    SchemeTag1, SchemeTag2, SchemeTag3, SchemeTag4, SchemeTag5,
    SchemeTag6, SchemeTag7, SchemeTag8, SchemeTag9
};
```

### Add keybindings (~line 274, in keys[] array)
```c
{ MODKEY,           XK_Tab,     alttabcycle,    {.i = +1 } },          // Alt+Tab forward
{ MODKEY|ShiftMask, XK_Tab,     alttabcycle,    {.i = -1 } },          // Alt+Shift+Tab backward
{ 0,                0,          togglesidebar,  {0} },                 // Toggle sidebar with indicator in bar
```

## File: config.def.h

### Mirror the same changes from config.def.h
Copy all changes from config.h to config.def.h to keep the default config consistent.

---

## Summary of Key Behaviors

1. **Sidebar auto-show**: When mouse cursor is within `sidebaretrigger` px of the left screen edge → sidebar appears instantly
2. **Sidebar auto-hide**: When mouse moves away from left edge → sidebar disappears instantly
3. **Alt hold**: Holding Mod1 (Alt) keeps sidebar visible; releasing hides it
4. **Alt+Tab**: Cycles through visible apps in current workspace with focus switching
5. **Color indicators**: Each app has a 2px colored bar on left matching the workspace tag color (workspace 1 = green, etc.)
6. **No padding between apps**: App items are stacked tightly, one on top of another
7. **Per-monitor**: Each monitor has its own sidebar
8. **Click to focus**: Clicking a sidebar icon focuses that window
