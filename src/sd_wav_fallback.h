#ifndef SD_WAV_FALLBACK_H
#define SD_WAV_FALLBACK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool sd_wav_fallback_init(const char *filename);
bool sd_wav_fallback_init_playlist(void);

void sd_wav_fallback_set_enabled(bool enabled);
bool sd_wav_fallback_is_enabled(void);

void sd_wav_fallback_task(void);
void sd_wav_fallback_close(void);

bool sd_wav_fallback_next(void);
bool sd_wav_fallback_prev(void);
bool sd_wav_fallback_restart(void);

const char *sd_wav_fallback_current_file(void);
int sd_wav_fallback_current_index(void);
int sd_wav_fallback_playlist_count(void);

#ifdef __cplusplus
}
#endif

#endif