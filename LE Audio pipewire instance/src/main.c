#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "dbus_bap.h"

static gboolean g_sink = TRUE;
static gchar   *g_adapter = NULL;  // auto-detect if NULL

static GOptionEntry entries[] = {
  { "source", 0, 0, G_OPTION_ARG_NONE, &g_sink, "Run as LC3 source (TX). Default: sink (RX)", NULL },
  { "adapter", 'a', 0, G_OPTION_ARG_STRING, &g_adapter, "BlueZ adapter path (e.g., /org/bluez/hci0). Auto if omitted.", "PATH" },
  { NULL }
};

int main(int argc, char **argv) {
  GError *err = NULL;
  GOptionContext *ctx = g_option_context_new("- LE Audio micro-daemon");
  g_option_context_add_main_entries(ctx, entries, NULL);
  if(!g_option_context_parse(ctx, &argc, &argv, &err)) {
    g_printerr("Option parse error: %s\n", err->message);
    return 1;
  }
  g_option_context_free(ctx);

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  if (!dbus_bap_init(loop)) {
    g_printerr("D-Bus init failed\n");
    return 1;
  }

  BapEndpointCfg ep = {
    .role         = g_sink ? ROLE_SINK : ROLE_SOURCE,
    .sample_rate  = 48000,
    .frame_ms     = 10,
    .frame_bytes  = 120,       // typical LC3 frame for 48k/10ms/mono@96kbps
    .adapter      = g_adapter ? g_strdup(g_adapter) : NULL,
    .obj_path     = g_sink ? g_strdup("/leaudio/ep_sink") : g_strdup("/leaudio/ep_source"),
  };

  if (!dbus_bap_register_endpoint(&ep)) {
    g_printerr("Failed to register endpoint\n");
    return 2;
  }

  g_print("LE Audio %s ready. Waiting for SetConfiguration/Acquire...\n",
          g_sink ? "sink (RX)" : "source (TX)");
  g_main_loop_run(loop);

  dbus_bap_deinit();
  g_main_loop_unref(loop);
  g_free(ep.adapter);
  g_free(ep.obj_path);
  return 0;
}