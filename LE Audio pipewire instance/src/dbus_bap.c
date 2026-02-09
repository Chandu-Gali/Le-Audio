#include "dbus_bap.h"
#include "iso_io.h"
#include "lc3_codec.h"

#include <gio/gio.h>
#include <string.h>
#include <unistd.h>

#define BLUEZ_BUS              "org.bluez"
#define IFACE_OBJMGR           "org.freedesktop.DBus.ObjectManager"
#define IFACE_MEDIA            "org.bluez.Media1"
#define IFACE_ENDPOINT         "org.bluez.MediaEndpoint1"
#define IFACE_TRANSPORT        "org.bluez.MediaTransport1"

#define UUID_BAP_SINK  "00002bc9-0000-1000-8000-00805f9b34fb"
#define UUID_BAP_SRC   "00002bcb-0000-1000-8000-00805f9b34fb"

typedef struct {
  GMainLoop *loop;
  GDBusConnection *sys;
  gchar *adapter_path;
  BapEndpointCfg ep;
  GDBusNodeInfo *endpoint_introspection;
  guint endpoint_reg_id;
} Ctx;

static Ctx g = {0};

static const gchar *xml_endpoint =
  "<node>"
  "  <interface name='org.bluez.MediaEndpoint1'>"
  "    <method name='SelectConfiguration'>"
  "      <arg type='ay' name='capabilities' direction='in'/>"
  "      <arg type='ay' name='configuration' direction='out'/>"
  "    </method>"
  "    <method name='SetConfiguration'>"
  "      <arg type='o'  name='transport' direction='in'/>"
  "      <arg type='a{sv}' name='properties' direction='in'/>"
  "    </method>"
  "    <method name='ClearConfiguration'>"
  "      <arg type='o' name='transport' direction='in'/>"
  "    </method>"
  "    <method name='Release'/>"
  "  </interface>"
  "</node>";

/* ------------ Utilities ------------ */

