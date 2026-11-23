/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dsound_buffer_adapter.h"
#include "dsound_api_adapter.h"
#include "dsound_buffer_model.h"
#include "dsound_mix_model.h"
#include "dsound_effects_adapter.h"
#include "dsound_hrtf.h"
#include "audio_output.h"
#include "xbox_adpcm.h"
#include "xapi_time_adapter.h"
#include "kernel_abi.h"
#include "stop_report.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BUFFER_COUNT = 256, ROOT_SIZE = 0x24, SETTINGS_SIZE = 0xe0, PUBLIC_OFFSET = 0x1c };
/* DOA2U's full-HRTF tables (response bank, [61][31] map, 45-step center gains).
   SC2 selects the light HRTF and its 5455 tables are not located yet, so 3D
   buffers stop at creation and these are never read. */
enum { HRTF_BANK = 0u, HRTF_BANK_SIZE = 0x115c0u, HRTF_MAP = 0u, HRTF_MAP_SIZE = 0xec6u,
    CENTER_FRONT = 0u, CENTER = 0u, CENTER_STEPS = 45 };
typedef struct Buffer {
    uint32_t root, settings, references, flags;
    uint32_t data, size, owned, owned_size, format, channels, bits, align, rate, frequency;
    uint32_t headroom;
    int32_t volume;
    const uint8_t *pcm;
    bool bad_block_noted;
    RecompDsoundBufferModel model;
    RecompDsoundMixModel mix;
    /* CTRL3D: the 5849 3D block fields DOA2 sets and their voice terms. */
    bool is3d;
    float position[3], velocity[3], min_distance, max_distance, distance_factor, rolloff;
    int32_t distance_volume, center_front, center, pan_front, pan_rear; /* hundredths of dB */
    int32_t doppler;                               /* 4096 per octave */
    RecompHrtfVoice hrtf;
    /* SetEG's amplitude envelope (DSENVELOPEDESC dwEG 1): its mode and times in the APU's 512-sample steps at
       48 kHz, sustain 0-255; how far the voice is into it, in seconds of output, and its release when StopEx asks
       for the envelope's (from level release_from, release_at seconds in). */
    uint32_t eg_mode, eg_delay, eg_attack, eg_hold, eg_decay, eg_release, eg_sustain;
    double eg_time, eg_release_time;
    float eg_release_from;
    bool eg_releasing;
} Buffer;
static Buffer buffers[BUFFER_COUNT];
static void update_3d(Buffer *buffer);
bool sc2_dsound_reverb_loaded(void);

/* RECOMP_DSOUND_LOG=1: each buffer's data, play, stop and volume to stderr, with
   the data's loudest sample (a debugging aid: a silent voice shows peak 0). */
static bool logging(void)
{
    static int on = -1;
    if (on < 0) on = getenv("RECOMP_DSOUND_LOG") != NULL;
    return on != 0;
}

static int peak(const Buffer *buffer)
{
    int top = 0;
    if (!buffer->pcm) return -1;
    if (buffer->format == 0x69u) {
        int16_t decoded[XBOX_ADPCM_BLOCK_SAMPLES * 2u];
        for (uint32_t at = 0u; at + buffer->align <= buffer->size; at += buffer->align) {
            if (!xbox_adpcm_decode_block(buffer->pcm + at, buffer->align, buffer->channels, decoded, 128u)) return -2;
            for (uint32_t i = 0u; i < 64u * buffer->channels; ++i) top = abs(decoded[i]) > top ? abs(decoded[i]) : top;
        }
    } else if (buffer->bits == 16u) {
        for (uint32_t at = 0u; at + 2u <= buffer->size; at += 2u) {
            int16_t v; memcpy(&v, buffer->pcm + at, 2); top = abs(v) > top ? abs(v) : top;
        }
    } else {
        for (uint32_t at = 0u; at < buffer->size; ++at) top = abs(buffer->pcm[at] - 128) > top ? abs(buffer->pcm[at] - 128) : top;
    }
    return top;
}

static uint32_t canonical(uint32_t address)
{
    uint32_t window = address & ~(RECOMP_XBOX_RAM_SIZE - 1u);
    return window == 0u || window == RECOMP_XBOX_CACHED_ALIAS ||
        window == RECOMP_XBOX_PHYSICAL_ALIAS ? address & (RECOMP_XBOX_RAM_SIZE - 1u) : UINT32_MAX;
}

static uint8_t *span(uint32_t address, uint32_t size)
{
    uint32_t offset = canonical(address);
    return address && offset < RECOMP_XBOX_RAM_SIZE && size <= RECOMP_XBOX_RAM_SIZE - offset
        ? recomp_memory(address, size) : NULL;
}

static uint32_t read32(uint32_t address)
{
    uint32_t value;
    memcpy(&value, recomp_memory(address, sizeof value), sizeof value);
    return value;
}

static void write32(uint32_t address, uint32_t value)
{
    memcpy(recomp_memory(address, sizeof value), &value, sizeof value);
}

static uint64_t now_ms(void)
{
#ifdef RECOMP_DSOUND_TEST_CLOCK
    uint64_t recomp_test_dsound_now_ms(void);
    return recomp_test_dsound_now_ms();
#else
    return recomp_xapi_performance_counter() / 1000000u;
#endif
}

static Buffer *buffer_at(uint32_t handle)
{
    uint32_t address = canonical(handle);
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) {
        Buffer *buffer = &buffers[i];
        if (buffer->root && address == buffer->root + PUBLIC_OFFSET &&
            read32(buffer->root + 4u) == buffer->references &&
            read32(address) == buffer->settings) return buffer;
    }
    recomp_stop(2, "dsound:buffer-handle-invalid:%08x", handle);
    return NULL;
}

static uint32_t decoded_bytes(const Buffer *buffer, uint32_t bytes)
{
    return buffer->format == 0x69u
        ? bytes / buffer->align * XBOX_ADPCM_BLOCK_SAMPLES * buffer->channels * 2u : bytes;
}

static uint32_t source_cursor(const Buffer *buffer)
{
    uint32_t bytes = buffer->model.cursor_bytes;
    if (buffer->format == 0x69u) bytes = bytes / (64u * buffer->channels * 2u) * buffer->align;
    return bytes / buffer->align * buffer->align;
}

/* CVTTSS2SI: NaN and out-of-range values give the integer indefinite. */
static int32_t to_int(float value)
{
    return value > -2147483648.0f && value < 2147483648.0f ? (int32_t)value : INT32_MIN;
}

