#ifndef CLKS_BOOTSPLASH_H
#define CLKS_BOOTSPLASH_H

#include <clks/types.h>

void clks_bootsplash_init(void);
void clks_bootsplash_step(u32 percent, const char *label);
void clks_bootsplash_finish(void);
clks_bool clks_bootsplash_active(void);

#endif
