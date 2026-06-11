#include "audio_buffer.h"

#include <string.h>
#include "hardware/sync.h"

// Single-producer / single-consumer audio ring buffer.
// Producer: Bluetooth decoder or SD WAV task on core 0.
// Consumer: PWM IRQ on core 0.
// Visualizer: core 1 snapshots the latest samples without blocking audio.
//
// This version avoids holding a long critical section while pushing a block.
// That helps both Bluetooth and SD because the PWM IRQ can keep a steady sample
// cadence.  It also avoids adding any EQ/filtering, so it should not dull the
// Bluetooth path.

static int16_t audio_buffer[AUDIO_BUFFER_SIZE];

static volatile uint32_t write_index = 0;
static volatile uint32_t read_index  = 0;
static volatile AudioSource current_source = AUDIO_SRC_NONE;
static volatile bool has_data = false;

static inline uint32_t mask_index(uint32_t i) {
    return i & (AUDIO_BUFFER_SIZE - 1u);
}

static inline uint32_t next_index(uint32_t i) {
    return mask_index(i + 1u);
}

static inline uint32_t used_samples_unlocked(void) {
    return mask_index(write_index - read_index);
}

void audio_buf_init(void) {
    audio_buf_stop();
}

void audio_buf_push(const int16_t *samples, uint32_t num_samples,
                    AudioSource source) {
    if (!samples || num_samples == 0) {
        return;
    }

    for (uint32_t i = 0; i < num_samples; i++) {
        uint32_t w = write_index;
        uint32_t n = next_index(w);

        // Keep one slot open so read_index == write_index means empty.
        // If full, drop the oldest sample.  With the paced SD reader this should
        // be rare, but it prevents the song from replaying stale data.
        if (n == read_index) {
            read_index = next_index(read_index);
        }

        audio_buffer[w] = samples[i];

        // Publish after the sample write.
        write_index = n;
        current_source = source;
        has_data = true;
    }
}

int16_t audio_buf_read_one(void) {
    uint32_t r = read_index;

    if (r == write_index) {
        return 0;
    }

    int16_t sample = audio_buffer[r];
    read_index = next_index(r);
    return sample;
}

AudioSource audio_buf_snapshot(int16_t *dst) {
    if (!dst) {
        return AUDIO_SRC_NONE;
    }

    AudioSource src = current_source;
    uint32_t wi = write_index;

    uint32_t start = mask_index(wi - SCOPE_SAMPLES);

    for (uint32_t i = 0; i < SCOPE_SAMPLES; i++) {
        dst[i] = audio_buffer[mask_index(start + i)];
    }

    return src;
}

uint32_t audio_buf_copy_latest(int16_t *out, uint32_t max_samples) {
    if (!out || max_samples == 0) {
        return 0;
    }

    uint32_t available = used_samples_unlocked();
    uint32_t count = available < max_samples ? available : max_samples;
    uint32_t start = mask_index(write_index - count);

    for (uint32_t i = 0; i < count; i++) {
        out[i] = audio_buffer[mask_index(start + i)];
    }

    return count;
}

void audio_buf_stop(void) {
    // Reset needs a small critical section so the PWM IRQ cannot read halfway
    // through the index reset.
    uint32_t irq_state = save_and_disable_interrupts();

    memset(audio_buffer, 0, sizeof(audio_buffer));
    write_index = 0;
    read_index = 0;
    current_source = AUDIO_SRC_NONE;
    has_data = false;

    restore_interrupts(irq_state);
}

AudioSource audio_buf_get_source(void) {
    return current_source;
}

bool audio_buf_has_data(void) {
    return has_data;
}

uint32_t audio_buf_samples_available(void) {
    return used_samples_unlocked();
}

uint32_t audio_buf_free_space(void) {
    uint32_t used = used_samples_unlocked();

    // One slot is intentionally unused.
    if (used >= AUDIO_BUFFER_SIZE - 1u) {
        return 0;
    }

    return (AUDIO_BUFFER_SIZE - 1u) - used;
}