static int32_t clamp_db(int64_t value) { return value < -10000 ? -10000 : value > 0 ? 0 : (int32_t)value; }

/* 0x36344B: amplitude ratio to hundredths of dB. */
static int32_t amplitude_db(float ratio)
{
    return !(ratio > 0.0f) ? -10000 : ratio < 1.0f ? to_int(2000.0f * log10f(ratio)) : 0;
}

/* 0x369565 per-bin 3D terms. Cone and I3DL2 source terms stay zero at their
   defaults. */
static int32_t bin_3d(const Buffer *buffer, uint32_t bin)
{
    const int32_t base = clamp_db(buffer->distance_volume);
    if (bin == 2u) return clamp_db((int64_t)buffer->center + buffer->pan_front + base);
    if (bin == 6u || bin == 7u) return clamp_db((int64_t)buffer->center_front + buffer->pan_front + base);
    return bin == 8u || bin == 9u ? clamp_db((int64_t)buffer->pan_rear + base) : base;
}

/* 0x366A14 front/back split. 0x364B53 enables it only for surround or AC3
   speakers; stereo keeps the whole signal in front. */
static void pan_3d(Buffer *buffer, float azimuth, float elevation, float distance, bool surround)
{
    buffer->pan_front = 0;
    buffer->pan_rear = -10000;
    if (!surround) return;
    float p = ((fabsf(azimuth) * 0.011111111f - 1.0f) * (1.0f - fabsf(elevation) * 0.011111111f) + 1.0f) * 0.5f;
    p = p + p - 0.5f;
    p = !(p <= 1.0f) ? 1.0f : p < 0.0f ? 0.0f : p;
    /* Within half a meter the split eases toward an even front/back share. */
    const float meters = buffer->distance_factor * distance;
    if (meters < 0.5f) p = ((p - 0.5f) * meters + 1.0f) * 0.5f;
    buffer->pan_front = amplitude_db(1.0f - p);
    buffer->pan_rear = amplitude_db(p);
}

/* 0x3697B9: base pitch plus the 3D Doppler term, clamped to [-32767, 8191]
   at 4096 per octave from 48 kHz. */
static uint32_t pitch_hz(const Buffer *buffer)
{
    if (!buffer->doppler) return buffer->frequency;
    long pitch = lrint(4096.0 * log2(buffer->frequency / 48000.0)) + buffer->doppler;
    pitch = pitch < -32767 ? -32767 : pitch > 8191 ? 8191 : pitch;
    return (uint32_t)lround(48000.0 * exp2(pitch / 4096.0));
}

static void apply_routing(Buffer *buffer)
{
    uint32_t headroom[32], channels = buffer->is3d ? 3u : buffer->channels;
    float dry[18], wet[6];
    RecompDsoundMixModel mix = buffer->mix;
    for (uint32_t i = 0u; i < 32u; ++i) headroom[i] = recomp_dsound_effects_headroom(i);
    /* 3D voices reach their bins as the two HRTF ears and the unfiltered voice. */
    for (uint32_t i = 0u; buffer->is3d && i < mix.count; ++i) {
        mix.volumes[mix.bins[i]] = clamp_db((int64_t)mix.volumes[mix.bins[i]] + bin_3d(buffer, mix.bins[i]));
    }
    if (!recomp_dsound_mix_matrices(&mix, channels, recomp_audio_output_speakers(), headroom, dry, wet)) {
        recomp_stop(2, "dsound:signal-route-unbound");
    }
    /* SC2 sends every sound to bin 10 but downloads no DSP image: with no
       effect there the APU's send is silent, so the wet share is dropped. */
    if (!sc2_dsound_reverb_loaded()) memset(wet, 0, sizeof wet);
    if (!recomp_audio_output_set_voice_routing((uint32_t)(buffer - buffers), channels, dry, wet)) {
        recomp_stop(2, "dsound:routing-failed");
    }
}

void recomp_dsound_buffers_refresh_routing(void)
{
    for (uint32_t i = 0u; i < BUFFER_COUNT; ++i) if (buffers[i].root) {
        update_3d(&buffers[i]);
        apply_routing(&buffers[i]);
    }
}

/* recomp_dsound_mix_set as 5455 has it: a bin may be named twice. SC2 gives
   every sound bins 0, 1, 10, 10 (the reverb send twice, at one volume); each
   slot routes on its own, so the voice reaches the send twice, as the APU's
   would. 5849's rule refused it, and with it every effect and voice. */
static bool mix_set_5455(RecompDsoundMixModel *model, const RecompDsoundMixPair *pairs,
    uint32_t count, bool replace)
{
    if (count > 8u || (count && !pairs)) return false;
    RecompDsoundMixModel next = *model;
    if (replace) next.count = count;
    for (uint32_t i = 0; i < count; ++i) {
        if (pairs[i].bin >= 32u || pairs[i].volume < -10000 || pairs[i].volume > 0) return false;
        if (replace) next.bins[i] = pairs[i].bin;
        next.volumes[pairs[i].bin] = pairs[i].volume;
    }
    *model = next;
    return true;
}

static uint32_t read_mix(RecompDsoundMixModel *mix, uint32_t address, bool replace, bool is3d)
{
    if (!address) {
        if (!replace) return RECOMP_DSOUND_POINTER_ERROR;
        /* DirectSoundDefaultMixBins_3D (0x380424) or the stereo default. */
        if (is3d) *mix = (RecompDsoundMixModel){.count = 5u, .bins = {6u, 8u, 7u, 9u, 10u}};
        else recomp_dsound_mix_default(mix);
        return RECOMP_DSOUND_OK;
    }
    uint8_t *header = span(address, 8u);
    uint32_t words[2];
    if (!header) return RECOMP_DSOUND_POINTER_ERROR;
    memcpy(words, header, sizeof words);
    if (words[0] > 8u) return RECOMP_DSOUND_INVALID_PARAM;
    RecompDsoundMixPair pairs[8];
    uint8_t *data = words[0] ? span(words[1], words[0] * sizeof *pairs) : NULL;
    if (words[0] && !data) return RECOMP_DSOUND_POINTER_ERROR;
    if (words[0]) memcpy(pairs, data, words[0] * sizeof *pairs);
    RecompDsoundMixModel next = *mix;
    if (!mix_set_5455(&next, pairs, words[0], replace)) return RECOMP_DSOUND_INVALID_PARAM;
    uint32_t headroom[32] = {0};
    float dry[18], wet[6];
    if (!recomp_dsound_mix_matrices(&next, 2u, 2u, headroom, dry, wet)) {
        recomp_stop(2, "dsound:signal-route-unbound");
    }
    *mix = next;
    return RECOMP_DSOUND_OK;
}

