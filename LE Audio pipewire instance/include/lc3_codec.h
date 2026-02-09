#ifndef LC3_CODEC_H
#define LC3_CODEC_H

#include <stdint.h>
#include <stdlib.h>

typedef struct LC3Ctx LC3Ctx;

LC3Ctx* lc3_open(unsigned rate, unsigned frame_ms, unsigned frame_bytes, int channels);
void    lc3_close(LC3Ctx *ctx);

/* Encode PCM16 interleaved -> LC3 frame(s)
 * Returns bytes written to out_frame or <0 on error.
 */
int lc3_encode(LC3Ctx *ctx,
               const int16_t *pcm, size_t pcm_samples,  /* per channel * channels */
               uint8_t *out_frame, size_t out_frame_size);

/* Decode LC3 frame(s) -> PCM16 interleaved
 * Returns samples per channel written or <0 on error.
 */
int lc3_decode(LC3Ctx *ctx,
               const uint8_t *frame, size_t frame_size,
               int16_t *out_pcm, size_t out_pcm_capacity);

#endif // LC3_CODEC_H