#ifndef ISO_IO_H
#define ISO_IO_H

#include "lc3_codec.h"
#include <stdint.h>

typedef struct {
  int iso_fd;
  unsigned sample_rate;
  unsigned frame_ms;
  unsigned frame_bytes;
  LC3Ctx *lc3;
  const char *dump_pcm_path;  // optional: write decoded PCM here
} IsoRxParams;

typedef struct {
  int iso_fd;
  unsigned sample_rate;
  unsigned frame_ms;
  unsigned frame_bytes;
  LC3Ctx *lc3;
  const char *tone;           // "sine440" or NULL to read stdin PCM
} IsoTxParams;

void iso_start_rx_loop(const IsoRxParams *p);
void iso_start_tx_loop(const IsoTxParams *p);

#endif // ISO_IO_H