static void write_mix(Buffer *buffer)
{
    write32(buffer->settings + 0x24u, buffer->mix.count);
    for (uint32_t i = 0u; i < 8u; ++i) {
        *recomp_memory_i8(buffer->settings + 0x28u + i) =
            (int8_t)(i < buffer->mix.count ? buffer->mix.bins[i] : 0u);
    }
    memcpy(recomp_memory(buffer->settings + 0x30u, sizeof buffer->mix.volumes),
        buffer->mix.volumes, sizeof buffer->mix.volumes);
}

static void reset_output(Buffer *buffer)
{
    recomp_audio_output_reset_voice((uint32_t)(buffer - buffers));
    const RecompHrtfFilter filter = buffer->hrtf.filter;
    buffer->hrtf = (RecompHrtfVoice){.filter = filter, .rendered = filter};
}

enum { EG_DISABLE, EG_DELAY, EG_ATTACK, EG_HOLD, EG_DECAY, EG_SUSTAIN };
static const double EG_STEP = 512.0 / 48000.0; /* the APU's envelope step, seconds */

/* The amplitude envelope's level t seconds after Play: delay (silent), attack (rising), hold (full), decay (falling
   to the sustain level), then sustain, each phase linear; a mode past DELAY starts the voice at that phase. */
static float eg_level(const Buffer *buffer, double t)
{
    const double phases[4] = {buffer->eg_delay * EG_STEP, buffer->eg_attack * EG_STEP, buffer->eg_hold * EG_STEP,
        buffer->eg_decay * EG_STEP};
    const float sustain = buffer->eg_sustain / 255.0f;
    if (buffer->eg_mode == EG_DISABLE) return 1.0f;
    for (uint32_t phase = buffer->eg_mode - 1u; phase < 4u; ++phase) {
        if (t < phases[phase]) {
            const float through = (float)(t / phases[phase]);
            return phase == 0u ? 0.0f : phase == 1u ? through : phase == 2u ? 1.0f : 1.0f - (1.0f - sustain) * through;
        }
        t -= phases[phase];
    }
    return sustain;
}

/* Scales a pumped piece by the envelope, sample by sample; true once a release has run out (the rest silent). */
static bool apply_envelope(Buffer *buffer, uint8_t *pcm, uint32_t bytes, uint32_t bits)
{
    if (buffer->eg_mode == EG_DISABLE) return false;
    const uint32_t frame_bytes = bits / 8u * buffer->channels;
    const double step = 1.0 / buffer->model.sample_rate;
    const double release = buffer->eg_release * EG_STEP;
    bool ended = false;
    for (uint32_t at = 0u; at + frame_bytes <= bytes; at += frame_bytes) {
        float level;
        if (buffer->eg_releasing) {
            const double into = buffer->eg_time - buffer->eg_release_time;
            level = into >= release ? 0.0f : buffer->eg_release_from * (float)(1.0 - into / release);
            ended = ended || into >= release;
        } else level = eg_level(buffer, buffer->eg_time);
        buffer->eg_time += step;
        if (level >= 1.0f) continue;
        for (uint32_t c = 0u; c < buffer->channels; ++c) {
            if (bits == 16u) {
                int16_t v; memcpy(&v, pcm + at + c * 2u, 2u);
                v = (int16_t)lrintf(v * level); memcpy(pcm + at + c * 2u, &v, 2u);
            } else pcm[at + c] = (uint8_t)(128 + lrintf((pcm[at + c] - 128) * level));
        }
    }
    return ended;
}

static void pump(Buffer *buffer, uint64_t now)
{
    if (!recomp_audio_output_ready()) recomp_stop(2, "dsound:audio-output-failed");
    if (!buffer->model.playing || !buffer->pcm) return;
    uint32_t offset = 0u;
    uint32_t bytes = recomp_dsound_buffer_consume(&buffer->model, now, &offset);
    if (!bytes) return;
    int16_t samples[40000];
    uint8_t *pcm = (uint8_t *)samples;
    if (bytes > sizeof samples) recomp_stop(2, "dsound:buffer-output-size");
    uint32_t end = buffer->model.play_flags & RECOMP_DSOUND_PLAY_LOOPING
        ? buffer->model.loop_end_bytes : buffer->model.size_bytes;
    for (uint32_t written = 0u; written < bytes;) {
        uint32_t count = end - offset;
        const uint8_t *source;
        int16_t decoded[XBOX_ADPCM_BLOCK_SAMPLES * 2u];
        if (buffer->format == 0x69u) {
            uint32_t block_bytes = 64u * buffer->channels * 2u;
            uint32_t within = offset % block_bytes;
            if (count > block_bytes - within) count = block_bytes - within;
            const uint8_t *block = buffer->pcm + offset / block_bytes * buffer->align;
            if (!xbox_adpcm_decode_block(block, buffer->align, buffer->channels, decoded, 128u)) {
                /* A block whose header isn't ADPCM (step index over 88, or the reserved byte set), met as a
                   fight loads (issues #20, #22). The Xbox's audio unit plays on through such a block; this
                   stopped the game. Now the block is silent, noted once per buffer's data. */
                if (!buffer->bad_block_noted) {
                    buffer->bad_block_noted = true;
                    fprintf(stderr, "recomp dsound: buffer %u data=%08x size=%u: block at %u isn't ADPCM "
                            "(%02x %02x %02x %02x); played silent\n", (unsigned)(buffer - buffers),
                            buffer->data, buffer->size, offset / block_bytes * buffer->align,
                            block[0], block[1], block[2], block[3]);
                }
                memset(decoded, 0, sizeof decoded);
            }
            source = (const uint8_t *)decoded + within;
        } else source = buffer->pcm + offset;
        if (count > bytes - written) count = bytes - written;
        memcpy(pcm + written, source, count);
        written += count;
        offset += count;
        if (offset == end) offset = buffer->model.loop_start_bytes;
    }
    if (logging()) {
        static int loudest[BUFFER_COUNT];
        static uint64_t since[BUFFER_COUNT];
        const uint32_t which = (uint32_t)(buffer - buffers);
        for (uint32_t i = 0u; i + 2u <= bytes; i += 2u) {
            int16_t v; memcpy(&v, pcm + i, 2); if (abs(v) > loudest[which]) loudest[which] = abs(v);
        }
        if (now - since[which] >= 2000u) {
            fprintf(stderr, "dsound %llu buf%u heard peak=%d\n", (unsigned long long)now, which, loudest[which]);
            loudest[which] = 0; since[which] = now;
        }
    }
    /* The release ran out: the voice ends (what is queued, faded to silence, plays out). */
    if (apply_envelope(buffer, pcm, bytes, buffer->format == 0x69u ? 16u : buffer->bits)) {
        recomp_dsound_buffer_stop(&buffer->model, now);
        buffer->eg_releasing = false;
    }
    if (!recomp_audio_output_ready()) recomp_stop(2, "dsound:audio-output-failed");
    apply_routing(buffer);
    const uint32_t slot = (uint32_t)(buffer - buffers);
    if (!buffer->is3d) {
        recomp_audio_output_submit(slot, pcm, bytes, buffer->model.sample_rate, buffer->channels,
            buffer->format == 0x69u ? 16u : buffer->bits, buffer->volume);
    }
    /* The APU filters 3D voices at 48 kHz; pieces keep each submission in bounds. */
    static float ears[3u * 20000u];
    uint32_t limit = (uint32_t)((uint64_t)19000u * buffer->model.sample_rate / RECOMP_HRTF_RATE);
    if (!limit) limit = 1u;
    for (uint32_t done = 0u, frames = buffer->is3d ? bytes / 2u : 0u; done < frames;) {
        uint32_t piece = frames - done < limit ? frames - done : limit;
        uint32_t rendered = recomp_hrtf_render(&buffer->hrtf, samples + done, piece,
            buffer->model.sample_rate, ears, 20000u);
        if (rendered) recomp_audio_output_submit(slot, (const uint8_t *)ears, rendered * 12u,
            RECOMP_HRTF_RATE, 3u, 32u, buffer->volume);
        done += piece;
    }
    if (!recomp_audio_output_ready()) recomp_stop(2, "dsound:audio-output-failed");
}

