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

#include "urf-daemon.h"
#include "urf-device-ofono.h"
#include "urf-utils.h"

#define URF_DEVICE_OFONO_INTERFACE "org.freedesktop.URfkill.Device.Ofono"

#define OFONO_ERROR_IN_PROGRESS "GDBus.Error:org.ofono.Error.InProgress"
#define OFONO_ERROR_EMERGENCY   "GDBus.Error:org.ofono.Error.EmergencyActive"

static const char introspection_xml[] =
"  <interface name='org.freedesktop.URfkill.Device.Ofono'>"
"    <signal name='Changed'/>"
"    <property name='soft' type='b' access='read'/>"
"  </interface>";

enum
{
	PROP_0,
	PROP_SOFT,
	PROP_LAST
};

enum {
	SIGNAL_POWERED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

#define URF_DEVICE_OFONO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
                                           URF_TYPE_DEVICE_OFONO, UrfDeviceOfonoPrivate))

struct _UrfDeviceOfonoPrivate {
	gint index;
	char *name;
	char *modem_path;

	GHashTable *properties;
	gboolean soft;

	GDBusProxy *proxy;
	GTask *pending_block_task;
	gboolean pending_set_online_cb;
};

G_DEFINE_TYPE_WITH_PRIVATE (UrfDeviceOfono, urf_device_ofono, URF_TYPE_DEVICE)

/**
 * urf_device_ofono_update_states:
 *
 * Return value: #TRUE if the states of the blocks are changed,
 *               otherwise #FALSE
 **/
gboolean
urf_device_ofono_update_states (UrfDeviceOfono      *device,
                               const gboolean  soft,
                               const gboolean  hard)
{

	return TRUE;
}

/**
 * urf_device_ofono_get_modem_path:
 **/
gchar *
urf_device_ofono_get_modem_path (UrfDeviceOfono *ofono)
{
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (ofono);

	g_return_val_if_fail (URF_IS_DEVICE_OFONO (ofono), NULL);

	return g_strdup(priv->modem_path);
}

/**
 * get_index:
 **/
static gint
get_index (UrfDevice *device)
{
	UrfDeviceOfono *modem = URF_DEVICE_OFONO (device);
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (modem);

	return priv->index;
}

/**
 * get_rf_type:
 **/
static gint
get_rf_type (UrfDevice *device)
{
	/* oFono devices are modems so always of the WWAN type */
	return RFKILL_TYPE_WWAN;
}

static const char *
get_urf_type (UrfDevice *device)
{
	return URF_DEVICE_OFONO_INTERFACE;
}

/**
 * get_name:
 **/
static const char *
get_name (UrfDevice *device)
{
	UrfDeviceOfono *modem = URF_DEVICE_OFONO (device);
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (modem);
	GVariant *manufacturer = NULL;
	GVariant *model = NULL;

	if (priv->name)
		g_free (priv->name);

	if (!priv->proxy) {
		g_debug( "have no proxy");
	}

	manufacturer = g_hash_table_lookup (priv->properties, "Manufacturer");
	model = g_hash_table_lookup (priv->properties, "Model");

	priv->name = g_strjoin (" ",
	                        manufacturer
                                    ? g_variant_get_string (manufacturer, NULL)
	                            : _("unknown"),
	                        model
	                            ? g_variant_get_string (model, NULL)
	                            : _("unknown"),
	                        NULL);

	g_debug ("%s: new name: '%s'", __func__, priv->name);

	return priv->name;
}

/**
 * get_soft:
 **/
static gboolean
get_soft (UrfDevice *device)
{
	UrfDeviceOfono *modem = URF_DEVICE_OFONO (device);
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (modem);
	GVariant *online;
	gboolean soft = FALSE;

	online = g_hash_table_lookup (priv->properties, "Online");
	if (online)
		soft = !g_variant_get_boolean (online);

	return soft;
}

static void
set_online_cb (GObject *source_object,
               GAsyncResult *res,
               gpointer user_data)
{
	UrfDeviceOfono *modem;
	UrfDeviceOfonoPrivate *priv;
	GVariant *result;
	GError *error = NULL;
	gint code = 0;

	g_return_if_fail (URF_IS_DEVICE_OFONO (user_data));

	modem = URF_DEVICE_OFONO (user_data);
	priv = URF_DEVICE_OFONO_GET_PRIVATE (modem);

	priv->pending_set_online_cb = FALSE;
	result = g_dbus_proxy_call_finish (priv->proxy, res, &error);

	if (error == NULL) {
		g_debug ("online change successful: %s",
		         g_variant_print (result, TRUE));

		if (priv->pending_block_task) {
			g_task_return_pointer (priv->pending_block_task, NULL, NULL);
			priv->pending_block_task = NULL;
		}
	} else {
		g_warning ("Could not set Online property in oFono: %s",
		           error ? error->message : "(unknown error)");

		if (error->message) {
			if (g_strcmp0 (error->message, OFONO_ERROR_IN_PROGRESS) == 0) {
				code = URF_DAEMON_ERROR_IN_PROGRESS;
			} else if (g_strcmp0 (error->message, OFONO_ERROR_EMERGENCY) == 0) {
				code = URF_DAEMON_ERROR_EMERGENCY;
			}
		}

		if (priv->pending_block_task) {
			g_task_return_new_error(priv->pending_block_task,
						URF_DAEMON_ERROR, code,
						"set_soft failed: %s",
						urf_device_get_object_path (URF_DEVICE (modem)));
			priv->pending_block_task = NULL;
		}
	}

	g_object_unref (modem);
}

