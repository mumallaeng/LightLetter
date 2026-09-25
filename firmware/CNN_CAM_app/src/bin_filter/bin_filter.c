#include "bin_filter.h"

/*
 * Binary filtering is not part of the current application target.
 * Keep a no-op symbol because the legacy menu in main.c still calls it.
 * This avoids touching main.c, which is stored in a legacy Korean encoding.
 */
void bin_toggle(void)
{
    /* Intentionally disabled. */
}