/* DSOUND 5849 CDirectSound3DCalculator (0x365553, 0x364236) with the full-HRTF
   routines DirectSoundUseFullHRTF installs (0x366D1A). DOA2 never sets listener
   orientation or factors, cones, mode, rolloff curves or I3DL2 sources; they
   keep their 0x3803B4 defaults (+z ahead, +y up, factors 1). */
static void update_3d(Buffer *buffer)
{
    static const uint8_t *bank, *map;
    static const float *front_table, *center_table;
    float listener[6], d[3], n[3] = {0.0f, 0.0f, 0.0f}, u = 0.0f, azimuth, elevation;
    if (!buffer->is3d) return;
    pump(buffer, now_ms());
    recomp_dsound_listener_vectors(listener);
    for (int i = 0; i < 3; ++i) d[i] = buffer->position[i] - listener[i];
    const float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int i = 0; distance > 0.0f && i < 3; ++i) n[i] = d[i] / distance;

    /* 0x366873 without a curve: -2000 log10(1 + rolloff (d / min - 1)), d held at max. */
    const float held = buffer->max_distance < distance ? buffer->max_distance : distance;
    const float x = (held / buffer->min_distance - 1.0f) * buffer->rolloff;
    buffer->distance_volume = distance <= buffer->min_distance || x < 0.0f
        ? 0 : to_int(-2000.0f * log10f(x + 1.0f));

    if (!bank) {
        bank = recomp_memory(HRTF_BANK, HRTF_BANK_SIZE);
        map = recomp_memory(HRTF_MAP, HRTF_MAP_SIZE);
        front_table = (const float *)(const void *)recomp_memory(CENTER_FRONT, CENTER_STEPS * sizeof(float));
        center_table = (const float *)(const void *)recomp_memory(CENTER, CENTER_STEPS * sizeof(float));
    }
    recomp_hrtf_angles(n[0], n[1], n[2], &azimuth, &elevation);
    const bool surround = recomp_audio_output_speakers() == 6u;
    buffer->hrtf.filter = recomp_hrtf_lookup(bank, map, azimuth, elevation, surround);
    pan_3d(buffer, azimuth, elevation, distance, surround);

    /* 0x366AE3, enabled by 0x364970 when mix slot 4 is the center bin. */
    buffer->center_front = buffer->center = 0;
    if (buffer->mix.count >= 5u && buffer->mix.bins[4] == 2u) {
        const int32_t a = to_int(fabsf(azimuth)), e = to_int(fabsf(elevation));
        buffer->center = -10000;
        if (a >= 0 && a < CENTER_STEPS && e >= 0 && e < CENTER_STEPS) {
            buffer->center = amplitude_db(center_table[a] * center_table[e]);
            buffer->center_front = amplitude_db(1.0f - front_table[a] * front_table[e]);
        }
    }

    /* 0x366828 and 0x366B85: radial speed in m/s against the speed of sound. */
    for (int i = 0; i < 3; ++i) u += (buffer->velocity[i] - listener[3 + i]) * n[i];
    const float v = buffer->distance_factor * u;
    buffer->doppler = v == 0.0f ? 0 : !(v < 342.0f) ? -32767 : !(v > -342.0f) ? 4096
        : (int32_t)lrintf(4096.0f * log2f(1.0f - v * 0.0029239766f));
    if (buffer->size && buffer->model.sample_rate != pitch_hz(buffer)) {
        recomp_dsound_buffer_set_frequency(&buffer->model, pitch_hz(buffer), now_ms());
    }
}

void recomp_dsound_buffers_pump(void)
{
    /* shortcut: API/DoWork pumping drops spans after >100 ms stalls; add an
       independent producer when continuous playback across stalls is required. */
    uint64_t now = now_ms();
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) if (buffers[i].root) pump(&buffers[i], now);
}

static void dispose(Buffer *buffer)
{
    recomp_audio_output_reset_voice((uint32_t)(buffer - buffers));
    if (buffer->owned) recomp_kernel_free_pool(buffer->owned);
    if (buffer->settings) recomp_kernel_free_pool(buffer->settings);
    if (buffer->root) recomp_kernel_free_pool(buffer->root);
    *buffer = (Buffer){0};
}