/**
 * set_soft:
 **/
static void
set_soft (UrfDevice *device, gboolean blocked, GTask *task)
{
	UrfDeviceOfono *modem = URF_DEVICE_OFONO (device);
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (modem);

	if (priv->pending_set_online_cb) {
		g_message ("%s: pending callback, not setting WWAN", __func__);
		if (task)
			g_task_return_new_error (task, URF_DAEMON_ERROR,
						 URF_DAEMON_ERROR_IN_PROGRESS,
						 "%s: waiting for ofono response %s",
						 __func__,
						 urf_device_get_object_path (URF_DEVICE (modem)));
		return;
	}

	if (priv->proxy != NULL) {
		g_message ("%s: Setting WWAN to %s",
			   __func__,
			   blocked ? "blocked" : "unblocked");

		priv->soft = blocked;
		priv->pending_block_task = task;
		priv->pending_set_online_cb = TRUE;

		/* We increment the ref count of the object to make sure the
		 * object exists when callback is invoked.
		 */
		g_dbus_proxy_call (priv->proxy,
				   "SetProperty",
				   g_variant_new ("(sv)",
						  "Online",
						  g_variant_new_boolean (!blocked)),
				   G_DBUS_CALL_FLAGS_NONE,
				   -1,
				   NULL,
				   (GAsyncReadyCallback) set_online_cb,
				   g_object_ref (device));
	} else {
		g_warning ("%s: proxy not ready yet", __func__);
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

static void
update_powered (UrfDeviceOfono *modem, gboolean powered_new)
{
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (modem);
	GVariant *var_powered;
	gboolean powered;

	var_powered = g_hash_table_lookup (priv->properties, "Powered");
	if (var_powered == NULL) {
		if (powered_new) {
			g_message ("%s: creating %s", __func__,
				   priv->modem_path);
			g_signal_emit (G_OBJECT (modem),
				       signals[SIGNAL_POWERED], 0, powered_new);
		}
		return;
	}

	powered = g_variant_get_boolean (var_powered);

	if (powered == powered_new)
		return;

	g_message ("%s: %s powered=%d", __func__, priv->modem_path, powered_new);

	g_signal_emit (G_OBJECT (modem), signals[SIGNAL_POWERED], 0,
		       powered_new);
}

static void
modem_signal_cb (GDBusProxy *proxy,
                 gchar *sender_name,
                 gchar *signal_name,
                 GVariant *parameters,
                 gpointer user_data)
{
	UrfDeviceOfono *modem = URF_DEVICE_OFONO (user_data);
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (modem);

	if (g_strcmp0 (signal_name, "PropertyChanged") == 0) {
		gchar *prop_name;
		GVariant *prop_value = NULL;

		g_debug ("properties changed for %s: %s",
			 priv->modem_path,
		         g_variant_print (parameters, TRUE));

		g_variant_get_child (parameters, 0, "s", &prop_name);
		g_variant_get_child (parameters, 1, "v", &prop_value);

		if (g_strcmp0 ("Powered", prop_name) == 0) {
			gboolean powered = g_variant_get_boolean (prop_value);

			update_powered (modem, powered);
		}

		if (prop_value)
			g_hash_table_replace (priv->properties,
			                      g_strdup (prop_name),
			                      g_variant_ref (prop_value));

		if (g_strcmp0 ("Online", prop_name) == 0) {
			g_signal_emit_by_name(G_OBJECT (modem), "state-changed", 0);
		}

		g_free (prop_name);
		g_variant_unref (prop_value);
	}
}

static void
get_properties_cb (GObject *source_object,
                   GAsyncResult *res,
                   gpointer user_data)
{
	UrfDeviceOfono *modem = URF_DEVICE_OFONO (user_data);
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (modem);
	GVariant *result, *properties, *variant = NULL;
	GVariantIter iter;
	GError *error = NULL;
	gchar *key;

	result = g_dbus_proxy_call_finish (priv->proxy, res, &error);

	if (!error) {
		properties = g_variant_get_child_value (result, 0);
		g_debug ("%zd properties for %s", g_variant_n_children (properties),
			 priv->modem_path);
		g_debug ("%s", g_variant_print (properties, TRUE));

		g_variant_iter_init (&iter, properties);
		while (g_variant_iter_next (&iter, "{sv}", &key, &variant)) {
			if (g_strcmp0 ("Powered", key) == 0 ) {
				gboolean powered = g_variant_get_boolean (variant);

				update_powered (modem, powered);
			}

			g_hash_table_insert (priv->properties, g_strdup (key),
			                     g_variant_ref (variant));
			g_variant_unref (variant);
                        g_free (key);
                }

		g_variant_unref (properties);
		g_variant_unref (result);

		g_signal_emit_by_name(G_OBJECT (modem), "state-changed", 0);
	} else {
		g_warning ("Error getting properties: %s",
		           error ? error->message : "(unknown error)");
	}

	g_object_unref (modem);
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
	UrfDeviceOfono *device = URF_DEVICE_OFONO (object);

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

	object = G_OBJECT_CLASS (urf_device_ofono_parent_class)->constructor
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
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (object);

	if (priv->proxy) {
		g_object_unref (priv->proxy);
		priv->proxy = NULL;
	}

	if (priv->modem_path) {
		g_free (priv->modem_path);
		priv->modem_path = NULL;
	}

	if (priv->name) {
		g_free (priv->name);
		priv->name = NULL;
	}

	g_hash_table_unref (priv->properties);

	G_OBJECT_CLASS(urf_device_ofono_parent_class)->dispose(object);
}

/**
 * urf_device_ofono_init:
 **/
static void
urf_device_ofono_init (UrfDeviceOfono *device)
{
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (device);

	priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                          g_free, (GDestroyNotify) g_variant_unref);
	priv->soft = FALSE;
	priv->pending_set_online_cb = FALSE;
}

/**
 * urf_device_ofono_class_init:
 **/
static void
urf_device_ofono_class_init(UrfDeviceOfonoClass *class)
{
	GObjectClass *object_class = (GObjectClass *) class;
	UrfDeviceClass *parent_class = URF_DEVICE_CLASS (class);
	GParamSpec *pspec;

	object_class->constructor = constructor;
	object_class->get_property = get_property;
	object_class->set_property = set_property;
	object_class->dispose = dispose;

	parent_class->get_index = get_index;
	parent_class->get_state = get_state;
	parent_class->get_name = get_name;
	parent_class->get_urf_type = get_urf_type;
	parent_class->get_device_type = get_rf_type;
	parent_class->is_software_blocked = get_soft;
	parent_class->set_software_blocked = set_soft;

	pspec = g_param_spec_boolean ("soft",
				      "Soft Block",
				      "The soft block of the device",
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class,
					 PROP_SOFT,
					 pspec);

	signals[SIGNAL_POWERED] =
		g_signal_new ("ofono-device-powered",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (UrfDeviceOfonoClass, device_powered),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
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
	UrfDeviceOfono *device = URF_DEVICE_OFONO (user_data);

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
 * urf_device_ofono_new:
 */
UrfDevice *
urf_device_ofono_new (gint index, const char *object_path)
{
	UrfDeviceOfono *device = g_object_new (URF_TYPE_DEVICE_OFONO, NULL);
	UrfDeviceOfonoPrivate *priv = URF_DEVICE_OFONO_GET_PRIVATE (device);
	GError *error = NULL;

	priv->index = index;

	g_debug ("new ofono device: %p for %s", device, object_path);

	if (!urf_device_register_device (URF_DEVICE (device), interface_vtable, introspection_xml)) {
                g_object_unref (device);
                return NULL;
        }

	priv->modem_path = g_strdup (object_path);

	priv->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
						     G_DBUS_PROXY_FLAGS_NONE,
						     NULL,
						     "org.ofono",
						     object_path,
						     "org.ofono.Modem",
						     NULL,
						     &error);
	if (error == NULL) {
		g_signal_connect (priv->proxy, "g-signal",
		                  G_CALLBACK (modem_signal_cb), device);

		/* We increment the ref count of the object to make sure the
		 * object exists when callback is invoked.
		 */
		g_dbus_proxy_call (priv->proxy,
		                   "GetProperties",
		                   NULL,
		                   G_DBUS_CALL_FLAGS_NONE,
		                   -1,
		                   NULL,
		                   (GAsyncReadyCallback) get_properties_cb,
		                   g_object_ref (device));
	} else {
		g_warning ("Could not get oFono Modem proxy: %s",
		           error ? error->message : "(unknown error)");
	}

	return URF_DEVICE (device);
}
