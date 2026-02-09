#ifndef DBUS_BAP_H
#define DBUS_BAP_H

#include <glib.h>

typedef enum {
  ROLE_SINK  = 0,  // LC3 RX
  ROLE_SOURCE     // LC3 TX
} BapRole;

typedef struct {
  BapRole role;
  guint   sample_rate;   // e.g., 48000
  guint   frame_ms;      // 7.5 or 10
  guint   frame_bytes;   // e.g., 120 @48k/10ms (example)
  gchar  *adapter;       // e.g., "/org/bluez/hci0"
  gchar  *obj_path;      // our endpoint object path: e.g., "/leaudio/ep_sink"
} BapEndpointCfg;

gboolean dbus_bap_init(GMainLoop *loop);
gboolean dbus_bap_register_endpoint(const BapEndpointCfg *cfg);
void     dbus_bap_deinit(void);

#endif // DBUS_BAP_H