/* Save states (sc2_state.h): the buffers as they were, as the guest's record of them goes back. A slot that
   differs gets its voice reset (what it played is no longer this buffer's). */
size_t sc2_dsound_buffers_state_size(void) { return sizeof buffers; }
void sc2_dsound_buffers_state_save(void *out) { memcpy(out, buffers, sizeof buffers); }
void sc2_dsound_buffers_state_load(const void *in)
{
    const Buffer *saved = (const Buffer *)in;
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) {
        if (memcmp(&buffers[i], &saved[i], sizeof saved[i]) == 0) continue;
        if (buffers[i].root != saved[i].root || buffers[i].data != saved[i].data) recomp_audio_output_reset_voice(i);
        buffers[i] = saved[i];
    }
}

void recomp_dsound_buffers_reset(void)
{
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) if (buffers[i].root) {
        dispose(&buffers[i]);
        recomp_dsound_device_release_owner();
    }
}

int recomp_dsound_buffer_addref_root(uint32_t root, uint32_t *references)
{
    uint32_t address = canonical(root);
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) if (buffers[i].root && buffers[i].root == address) {
        Buffer *buffer = buffer_at(address + PUBLIC_OFFSET);
        if (buffer->references == UINT32_MAX) recomp_stop(2, "dsound:buffer-refcount-overflow");
        *references = ++buffer->references;
        write32(address + 4u, *references);
        return 1;
    }
    return 0;
}

static uint32_t set_data(Buffer *buffer, uint32_t data, uint32_t size)
{
    if (size % buffer->align || (buffer->format == 0x69u &&
        (uint64_t)(size / buffer->align) * 64u * buffer->channels * 2u > UINT32_MAX)) {
        return RECOMP_DSOUND_INVALID_PARAM;
    }
    if (data && size && !span(data, size)) return RECOMP_DSOUND_POINTER_ERROR;
    if (canonical(data) == canonical(buffer->data) && size == buffer->size) return RECOMP_DSOUND_OK;
    if (buffer->owned && data && size) {
        uint32_t start = canonical(data);
        if (start < buffer->owned + buffer->owned_size && buffer->owned < (uint64_t)start + size)
            return RECOMP_DSOUND_INVALID_PARAM;
    }
    uint32_t owned = 0u;
    if (!data && size) {
        if (size > RECOMP_XBOX_RAM_SIZE - 31u) return RECOMP_DSOUND_OUT_OF_MEMORY;
        owned = recomp_kernel_allocate_pool(size + 31u);
        if (!owned) return RECOMP_DSOUND_OUT_OF_MEMORY;
        data = (owned + 31u) & ~31u;
        uint8_t *storage = span(data, size);
        if (!storage) { recomp_kernel_free_pool(owned); return RECOMP_DSOUND_OUT_OF_MEMORY; }
        memset(storage, buffer->format == 1u && buffer->bits == 8u ? 0x80 : 0, size);
    }
    pump(buffer, now_ms());
    /* New data on a voice still fading out (StopEx's envelope release): the old sound is over. */
    if (buffer->eg_releasing) {
        recomp_dsound_buffer_stop(&buffer->model, now_ms());
        buffer->eg_releasing = false;
    }
    reset_output(buffer);
    if (buffer->owned) recomp_kernel_free_pool(buffer->owned);
    buffer->owned = owned;
    buffer->owned_size = owned ? size + 31u : 0u;
    buffer->data = size ? data : 0u;
    buffer->size = size;
    buffer->pcm = size ? span(data, size) : NULL;
    buffer->bad_block_noted = false;
    buffer->model = (RecompDsoundBufferModel){0};
    if (size) recomp_dsound_buffer_configure(&buffer->model, decoded_bytes(buffer, size),
        pitch_hz(buffer), buffer->channels * (buffer->format == 0x69u ? 2u : buffer->bits / 8u), now_ms());
    write32(buffer->settings + 8u, buffer->flags | (owned ? 0x80000000u : 0u));
    write32(buffer->settings + 0xbcu, buffer->data);
    write32(buffer->settings + 0xc0u, size);
    return RECOMP_DSOUND_OK;
}

