/* this file is part of evince, a gnome document viewer
 *
 *  Copyright (C) 2006 Carlos Garcia Campos <carlosgc@gnome.org>
 *
 * Evince is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Evince is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <glib/gstdio.h>
#include "ev-file-helpers.h"
#include "ev-image.h"

struct _EvImagePrivate {
	GdkPixbuf *pixbuf;
	gchar     *tmp_uri;
};

#define EV_IMAGE_GET_PRIVATE(object) \
                (G_TYPE_INSTANCE_GET_PRIVATE ((object), EV_TYPE_IMAGE, EvImagePrivate))

G_DEFINE_TYPE (EvImage, ev_image, G_TYPE_OBJECT)

static void
ev_image_finalize (GObject *object)
{
	EvImage *image = EV_IMAGE (object);

	if (image->priv->pixbuf) {
		g_object_unref (image->priv->pixbuf);
		image->priv->pixbuf = NULL;
	}

	if (image->priv->tmp_uri) {
		g_unlink (image->priv->tmp_uri);
		g_free (image->priv->tmp_uri);
		image->priv->tmp_uri = NULL;
	}

	(* G_OBJECT_CLASS (ev_image_parent_class)->finalize) (object);
}

static void
ev_image_class_init (EvImageClass *klass)
{
	GObjectClass *g_object_class;

	g_object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (g_object_class, sizeof (EvImagePrivate));

	g_object_class->finalize = ev_image_finalize;
}

static void
ev_image_init (EvImage *image)
{
	image->priv = EV_IMAGE_GET_PRIVATE (image);
}

EvImage *
ev_image_new_from_pixbuf (GdkPixbuf *pixbuf)
{
	EvImage *image;

	g_return_val_if_fail (GDK_IS_PIXBUF (pixbuf), NULL);

	image = EV_IMAGE (g_object_new (EV_TYPE_IMAGE, NULL));
	image->priv->pixbuf = g_object_ref (pixbuf);

	return image;
}

GdkPixbuf *
ev_image_get_pixbuf (EvImage *image)
{
	g_return_val_if_fail (EV_IS_IMAGE (image), NULL);
	g_return_val_if_fail (GDK_IS_PIXBUF (image->priv->pixbuf), NULL);

	return image->priv->pixbuf;
}

const gchar *
ev_image_save_tmp (EvImage *image)
{
	GError *error = NULL;
	
	g_return_val_if_fail (EV_IS_IMAGE (image), NULL);
	g_return_val_if_fail (GDK_IS_PIXBUF (image->priv->pixbuf), NULL);

	if (image->priv->tmp_uri)
		return image->priv->tmp_uri;

	image->priv->tmp_uri = ev_tmp_filename ("image");
	gdk_pixbuf_save (image->priv->pixbuf,
			 image->priv->tmp_uri, "png", &error,
			 "compression", "3", NULL);
	if (!error)
		return image->priv->tmp_uri;

	/* Erro saving image */
	g_warning (error->message);
	g_error_free (error);
	g_free (image->priv->tmp_uri);
	image->priv->tmp_uri = NULL;

	return NULL;
}

const gchar *
ev_image_get_tmp_uri (EvImage *image)
{
	g_return_val_if_fail (EV_IS_IMAGE (image), NULL);

	return image->priv->tmp_uri;
}

/* EvImageMapping */
static void
ev_image_mapping_free_foreach (EvImageMapping *mapping)
{
	g_object_unref (mapping->image);
	g_free (mapping);
}

void
ev_image_mapping_free (GList *image_mapping)
{
	if (!image_mapping)
		return;

	g_list_foreach (image_mapping, (GFunc) ev_image_mapping_free_foreach, NULL);
	g_list_free (image_mapping);
}

EvImage *
ev_image_mapping_find (GList   *image_mapping,
		       gdouble  x,
		       gdouble  y)
{
	GList *list;

	for (list = image_mapping; list; list = list->next) {
		EvImageMapping *mapping = list->data;

		if ((x >= mapping->x1) &&
		    (y >= mapping->y1) &&
		    (x <= mapping->x2) &&
		    (y <= mapping->y2)) {
			return mapping->image;
		}
	}

	return NULL;
}

