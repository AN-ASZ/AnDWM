/*
 * cgwrite — setuid root helper for GPU VRAM cgroup boost
 *
 * Usage: cgwrite <pid>
 *
 * Install:
 *   sudo cp cgwrite /usr/local/bin/cgwrite
 *   sudo chown root:root /usr/local/bin/cgwrite
 *   sudo chmod 4755 /usr/local/bin/cgwrite
 *
 * dwm calls this from cgwrite_focused() via execv, passing the focused
 * window's PID. No root privilege is needed in dwm itself.
 */

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CGROUP_ROOT "/sys/fs/cgroup"
#define CGROUP_PATH_MAX 512
#define BOOST_SIZE "4294967296" /* 4 GiB */
#define DRM_RESOURCE "0"        /* GPU minor node index */

/* -------------------------------------------------------------------------- */
/* Logging */
/* -------------------------------------------------------------------------- */

static void cgwrite_log_error(const char *fmt, ...) {
  va_list ap;
  char msg[1024];
  char logpath[CGROUP_PATH_MAX];
  const char *user;
  FILE *f;
  time_t t;
  struct tm tm;
  char timestr[64];

  if (!fmt)
    return;

  user = getenv("USER");
  if (user && user[0])
    snprintf(logpath, sizeof(logpath), "/home/%s/dwm-cgwrite-errors.log", user);
  else
    snprintf(logpath, sizeof(logpath), "/tmp/dwm-cgwrite-errors.log");

  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  t = time(NULL);
  timestr[0] = '\0';
  if (t != (time_t)-1 && localtime_r(&t, &tm))
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm);

  f = fopen(logpath, "a");
  if (!f)
    return;

  if (timestr[0])
    fprintf(f, "%s %s\n", timestr, msg);
  else
    fprintf(f, "%s\n", msg);

  fclose(f);
}

/* -------------------------------------------------------------------------- */
/* cgroup helpers */
/* -------------------------------------------------------------------------- */

/* Resolve /proc/<pid>/cgroup → full /sys/fs/cgroup/... path */
static int get_cgroup_path(pid_t pid, char *buf, size_t bufsize) {
  char procfile[64];
  FILE *f;
  char line[512];

  if (pid <= 0 || !buf || bufsize == 0)
    return 0;

  snprintf(procfile, sizeof(procfile), "/proc/%d/cgroup", (int)pid);
  f = fopen(procfile, "r");
  if (!f) {
    cgwrite_log_error("get_cgroup_path: cannot open %s: %s", procfile,
                      strerror(errno));
    return 0;
  }

  /* Format: "0::<relative_path>\n" */
  if (fgets(line, sizeof(line), f)) {
    char *p = strchr(line, ':');
    if (p) {
      p = strchr(p + 1, ':');
      if (p) {
        p++; /* skip second colon */
        char *nl = strchr(p, '\n');
        if (nl)
          *nl = '\0';
        snprintf(buf, bufsize, "%s%s", CGROUP_ROOT, p);
        fclose(f);
        return 1;
      }
    }
  }

  cgwrite_log_error("get_cgroup_path: malformed /proc/%d/cgroup", (int)pid);
  fclose(f);
  return 0;
}

/* Write value to a cgroup control file */
static void write_cgroup_file(const char *path, const char *value) {
  FILE *f;

  if (!path || !value)
    return;

  f = fopen(path, "w");
  if (!f) {
    cgwrite_log_error("write_cgroup_file: cannot open %s: %s", path,
                      strerror(errno));
    return;
  }

  if (fputs(value, f) == EOF)
    cgwrite_log_error("write_cgroup_file: write failed for %s: %s", path,
                      strerror(errno));

  fclose(f);
}

/*
 * Walk from cgroup_path up toward CGROUP_ROOT, enabling "+dmem" in
 * cgroup.subtree_control at each level that doesn't already have it.
 */
static void enable_dmem_subtree(const char *cgroup_path) {
  char path[CGROUP_PATH_MAX];
  char subtree_file[CGROUP_PATH_MAX];
  char buf[256];
  FILE *f;
  char *slash;

  if (!cgroup_path || strlen(cgroup_path) <= strlen(CGROUP_ROOT))
    return;

  strncpy(path, cgroup_path, sizeof(path) - 1);
  path[sizeof(path) - 1] = '\0';

  while (strcmp(path, CGROUP_ROOT) != 0) {
    snprintf(subtree_file, sizeof(subtree_file), "%s/cgroup.subtree_control",
             path);

    /* Check if dmem is already present */
    f = fopen(subtree_file, "r");
    if (f) {
      int found = 0;
      while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, "dmem")) {
          found = 1;
          break;
        }
      }
      fclose(f);
      if (found) {
        /* Already enabled at this level — climb to parent */
        slash = strrchr(path, '/');
        if (slash && slash != path)
          *slash = '\0';
        else
          break;
        continue;
      }
    }

    write_cgroup_file(subtree_file, "+dmem\n");

    slash = strrchr(path, '/');
    if (slash && slash != path)
      *slash = '\0';
    else
      break;
  }
}

/* -------------------------------------------------------------------------- */
/* Main */
/* -------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
  pid_t pid;
  char cgroup_full[CGROUP_PATH_MAX];
  char dmem_low_path[CGROUP_PATH_MAX];
  char boost_value[128];

  if (argc != 2) {
    fprintf(stderr, "usage: cgwrite <pid>\n");
    return 1;
  }

  pid = (pid_t)atoi(argv[1]);
  if (pid <= 0) {
    cgwrite_log_error("cgwrite: invalid pid argument: %s", argv[1]);
    return 1;
  }

  /* Verify process exists */
  if (kill(pid, 0) != 0) {
    cgwrite_log_error("cgwrite: process %d not running: %s", (int)pid,
                      strerror(errno));
    return 1;
  }

  /* Resolve cgroup path */
  if (!get_cgroup_path(pid, cgroup_full, sizeof(cgroup_full))) {
    cgwrite_log_error("cgwrite: cannot resolve cgroup for pid %d", (int)pid);
    return 1;
  }

  /* Validate path is safely under CGROUP_ROOT */
  if (strncmp(cgroup_full, CGROUP_ROOT, strlen(CGROUP_ROOT)) != 0) {
    cgwrite_log_error("cgwrite: cgroup path outside root: %s", cgroup_full);
    return 1;
  }
  if (strstr(cgroup_full, "..") != NULL) {
    cgwrite_log_error("cgwrite: path traversal detected: %s", cgroup_full);
    return 1;
  }
  if (access(cgroup_full, F_OK) != 0) {
    cgwrite_log_error("cgwrite: cgroup path not found: %s", cgroup_full);
    return 1;
  }

  /* Enable dmem in subtree controls */
  enable_dmem_subtree(cgroup_full);

  /* Write VRAM boost to dmem.low */
  snprintf(dmem_low_path, sizeof(dmem_low_path), "%s/dmem.low", cgroup_full);
  snprintf(boost_value, sizeof(boost_value), "%s %s\n", DRM_RESOURCE,
           BOOST_SIZE);

  write_cgroup_file(dmem_low_path, boost_value);

  return 0;
}