static void create_buffer(void)
{
    recomp_dsound_require_device(kernel_arg(1u));
    uint32_t desc_address = kernel_arg(2u), output = kernel_arg(3u);
    uint32_t result = RECOMP_DSOUND_POINTER_ERROR, desc[6];
    uint8_t *desc_span = span(desc_address, sizeof desc);
    if (!span(output, 4u)) { kernel_return(4u, result); return; }
    write32(output, 0u);
    if (!desc_span) { kernel_return(4u, result); return; }
    memcpy(desc, desc_span, sizeof desc);
    if (desc[0] != sizeof desc || kernel_arg(4u)) { kernel_return(4u, RECOMP_DSOUND_INVALID_PARAM); return; }
    if (desc[5]) recomp_stop(2, "dsound:buffer-input-bin-unbound");
    /* DSBCAPS_CTRLPOSITIONNOTIFY (0x100) is a capability only: SC2 links no
       SetNotificationPositions, so no notification can be asked for. */
    if (desc[1] & ~0x401b0u) recomp_stop(2, "dsound:buffer-create-flags-unbound:%08x", desc[1]);
    uint8_t *format = span(desc[3], 18u);
    if (!format) { kernel_return(4u, result); return; }
    uint16_t tag, channels, bits, align, extra;
    uint32_t rate, average;
    memcpy(&tag, format, 2); memcpy(&channels, format + 2, 2);
    memcpy(&rate, format + 4, 4); memcpy(&average, format + 8, 4);
    memcpy(&align, format + 12, 2); memcpy(&bits, format + 14, 2); memcpy(&extra, format + 16, 2);
    uint16_t samples = 0u;
    if (tag == 0x69u && extra == 2u && span(desc[3], 20u)) memcpy(&samples, format + 18, 2);
    if (logging()) fprintf(stderr, "dsound create flags=%x size=%u tag=%x ch=%u rate=%u avg=%u align=%u bits=%u extra=%u samples=%u mix=%08x\n",
        desc[1], desc[2], tag, channels, rate, average, align, bits, extra, samples, desc[4]);
    result = RECOMP_DSOUND_INVALID_PARAM;
    if (channels < 1 || channels > 2 || rate < 1000 || rate > 200000 ||
        (tag != 1u && tag != 0x69u) ||
        (tag == 1u && ((bits != 8 && bits != 16) || align != channels * bits / 8 ||
            average != rate * align || extra != 0u)) ||
        (tag == 0x69u && (bits != 4 || align != 36u * channels || extra != 2u ||
            !span(desc[3], 20u) || samples != 64u))) {
        kernel_return(4u, result); return;
    }
    const bool is3d = (desc[1] & 0x10u) != 0u;
    if (is3d) recomp_stop(2, "dsound:buffer-3d-unbound");
    uint32_t slot = 0u;
    RecompDsoundMixModel mix;
    recomp_dsound_mix_default(&mix);
    result = read_mix(&mix, desc[4], true, is3d);
    if (result != RECOMP_DSOUND_OK) {
        if (logging()) {
            const uint8_t *m = span(desc[4], 8u);
            uint32_t w[2] = {0, 0};
            if (m) memcpy(w, m, 8);
            const uint8_t *pairs = w[0] && w[0] < 64u ? span(w[1], w[0] * 8u) : NULL;
            fprintf(stderr, "dsound create mix refused result=%x count=%u pairs=%08x:", result, w[0], w[1]);
            for (uint32_t i = 0; pairs && i < w[0] * 2u; ++i) { uint32_t v; memcpy(&v, pairs + 4 * i, 4); fprintf(stderr, " %d", (int32_t)v); }
            fprintf(stderr, "\n");
        }
        kernel_return(4u, result); return;
    }
    while (slot < BUFFER_COUNT && buffers[slot].root) ++slot;
    if (slot == BUFFER_COUNT) {
        if (logging()) fprintf(stderr, "dsound create: all %u buffers in use\n", BUFFER_COUNT);
        kernel_return(4u, RECOMP_DSOUND_OUT_OF_MEMORY); return;
    }
    Buffer *buffer = &buffers[slot];
    *buffer = (Buffer){.root = recomp_kernel_allocate_pool(ROOT_SIZE),
        .settings = recomp_kernel_allocate_pool(SETTINGS_SIZE), .references = 1u,
        .flags = desc[1], .format = tag, .channels = channels, .bits = bits, .align = align,
        .rate = rate, .frequency = rate, .headroom = (desc[1] & 0x382010u) ? 0u : 600u, .is3d = is3d,
        /* 3D defaults from the 5849 template at 0x3803B4. */
        .min_distance = 1.0f, .max_distance = 1000000000.0f, .distance_factor = 1.0f, .rolloff = 1.0f,
        .pan_rear = -10000};
    if (!buffer->root || !buffer->settings) { dispose(buffer); kernel_return(4u, RECOMP_DSOUND_OUT_OF_MEMORY); return; }
    buffer->volume = -(int32_t)buffer->headroom;
    buffer->mix = mix;
    memset(recomp_memory(buffer->root, ROOT_SIZE), 0, ROOT_SIZE);
    memset(recomp_memory(buffer->settings, SETTINGS_SIZE), 0, SETTINGS_SIZE);
    write32(buffer->root, 0x001899fcu);
    write32(buffer->root + 4u, 1u);
    write32(buffer->root + 8u, recomp_dsound_current_device() - 8u);
    write32(buffer->root + 0x10u, read32(recomp_dsound_current_device()));
    write32(buffer->root + 0x14u, buffer->root + 0x14u);
    write32(buffer->root + 0x18u, buffer->root + 0x14u);
    write32(buffer->root + PUBLIC_OFFSET, buffer->settings);
    write32(buffer->settings + 8u, buffer->flags);
    write32(buffer->settings + 0x1cu, (uint32_t)buffer->volume);
    write32(buffer->settings + 0x20u, buffer->headroom);
    write_mix(buffer);
    update_3d(buffer);
    buffer->hrtf.rendered = buffer->hrtf.filter;
    result = set_data(buffer, 0u, desc[2]);
    if (result != RECOMP_DSOUND_OK) dispose(buffer);
    else { recomp_dsound_device_add_owner(); write32(output, buffer->root + PUBLIC_OFFSET); }
    if (logging()) fprintf(stderr, "dsound created buf%u result=%x\n", slot, result);
    kernel_return(4u, result);
}

static uint32_t release_buffer(Buffer *buffer)
{
    uint32_t references = --buffer->references;
    write32(buffer->root + 4u, references);
    if (!references) { dispose(buffer); recomp_dsound_device_release_owner(); }
    return references;
}

enum Operation { RELEASE, DATA, LOCK, UNLOCK, PLAY, STOP, STOP_EX, STATUS, POSITION, SEEK, LOOP, FREQUENCY, VOLUME, HEADROOM, MIX_BINS, MIX_VOLUMES,
    POSITION_3D, VELOCITY_3D, MIN_DISTANCE, MAX_DISTANCE, DISTANCE_FACTOR, ROLLOFF };
