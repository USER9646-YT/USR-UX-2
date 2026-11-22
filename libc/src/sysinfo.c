#include <sys/sysinfo.h>
#include <stdio.h>
#include <string.h>

int sysinfo(struct sysinfo *info) {
    if (!info) return -1;
    memset(info, 0, sizeof(struct sysinfo));

    // Default memory unit to bytes
    info->mem_unit = 1;

    // Example: Read uptime from procfs if available
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        double up_secs = 0;
        if (fscanf(f, "%lf", &up_secs) == 1) {
            info->uptime = (long)up_secs;
        }
        fclose(f);
    }

    // Example: Read memory info from procfs if available
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[64];
        unsigned long val;
        while (fscanf(f, "%63s %lu", key, &val) == 2) {
            if (strcmp(key, "MemTotal:") == 0) {
                info->totalram = val * 1024; // Assuming procfs reports KB
            } else if (strcmp(key, "MemFree:") == 0) {
                info->freeram = val * 1024;
            }
        }
        fclose(f);
    }

    return 0;
}