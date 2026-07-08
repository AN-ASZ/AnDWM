# Sidebar Fixes

## Changes

### dwm.c: Line 58 — width 28 → 48
```c
#define SIDEBAR_WIDTH 48
```

### updatebars() ~line 4876 — don't map empty sidebar window
Remove `XMapRaised(dpy, m->sidebarwin);` — sidebar stays unmapped until apps exist.

### drawsidebar() ~lines 1967,1971,1993 — use 1/3 monitor height
```c
int itemh = m->mh / 3 / count;  // height = 1/3 monitor / apps
drw_rect(drw, 0, 0, SIDEBAR_WIDTH, m->mh / 3, 1, 1);
drw_map(drw, m->sidebarwin, 0, 0, SIDEBAR_WIDTH, m->mh / 3);
```

### updatebarpos() ~line 4940 — same height, correct position
```c
XMoveResizeWindow(dpy, m->sidebarwin, m->mx, m->my, SIDEBAR_WIDTH, m->mh / 3);
```

### showsidbar() — only show when apps exist
Add check before mapping:
```c
for (c = m->clients; c; c = c->next)
  if (ISVISIBLE(c) && !HIDDEN(c)) { count++; break; }
if (!count) { hideosidebar(m); return; }
```

### motionnotify() ~line 2936 — simplify hide
```c
} else if (m->sidebarvisible && ev->x_root > (long)m->mx + (long)SIDEBAR_WIDTH) {
```
