#include "sd_wav_fallback.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "audio_buffer.h"
#include "ff.h"

#define WAV_BLOCK_SAMPLES 256

static FATFS fs;
static FIL wav_file;

static bool mounted = false;
static bool file_open = false;
static bool enabled = false;

static uint32_t data_start = 0;
static uint32_t data_size = 0;
static uint32_t data_pos = 0;

static uint16_t wav_channels = 0;
static uint32_t wav_sample_rate = 0;
static uint16_t wav_bits_per_sample = 0;

static int16_t sample_block[WAV_BLOCK_SAMPLES];

// Fixed 5-song playlist.
// Put these exact files in the root of the SD card.
static const char *playlist[] = {
    "song1.wav",
    "song2.wav",
    "song3.wav",
    "song4.wav",
    "song5.wav"
};

static const int playlist_count = sizeof(playlist) / sizeof(playlist[0]);
static int playlist_index = 0;

static char current_filename[32] = "test.wav";

static uint32_t read_le32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t read_le16(const uint8_t *p) {
    return ((uint16_t)p[0]) |
           ((uint16_t)p[1] << 8);
}

static bool read_exact(void *buf, UINT bytes) {
    UINT br = 0;
    FRESULT fr = f_read(&wav_file, buf, bytes, &br);
    return fr == FR_OK && br == bytes;
}

static void close_current_file_only(void) {
    if (file_open) {
        f_close(&wav_file);
        file_open = false;
    }
}

static bool parse_wav_header(void) {
    uint8_t riff_header[12];

    if (!read_exact(riff_header, 12)) {
        printf("WAV: failed to read RIFF header\n");
        return false;
    }

    if (memcmp(riff_header, "RIFF", 4) != 0 ||
        memcmp(riff_header + 8, "WAVE", 4) != 0) {
        printf("WAV: not a RIFF/WAVE file\n");
        return false;
    }

    bool found_fmt = false;
    bool found_data = false;

    while (!found_data) {
        uint8_t chunk_header[8];

        if (!read_exact(chunk_header, 8)) {
            printf("WAV: failed to read chunk header\n");
            return false;
        }

        uint32_t chunk_size = read_le32(chunk_header + 4);

        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            uint8_t fmt[32];

            if (chunk_size > sizeof(fmt)) {
                printf("WAV: fmt chunk too large\n");
                return false;
            }

            if (!read_exact(fmt, chunk_size)) {
                printf("WAV: failed to read fmt chunk\n");
                return false;
            }

            uint16_t audio_format = read_le16(fmt + 0);
            wav_channels = read_le16(fmt + 2);
            wav_sample_rate = read_le32(fmt + 4);
            wav_bits_per_sample = read_le16(fmt + 14);

            if (audio_format != 1) {
                printf("WAV: unsupported format %u, need PCM format 1\n",
                       audio_format);
                return false;
            }

            if (wav_bits_per_sample != 16) {
                printf("WAV: unsupported bits/sample %u, need 16-bit\n",
                       wav_bits_per_sample);
                return false;
            }

            if (wav_channels != 1 && wav_channels != 2) {
                printf("WAV: unsupported channels %u, need mono or stereo\n",
                       wav_channels);
                return false;
            }

            found_fmt = true;

            printf("WAV: PCM %u-bit, %u channel(s), %lu Hz\n",
                   wav_bits_per_sample,
                   wav_channels,
                   (unsigned long)wav_sample_rate);

        } else if (memcmp(chunk_header, "data", 4) == 0) {
            if (!found_fmt) {
                printf("WAV: found data before fmt\n");
                return false;
            }

            data_start = (uint32_t)f_tell(&wav_file);
            data_size = chunk_size;
            data_pos = 0;
            found_data = true;

            printf("WAV: data start=%lu size=%lu\n",
                   (unsigned long)data_start,
                   (unsigned long)data_size);

        } else {
            FSIZE_t next_pos = f_tell(&wav_file) + chunk_size;

            // WAV chunks are word-aligned.
            if (chunk_size & 1) {
                next_pos++;
            }

            FRESULT fr = f_lseek(&wav_file, next_pos);

            if (fr != FR_OK) {
                printf("WAV: failed to skip unknown chunk\n");
                return false;
            }
        }
    }

    return found_fmt && found_data;
}

static bool open_wav_file(const char *filename) {
    close_current_file_only();

    strncpy(current_filename, filename, sizeof(current_filename) - 1);
    current_filename[sizeof(current_filename) - 1] = '\0';

    printf("SD WAV: opening %s...\n", current_filename);

    FRESULT fr = f_open(&wav_file, current_filename, FA_READ);

    if (fr != FR_OK) {
        printf("SD WAV: open failed for %s, FRESULT=%d\n",
               current_filename,
               fr);
        file_open = false;
        return false;
    }

    file_open = true;

    data_start = 0;
    data_size = 0;
    data_pos = 0;
    wav_channels = 0;
    wav_sample_rate = 0;
    wav_bits_per_sample = 0;

    if (!parse_wav_header()) {
        printf("SD WAV: WAV parse failed for %s\n", current_filename);
        close_current_file_only();
        return false;
    }

    printf("SD WAV: ready file=%s index=%d/%d\n",
           current_filename,
           playlist_index + 1,
           playlist_count);

    return true;
}

