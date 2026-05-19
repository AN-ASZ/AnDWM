/*
 * cgwrite — setuid root helper for GPU VRAM cgroup boost
 *
 * Usage: cgwrite <pid> [drm_resource boost_size]
 *
 * Install:
 *   sudo cp cgwrite /usr/local/bin/cgwrite
 *   sudo chown root:root /usr/local/bin/cgwrite
 *   sudo chmod 4755 /usr/local/bin/cgwrite
 *
 * dwm calls this from cgwrite_focused() via execv, passing the focused
 * window's PID plus the DRM resource and boost size detected at DWM startup.
 * No root privilege is needed in dwm itself.
 */

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CGROUP_ROOT "/sys/fs/cgroup"
#define CGROUP_PATH_MAX 512
#define DEFAULT_BOOST_SIZE "4294967296" /* 4 GiB */
#define DEFAULT_DRM_RESOURCE 0          /* GPU minor node index */
#define CGWRITE_LOG_PATH "/tmp/dwm-cgwrite-errors.log"

/* -------------------------------------------------------------------------- */
/* Logging */
/* -------------------------------------------------------------------------- */

static void cgwrite_log_error(const char *fmt, ...) {
  va_list ap;
  char msg[1024];
  char logpath[CGROUP_PATH_MAX];
  struct passwd *pw;
  FILE *f;
  time_t t;
  struct tm tm;
  char timestr[64];

  if (!fmt)
    return;

  pw = getpwuid(getuid());
  if (pw && pw->pw_dir && pw->pw_dir[0])
    snprintf(logpath, sizeof(logpath), "%s/dwm-cgwrite-errors.log",
             pw->pw_dir);
  else
    snprintf(logpath, sizeof(logpath), "%s", CGWRITE_LOG_PATH);

  if (strcmp(logpath, CGWRITE_LOG_PATH) != 0) {
    unlink(logpath);
    symlink(CGWRITE_LOG_PATH, logpath);
  }

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
  char subtree_file[CGROUP_PATH_MAX + sizeof("/cgroup.subtree_control")];
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

static int parse_pid_arg(const char *arg, pid_t *pid) {
  char *end;
  long value;

  if (!arg || !pid)
    return 0;

  errno = 0;
  value = strtol(arg, &end, 10);
  if (errno != 0 || !end || *end != '\0' || value <= 0 || value > INT_MAX)
    return 0;

  *pid = (pid_t)value;
  return 1;
}

static int parse_drm_resource_arg(const char *arg, int *resource) {
  char *end;
  long value;

  if (!arg || !resource)
    return 0;

  errno = 0;
  value = strtol(arg, &end, 10);
  if (errno != 0 || !end || *end != '\0' || value < 0 || value > 255)
    return 0;

  *resource = (int)value;
  return 1;
}

static int parse_boost_size_arg(const char *arg, char *boost,
                                size_t boost_size) {
  char *end;
  unsigned long long value;

  if (!arg || !boost || boost_size == 0)
    return 0;

  errno = 0;
  value = strtoull(arg, &end, 10);
  if (errno != 0 || !end || *end != '\0' || value == 0)
    return 0;

  snprintf(boost, boost_size, "%llu", value);
  return 1;
}

int main(int argc, char *argv[]) {
  pid_t pid;
  char cgroup_full[CGROUP_PATH_MAX];
  char dmem_low_path[CGROUP_PATH_MAX];
  char boost_value[128];
  char boost_size[64];
  int drm_resource;

  if (argc != 2 && argc != 4) {
    fprintf(stderr, "usage: cgwrite <pid> [drm_resource boost_size]\n");
    return 1;
  }

  if (!parse_pid_arg(argv[1], &pid)) {
    cgwrite_log_error("cgwrite: invalid pid argument: %s", argv[1]);
    return 1;
  }

  drm_resource = DEFAULT_DRM_RESOURCE;
  snprintf(boost_size, sizeof(boost_size), "%s", DEFAULT_BOOST_SIZE);
  if (argc == 4) {
    if (!parse_drm_resource_arg(argv[2], &drm_resource)) {
      cgwrite_log_error("cgwrite: invalid drm resource argument: %s", argv[2]);
      return 1;
    }
    if (!parse_boost_size_arg(argv[3], boost_size, sizeof(boost_size))) {
      cgwrite_log_error("cgwrite: invalid boost size argument: %s", argv[3]);
      return 1;
    }
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
  snprintf(boost_value, sizeof(boost_value), "%d %s\n", drm_resource,
           boost_size);

  write_cgroup_file(dmem_low_path, boost_value);

  return 0;
}
