#include "audio_buffer.h"

#include <string.h>
#include "pico/stdlib.h"
#include "pico/critical_section.h"

static int16_t audio_buffer[AUDIO_BUFFER_SIZE];

static volatile uint32_t write_index = 0;
static volatile uint32_t read_index = 0;
static volatile uint32_t samples_available = 0;

static volatile AudioSource current_source = AUDIO_SRC_NONE;
static volatile bool has_data = false;

static critical_section_t audio_cs;

void audio_buf_init(void) {
    critical_section_init(&audio_cs);

    critical_section_enter_blocking(&audio_cs);

    memset(audio_buffer, 0, sizeof(audio_buffer));
    write_index = 0;
    read_index = 0;
    samples_available = 0;
    current_source = AUDIO_SRC_NONE;
    has_data = false;

    critical_section_exit(&audio_cs);
}

void audio_buf_push(const int16_t *samples, uint32_t num_samples, AudioSource source) {
    if (!samples || num_samples == 0) {
        return;
    }

    critical_section_enter_blocking(&audio_cs);

    for (uint32_t i = 0; i < num_samples; i++) {
        audio_buffer[write_index] = samples[i];
        write_index = (write_index + 1) % AUDIO_BUFFER_SIZE;

        if (samples_available < AUDIO_BUFFER_SIZE) {
            samples_available++;
        } else {
            // Buffer is full, so move read cursor forward to avoid stale data.
            read_index = (read_index + 1) % AUDIO_BUFFER_SIZE;
        }
    }

    current_source = source;
    has_data = true;

    critical_section_exit(&audio_cs);
}

int16_t audio_buf_read_one(void) {
    critical_section_enter_blocking(&audio_cs);

    if (samples_available == 0) {
        critical_section_exit(&audio_cs);
        return 0;
    }

    int16_t sample = audio_buffer[read_index];

    read_index = (read_index + 1) % AUDIO_BUFFER_SIZE;
    samples_available--;

    critical_section_exit(&audio_cs);

    return sample;
}

AudioSource audio_buf_snapshot(int16_t *dst) {
    if (!dst) {
        return AUDIO_SRC_NONE;
    }

    critical_section_enter_blocking(&audio_cs);

    AudioSource src = current_source;

    uint32_t start = 0;

    if (write_index >= SCOPE_SAMPLES) {
        start = write_index - SCOPE_SAMPLES;
    } else {
        start = AUDIO_BUFFER_SIZE + write_index - SCOPE_SAMPLES;
    }

    for (int i = 0; i < SCOPE_SAMPLES; i++) {
        dst[i] = audio_buffer[(start + i) % AUDIO_BUFFER_SIZE];
    }

    critical_section_exit(&audio_cs);

    return src;
}

uint32_t audio_buf_copy_latest(int16_t *out, uint32_t max_samples) {
    if (!out || max_samples == 0) {
        return 0;
    }

    critical_section_enter_blocking(&audio_cs);

    uint32_t count = samples_available < max_samples ? samples_available : max_samples;

    uint32_t start = 0;

    if (write_index >= count) {
        start = write_index - count;
    } else {
        start = AUDIO_BUFFER_SIZE + write_index - count;
    }

    for (uint32_t i = 0; i < count; i++) {
        out[i] = audio_buffer[(start + i) % AUDIO_BUFFER_SIZE];
    }

    critical_section_exit(&audio_cs);

    return count;
}

void audio_buf_stop(void) {
    critical_section_enter_blocking(&audio_cs);

    memset(audio_buffer, 0, sizeof(audio_buffer));
    write_index = 0;
    read_index = 0;
    samples_available = 0;
    current_source = AUDIO_SRC_NONE;
    has_data = false;

    critical_section_exit(&audio_cs);
}

AudioSource audio_buf_get_source(void) {
    return current_source;
}

bool audio_buf_has_data(void) {
    return has_data;
}

uint32_t audio_buf_samples_available(void) {
    critical_section_enter_blocking(&audio_cs);

    uint32_t count = samples_available;

    critical_section_exit(&audio_cs);

    return count;
}

uint32_t audio_buf_free_space(void) {
    critical_section_enter_blocking(&audio_cs);

    uint32_t free_count = AUDIO_BUFFER_SIZE - samples_available;

    critical_section_exit(&audio_cs);

    return free_count;
}