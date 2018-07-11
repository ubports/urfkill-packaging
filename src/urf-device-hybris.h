/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Canonical
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

#ifndef URF_DEVICE_HYBRIS_H
#define URF_DEVICE_HYBRIS_H

#include <glib-object.h>
#include "urf-device.h"
#include "urf-utils.h"

G_BEGIN_DECLS

#define URF_TYPE_DEVICE_HYBRIS (urf_device_hybris_get_type())
#define URF_DEVICE_HYBRIS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
				URF_TYPE_DEVICE, UrfDeviceHybris))
#define URF_DEVICE_HYBRIS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), \
                                       URF_TYPE_DEVICE, UrfDeviceHybrisClass))
#define URF_IS_DEVICE_HYBRIS(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
					URF_TYPE_DEVICE_HYBRIS))
#define URF_IS_DEVICE_HYBRIS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), \
                                          URF_TYPE_DEVICE_HYBRIS))
#define URF_GET_DEVICE_HYBRIS_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), \
				URF_TYPE_DEVICE_HYBRIS, UrfDeviceHybrisClass))

typedef struct _UrfDeviceHybrisPrivate UrfDeviceHybrisPrivate;

typedef struct {
        UrfDevice parent;
} UrfDeviceHybris;

typedef struct {
        UrfDeviceClass parent;
} UrfDeviceHybrisClass;


GType		 urf_device_hybris_get_type	(void);

UrfDevice	*urf_device_hybris_new		(void);

gchar		*urf_device_hybris_get_path	(UrfDeviceHybris *hybris);

G_END_DECLS

#endif /* URF_DEVICE_HYBRIS_H */
