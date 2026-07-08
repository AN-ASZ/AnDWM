# Sidebar Bug Fixes Final Plan

## Root Cause Analysis

### Problem 1: Sidebar never hides when mouse goes from sidebar → window
In `motionnotify()`, the sidebar window has `PointerMotionMask`, so when the cursor moves inside the sidebar, motion events are delivered to the sidebar window. The code at line 2942 has:
```c
if (ev->window != root) return;
```
This runs BEFORE the sidebar show/hide logic. So when the user moves the mouse from inside the sidebar into a client window, motion events target the client window (not root) and the function returns immediately. The hide check never runs.

**Fix**: before the `if (ev->window != root) return;` line, add a check: if the current monitor's sidebar is visible and `ev->x_root > m->mx + SIDEBAR_WIDTH`, hide it immediately. This runs on EVERY motion event regardless of which window received it.

### Problem 2: updatebarpos() height is wrong
Line 4953 resizes sidebar X window to `m->wh` (window area height) instead of `m->mh / 3`. The X window appears fullscreen height even though drawn content is 1/3.

**Fix**: change `m->wh` to `m->mh / 3`.

### Problem 3: Click handler uses wrong height
Line 813 in `buttonpress()`: `int itemh = selmon->wh / count;` should match `selmon->mh / 3`.

**Fix**: change to `int itemh = selmon->mh / 3 / count;`

## Files Modified
- `/home/Hi/Projects/AnDWM2/AnDWM/AnDWM/dwm.c` — 3 surgical edits

### Change 1: motionnotify() ~line 2941
Before the early return:
```c
if (ev->window != root && enablesidebar) {
  m = recttomon(ev->x_root, ev->y_root, 1, 1);
  if (m->sidebarvisible && ev->x_root > m->mx + SIDEBAR_WIDTH)
    hideosidebar(m);
} else if (ev->window != root) {
  return;
}
```

### Change 2: updatebarpos() line 4953
```c
XMoveResizeWindow(dpy, m->sidebarwin, m->mx, m->my, SIDEBAR_WIDTH, m->mh / 3);
```

### Change 3: buttonpress() line 813
```c
int itemh = selmon->mh / 3 / count;
```
