/* SPDX-License-Identifier: 0BSD */
/* Copyright (C) 2026 by I.C.KaZe */
#ifndef INDEPENDENT_OPM_C_API_H
#define INDEPENDENT_OPM_C_API_H
#include <stdint.h>
#if defined(_WIN32)
# define IOPM_CALL __cdecl
# if defined(IOPM_BUILD_DLL)
#  define IOPM_API /* exports are fixed by dll/independent_opm.def */
# else
#  define IOPM_API __declspec(dllimport)
# endif
#else
# define IOPM_CALL
# define IOPM_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define IOPM_ABI_VERSION 1u
#define IOPM_OK 0
#define IOPM_INVALID_ARGUMENT (-1)
#define IOPM_OUT_OF_MEMORY (-2)
#define IOPM_INTERNAL_ERROR (-3)
#define IOPM_BUFFER_TOO_SMALL (-4)
#define IOPM_INVALID_STATE (-5)
typedef struct iopm_instance* iopm_handle;
/* No packing pragmas required: size 12, offsets 0,4,8 on x86 and x64. */
typedef struct iopm_event {
    uint32_t clock_offset; /* relative to clock_count at render entry */
    uint32_t address;      /* 0..255 */
    uint32_t value;        /* 0..255 */
} iopm_event;
IOPM_API uint32_t IOPM_CALL iopm_abi_version(void);
IOPM_API const char* IOPM_CALL iopm_version(void); /* static; do not free */
IOPM_API int32_t IOPM_CALL iopm_create(uint32_t clock_hz, uint32_t output_rate, iopm_handle* result);
IOPM_API void IOPM_CALL iopm_destroy(iopm_handle handle); /* NULL allowed */
IOPM_API int32_t IOPM_CALL iopm_reset(iopm_handle handle);
IOPM_API int32_t IOPM_CALL iopm_clone(iopm_handle source, iopm_handle* result);
/* Versioned state includes core, resampler and output-clock remainder.
   NULL buffer + capacity=0 queries required size. Save/load allocate memory.
   No concurrent access to the same handle. Failed load leaves it unchanged. */
IOPM_API int32_t IOPM_CALL iopm_state_size(iopm_handle handle, uint32_t* size);
IOPM_API int32_t IOPM_CALL iopm_save_state(iopm_handle handle, void* buffer, uint32_t capacity, uint32_t* written);
IOPM_API int32_t IOPM_CALL iopm_load_state(iopm_handle handle, const void* buffer, uint32_t size);
/* Integration write: bypasses BUSY rejection, like core write_register. */
IOPM_API int32_t IOPM_CALL iopm_write_register(iopm_handle handle, uint32_t address, uint32_t value);
IOPM_API int32_t IOPM_CALL iopm_read_status(iopm_handle handle, uint32_t* status, uint32_t* irq);
IOPM_API int32_t IOPM_CALL iopm_clock_count(iopm_handle handle, uint64_t* clocks);
IOPM_API int32_t IOPM_CALL iopm_set_measured_output_timing(iopm_handle handle, uint32_t enabled);
IOPM_API int32_t IOPM_CALL iopm_set_output_delays(iopm_handle handle, uint32_t channel, uint32_t left_mask, uint32_t right_mask);
/* Interleaved L,R. Caller supplies at least 2*frames elements. No gain added.
   f32 is unclipped; s16 clips to [-1,+1]. Zero frames permits NULL buffer.
   Render does not allocate. Do not concurrently access the same handle. */
IOPM_API int32_t IOPM_CALL iopm_render_f32(iopm_handle handle, float* output, uint32_t frames);
IOPM_API int32_t IOPM_CALL iopm_render_s16(iopm_handle handle, int16_t* output, uint32_t frames);
/* Events sorted by clock_offset, preserving order at equal offsets.
   Offset range: 0..floor((remainder + frames*clock_hz)/output_rate).
   Offset=end is applied after advancement to the endpoint. At a native-sample
   boundary, synthesis at that clock occurs before the register write.
   All event arguments validated before state/output changes. */
IOPM_API int32_t IOPM_CALL iopm_render_events_f32(iopm_handle handle, float* output, uint32_t frames,
                                               const iopm_event* events, uint32_t event_count);
/* Pre-pan channel outputs, with existing 1/8 normalization and output timing.
   output: 2*frames floats, channels: 16*frames floats, both required for frames>0.
   Layout: channels[16*frame + 2*channel + side], channel=0..7, side=L0/R1.
   Same output rate/filter as mix; unclipped. Buffers must not overlap.
   Pan bits affect only output, not channels. No second advancement. */
IOPM_API int32_t IOPM_CALL iopm_render_channels_f32(iopm_handle handle, float* output, float* channels, uint32_t frames);
IOPM_API int32_t IOPM_CALL iopm_render_events_channels_f32(iopm_handle handle, float* output, float* channels, uint32_t frames, const iopm_event* events, uint32_t event_count);
#ifdef __cplusplus
}
#endif
#endif
