/* Forward-declare our functions so users can mention them in their
 * configs at the top of the file rather than near the bottom. */

// Original code: Copyright (c) 2020. Sam Barr
// The original code is licensed under the MIT License (see THIRDPARTYLICENSE)
// Modifications: Copyright (c) 2026 USER9646
// Modifications are licensed under the main project's license.


static char *get_title(),
            *get_bar(),
            *get_os(),
            *get_kernel(),
            *get_host(),
            *get_uptime(),
            *get_battery_percentage(),
            *get_packages_pacman(),
            *get_shell(),
            *get_resolution(),
            *get_terminal(),
            *get_cpu(),
            *get_gpu1(),
            *get_gpu2(),
            *get_memory(),
            *get_disk_usage_root(),
            *get_disk_usage_home(),
            *get_colors1(),
            *get_colors2(),
            *spacer();

#define SPACER {"", spacer, false},
#define REMOVE(A) { (A), NULL, sizeof(A) - 1 , 0 }
#define REPLACE(A, B) { (A), (B), sizeof(A) - 1, sizeof(B) - 1 }
