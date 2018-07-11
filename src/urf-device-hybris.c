/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include <linux/rfkill.h>

#include <android/hardware_legacy/wifi.h>

#include "urf-device-hybris.h"

#include "urf-daemon.h"
#include "urf-utils.h"

#define URF_DEVICE_HYBRIS_INTERFACE "org.freedesktop.URfkill.Device.Hybris"

static const char introspection_xml[] =
"  <interface name='org.freedesktop.URfkill.Device.Hybris'>"
"    <signal name='Changed'/>"
"    <property name='soft' type='b' access='read'/>"
"  </interface>";

enum
{
	PROP_0,
	PROP_SOFT,
	PROP_LAST
};

#define HYBRIS_INDEX 200

#define URF_DEVICE_HYBRIS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
				URF_TYPE_DEVICE_HYBRIS, UrfDeviceHybrisPrivate))

struct _UrfDeviceHybrisPrivate {
	gint index;
	char *name;

	gboolean soft;
};

G_DEFINE_TYPE_WITH_PRIVATE (UrfDeviceHybris, urf_device_hybris, URF_TYPE_DEVICE)

/**
 * get_index:
 **/
static gint
get_index (UrfDevice *device)
{
	UrfDeviceHybris *hybris = URF_DEVICE_HYBRIS (device);
	UrfDeviceHybrisPrivate *priv = URF_DEVICE_HYBRIS_GET_PRIVATE (hybris);

	return priv->index;
}

/**
 * get_rf_type:
 **/
static gint
get_rf_type (UrfDevice *device)
{
	return RFKILL_TYPE_WLAN;
}

static const char *
get_urf_type (UrfDevice *device)
{
	return URF_DEVICE_HYBRIS_INTERFACE;
}

/**
 * get_name:
 **/
static const char *
get_name (UrfDevice *device)
{
	return "hybris_wifi";
}

/**
 * get_soft:
 **/
static gboolean
get_soft (UrfDevice *device)
{
	UrfDeviceHybris *hybris = URF_DEVICE_HYBRIS (device);
	UrfDeviceHybrisPrivate *priv = URF_DEVICE_HYBRIS_GET_PRIVATE (hybris);

	return priv->soft;
}

static inline int is_soft_blocked()
{
	return !is_wifi_driver_loaded();
}

/**
 * set_soft:
 **/
static void
set_soft (UrfDevice *device, gboolean blocked, GTask *task)
{
	UrfDeviceHybris *hybris = URF_DEVICE_HYBRIS (device);
	UrfDeviceHybrisPrivate *priv = URF_DEVICE_HYBRIS_GET_PRIVATE (hybris);
	int res;
	gboolean prev_blocked = priv->soft;

	if (blocked)
		res = wifi_unload_driver();
	else
		res = wifi_load_driver();

	priv->soft = is_soft_blocked();

	if (prev_blocked != priv->soft)
		g_signal_emit_by_name(G_OBJECT (device), "state-changed", 0);

	if (res < 0) {
		g_warning ("Error setting hybris_wifi soft to %d", blocked);

		if (task)
			g_task_return_new_error(task,
						URF_DAEMON_ERROR,
						URF_DAEMON_ERROR_GENERAL,
						"set_soft failed hybris Wi-Fi");
	} else {
		g_message ("hybris_wifi soft blocked set to %d", blocked);

		if (task)
			g_task_return_pointer (task, NULL, NULL);

	}
}

/**
 * get_state:
 **/
static KillswitchState
get_state (UrfDevice *device)
{
	KillswitchState state = KILLSWITCH_STATE_UNBLOCKED;
	gboolean soft;

	soft = get_soft (device);
	if (soft)
		state = KILLSWITCH_STATE_SOFT_BLOCKED;

	return state;
}

/**
 * get_property:
 **/
static void
get_property (GObject    *object,
	      guint       prop_id,
	      GValue     *value,
	      GParamSpec *pspec)
{
	UrfDeviceHybris *device = URF_DEVICE_HYBRIS (object);

	switch (prop_id) {
	case PROP_SOFT:
		g_value_set_boolean (value, get_soft (URF_DEVICE (device)));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object,
	      guint prop_id,
	      const GValue *value,
	      GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static GObject*
constructor (GType type,
	     guint n_construct_params,
	     GObjectConstructParam *construct_params)
{
	GObject *object;

	object = G_OBJECT_CLASS (urf_device_hybris_parent_class)->constructor
				 (type,
				  n_construct_params,
				  construct_params);

	if (!object)
		return NULL;

	return object;
}

/**
 * dispose:
 **/
static void
dispose (GObject *object)
{
	G_OBJECT_CLASS (urf_device_hybris_parent_class)->dispose(object);
}

/**
 * urf_device_hybris_init:
 **/
static void
urf_device_hybris_init (UrfDeviceHybris *device)
{
	UrfDeviceHybrisPrivate *priv = URF_DEVICE_HYBRIS_GET_PRIVATE (device);

	priv->soft = is_soft_blocked();
}

/**
 * urf_device_hybris_class_init:
 **/
static void
urf_device_hybris_class_init(UrfDeviceHybrisClass *class)
{
	GObjectClass *object_class = (GObjectClass *) class;
	UrfDeviceClass *parent_class = URF_DEVICE_CLASS (class);
	GParamSpec *pspec;

	object_class->constructor = constructor;
	object_class->get_property = get_property;
	object_class->set_property = set_property;
	object_class->dispose = dispose;

	parent_class->get_index = get_index;
	parent_class->get_device_type = get_rf_type;
	parent_class->get_urf_type = get_urf_type;
	parent_class->get_name = get_name;
	parent_class->get_state = get_state;
	parent_class->set_software_blocked = set_soft;
	parent_class->is_software_blocked = get_soft;

	pspec = g_param_spec_boolean ("soft",
				      "Soft Block",
				      "The soft block of the device",
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class,
					 PROP_SOFT,
					 pspec);
}

static GVariant *
handle_get_property (GDBusConnection *connection,
		     const gchar *sender,
		     const gchar *object_path,
		     const gchar *interface_name,
		     const gchar *property_name,
		     GError **error,
		     gpointer user_data)
{
	UrfDeviceHybris *device = URF_DEVICE_HYBRIS (user_data);

	GVariant *retval = NULL;

	if (g_strcmp0 (property_name, "soft") == 0)
		retval = g_variant_new_boolean (get_soft (URF_DEVICE (device)));

	return retval;
}

static gboolean
handle_set_property (GDBusConnection *connection,
		     const gchar *sender,
		     const gchar *object_path,
		     const gchar *interface_name,
		     const gchar *property_name,
		     GVariant *value,
		     GError **error,
		     gpointer user_data)
{
	return TRUE;
}

static const GDBusInterfaceVTable interface_vtable =
{
	NULL, /* handle method_call */
	handle_get_property,
	handle_set_property,
};

/**
 * urf_device_hybris_new:
 */
UrfDevice *
urf_device_hybris_new (void)
{
	UrfDeviceHybris *device = g_object_new (URF_TYPE_DEVICE_HYBRIS, NULL);
	UrfDeviceHybrisPrivate *priv = URF_DEVICE_HYBRIS_GET_PRIVATE (device);

	priv->index = HYBRIS_INDEX;

	g_debug ("new hybris device: %p for index %d", device, priv->index);

	if (!urf_device_register_device (URF_DEVICE (device),
					 interface_vtable,
					 introspection_xml)) {
		g_object_unref (device);
		return NULL;
	}

	return URF_DEVICE (device);
}
