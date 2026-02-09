#include "iso_io.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <glib.h>

/* Simple helpers */
static void write_all(int fd, const uint8_t *buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(fd, buf + off, len - off);
    if (n <= 0) return;
    off += (size_t)n;
  }
}

static void generate_sine(int16_t *pcm, size_t samples, unsigned rate, float freq) {
  static double ph = 0.0;
  double step = 2.0 * M_PI * freq / rate;
  for (size_t i=0;i<samples;i++) {
    pcm[i] = (int16_t)(sin(ph) * 28000);
    ph += step; if (ph > 2.0*M_PI) ph -= 2.0*M_PI;
  }
}

/* -------- RX: read LC3 frames from ISO, decode to PCM, dump to file -------- */
void iso_start_rx_loop(const IsoRxParams *p_in) {
  IsoRxParams p = *p_in;
  const size_t frame_sz = p.frame_bytes;
  uint8_t *frame = g_malloc0(frame_sz);
  const size_t max_pcm = (p.sample_rate * p.frame_ms / 1000); // mono samples per frame
  int16_t *pcm = g_malloc0(max_pcm * sizeof(int16_t));
  FILE *dump = NULL;
  if (p.dump_pcm_path) dump = fopen(p.dump_pcm_path, "wb");

  g_print("ISO RX loop started (fd=%d)\n", p.iso_fd);
  while (1) {
    ssize_t n = read(p.iso_fd, frame, frame_sz);
    if (n < 0) {
      if (errno == EINTR) continue;
      g_printerr("ISO RX read error: %s\n", g_strerror(errno));
      break;
    }
    if (n == 0) continue;
    int dec = lc3_decode(p.lc3, frame, (size_t)n, pcm, max_pcm);
    if (dec > 0 && dump) write_all(fileno(dump), (uint8_t*)pcm, (size_t)dec * sizeof(int16_t));
  }
  if (dump) fclose(dump);
  g_free(frame);
  g_free(pcm);
  lc3_close(p.lc3);
  close(p.iso_fd);
}

/* -------- TX: generate tone or read stdin PCM, encode to LC3, write ISO ----- */
void iso_start_tx_loop(const IsoTxParams *p_in) {
  IsoTxParams p = *p_in;
  const size_t frame_pcm = (p.sample_rate * p.frame_ms / 1000); // mono
  int16_t *pcm = g_malloc0(frame_pcm * sizeof(int16_t));
  uint8_t *frame = g_malloc0(p.frame_bytes);

  g_print("ISO TX loop started (fd=%d)\n", p.iso_fd);
  while (1) {
    if (p.tone && strcmp(p.tone,"sine440")==0) {
      generate_sine(pcm, frame_pcm, p.sample_rate, 440.0f);
    } else {
      size_t need = frame_pcm * sizeof(int16_t);
      size_t off = 0;
      while (off < need) {
        ssize_t r = read(STDIN_FILENO, ((uint8_t*)pcm)+off, need - off);
        if (r <= 0) break;
        off += (size_t)r;
      }
      if (off < need) memset(((uint8_t*)pcm)+off, 0, need-off);
    }
    int enc = lc3_encode(p.lc3, pcm, frame_pcm, frame, p.frame_bytes);
    if (enc > 0) {
      ssize_t n = write(p.iso_fd, frame, (size_t)enc);
      if (n < 0) {
        if (errno == EINTR) continue;
        g_printerr("ISO TX write error: %s\n", g_strerror(errno));
        break;
      }
    }
  }
  g_free(pcm);
  g_free(frame);
  lc3_close(p.lc3);
  close(p.iso_fd);
}