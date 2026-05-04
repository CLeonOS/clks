#ifndef CLKS_LOCALE_H
#define CLKS_LOCALE_H

#include <clks/types.h>

#define CLKS_LOCALE_MAX 32U
#define CLKS_LOCALE_CONFIG_PATH "/system/locale.conf"
#define CLKS_LOCALE_DEFAULT "en_US.UTF-8"

void clks_locale_init(void);
const char *clks_locale_current(void);
clks_bool clks_locale_is_valid(const char *locale);
clks_bool clks_locale_set(const char *locale, clks_bool persist);

#endif