static gchar *first_adapter(GDBusConnection *bus) {
  GError *err = NULL;
  GVariant *ret = g_dbus_connection_call_sync(
      bus, BLUEZ_BUS, "/",
      IFACE_OBJMGR, "GetManagedObjects",
      NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

  if (!ret) { if (err) g_error_free(err); return NULL; }

  GVariantIter *it_objects;
  const gchar *path;
  GVariant *ifaces;
  g_variant_get(ret, "(a{oa{sa{sv}}})", &it_objects);
  while (g_variant_iter_loop(it_objects, "{&oa{sa{sv}}}", &path, &ifaces)) {
    GVariantIter *it_ifaces;
    const gchar *iname;
    GVariant *props;
    g_variant_get(ifaces, "a{sa{sv}}", &it_ifaces);
    while (g_variant_iter_loop(it_ifaces, "{&sa{sv}}", &iname, &props)) {
      if (g_strcmp0(iname, "org.bluez.Adapter1") == 0) {
        g_variant_iter_free(it_ifaces);
        g_variant_iter_free(it_objects);
        g_variant_unref(ret);
        return g_strdup(path);
      }
    }
    g_variant_iter_free(it_ifaces);
  }
  g_variant_iter_free(it_objects);
  g_variant_unref(ret);
  return NULL;
}

/* ------------ MediaEndpoint1 method handlers ------------ */

static void handle_select_configuration(GDBusMethodInvocation *inv, GVariant *params) {
  /* For MVP: just echo back the incoming capabilities as configuration,
   * letting BlueZ/remote peer narrow it further (this is accepted by BlueZ).
   * Production should parse LC3 LTVs and choose a preset deterministically.
   * (Behavior and expected LTVs are implied by BAP; endpoint API described in manpage)
   */
  GVariant *caps;
  g_variant_get(params, "(@ay)", &caps);

  g_dbus_method_invocation_return_value(inv, g_variant_new("(@ay)", caps));
  g_variant_unref(caps);
}

typedef struct {
  gchar *transport_path;
  guint16 mtu_read, mtu_write;
  int iso_fd;
} Transport;

static gboolean acquire_transport_fd(const gchar *transport_path, Transport *t) {
  GError *err = NULL;
  GUnixFDList *fdlist = NULL;
  GVariant *ret = g_dbus_connection_call_with_unix_fd_list_sync(
      g.sys, BLUEZ_BUS, transport_path, IFACE_TRANSPORT, "Acquire",
      NULL, G_VARIANT_TYPE("(hqq)"),
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &fdlist, NULL, &err);

  if (!ret) {
    if (err) { g_printerr("Acquire failed: %s\n", err->message); g_error_free(err); }
    return FALSE;
  }

  gint idx;
  guint16 mtu_r, mtu_w;
  g_variant_get(ret, "(hqq)", &idx, &mtu_r, &mtu_w);
  t->iso_fd = g_unix_fd_list_get(fdlist, idx, NULL);
  t->mtu_read  = mtu_r;
  t->mtu_write = mtu_w;
  t->transport_path = g_strdup(transport_path);

  g_variant_unref(ret);
  g_object_unref(fdlist);

  g_print("Acquired transport %s (fd=%d, mtu_r=%u, mtu_w=%u)\n",
          transport_path, t->iso_fd, t->mtu_read, t->mtu_write);
  return t->iso_fd >= 0;
}

static void handle_set_configuration(GDBusMethodInvocation *inv, GVariant *params) {
  const gchar *transport;
  GVariant *props;
  g_variant_get(params, "(&oa{sv})", &transport, &props);

  /* In a full impl you would parse props: Configuration, QoS, Metadata, etc.
   * For LE Audio specifics, MediaTransport1 properties include ISO-only fields,
   * and QoS dict when configured. (See org.bluez.MediaTransport1 docs)
   */
  g_print("SetConfiguration: transport=%s\n", transport); /* For visibility */

  /* Acquire ISO fd now (moves transport to 'active' upon success) */
  Transport t = {0};
  if (!acquire_transport_fd(transport, &t)) {
    g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_FAILED,
                                          "Acquire failed");
    return;
  }

  /* Start RX/TX loop depending on role */
  LC3Ctx *lc3 = lc3_open(g.ep.sample_rate, g.ep.frame_ms, g.ep.frame_bytes, 1 /*mono*/);
  if (!lc3) {
    g_printerr("LC3 init failed\n");
  } else if (g.ep.role == ROLE_SINK) {
    IsoRxParams p = {
      .iso_fd = t.iso_fd, .sample_rate=g.ep.sample_rate, .frame_ms=g.ep.frame_ms,
      .frame_bytes=g.ep.frame_bytes, .lc3=lc3, .dump_pcm_path="le_rx_dump.pcm"
    };
    g_thread_new("iso-rx", (GThreadFunc)iso_start_rx_loop, g_memdup(&p, sizeof(p)));
  } else {
    IsoTxParams p = {
      .iso_fd = t.iso_fd, .sample_rate=g.ep.sample_rate, .frame_ms=g.ep.frame_ms,
      .frame_bytes=g.ep.frame_bytes, .lc3=lc3, .tone="sine440"
    };
    g_thread_new("iso-tx", (GThreadFunc)iso_start_tx_loop, g_memdup(&p, sizeof(p)));
  }

  /* Must ack SetConfiguration with void */
  g_dbus_method_invocation_return_value(inv, NULL);
  (void)props;
}

static void handle_clear_configuration(GDBusMethodInvocation *inv, GVariant *params) {
  const gchar *transport;
  g_variant_get(params, "(&o)", &transport);
  g_print("ClearConfiguration: %s (stop streaming)\n", transport);
  g_dbus_method_invocation_return_value(inv, NULL);
}

static void handle_release(GDBusMethodInvocation *inv) {
  g_print("Endpoint Release\n");
  g_dbus_method_invocation_return_value(inv, NULL);
}