static void buffer_call(enum Operation operation, uint32_t arguments)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    if (operation != RELEASE && !recomp_audio_output_ready()) recomp_stop(2, "dsound:audio-output-failed");
    uint32_t value = arguments >= 2u ? kernel_arg(2u) : 0u;
    uint32_t result = RECOMP_DSOUND_OK;
    uint64_t now = now_ms();
    if (logging() && operation != STATUS && operation != POSITION && operation != LOCK && operation != UNLOCK) {
        fprintf(stderr, "dsound %llu buf%u op%d a=%08x b=%08x fmt=%x ch=%u rate=%u freq=%u size=%u vol=%d flags=%x%s",
            (unsigned long long)now, (unsigned)(buffer - buffers), (int)operation, value,
            arguments >= 3u ? kernel_arg(3u) : 0u, buffer->format, buffer->channels, buffer->rate, buffer->frequency,
            buffer->size, buffer->volume, buffer->flags, operation == PLAY ? "" : "\n");
        if (operation == PLAY) fprintf(stderr, " playflags=%x peak=%d\n", kernel_arg(4u), peak(buffer));
    }
    switch (operation) {
    case MIX_BINS: case MIX_VOLUMES:
        result = read_mix(&buffer->mix, value, operation == MIX_BINS, buffer->is3d);
        if (!result) { write_mix(buffer); update_3d(buffer); apply_routing(buffer); }
        break;
    case POSITION_3D: case VELOCITY_3D: case MIN_DISTANCE: case MAX_DISTANCE: case DISTANCE_FACTOR: case ROLLOFF: {
        /* The 5849 setters store without validation (0x364C2B-0x364D68).
           shortcut: deferred (apply=1) values take effect at once instead of at
           commit; track deferral if a game sets and commits in different frames. */
        if (!buffer->is3d) recomp_stop(2, "dsound:buffer-not-3d");
        float *target = operation == POSITION_3D ? buffer->position : operation == VELOCITY_3D ? buffer->velocity
            : operation == MIN_DISTANCE ? &buffer->min_distance : operation == MAX_DISTANCE ? &buffer->max_distance
            : operation == DISTANCE_FACTOR ? &buffer->distance_factor : &buffer->rolloff;
        for (uint32_t i = 0u; i + 2u < arguments; ++i) {
            uint32_t bits = kernel_arg(2u + i);
            memcpy(target + i, &bits, sizeof bits);
        }
        update_3d(buffer);
        break;
    }
    case RELEASE: result = release_buffer(buffer); break;
    case DATA: result = set_data(buffer, value, kernel_arg(3u)); break;
    case LOCK: {
        uint32_t bytes = kernel_arg(3u), flags = kernel_arg(8u), size = buffer->size;
        uint32_t p1 = kernel_arg(4u), n1 = kernel_arg(5u), p2 = kernel_arg(6u), n2 = kernel_arg(7u);
        if (!span(p1, 4u) || !span(n1, 4u) || (p2 && !span(p2, 4u)) ||
            (n2 && !span(n2, 4u)) || (!p2 != !n2)) { result = RECOMP_DSOUND_POINTER_ERROR; break; }
        pump(buffer, now);
        if (flags & 1u) value = source_cursor(buffer);
        if (flags & 2u) bytes = size;
        if (!size || value >= size || !bytes || bytes > size || (flags & ~3u) ||
            value % buffer->align || bytes % buffer->align) { result = RECOMP_DSOUND_INVALID_PARAM; break; }
        uint32_t first = bytes < size - value ? bytes : size - value;
        if (bytes > first && !p2) { result = RECOMP_DSOUND_INVALID_PARAM; break; }
        recomp_dsound_buffer_lock(&buffer->model, decoded_bytes(buffer, bytes));
        write32(p1, buffer->data + value); write32(n1, first);
        if (p2) { write32(p2, bytes > first ? buffer->data : 0u); write32(n2, bytes - first); }
        break;
    }
    case UNLOCK: {
        uint32_t size = kernel_arg(3u), second = kernel_arg(4u), second_size = kernel_arg(5u);
        uint32_t offset = canonical(value), data = canonical(buffer->data);
        if (!buffer->size || offset < data || offset >= data + buffer->size ||
            size > data + buffer->size - offset || second_size > buffer->size - size ||
            (second_size && (offset + size != data + buffer->size || canonical(second) != data))) result = RECOMP_DSOUND_INVALID_PARAM;
        break;
    }
    case PLAY: {
        uint32_t flags = kernel_arg(4u);
        if (flags & ~3u) recomp_stop(2, "dsound:buffer-play-flags-unbound:%08x", flags);
        pump(buffer, now);
        /* Play on a voice fading out after StopEx: a new start, not the stopping sound going on. */
        if (buffer->eg_releasing) {
            recomp_dsound_buffer_stop(&buffer->model, now);
            buffer->eg_releasing = false;
        }
        uint32_t continuing = buffer->model.playing && !(flags & RECOMP_DSOUND_PLAY_FROMSTART);
        result = recomp_dsound_buffer_play(&buffer->model, flags, now);
        if (!result && !continuing) {
            reset_output(buffer);
            buffer->eg_time = 0.0;
            buffer->eg_releasing = false;
        }
        break;
    }
    case STOP_EX: {
        /* StopEx(time, flags). SC2 passes time 0 (now) with flags 0, 1 or 2.
           2 (release waveform) lets a looping sound play out to its end.
           1 (envelope) moves the amplitude envelope to its release: the voice plays on, fading out over the
           release time from where it is (most of SC2's sounds: 60 steps, 0.64 s; Inferno's #97), and ends with
           it; with no envelope, or none to release, it stops at once. */
        const uint32_t flags = kernel_arg(4u);
        if (value || kernel_arg(3u) || (flags & ~3u)) recomp_stop(2, "dsound:buffer-stop-ex-unbound");
        if ((flags & 1u) && buffer->eg_mode != EG_DISABLE && buffer->eg_release && buffer->model.playing) {
            pump(buffer, now);
            if (!buffer->eg_releasing) {
                buffer->eg_release_from = eg_level(buffer, buffer->eg_time);
                buffer->eg_release_time = buffer->eg_time;
                buffer->eg_releasing = true;
            }
            if (flags & 2u) buffer->model.play_flags &= ~RECOMP_DSOUND_PLAY_LOOPING;
            break;
        }
        if (flags == 2u) {
            pump(buffer, now);
            buffer->model.play_flags &= ~RECOMP_DSOUND_PLAY_LOOPING;
            break;
        }
    }
        /* fall through: immediate Stop semantics */
    case STOP:
        pump(buffer, now); recomp_dsound_buffer_stop(&buffer->model, now);
        buffer->eg_releasing = false;
        reset_output(buffer); break;
    case STATUS:
        if (!span(value, 4u)) { result = RECOMP_DSOUND_POINTER_ERROR; break; }
        pump(buffer, now);
        write32(value, buffer->model.playing ? 1u | ((buffer->model.play_flags & 1u) ? 4u : 0u) : 0u); break;
    case POSITION: {
        uint32_t write = kernel_arg(3u);
        if ((!value && !write) || (value && !span(value, 4u)) || (write && !span(write, 4u))) {
            result = RECOMP_DSOUND_POINTER_ERROR; break;
        }
        pump(buffer, now);
        if (value) write32(value, source_cursor(buffer));
        if (write) write32(write, source_cursor(buffer));
        break;
    }
    case SEEK:
        if (value >= buffer->size || value % buffer->align) { result = RECOMP_DSOUND_INVALID_PARAM; break; }
        pump(buffer, now);
        result = recomp_dsound_buffer_set_position(&buffer->model, decoded_bytes(buffer, value), now);
        if (!result) reset_output(buffer); break;
    case LOOP: {
        uint32_t length = kernel_arg(3u);
        /* 5455 (0x16164C): a length of 0 runs to the buffer's end; SC2 sets
           every sound's region so, and gives a refused sound up unplayed. */
        if (!length && value < buffer->size) length = buffer->size - value;
        if (value >= buffer->size || !length || length > buffer->size - value ||
            value % buffer->align || length % buffer->align) { result = RECOMP_DSOUND_INVALID_PARAM; break; }
        pump(buffer, now);
        buffer->model.loop_start_bytes = decoded_bytes(buffer, value);
        buffer->model.loop_end_bytes = decoded_bytes(buffer, value + length);
        buffer->model.stream_locks = 0u;
        buffer->model.stream_remaining_bytes = 0u;
        reset_output(buffer); break;
    }
    case FREQUENCY:
        if (!value) value = buffer->rate;
        if (value < 1000u || value > 200000u) { result = RECOMP_DSOUND_INVALID_PARAM; break; }
        pump(buffer, now); buffer->frequency = value;
        if (buffer->size) result = recomp_dsound_buffer_set_frequency(&buffer->model, pitch_hz(buffer), now); break;
    case VOLUME: case HEADROOM: {
        int32_t input = (int32_t)value;
        int64_t volume = operation == HEADROOM
            ? (int64_t)buffer->volume + buffer->headroom - value : (int64_t)input - buffer->headroom;
        /* Native arithmetic rejects overflow; retail has no headroom bound. */
        if ((operation == VOLUME && (input < -10000 || input > 0)) || volume < INT32_MIN || volume > 0) {
            result = RECOMP_DSOUND_INVALID_PARAM; break;
        }
        pump(buffer, now); buffer->volume = (int32_t)volume;
        if (operation == HEADROOM) { buffer->headroom = value; write32(buffer->settings + 0x20u, value); }
        write32(buffer->settings + 0x1cu, (uint32_t)buffer->volume); break;
    }
    }
    kernel_return(arguments, result);
}

