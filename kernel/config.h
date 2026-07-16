#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

int config_init(void);
const char *config_get(const char *key);
int config_set(const char *key, const char *value);

#endif
