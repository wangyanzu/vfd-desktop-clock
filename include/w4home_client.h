#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void w4home_start(void);
void w4home_pause(bool paused);
bool w4home_idle(void);
/* Called on the network worker: read only a synchronized status snapshot. */
void w4home_status(char *output, size_t capacity);
#ifdef __cplusplus
}
#endif
