#ifndef CLKS_CONFIG_H
#define CLKS_CONFIG_H

#include <clks/types.h>

#define CLKS_CONFIG_THEME_PATH "/system/configs/theme.conf"
#define CLKS_CONFIG_STARTUP_PATH "/system/configs/startup.conf"
#define CLKS_CONFIG_THEME_MAX 64U
#define CLKS_CONFIG_STARTUP_MAX 256U

void clks_config_init(void);
const char *clks_config_theme(void);
const char *clks_config_startup_command(void);

#endif