#define METHOD(name, op, count) static void name(void) { buffer_call(op, count); }
METHOD(release, RELEASE, 1u) METHOD(data, DATA, 3u) METHOD(lock_buffer, LOCK, 8u)
METHOD(unlock, UNLOCK, 5u) METHOD(play, PLAY, 4u) METHOD(stop, STOP, 1u)
METHOD(stop_ex, STOP_EX, 4u) METHOD(status, STATUS, 2u) METHOD(position, POSITION, 3u)
METHOD(seek, SEEK, 2u) METHOD(loop, LOOP, 3u) METHOD(frequency, FREQUENCY, 2u)
METHOD(volume, VOLUME, 2u) METHOD(headroom, HEADROOM, 2u)
METHOD(mix_bins, MIX_BINS, 2u) METHOD(mix_volumes, MIX_VOLUMES, 2u)
METHOD(position_3d, POSITION_3D, 5u) METHOD(velocity_3d, VELOCITY_3D, 5u)
METHOD(min_distance, MIN_DISTANCE, 3u) METHOD(max_distance, MAX_DISTANCE, 3u)
METHOD(distance_factor, DISTANCE_FACTOR, 3u) METHOD(rolloff, ROLLOFF, 3u)
static void private_release(void)
{
    kernel_return(1u, release_buffer(buffer_at(kernel_arg(1u) + PUBLIC_OFFSET)));
}
static void unbound_stream(void) { recomp_stop(2, "dsound:stream-unbound"); }

/* SetLFO and SetEG(buffer, descriptor), ret 8, which SC2 uses and DOA2U did not.
   shortcut: the voice model has no LFO and no pitch/filter envelope (dwEG 0, SC2's all off), so their
   descriptors are accepted and not heard; each is named once. The amplitude envelope is set_envelope's. */
static void modulation(const char *what)
{
    static unsigned named;
    const unsigned bit = what[0] == 'L' ? 1u : 2u;
    if (!(named & bit)) {
        named |= bit;
        fprintf(stderr, "recomp dsound: %s accepted without modulation\n", what);
    }
    kernel_return(2u, RECOMP_DSOUND_OK);
}

static void set_lfo(void) { modulation("LFO"); }
/* DSENVELOPEDESC {dwEG, dwMode, dwDelay, dwAttack, dwHold, dwDecay, dwRelease, dwSustain, lPitchScale,
   lFilterCutOff}: the library's SetEG (0x165600) takes dwEG 1 as the amplitude envelope (0 writes the pitch and
   filter terms too). SC2 gives most sounds mode 1 (from the delay), full sustain and a 60-step release. */
static void set_envelope(void)
{
    const uint32_t descriptor = kernel_arg(2u);
    if (!span(descriptor, 32u)) { kernel_return(2u, RECOMP_DSOUND_POINTER_ERROR); return; }
    const uint32_t *d = recomp_memory_u32(descriptor);
    if (d[0] != 1u) { modulation("EG"); return; }
    Buffer *buffer = buffer_at(kernel_arg(1u));
    pump(buffer, now_ms());
    if (d[1] > EG_SUSTAIN) recomp_stop(2, "dsound:eg-mode-unbound:%u", d[1]);
    buffer->eg_mode = d[1]; buffer->eg_delay = d[2] & 0xfffu; buffer->eg_attack = d[3] & 0xfffu;
    buffer->eg_hold = d[4] & 0xfffu; buffer->eg_decay = d[5] & 0xfffu; buffer->eg_release = d[6] & 0xfffu;
    buffer->eg_sustain = d[7] & 0xffu;
    kernel_return(2u, RECOMP_DSOUND_OK);
}

RecompFunction recomp_dsound_buffer_lookup_manual(uint32_t address)
{
    /* SC2's 5455 buffer entry points, matched to DOA2U's verified 5849 ones by
       call tree. SC2 links no 3D buffer, mix-bin volume or stream API. */
    switch (address) {
    case 0x162da7u: return create_buffer;
    case 0x160f91u: return release; case 0x1614b8u: return private_release;
    case 0x162815u: return data; case 0x161ea5u: return lock_buffer; case 0x160f8cu: return unlock;
    case 0x161dcdu: return play; case 0x161df1u: return stop; case 0x161e09u: return stop_ex;
    case 0x161e4du: return status; case 0x161e69u: return position; case 0x161e89u: return seek;
    case 0x161e2du: return loop; case 0x1622c7u: return frequency;
    case 0x161d5du: return volume; case 0x161db1u: return headroom;
    case 0x1622e3u: return mix_bins;
    case 0x161d79u: return set_lfo; case 0x161d95u: return set_envelope;
    default: return NULL;
    }
}
