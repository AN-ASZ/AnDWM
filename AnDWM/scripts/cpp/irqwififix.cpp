#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define MAX_LINE 512

int main() {
    FILE *f = fopen("/proc/interrupts", "r");
    if (!f) return 1;

    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count <= 0) return 1;

    // CPU mask for core 0 only
    unsigned long long mask = 1ULL;  // CPU0

    char line[MAX_LINE];

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace(*p)) p++;

        // Read IRQ number
        int irq = atoi(p);
        if (irq <= 0) continue;

        // Path to smp_affinity
        char smp_path[128];
        snprintf(smp_path, sizeof(smp_path), "/proc/irq/%d/smp_affinity", irq);

        FILE *smp = fopen(smp_path, "w");
        if (smp) {
            fprintf(smp, "%llx\n", mask);
            fclose(smp);
        } else {
            // Uncomment to debug permissions or missing IRQ files
            // perror(smp_path);
        }
    }

    fclose(f);
    return 0;
}
