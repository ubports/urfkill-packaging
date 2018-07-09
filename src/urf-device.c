/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Gary Ching-Pang Lin <glin@suse.com>
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
#include <glib.h>
#include <linux/rfkill.h>
#include <gio/gio.h>
#include <libudev.h>

#include "urf-device.h"

#include "urf-utils.h"

#define URF_DEVICE_INTERFACE "org.freedesktop.URfkill.Device"

static const char introspection_generic[] =
"  <interface name='org.freedesktop.URfkill.Device'>"
"    <signal name='Changed'/>"
"    <property name='index' type='i' access='read'/>"
"    <property name='type' type='i' access='read'/>"
"    <property name='urftype' type='s' access='read'/>"
"    <property name='name' type='s' access='read'/>"
"    <property name='platform' type='b' access='read'/>"
"  </interface>";

enum
{
	PROP_0,
	PROP_DEVICE_INDEX,
	PROP_DEVICE_TYPE,
	PROP_URF_TYPE,
	PROP_DEVICE_NAME,
	PROP_DEVICE_STATE,
	PROP_DEVICE_PLATFORM,
	PROP_LAST
};

enum {
	SIGNAL_CHANGED,
	LAST_SIGNAL
};

static int signals[LAST_SIGNAL] = { 0 };

#define URF_DEVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
                                URF_TYPE_DEVICE, UrfDevicePrivate))

