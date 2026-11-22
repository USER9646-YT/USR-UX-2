#include "logos/arch.h"
#define COLOR "\e[1;36m"

// Original code: Copyright (c) 2020. Sam Barr
// The original code is licensed under the MIT License (see THIRDPARTYLICENSE)
// Modifications: Copyright (c) 2026 USER9646
// Modifications are licensed under the main project's license.

#define CONFIG \
{ \
   /* name            function                 cached */\
    { "",             get_title,               false }, \
    { "",             get_bar,                 false }, \
    { "OS: ",         get_os,                  true  }, \
    { "Kernel: ",     get_kernel,              true  }, \
    { "Uptime: ",     get_uptime,              false }, \
    SPACER \
    { "Shell: ",      get_shell,             false }, \
    { "CPU: ",        get_cpu,                 true  }, \
    { "Memory: ",     get_memory,              false }, \
    SPACER \
    { "",             get_colors1,             false }, \
    { "",             get_colors2,             false }, \
}

#define CPU_CONFIG \
{ \
   REMOVE("(R)"), \
   REMOVE("(TM)"), \
   REMOVE("Dual-Core"), \
   REMOVE("Quad-Core"), \
   REMOVE("Six-Core"), \
   REMOVE("Eight-Core"), \
   REMOVE("Core"), \
   REMOVE("CPU"), \
}

#define GPU_CONFIG \
{ \
    REMOVE("Corporation"), \
}
