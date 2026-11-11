#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(ARDUINO_ARCH_ESP8266)
static inline void w4home_start(void) {}
static inline void w4home_resume(void) {}
static inline void w4home_pause(bool) {}
static inline bool w4home_idle(void) { return true; }
#else
void w4home_start(void);
bool w4home_service_request(const char*,bool);
void w4home_resume(void);
void w4home_local_state(char*,size_t,bool*,bool*);
/* Sanitized local diagnostics; never includes credentials. */
void w4home_diagnostics(char *output, size_t capacity);
void w4home_pause(bool paused);
bool w4home_idle(void);
/* Called on the network worker: read only a synchronized status snapshot. */
void w4home_status(char *output, size_t capacity);
#endif
#ifdef __cplusplus
}
#endif
