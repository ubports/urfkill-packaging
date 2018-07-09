/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Gary Ching-Pang Lin <glin@suse.com>
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

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <libudev.h>
#include <linux/input.h>

#include <glib.h>

#define KEY_RELEASE 0
#define KEY_PRESS 1
#define KEY_KEEPING_PRESSED 2

#include "urf-input.h"

enum {
	RF_KEY_PRESSED,
	LAST_SIGNAL
};

static int signals[LAST_SIGNAL] = { 0 };

#define URF_INPUT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
                                URF_TYPE_INPUT, UrfInputPrivate))

struct UrfInputPrivate {
	int		 fd;
	guint		 watch_id;
	GIOChannel	*channel;
};

G_DEFINE_TYPE(UrfInput, urf_input, G_TYPE_OBJECT)

static const char *
match_hotkey_device (struct udev_device *dev)
{
	struct udev_device *input_dev;
	struct udev_device *platform_dev;
	const char *dev_name;

	/* We only need the input device from the platform subsystem. */
	platform_dev = udev_device_get_parent_with_subsystem_devtype(dev, "platform", 0);
	if (!platform_dev)
		return NULL;
	input_dev = udev_device_get_parent_with_subsystem_devtype(dev, "input", 0);
	if (!input_dev)
		return NULL;

	dev_name = udev_device_get_sysattr_value (input_dev, "name");

	return dev_name;
}

static gboolean
input_event_cb (GIOChannel   *source,
		GIOCondition  condition,
		UrfInput     *input)
{
	g_debug ("input event condition %x (%s%s%s%s )",
		 condition,
		 condition & G_IO_IN ? " G_IO_IN" : "",
		 condition & G_IO_ERR ? " G_IO_ERR" : "",
		 condition & G_IO_HUP ? " G_IO_HUP" : "",
		 condition & ~(G_IO_IN | G_IO_ERR | G_IO_HUP) ? " with extra state" : "");

	if (condition & G_IO_IN) {
		GIOStatus status;
		struct input_event event;
		gsize read;

		status = g_io_channel_read_chars (source,
						  (char *) &event,
						  sizeof(event),
						  &read,
						  NULL);

		while (status == G_IO_STATUS_NORMAL && read == sizeof(event)) {
			if (event.value == KEY_PRESS) {
				switch (event.code) {
				case KEY_WLAN:
				case KEY_BLUETOOTH:
				case KEY_UWB:
				case KEY_WIMAX:
#ifdef KEY_RFKILL
				case KEY_RFKILL:
#endif
					g_signal_emit (G_OBJECT (input),
						       signals[RF_KEY_PRESSED],
						       0,
						       event.code);
					break;
				default:
					break;
				}
			}

			status = g_io_channel_read_chars (source,
							  (char *) &event,
							  sizeof(event),
							  &read,
							  NULL);
		}
	} else {
		g_warning ("Failed to fetch the input event");
		return FALSE;
	}

	return TRUE;
}

static gboolean
input_dev_open_channel (UrfInput   *input,
			const char *dev_node)
{
	UrfInputPrivate *priv = input->priv;
	int fd;

	fd = open(dev_node, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		if (errno == EACCES)
			g_warning ("Could not open %s", dev_node);
		return FALSE;
	}

	/* Setup a channel for the device node */
	priv->fd = fd;
	priv->channel = g_io_channel_unix_new (priv->fd);
	g_io_channel_set_encoding (priv->channel, NULL, NULL);
	priv->watch_id = g_io_add_watch (priv->channel,
					 G_IO_IN | G_IO_HUP | G_IO_ERR,
					 (GIOFunc) input_event_cb,
					 input);
	g_debug ("Watch %s", dev_node);

	return TRUE;
}

/**
 * urf_input_startup:
 **/
gboolean
urf_input_startup (UrfInput *input)
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices;
	struct udev_list_entry *dev_list_entry;
	struct udev_device *dev;
	char *dev_node = NULL;
	gboolean ret;

	udev = udev_new ();
	if (!udev) {
		g_warning ("Cannot create udev object");
		return FALSE;
	}

	enumerate = udev_enumerate_new (udev);
	udev_enumerate_add_match_subsystem (enumerate, "input");
	udev_enumerate_add_match_property (enumerate, "ID_INPUT_KEY", "1");
	udev_enumerate_scan_devices (enumerate);
	devices = udev_enumerate_get_list_entry (enumerate);

	udev_list_entry_foreach (dev_list_entry, devices) {
		const char *path;
		const char *dev_name;

		path = udev_list_entry_get_name (dev_list_entry);
		dev = udev_device_new_from_syspath (udev, path);

		dev_name = match_hotkey_device (dev);
		if (!dev_name) {
			udev_device_unref(dev);
			continue;
		}

		g_free (dev_node);
		dev_node = g_strdup (udev_device_get_devnode (dev));
		if (!dev_node) {
			udev_device_unref(dev);
			continue;
		}

		/* Use the device from the platform driver if there is one. */
		if (g_strcmp0 (dev_name, "AT Translated Set 2 keyboard") != 0) {
			udev_device_unref(dev);
			break;
		}

		udev_device_unref(dev);
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	if (!dev_node)
		return FALSE;

	ret = input_dev_open_channel (input, dev_node);
	g_free (dev_node);

	return ret;
}

/**
 * urf_input_init:
 **/
static void
urf_input_init (UrfInput *input)
{
	input->priv = URF_INPUT_GET_PRIVATE (input);
	input->priv->fd = -1;
	input->priv->channel = NULL;
}

/**
 * urf_input_finalize:
 **/
static void
urf_input_finalize (GObject *object)
{
	UrfInputPrivate *priv = URF_INPUT_GET_PRIVATE (object);

	if (priv->fd > 0) {
		g_source_remove (priv->fd);
		g_io_channel_shutdown (priv->channel, FALSE, NULL);
		g_io_channel_unref (priv->channel);
		close (priv->fd);
		priv->fd = 0;
	}

	G_OBJECT_CLASS(urf_input_parent_class)->finalize(object);
}

/**
 * urf_input_class_init:
 **/
static void
urf_input_class_init(UrfInputClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;

	g_type_class_add_private(klass, sizeof(UrfInputPrivate));
	object_class->finalize = urf_input_finalize;

	signals[RF_KEY_PRESSED] =
		g_signal_new ("rf-key-pressed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (UrfInputClass, rf_key_pressed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * urf_input_new:
 **/
UrfInput *
urf_input_new (void)
{
	UrfInput *input;
	input = URF_INPUT(g_object_new (URF_TYPE_INPUT, NULL));
	return input;
}
