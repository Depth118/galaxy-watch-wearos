#pragma once
#include <string.h>
#include <sys/system_properties.h>
#define PROPERTY_VALUE_MAX PROP_VALUE_MAX
#define PROPERTY_KEY_MAX PROP_NAME_MAX
static inline int property_get(const char *k, char *v, const char *d)
{
	int n = __system_property_get(k, v);
	if (n > 0) return n;
	if (d) { strncpy(v, d, PROPERTY_VALUE_MAX - 1); v[PROPERTY_VALUE_MAX - 1] = 0; return strlen(v); }
	v[0] = 0; return 0;
}
static inline int property_set(const char *k, const char *v) { return __system_property_set(k, v); }