struct _UrfDevicePrivate {
	char		*object_path;
	GDBusConnection	*connection;
	GDBusNodeInfo	*introspection_data;
	guint            dev_iface_id;
	guint            child_iface_id;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (UrfDevice, urf_device, G_TYPE_OBJECT)


/**
 * urf_device_get_connection:
 **/
GDBusConnection *
urf_device_get_connection (UrfDevice *device)
{
	UrfDevicePrivate *priv = URF_DEVICE_GET_PRIVATE (device);

	return priv->connection;
}

/**
 * urf_device_get_index:
 **/
gint
urf_device_get_index (UrfDevice *device)
{
	g_return_val_if_fail (URF_IS_DEVICE (device), -1);

	if (URF_GET_DEVICE_CLASS (device)->get_index)
		return URF_GET_DEVICE_CLASS (device)->get_index (device);

	return -1;
}

/**
 * urf_device_get_device_type:
 **/
gint
urf_device_get_device_type (UrfDevice *device)
{
	g_return_val_if_fail (URF_IS_DEVICE (device), -1);

	if (URF_GET_DEVICE_CLASS (device)->get_device_type)
		return URF_GET_DEVICE_CLASS (device)->get_device_type (device);

	return -1;
}

/**
 * urf_device_get_urf_type:
 **/
const char *
urf_device_get_urf_type (UrfDevice *device)
{
	g_return_val_if_fail (URF_IS_DEVICE (device), NULL);

	if (URF_GET_DEVICE_CLASS (device)->get_urf_type)
		return URF_GET_DEVICE_CLASS (device)->get_urf_type (device);

	return URF_DEVICE_INTERFACE;
}

/**
 * urf_device_get_name:
 **/
const char *
urf_device_get_name (UrfDevice *device)
{
	g_return_val_if_fail (URF_IS_DEVICE (device), NULL);

	if (URF_GET_DEVICE_CLASS (device)->get_name)
		return URF_GET_DEVICE_CLASS (device)->get_name (device);

	return NULL;
}

/**
 * urf_device_is_hardware_blocked:
 **/
gboolean
urf_device_is_hardware_blocked (UrfDevice *device)
{
	g_return_val_if_fail (URF_IS_DEVICE (device), FALSE);

	if (URF_GET_DEVICE_CLASS (device)->is_hardware_blocked)
		return URF_GET_DEVICE_CLASS (device)->is_hardware_blocked (device);

	return FALSE;
}

/**
 * urf_device_is_software_blocked:
 **/
gboolean
urf_device_is_software_blocked (UrfDevice *device)
{
	g_return_val_if_fail (URF_IS_DEVICE (device), FALSE);

	if (URF_GET_DEVICE_CLASS (device)->is_software_blocked)
		return URF_GET_DEVICE_CLASS (device)->is_software_blocked (device);

	return FALSE;
}

/**
 * urf_device_set_hardware_blocked:
 **/
gboolean
urf_device_set_hardware_blocked (UrfDevice *device, gboolean blocked)
{
	g_return_val_if_fail (URF_IS_DEVICE (device), FALSE);

	if (URF_GET_DEVICE_CLASS (device)->set_hardware_blocked)
		return URF_GET_DEVICE_CLASS (device)->set_hardware_blocked (device, blocked);

	return FALSE;
}

/**
 * urf_device_set_software_blocked:
 **/
void
urf_device_set_software_blocked (UrfDevice *device, gboolean blocked, GTask *task)
{
	g_assert (URF_IS_DEVICE (device));

	if (URF_GET_DEVICE_CLASS (device)->set_software_blocked)
		URF_GET_DEVICE_CLASS (device)->set_software_blocked (device, blocked, task);
}

/**
 * urf_device_get_state:
 **/
KillswitchState
urf_device_get_state (UrfDevice *device)
{
	g_return_val_if_fail (URF_IS_DEVICE (device), -1);

	if (URF_GET_DEVICE_CLASS (device)->get_state)
		return URF_GET_DEVICE_CLASS (device)->get_state (device);

	return -1;
}

/**
 * urf_device_set_state:
 **/
void
urf_device_set_state (UrfDevice *device, KillswitchState state)
{
	g_return_if_fail (URF_IS_DEVICE (device));

	if (URF_GET_DEVICE_CLASS (device)->set_state)
		URF_GET_DEVICE_CLASS (device)->set_state (device, state);
}

/**
 * urf_device_get_object_path:
 **/
const char *
urf_device_get_object_path (UrfDevice *device)
{
	UrfDevicePrivate *priv = URF_DEVICE_GET_PRIVATE (device);

	return priv->object_path;
}

/**
 * urf_device_is_platform:
 */
gboolean
urf_device_is_platform (UrfDevice *device)
{
	g_return_val_if_fail (URF_IS_DEVICE (device), FALSE);

	if (URF_GET_DEVICE_CLASS (device)->is_platform)
		return URF_GET_DEVICE_CLASS (device)->is_platform (device);

	return FALSE;
}

/**
 * urf_device_update_states:
 *
 * Return value: #TRUE if the states of the blocks are changed,
 *               otherwise #FALSE
 **/
gboolean
urf_device_update_states (UrfDevice      *device,
			  const gboolean  soft,
			  const gboolean  hard)
{
	if (URF_GET_DEVICE_CLASS (device)->update_states)
		return URF_GET_DEVICE_CLASS (device)->update_states (device, soft, hard);

	return FALSE;
}

/**
 * urf_device_get_property:
 **/
static void
urf_device_get_property (GObject    *object,
			 guint       prop_id,
			 GValue     *value,
			 GParamSpec *pspec)
{
	UrfDevice *device = URF_DEVICE (object);

	switch (prop_id) {
	case PROP_DEVICE_INDEX:
		g_value_set_int (value, urf_device_get_index (device));
		break;
	case PROP_DEVICE_TYPE:
		g_value_set_int (value, urf_device_get_device_type (device));
		break;
	case PROP_DEVICE_NAME:
		g_value_set_string (value, urf_device_get_name (device));
		break;
	case PROP_URF_TYPE:
		g_value_set_string (value, urf_device_get_urf_type (device));
		break;
	case PROP_DEVICE_PLATFORM:
		g_value_set_boolean (value, urf_device_is_platform (device));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject*
constructor (GType type,
             guint n_construct_params,
             GObjectConstructParam *construct_params)
{
	GObject *object;

	object = G_OBJECT_CLASS (urf_device_parent_class)->constructor (type,
	                         n_construct_params,
	                         construct_params);

	if (!object)
		return NULL;

	return object;
}

static void
constructed (GObject *object)
{
	if (G_OBJECT_CLASS (urf_device_parent_class)->constructed)
		G_OBJECT_CLASS (urf_device_parent_class)->constructed (object);
}

/**
 * urf_device_dispose:
 **/
static void
urf_device_dispose (GObject *object)
{
	UrfDevicePrivate *priv = URF_DEVICE_GET_PRIVATE (object);

	if (priv->introspection_data) {
		g_dbus_node_info_unref (priv->introspection_data);
		priv->introspection_data = NULL;
	}

	if (priv->connection) {
		if (priv->child_iface_id > 0)
			g_dbus_connection_unregister_object (priv->connection,
							     priv->child_iface_id);
		if (priv->dev_iface_id > 0)
			g_dbus_connection_unregister_object (priv->connection,
							     priv->dev_iface_id);
		priv->child_iface_id = 0;
		priv->dev_iface_id = 0;
		g_object_unref (priv->connection);
		priv->connection = NULL;
	}

	if (priv->introspection_data) {
		g_dbus_node_info_unref (priv->introspection_data);
		priv->introspection_data = NULL;
	}

	G_OBJECT_CLASS(urf_device_parent_class)->dispose(object);
}

/**
 * urf_device_finalize:
 **/
static void
urf_device_finalize (GObject *object)
{
	UrfDevicePrivate *priv = URF_DEVICE_GET_PRIVATE (object);

	if (priv->object_path)
		g_free (priv->object_path);

	G_OBJECT_CLASS(urf_device_parent_class)->finalize(object);
}

/**
 * urf_device_class_init:
 **/
static void
urf_device_class_init(UrfDeviceClass *class)
{
	GObjectClass *object_class = (GObjectClass *) class;
	GParamSpec *pspec;

	object_class->get_property = urf_device_get_property;
	object_class->dispose = urf_device_dispose;
	object_class->finalize = urf_device_finalize;
	object_class->constructor = constructor;
	object_class->constructed = constructed;

	signals[SIGNAL_CHANGED] =
		g_signal_new ("state-changed",
			      G_OBJECT_CLASS_TYPE (class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0, G_TYPE_NONE);

	pspec = g_param_spec_int ("index",
				   "Killswitch Index",
				   "The Index of the killswitch device",
				   -1, G_MAXINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class,
					 PROP_DEVICE_INDEX,
					 pspec);

	pspec = g_param_spec_int ("type",
				  "Killswitch Type",
				  "The type of the killswitch device",
				  -1, NUM_RFKILL_TYPES, 0,
				  G_PARAM_READABLE);
	g_object_class_install_property (object_class,
					 PROP_DEVICE_TYPE,
					 pspec);

	pspec = g_param_spec_string ("urftype",
				     "Device Type for urfkill",
				     "The device dbus interface",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class,
					 PROP_URF_TYPE,
					 pspec);

	pspec = g_param_spec_string ("name",
				     "Killswitch Name",
				     "The name of the killswitch device",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class,
					 PROP_DEVICE_NAME,
					 pspec);

	pspec = g_param_spec_boolean ("platform",
				      "Platform Driver",
				      "Whether the device is generated by a platform driver or not",
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class,
					 PROP_DEVICE_PLATFORM,
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
	UrfDevice *device = URF_DEVICE (user_data);
	GVariant *retval = NULL;

	if (g_strcmp0 (property_name, "index") == 0)
		retval = g_variant_new_int32 (urf_device_get_index (device));
	else if (g_strcmp0 (property_name, "type") == 0)
		retval = g_variant_new_int32 (urf_device_get_device_type (device));
	else if (g_strcmp0 (property_name, "state") == 0)
		retval = g_variant_new_int32 (urf_device_get_state (device));
	else if (g_strcmp0 (property_name, "urftype") == 0)
		retval = g_variant_new_string (urf_device_get_urf_type (device));
	else if (g_strcmp0 (property_name, "name") == 0)
		retval = g_variant_new_string (urf_device_get_name (device));
	else if (g_strcmp0 (property_name, "platform") == 0)
		retval = g_variant_new_boolean (urf_device_is_platform (device));

	return retval;
}

static const GDBusInterfaceVTable interface_vtable =
{
	NULL, /* handle method_call */
	handle_get_property,
	NULL, /* handle set_property */
};

/**
 * urf_device_compute_object_path:
 **/
static char *
urf_device_compute_object_path (UrfDevice *device)
{
	const char *path_template = "/org/freedesktop/URfkill/devices/%u";

	return g_strdup_printf (path_template, urf_device_get_index (device));
}

/**
 * urf_device_register_device:
 **/
gboolean
urf_device_register_device (UrfDevice *device, const GDBusInterfaceVTable vtable, const char *xml)
{
	UrfDevicePrivate *priv = URF_DEVICE_GET_PRIVATE (device);
	GDBusInterfaceInfo **infos;
	guint reg_id;
	GError *error = NULL;
	GString *introspection_xml;
	gchar *xml_data;

	introspection_xml = g_string_new ("<node>");
	introspection_xml = g_string_append (introspection_xml, introspection_generic);
	introspection_xml = g_string_append (introspection_xml, xml);
	introspection_xml = g_string_append (introspection_xml, "</node>");
	xml_data = g_string_free (introspection_xml, FALSE);

	priv->introspection_data = g_dbus_node_info_new_for_xml (xml_data, NULL);
	g_assert (priv->introspection_data != NULL);
	g_free (xml_data);

	priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (priv->connection == NULL) {
		g_error ("error getting system bus: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	error = NULL;

	priv->object_path = urf_device_compute_object_path (device);
	g_debug ("%s: priv->object_path: %s", __func__, priv->object_path);

	infos = priv->introspection_data->interfaces;

	reg_id = g_dbus_connection_register_object (priv->connection,
		                                    priv->object_path,
		                                    infos[0],
		                                    &interface_vtable,
		                                    device,
		                                    NULL,
		                                    &error);

	if (error != NULL)
		g_warning ("Error registering Device interface: %s", error->message);

	g_assert (reg_id > 0);

	priv->dev_iface_id = reg_id;

	reg_id = g_dbus_connection_register_object (priv->connection,
		                                    priv->object_path,
		                                    infos[1],
		                                    &vtable,
		                                    device,
		                                    NULL,
		                                    &error);
	if (error != NULL)
		g_warning ("Error registering Device interface: %s", error->message);

	g_assert (reg_id > 0);

	priv->child_iface_id = reg_id;

	return TRUE;
}

/**
 * urf_device_init:
 **/
static void
urf_device_init (UrfDevice *device)
{
	UrfDevicePrivate *priv = URF_DEVICE_GET_PRIVATE (device);

	priv->object_path = NULL;
}