static void on_method_call(GDBusConnection *conn, const gchar *sender,
                           const gchar *obj_path, const gchar *iface,
                           const gchar *method, GVariant *params,
                           GDBusMethodInvocation *inv, gpointer user_data) {
  if (g_strcmp0(iface, IFACE_ENDPOINT) != 0) return;

  if (g_strcmp0(method, "SelectConfiguration") == 0)      handle_select_configuration(inv, params);
  else if (g_strcmp0(method, "SetConfiguration") == 0)    handle_set_configuration(inv, params);
  else if (g_strcmp0(method, "ClearConfiguration") == 0)  handle_clear_configuration(inv, params);
  else if (g_strcmp0(method, "Release") == 0)             handle_release(inv);
  else g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_FAILED, "Unknown method");
}

static const GDBusInterfaceVTable vtable = { on_method_call, NULL, NULL };

/* ------------ Endpoint registration ------------ */

static gboolean register_endpoint_with_bluez(const BapEndpointCfg *cfg) {
  GError *err = NULL;
  const gchar *uuid = (cfg->role == ROLE_SINK) ? UUID_BAP_SINK : UUID_BAP_SRC;
  GVariantBuilder props;
  g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&props, "{sv}", "UUID", g_variant_new_string(uuid));
  g_variant_builder_add(&props, "{sv}", "Codec", g_variant_new_byte(0x06)); // LC3
  /* Capabilities are optional here; real-world endpoints should provide LC3 LTVs */

  /* Call Media1.RegisterEndpoint(adapter, endpoint_path, props) */
  GVariant *ret = g_dbus_connection_call_sync(
      g.sys, BLUEZ_BUS, g.adapter_path, IFACE_MEDIA,
      "RegisterEndpoint",
      g_variant_new("(oa{sv})", cfg->obj_path, &props),
      NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

  if (!ret) {
    g_printerr("RegisterEndpoint failed: %s\n", err ? err->message : "unknown");
    if (err) g_error_free(err);
    return FALSE;
  }
  g_variant_unref(ret);

  g_print("Registered %s endpoint at %s on %s\n", uuid, cfg->obj_path, g.adapter_path);
  return TRUE;
}

gboolean dbus_bap_init(GMainLoop *loop) {
  g.loop = loop;
  GError *err = NULL;

  g.sys = g_dbus_connection_new_for_address_sync(
            "unix:path=/var/run/dbus/system_bus_socket",
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
            NULL, NULL, &err);
  if (!g.sys) {
    if (err) { g_printerr("system bus: %s\n", err->message); g_error_free(err); }
    return FALSE;
  }
  return TRUE;
}

gboolean dbus_bap_register_endpoint(const BapEndpointCfg *cfg_in) {
  g.ep = *cfg_in;

  if (!g.ep.adapter) {
    g.adapter_path = first_adapter(g.sys);
    if (!g.adapter_path) {
      g_printerr("No BlueZ adapter found\n");
      return FALSE;
    }
  } else {
    g.adapter_path = g_strdup(g.ep.adapter);
  }

  GError *err = NULL;
  g.endpoint_introspection = g_dbus_node_info_new_for_xml(xml_endpoint, &err);
  if (!g.endpoint_introspection) {
    g_printerr("introspection parse error: %s\n", err->message);
    g_error_free(err);
    return FALSE;
  }

  g.endpoint_reg_id = g_dbus_connection_register_object(
      g.sys, g.ep.obj_path,
      g.endpoint_introspection->interfaces[0],
      &vtable, NULL, NULL, &err);
  if (!g.endpoint_reg_id) {
    g_printerr("register object failed: %s\n", err->message);
    g_error_free(err);
    return FALSE;
  }
  return register_endpoint_with_bluez(&g.ep);
}

void dbus_bap_deinit(void) {
  if (g.endpoint_reg_id) {
    g_dbus_connection_unregister_object(g.sys, g.endpoint_reg_id);
    g.endpoint_reg_id = 0;
  }
  if (g.endpoint_introspection) {
    g_dbus_node_info_unref(g.endpoint_introspection);
    g.endpoint_introspection = NULL;
  }
  g_clear_pointer(&g.adapter_path, g_free);
  if (g.sys) { g_object_unref(g.sys); g.sys = NULL; }
}