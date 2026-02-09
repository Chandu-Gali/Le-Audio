#include "lc3_codec.h"
#include <string.h>

#ifdef USE_LIBLC3
# include <lc3.h>
#endif

struct LC3Ctx {
  unsigned rate, frame_ms, frame_bytes;
  int channels;
#ifdef USE_LIBLC3
  lc3_encoder_t enc;
  lc3_decoder_t dec;
  void *enc_mem;
  void *dec_mem;
#endif
};

LC3Ctx* lc3_open(unsigned rate, unsigned frame_ms, unsigned frame_bytes, int channels) {
  LC3Ctx *c = calloc(1, sizeof(*c));
  c->rate=rate; c->frame_ms=frame_ms; c->frame_bytes=frame_bytes; c->channels=channels;
#ifdef USE_LIBLC3
  lc3_pcm_format_t fmt = LC3_PCM_FORMAT_S16;
  int ns = (int)(rate * frame_ms / 1000);
  c->enc_mem = malloc(lc3_encoder_size(channels));
  c->dec_mem = malloc(lc3_decoder_size(channels));
  c->enc = lc3_setup_encoder(rate, frame_ms, channels, c->enc_mem);
  c->dec = lc3_setup_decoder(rate, frame_ms, channels, c->dec_mem);
  (void)fmt; (void)ns;
#endif
  return c;
}

void lc3_close(LC3Ctx *c) {
#ifdef USE_LIBLC3
  free(c->enc_mem);
  free(c->dec_mem);
#endif
  free(c);
}

int lc3_encode(LC3Ctx *c, const int16_t *pcm, size_t pcm_samples,
               uint8_t *out_frame, size_t out_frame_size) {
#ifdef USE_LIBLC3
  if (out_frame_size < c->frame_bytes) return -1;
  return lc3_encode(c->enc, LC3_PCM_FORMAT_S16, pcm, (int)(pcm_samples), c->frame_bytes, out_frame);
#else
  /* STUB: produce silence frame of requested size */
  memset(out_frame, 0, out_frame_size);
  return (int)out_frame_size;
#endif
}

int lc3_decode(LC3Ctx *c, const uint8_t *frame, size_t frame_size,
               int16_t *out_pcm, size_t out_pcm_capacity) {
#ifdef USE_LIBLC3
  (void)frame_size; /* liblc3 knows bytes from encoder bitrate */
  int ns = (int)(c->rate * c->frame_ms / 1000);
  if (out_pcm_capacity < (size_t)ns * c->channels) return -1;
  int rc = lc3_decode(c->dec, frame, c->frame_bytes, LC3_PCM_FORMAT_S16, out_pcm, ns);
  return rc < 0 ? rc : ns;
#else
  /* STUB: write silence samples */
  size_t ns = (size_t)(c->rate * c->frame_ms / 1000);
  if (out_pcm_capacity < ns) return -1;
  memset(out_pcm, 0, ns * sizeof(int16_t));
  return (int)ns;
#endif
}