bool sd_wav_fallback_init(const char *filename) {
    printf("SD WAV: mounting...\n");

    FRESULT fr = f_mount(&fs, "", 1);

    if (fr != FR_OK) {
        printf("SD WAV: mount failed, FRESULT=%d\n", fr);
        mounted = false;
        file_open = false;
        enabled = false;
        return false;
    }

    mounted = true;
    printf("SD WAV: mounted\n");

    bool ok = open_wav_file(filename);

    enabled = false;

    return ok;
}

bool sd_wav_fallback_init_playlist(void) {
    playlist_index = 0;
    return sd_wav_fallback_init(playlist[playlist_index]);
}

void sd_wav_fallback_set_enabled(bool en) {
    enabled = en;

    if (!enabled) {
        audio_buf_stop();
    }
}

bool sd_wav_fallback_is_enabled(void) {
    return enabled;
}

bool sd_wav_fallback_restart(void) {
    if (!mounted || !file_open) {
        return false;
    }

    FRESULT fr = f_lseek(&wav_file, data_start);

    if (fr != FR_OK) {
        printf("SD WAV: restart failed, FRESULT=%d\n", fr);
        return false;
    }

    data_pos = 0;
    audio_buf_stop();

    printf("SD WAV: restarted %s\n", current_filename);
    return true;
}

bool sd_wav_fallback_next(void) {
    if (!mounted) {
        printf("SD WAV: next failed, SD not mounted\n");
        return false;
    }

    bool was_enabled = enabled;

    enabled = false;
    audio_buf_stop();

    playlist_index++;
    if (playlist_index >= playlist_count) {
        playlist_index = 0;
    }

    printf("SD WAV: next -> %s\n", playlist[playlist_index]);

    bool ok = open_wav_file(playlist[playlist_index]);

    enabled = was_enabled && ok;

    return ok;
}

bool sd_wav_fallback_prev(void) {
    if (!mounted) {
        printf("SD WAV: prev failed, SD not mounted\n");
        return false;
    }

    bool was_enabled = enabled;

    enabled = false;
    audio_buf_stop();

    playlist_index--;
    if (playlist_index < 0) {
        playlist_index = playlist_count - 1;
    }

    printf("SD WAV: prev -> %s\n", playlist[playlist_index]);

    bool ok = open_wav_file(playlist[playlist_index]);

    enabled = was_enabled && ok;

    return ok;
}

const char *sd_wav_fallback_current_file(void) {
    return current_filename;
}

int sd_wav_fallback_current_index(void) {
    return playlist_index;
}

int sd_wav_fallback_playlist_count(void) {
    return playlist_count;
}

void sd_wav_fallback_task(void) {
    if (!enabled || !mounted || !file_open) {
        return;
    }

    if (data_pos >= data_size) {
        // Auto-advance when song ends.
        printf("SD WAV: end of %s, advancing\n", current_filename);
        sd_wav_fallback_next();
        return;
    }

    if (wav_channels == 1) {
        UINT bytes_to_read = WAV_BLOCK_SAMPLES * sizeof(int16_t);

        if (data_pos + bytes_to_read > data_size) {
            bytes_to_read = data_size - data_pos;
        }

        UINT br = 0;
        FRESULT fr = f_read(&wav_file, sample_block, bytes_to_read, &br);

        if (fr != FR_OK || br == 0) {
            printf("SD WAV: read failed or EOF, fr=%d br=%u\n", fr, br);
            sd_wav_fallback_next();
            return;
        }

        data_pos += br;

        uint32_t samples_read = br / sizeof(int16_t);

        audio_buf_push(
            sample_block,
            samples_read,
            AUDIO_SRC_SD
        );

    } else {
        // Stereo WAV: read L/R pairs, convert to mono by averaging.
        int16_t stereo_temp[WAV_BLOCK_SAMPLES * 2];

        UINT bytes_to_read = WAV_BLOCK_SAMPLES * 2 * sizeof(int16_t);

        if (data_pos + bytes_to_read > data_size) {
            bytes_to_read = data_size - data_pos;
        }

        UINT br = 0;
        FRESULT fr = f_read(&wav_file, stereo_temp, bytes_to_read, &br);

        if (fr != FR_OK || br == 0) {
            printf("SD WAV: read failed or EOF, fr=%d br=%u\n", fr, br);
            sd_wav_fallback_next();
            return;
        }

        data_pos += br;

        uint32_t int16_count = br / sizeof(int16_t);
        uint32_t frames = int16_count / 2;

        for (uint32_t i = 0; i < frames; i++) {
            int32_t left = stereo_temp[2 * i];
            int32_t right = stereo_temp[2 * i + 1];

            sample_block[i] = (int16_t)((left + right) / 2);
        }

        audio_buf_push(
            sample_block,
            frames,
            AUDIO_SRC_SD
        );
    }
}

void sd_wav_fallback_close(void) {
    close_current_file_only();

    if (mounted) {
        f_mount(NULL, "", 0);
        mounted = false;
    }

    enabled = false;
}