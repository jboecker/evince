/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; c-indent-level: 8 -*- */
/* this file is part of evince, a gnome document viewer
 *
 *  Copyright (C) 2004 Red Hat, Inc
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

#include "config.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "ev-mapping.h"
#include "ev-document-forms.h"
#include "ev-document-images.h"
#include "ev-document-links.h"
#include "ev-document-misc.h"
#include "ev-pixbuf-cache.h"
#include "ev-page-cache.h"
#include "ev-view-marshal.h"
#include "ev-document-annotations.h"
#include "ev-annotation-window.h"
#include "ev-view.h"
#include "ev-view-accessible.h"
#include "ev-view-private.h"
#include "ev-view-type-builtins.h"




#define EV_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EV_TYPE_VIEW, EvViewClass))
#define EV_IS_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EV_TYPE_VIEW))
#define EV_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), EV_TYPE_VIEW, EvViewClass))

enum {
	SIGNAL_BINDING_ACTIVATED,
	SIGNAL_HANDLE_LINK,
	SIGNAL_EXTERNAL_LINK,
	SIGNAL_POPUP_MENU,
	SIGNAL_SELECTION_CHANGED,
	N_SIGNALS
};

enum {
	TARGET_DND_URI,
	TARGET_DND_TEXT,
	TARGET_DND_IMAGE
};

static guint signals[N_SIGNALS];

typedef enum {
	EV_VIEW_FIND_NEXT,
	EV_VIEW_FIND_PREV
} EvViewFindDirection;

#define ZOOM_IN_FACTOR  1.2
#define ZOOM_OUT_FACTOR (1.0/ZOOM_IN_FACTOR)

#define SCROLL_TIME 150

/*** Scrolling ***/
static void       ev_view_set_scroll_adjustments             (GtkLayout          *layout,
							      GtkAdjustment      *hadjustment,
							      GtkAdjustment      *vadjustment);
static void       view_update_range_and_current_page         (EvView             *view);
static void       set_scroll_adjustment                      (EvView             *view,
							      GtkOrientation      orientation,
							      GtkAdjustment      *adjustment);
static void       add_scroll_binding_keypad                  (GtkBindingSet      *binding_set,
							      guint               keyval,
							      GdkModifierType modifiers,
							      GtkScrollType       scroll,
							      gboolean            horizontal);
static void       ensure_rectangle_is_visible                (EvView             *view,
							      GdkRectangle       *rect);

/*** Geometry computations ***/
static void       compute_border                             (EvView             *view,
							      int                 width,
							      int                 height,
							      GtkBorder          *border);
static void       get_page_y_offset                          (EvView             *view,
							      int                 page,
							      int                *y_offset);
static gboolean   get_page_extents                           (EvView             *view,
							      gint                page,
							      GdkRectangle       *page_area,
							      GtkBorder          *border);
static void       view_rect_to_doc_rect                      (EvView             *view,
							      GdkRectangle       *view_rect,
							      GdkRectangle       *page_area,
							      EvRectangle        *doc_rect);
static void       doc_rect_to_view_rect                      (EvView             *view,
							      int                 page,
							      EvRectangle        *doc_rect,
							      GdkRectangle       *view_rect);
static gboolean   get_dual_even_left                         (EvView             *view);
static void       find_page_at_location                      (EvView             *view,
							      gdouble             x,
							      gdouble             y,
							      gint               *page,
							      gint               *x_offset,
							      gint               *y_offset);
static void       doc_point_to_view_point 		     (EvView             *view,
				                              int                 page,
							      EvPoint            *doc_point,
					     	              GdkPoint           *view_point);
/*** Hyperrefs ***/
static EvLink *   ev_view_get_link_at_location 		     (EvView             *view,
				  	         	      gdouble             x,
		            				      gdouble             y);
static char*      tip_from_link                              (EvView             *view,
							      EvLink             *link);
/*** Forms ***/
static EvFormField *ev_view_get_form_field_at_location       (EvView             *view,
							       gdouble            x,
							       gdouble            y);

/*** Annotations ***/
static EvAnnotation *ev_view_get_annotation_at_location      (EvView             *view,
							      gdouble             x,
							      gdouble             y);
static void          show_annotation_windows                 (EvView             *view,
							      gint                page);
static void          hide_annotation_windows                 (EvView             *view,
							      gint                page);
/*** GtkWidget implementation ***/
static void       ev_view_size_request_continuous_dual_page  (EvView             *view,
							      GtkRequisition     *requisition);
static void       ev_view_size_request_continuous            (EvView             *view,
							      GtkRequisition     *requisition);
static void       ev_view_size_request_dual_page             (EvView             *view,
							      GtkRequisition     *requisition);
static void       ev_view_size_request_single_page           (EvView             *view,
							      GtkRequisition     *requisition);
static void       ev_view_size_request                       (GtkWidget          *widget,
							      GtkRequisition     *requisition);
static void       ev_view_size_allocate                      (GtkWidget          *widget,
							      GtkAllocation      *allocation);
static void       ev_view_realize                            (GtkWidget          *widget);
static gboolean   ev_view_scroll_event                       (GtkWidget          *widget,
							      GdkEventScroll     *event);
static gboolean   ev_view_expose_event                       (GtkWidget          *widget,
							      GdkEventExpose     *event);
static gboolean   ev_view_popup_menu                         (GtkWidget 	 *widget);
static gboolean   ev_view_button_press_event                 (GtkWidget          *widget,
							      GdkEventButton     *event);
static gboolean   ev_view_motion_notify_event                (GtkWidget          *widget,
							      GdkEventMotion     *event);
static gboolean   ev_view_button_release_event               (GtkWidget          *widget,
							      GdkEventButton     *event);
static gboolean   ev_view_enter_notify_event                 (GtkWidget          *widget,
							      GdkEventCrossing   *event);
static gboolean   ev_view_leave_notify_event                 (GtkWidget          *widget,
							      GdkEventCrossing   *event);
static void       ev_view_style_set                          (GtkWidget          *widget,
							      GtkStyle           *old_style);
static void       ev_view_remove_all                         (EvView             *view);

static AtkObject *ev_view_get_accessible                     (GtkWidget *widget);

/*** Drawing ***/
static guint32    ev_gdk_color_to_rgb                        (const GdkColor     *color);
static void       draw_rubberband                            (GtkWidget          *widget,
							      GdkWindow          *window,
							      const GdkRectangle *rect,
							      guchar              alpha);
static void       highlight_find_results                     (EvView             *view,
							      int                 page);
static void       draw_one_page                              (EvView             *view,
							      gint                page,
							      cairo_t            *cr,
							      GdkRectangle       *page_area,
							      GtkBorder          *border,
							      GdkRectangle       *expose_area,
							      gboolean		 *page_ready);
static void	  draw_loading_text 			     (EvView             *view,
							      GdkRectangle       *page_area,
							      GdkRectangle       *expose_area);
static void       ev_view_reload_page                        (EvView             *view,
							      gint                page,
							      GdkRegion          *region);

/*** Callbacks ***/
static void       ev_view_change_page                        (EvView             *view,
							      gint                new_page);
static void       job_finished_cb                            (EvPixbufCache      *pixbuf_cache,
							      GdkRegion          *region,
							      EvView             *view);
static void       ev_view_page_changed_cb                    (EvDocumentModel    *model,
							      gint                old_page,
							      gint                new_page,
							      EvView             *view);
static void       on_adjustment_value_changed                (GtkAdjustment      *adjustment,
							      EvView             *view);

/*** GObject ***/
static void       ev_view_finalize                           (GObject            *object);
static void       ev_view_destroy                            (GtkObject          *object);
static void       ev_view_class_init                         (EvViewClass        *class);
static void       ev_view_init                               (EvView             *view);

/*** Zoom and sizing ***/
static double   zoom_for_size_fit_width	 		     (gdouble doc_width,
							      gdouble doc_height,
	    						      int     target_width,
							      int     target_height);
static double   zoom_for_size_fit_height		     (gdouble doc_width,
			  				      gdouble doc_height,
							      int     target_width,
							      int     target_height);
static double	zoom_for_size_best_fit 			     (gdouble doc_width,
							      gdouble doc_height,
							      int     target_width,
							      int     target_height);
static void     ev_view_zoom_for_size                        (EvView *view,
							      int     width,
							      int     height);
static void	ev_view_zoom_for_size_continuous_and_dual_page (EvView *view,
							        int     width,
						     	        int     height);
static void	ev_view_zoom_for_size_continuous	       (EvView *view,
					    		        int     width,
								int     height);
static void 	ev_view_zoom_for_size_dual_page 	       (EvView *view,
						    		int     width,
								int     height);
static void	ev_view_zoom_for_size_single_page 	       (EvView *view,
				    			        int     width,
					    			int     height);
/*** Cursors ***/
static void       ev_view_set_cursor                         (EvView             *view,
							      EvViewCursor        new_cursor);
static void       ev_view_handle_cursor_over_xy              (EvView *view,
							      gint x,
							      gint y);

/*** Find ***/
static gint         ev_view_find_get_n_results               (EvView             *view,
							      gint                page);
static EvRectangle *ev_view_find_get_result                  (EvView             *view,
							      gint                page,
							      gint                result);
static void       jump_to_find_result                        (EvView             *view);
static void       jump_to_find_page                          (EvView             *view, 
							      EvViewFindDirection direction,
							      gint                shift);

/*** Selection ***/
static void       compute_selections                         (EvView             *view,
							      EvSelectionStyle    style,
							      GdkPoint           *start,
							      GdkPoint           *stop);
static void       clear_selection                            (EvView             *view);
static void       clear_link_selected                        (EvView             *view);
static void       selection_free                             (EvViewSelection    *selection);
static char*      get_selected_text                          (EvView             *ev_view);
static void       ev_view_primary_get_cb                     (GtkClipboard       *clipboard,
							      GtkSelectionData   *selection_data,
							      guint               info,
							      gpointer            data);
static void       ev_view_primary_clear_cb                   (GtkClipboard       *clipboard,
							      gpointer            data);
static void       ev_view_update_primary_selection           (EvView             *ev_view);

G_DEFINE_TYPE (EvView, ev_view, GTK_TYPE_LAYOUT)

/* HeightToPage cache */
#define EV_HEIGHT_TO_PAGE_CACHE_KEY "ev-height-to-page-cache"

static void
build_height_to_page (EvHeightToPageCache *cache,
		      EvDocument          *document,
		      gint                 rotation)
{
	gboolean swap, uniform, dual_even_left;
	int i;
	double uniform_height, page_height, next_page_height;
	double saved_height;
	gdouble u_width, u_height;
	gint n_pages;

	swap = (rotation == 90 || rotation == 270);

	uniform = ev_document_is_page_size_uniform (document);
	n_pages = ev_document_get_n_pages (document);
	dual_even_left = (n_pages > 2);

	g_free (cache->height_to_page);
	g_free (cache->dual_height_to_page);

	cache->rotation = rotation;
	cache->height_to_page = g_new0 (gdouble, n_pages + 1);
	cache->dual_height_to_page = g_new0 (gdouble, n_pages + 2);

	if (uniform)
		ev_document_get_page_size (document, 0, &u_width, &u_height);

	saved_height = 0;
	for (i = 0; i <= n_pages; i++) {
		if (uniform) {
			uniform_height = swap ? u_width : u_height;
			cache->height_to_page[i] = i * uniform_height;
		} else {
			if (i < n_pages) {
				gdouble w, h;

				ev_document_get_page_size (document, i, &w, &h);
				page_height = swap ? w : h;
			} else {
				page_height = 0;
			}
			cache->height_to_page[i] = saved_height;
			saved_height += page_height;
		}
	}

	if (dual_even_left && !uniform) {
		gdouble w, h;

		ev_document_get_page_size (document, 0, &w, &h);
		saved_height = swap ? w : h;
	} else {
		saved_height = 0;
	}

	for (i = dual_even_left; i < n_pages + 2; i += 2) {
    		if (uniform) {
			uniform_height = swap ? u_width : u_height;
			cache->dual_height_to_page[i] = ((i + dual_even_left) / 2) * uniform_height;
			if (i + 1 < n_pages + 2)
				cache->dual_height_to_page[i + 1] = ((i + dual_even_left) / 2) * uniform_height;
		} else {
			if (i + 1 < n_pages) {
				gdouble w, h;

				ev_document_get_page_size (document, i + 1, &w, &h);
				next_page_height = swap ? w : h;
			} else {
				next_page_height = 0;
			}

			if (i < n_pages) {
				gdouble w, h;

				ev_document_get_page_size (document, i, &w, &h);
				page_height = swap ? w : h;
			} else {
				page_height = 0;
			}

			if (i + 1 < n_pages + 2) {
				cache->dual_height_to_page[i] = saved_height;
				cache->dual_height_to_page[i + 1] = saved_height;
				saved_height += MAX(page_height, next_page_height);
			} else {
				cache->dual_height_to_page[i] = saved_height;
			}
		}
	}
}

static void
ev_height_to_page_cache_get_height (EvHeightToPageCache *cache,
				    EvDocument          *document,
				    gint                 page,
				    gint                 rotation,
				    gdouble             *height,
				    gdouble             *dual_height)
{
	if (cache->rotation != rotation)
		build_height_to_page (cache, document, rotation);

	*height = cache->height_to_page[page];
	*dual_height = cache->dual_height_to_page[page];
}

static void
ev_height_to_page_cache_free (EvHeightToPageCache *cache)
{
	if (cache->height_to_page) {
		g_free (cache->height_to_page);
		cache->height_to_page = NULL;
	}

	if (cache->dual_height_to_page) {
		g_free (cache->dual_height_to_page);
		cache->dual_height_to_page = NULL;
	}
	g_free (cache);
}

static EvHeightToPageCache *
ev_view_get_height_to_page_cache (EvView *view)
{
	EvHeightToPageCache *cache;

	if (!view->document)
		return NULL;

	cache = g_object_get_data (G_OBJECT (view->document), EV_HEIGHT_TO_PAGE_CACHE_KEY);
	if (!cache) {
		cache = g_new0 (EvHeightToPageCache, 1);
		build_height_to_page (cache, view->document, view->rotation);
		g_object_set_data_full (G_OBJECT (view->document),
					EV_HEIGHT_TO_PAGE_CACHE_KEY,
					cache,
					(GDestroyNotify)ev_height_to_page_cache_free);
	}

	return cache;
}

static void
ev_view_get_height_to_page (EvView *view,
			    gint    page,
			    gint   *height,
			    gint   *dual_height)
{
	gdouble h, dh;

	if (!view->height_to_page_cache)
		return;

	ev_height_to_page_cache_get_height (view->height_to_page_cache,
					    view->document,
					    page,
					    view->rotation,
					    &h, &dh);
	if (height)
		*height = (gint)(h * view->scale + 0.5);

	if (dual_height)
		*dual_height = (gint)(dh * view->scale + 0.5);
}

static gint
ev_view_get_scrollbar_size (EvView        *view,
			    GtkOrientation orientation)
{
	GtkWidget *widget = GTK_WIDGET (view);
	GtkWidget *sb;
	GtkWidget *swindow = gtk_widget_get_parent (GTK_WIDGET (view));
	GtkRequisition req;
	gint spacing;

	if (!GTK_IS_SCROLLED_WINDOW (swindow))
		return 0;

	if (orientation == GTK_ORIENTATION_VERTICAL) {
		if (widget->allocation.height >= widget->requisition.height)
			sb = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (swindow));
		else
			return 0;
	} else {
		if (widget->allocation.width >= widget->requisition.width)
			sb = gtk_scrolled_window_get_hscrollbar (GTK_SCROLLED_WINDOW (swindow));
		else
			return 0;
	}

	gtk_widget_style_get (swindow, "scrollbar_spacing", &spacing, NULL);
	gtk_widget_size_request (sb, &req);

	return (orientation == GTK_ORIENTATION_VERTICAL ? req.width : req.height) + spacing;
}

static void
scroll_to_point (EvView        *view,
		 gdouble        x,
		 gdouble        y,
		 GtkOrientation orientation)
{
	if (orientation == GTK_ORIENTATION_VERTICAL) {
		if (view->continuous) {
    			gtk_adjustment_clamp_page (view->vadjustment,
						   y - view->spacing / 2,
						   y + view->vadjustment->page_size);
		} else {
			gtk_adjustment_set_value (view->vadjustment,
						  CLAMP (y,
						  view->vadjustment->lower,
						  view->vadjustment->upper -
						  view->vadjustment->page_size));
		}
	} else {
		if (view->dual_page) {
			gtk_adjustment_clamp_page (view->hadjustment, x,
						   x + view->hadjustment->page_size);
		} else {
			gtk_adjustment_set_value (view->hadjustment,
						  CLAMP (x,
						  view->hadjustment->lower,
						  view->hadjustment->upper -
						  view->hadjustment->page_size));
		}
	}
}

static void
ev_view_scroll_to_page_position (EvView *view, GtkOrientation orientation)
{
	gdouble x, y;

	if (!view->document)
		return;

	if ((orientation == GTK_ORIENTATION_VERTICAL && view->pending_point.y == 0) ||
	    (orientation == GTK_ORIENTATION_HORIZONTAL && view->pending_point.x == 0)) {
		GdkRectangle page_area;
		GtkBorder    border;

		get_page_extents (view, view->current_page, &page_area, &border);
		x = page_area.x;
		y = page_area.y;
	} else {
		GdkPoint view_point;

		doc_point_to_view_point (view, view->current_page,
					 &view->pending_point, &view_point);
		x = view_point.x;
		y = view_point.y;
	}

	scroll_to_point (view, x, y, orientation);
}

static void
view_set_adjustment_values (EvView         *view,
			    GtkOrientation  orientation)
{
	GtkWidget *widget = GTK_WIDGET (view);
	GtkAdjustment *adjustment;
	int requisition;
	int allocation;

	double factor;
	gint new_value;

	if (orientation == GTK_ORIENTATION_HORIZONTAL)  {
		requisition = widget->requisition.width;
		allocation = widget->allocation.width;
		adjustment = view->hadjustment;
	} else {
		requisition = widget->requisition.height;
		allocation = widget->allocation.height;
		adjustment = view->vadjustment;
	}

	if (!adjustment)
		return;

	factor = 1.0;
	switch (view->pending_scroll) {
    	        case SCROLL_TO_KEEP_POSITION:
    	        case SCROLL_TO_FIND_LOCATION:
			factor = (adjustment->value) / adjustment->upper;
			break;
    	        case SCROLL_TO_PAGE_POSITION:
			break;
    	        case SCROLL_TO_CENTER:
			factor = (adjustment->value + adjustment->page_size * 0.5) / adjustment->upper;
			break;
	}

	adjustment->page_size = allocation;
	adjustment->step_increment = allocation * 0.1;
	adjustment->page_increment = allocation * 0.9;
	adjustment->lower = 0;
	adjustment->upper = MAX (allocation, requisition);

	/*
	 * We add 0.5 to the values before to average out our rounding errors.
	 */
	switch (view->pending_scroll) {
    	        case SCROLL_TO_KEEP_POSITION:
    	        case SCROLL_TO_FIND_LOCATION:
			new_value = CLAMP (adjustment->upper * factor + 0.5, 0, adjustment->upper - adjustment->page_size);
			gtk_adjustment_set_value (adjustment, (int)new_value);
			break;
    	        case SCROLL_TO_PAGE_POSITION:
			ev_view_scroll_to_page_position (view, orientation);
			break;
    	        case SCROLL_TO_CENTER:
			new_value = CLAMP (adjustment->upper * factor - adjustment->page_size * 0.5 + 0.5,
					   0, adjustment->upper - adjustment->page_size);
			gtk_adjustment_set_value (adjustment, (int)new_value);
			break;
	}

	gtk_adjustment_changed (adjustment);
}

static void
view_update_range_and_current_page (EvView *view)
{
	gint start = view->start_page;
	gint end = view->end_page;

	if (ev_document_get_n_pages (view->document) <= 0 ||
	    !ev_document_check_dimensions (view->document))
		return;

	if (view->continuous) {
		GdkRectangle current_area, unused, page_area;
		GtkBorder border;
		gboolean found = FALSE;
		gint area_max = -1, area;
		gint best_current_page = -1;
		int i, j = 0;

		if (!(view->vadjustment && view->hadjustment))
			return;

		current_area.x = view->hadjustment->value;
		current_area.width = view->hadjustment->page_size;
		current_area.y = view->vadjustment->value;
		current_area.height = view->vadjustment->page_size;

		for (i = 0; i < ev_document_get_n_pages (view->document); i++) {

			get_page_extents (view, i, &page_area, &border);

			if (gdk_rectangle_intersect (&current_area, &page_area, &unused)) {
				area = unused.width * unused.height;

				if (!found) {
					area_max = area;
					view->start_page = i;
					found = TRUE;
					best_current_page = i;
				}
				if (area > area_max) {
					best_current_page = (area == area_max) ? MIN (i, best_current_page) : i;
					area_max = area;
				}

				view->end_page = i;
				j = 0;
			} else if (found && view->current_page <= view->end_page) {
				if (view->dual_page && j < 1) {
					/* In dual mode  we stop searching
					 * after two consecutive non-visible pages.
					 */
					j++;
					continue;
				}
				break;
			}
		}

		if (view->pending_scroll == SCROLL_TO_KEEP_POSITION) {
			best_current_page = MAX (best_current_page, view->start_page);

			if (view->current_page != best_current_page) {
				view->current_page = best_current_page;
				ev_document_model_set_page (view->model, best_current_page);
			}
		}
	} else if (view->dual_page) {
		if (view->current_page % 2 == get_dual_even_left (view)) {
			view->start_page = view->current_page;
			if (view->current_page + 1 < ev_document_get_n_pages (view->document))
				view->end_page = view->start_page + 1;
			else
				view->end_page = view->start_page;
		} else {
			if (view->current_page < 1)
				view->start_page = view->current_page;
			else
				view->start_page = view->current_page - 1;
			view->end_page = view->current_page;
		}
	} else {
		view->start_page = view->current_page;
		view->end_page = view->current_page;
	}

	if (start != view->start_page || end != view->end_page) {
		gint i;

		for (i = start; i < view->start_page && start != -1; i++) {
			hide_annotation_windows (view, i);
		}

		for (i = end; i > view->end_page && end != -1; i--) {
			hide_annotation_windows (view, i);
		}
	}

	ev_page_cache_set_page_range (view->page_cache,
				      view->start_page,
				      view->end_page);
	ev_pixbuf_cache_set_page_range (view->pixbuf_cache,
					view->start_page,
					view->end_page,
					view->rotation,
					view->scale,
					view->selection_info.selections);

	if (ev_pixbuf_cache_get_surface (view->pixbuf_cache, view->current_page))
	    gtk_widget_queue_draw (GTK_WIDGET (view));
}

static void
set_scroll_adjustment (EvView *view,
		       GtkOrientation  orientation,
		       GtkAdjustment  *adjustment)
{
	GtkAdjustment **to_set;

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
		to_set = &view->hadjustment;
	else
		to_set = &view->vadjustment;

	if (*to_set != adjustment) {
		if (*to_set) {
			g_signal_handlers_disconnect_by_func (*to_set,
							      (gpointer) on_adjustment_value_changed,
							      view);
			g_object_unref (*to_set);
		}

		*to_set = adjustment;
		view_set_adjustment_values (view, orientation);

		if (*to_set) {
			g_object_ref (*to_set);
			g_signal_connect (*to_set, "value_changed",
					  G_CALLBACK (on_adjustment_value_changed), view);
		}
	}
}

static void
ev_view_set_scroll_adjustments (GtkLayout      *layout,
				GtkAdjustment  *hadjustment,
				GtkAdjustment  *vadjustment)
{
	EvView *view = EV_VIEW (layout);
	
	set_scroll_adjustment (view, GTK_ORIENTATION_HORIZONTAL, hadjustment);
	set_scroll_adjustment (view, GTK_ORIENTATION_VERTICAL, vadjustment);
	
	on_adjustment_value_changed (NULL, view);
}

static void
add_scroll_binding_keypad (GtkBindingSet  *binding_set,
    			   guint           keyval,
    			   GdkModifierType modifiers,
    			   GtkScrollType   scroll,
			   gboolean        horizontal)
{
  guint keypad_keyval = keyval - GDK_Left + GDK_KP_Left;

  gtk_binding_entry_add_signal (binding_set, keyval, modifiers,
                                "binding_activated", 2,
                                GTK_TYPE_SCROLL_TYPE, scroll,
				G_TYPE_BOOLEAN, horizontal);
  gtk_binding_entry_add_signal (binding_set, keypad_keyval, modifiers,
                                "binding_activated", 2,
                                GTK_TYPE_SCROLL_TYPE, scroll,
				G_TYPE_BOOLEAN, horizontal);
}

static gdouble
compute_scroll_increment (EvView        *view,
			  GtkScrollType  scroll)
{
	GtkWidget *widget = GTK_WIDGET (view);
	GtkAdjustment *adjustment = view->vadjustment;
	GdkRegion *text_region, *region;
	gint page;
	GdkRectangle rect;
	EvRectangle doc_rect;
	GdkRectangle page_area;
	GtkBorder border;
	GdkRectangle *recs;
	gint n_recs;
	gdouble fraction = 1.0;

	if (scroll != GTK_SCROLL_PAGE_BACKWARD && scroll != GTK_SCROLL_PAGE_FORWARD)
		return adjustment->page_size;

	page = scroll == GTK_SCROLL_PAGE_BACKWARD ? view->start_page : view->end_page;

	text_region = ev_page_cache_get_text_mapping (view->page_cache, page);
	if (!text_region || gdk_region_empty (text_region))
		return adjustment->page_size;

	get_page_extents (view, page, &page_area, &border);
	rect.x = page_area.x + view->scroll_x;
	rect.y = view->scroll_y + (scroll == GTK_SCROLL_PAGE_BACKWARD ? 5 : widget->allocation.height - 5);
	rect.width = page_area.width;
	rect.height = 1;
	view_rect_to_doc_rect (view, &rect, &page_area, &doc_rect);

	/* Convert the doc rectangle into a GdkRectangle */
	rect.x = doc_rect.x1;
	rect.y = doc_rect.y1;
	rect.width = doc_rect.x2 - doc_rect.x1;
	rect.height = MAX (1, doc_rect.y2 - doc_rect.y1);
	region = gdk_region_rectangle (&rect);

	gdk_region_intersect (region, text_region);
	gdk_region_get_rectangles (region, &recs, &n_recs);
	gdk_region_destroy (region);
	if (n_recs > 0) {
		EvRenderContext *rc;
		EvPage  *ev_page;

		ev_page = ev_document_get_page (view->document, page);
		rc = ev_render_context_new (ev_page, view->rotation, view->scale);
		g_object_unref (ev_page);
		/* Get the selection region to know the height of the line */
		doc_rect.x1 = doc_rect.x2 = recs[0].x + 0.5;
		doc_rect.y1 = doc_rect.y2 = recs[0].y + 0.5;

		ev_document_doc_mutex_lock ();
		region = ev_selection_get_selection_region (EV_SELECTION (view->document),
							    rc, EV_SELECTION_STYLE_LINE,
							    &doc_rect);
		ev_document_doc_mutex_unlock ();

		g_object_unref (rc);
		g_free (recs);
		gdk_region_get_rectangles (region, &recs, &n_recs);
		gdk_region_destroy (region);
		if (n_recs > 0) {
			fraction = 1 - (recs[0].height / adjustment->page_size);
		}
		g_free (recs);
	}

	return adjustment->page_size * fraction;

}

void
ev_view_scroll (EvView        *view,
	        GtkScrollType  scroll,
		gboolean       horizontal)
{
	GtkAdjustment *adjustment;
	double value, increment;
	gboolean first_page = FALSE;
	gboolean last_page = FALSE;

	view->jump_to_find_result = FALSE;

	if (view->sizing_mode == EV_SIZING_BEST_FIT) {
		switch (scroll) {
			case GTK_SCROLL_PAGE_BACKWARD:
			case GTK_SCROLL_STEP_BACKWARD:
				ev_view_previous_page (view);
				break;
			case GTK_SCROLL_PAGE_FORWARD:
			case GTK_SCROLL_STEP_FORWARD:
				ev_view_next_page (view);
				break;
			default:
				break;
		}
		return;
	}

	/* Assign values for increment and vertical adjustment */
	adjustment = horizontal ? view->hadjustment : view->vadjustment;
	value = adjustment->value;

	/* Assign boolean for first and last page */
	if (view->current_page == 0)
		first_page = TRUE;
	if (view->current_page == ev_document_get_n_pages (view->document) - 1)
		last_page = TRUE;

	switch (scroll) {
		case GTK_SCROLL_PAGE_BACKWARD:
			/* Do not jump backwards if at the first page */
			if (value == (adjustment->lower) && first_page) {
				/* Do nothing */
				/* At the top of a page, assign the upper bound limit of previous page */
			} else if (value == (adjustment->lower)) {
				value = adjustment->upper - adjustment->page_size;
				ev_view_previous_page (view);
				/* Jump to the top */
			} else {
				increment = compute_scroll_increment (view, GTK_SCROLL_PAGE_BACKWARD);
				value = MAX (value - increment, adjustment->lower);
			}
			break;
		case GTK_SCROLL_PAGE_FORWARD:
			/* Do not jump forward if at the last page */
			if (value == (adjustment->upper - adjustment->page_size) && last_page) {
				/* Do nothing */
			/* At the bottom of a page, assign the lower bound limit of next page */
			} else if (value == (adjustment->upper - adjustment->page_size)) {
				value = 0;
				ev_view_next_page (view);
			/* Jump to the bottom */
			} else {
				increment = compute_scroll_increment (view, GTK_SCROLL_PAGE_FORWARD);
				value = MIN (value + increment, adjustment->upper - adjustment->page_size);
			}
			break;
	        case GTK_SCROLL_STEP_BACKWARD:
			value -= adjustment->step_increment;
			break;
	        case GTK_SCROLL_STEP_FORWARD:
			value += adjustment->step_increment;
			break;
        	case GTK_SCROLL_STEP_DOWN:
			value -= adjustment->step_increment / 10;
			break;
        	case GTK_SCROLL_STEP_UP:
			value += adjustment->step_increment / 10;
			break;
        	default:
			break;
	}

	value = CLAMP (value, adjustment->lower,
		       adjustment->upper - adjustment->page_size);	

	gtk_adjustment_set_value (adjustment, value);
}

#define MARGIN 5

static void
ensure_rectangle_is_visible (EvView *view, GdkRectangle *rect)
{
	GtkWidget *widget = GTK_WIDGET (view);
	GtkAdjustment *adjustment;
	int value;

	view->pending_scroll = SCROLL_TO_FIND_LOCATION;

	adjustment = view->vadjustment;

	if (rect->y < adjustment->value) {
		value = MAX (adjustment->lower, rect->y - MARGIN);
		gtk_adjustment_set_value (view->vadjustment, value);
	} else if (rect->y + rect->height >
		   adjustment->value + widget->allocation.height) {
		value = MIN (adjustment->upper, rect->y + rect->height -
			     widget->allocation.height + MARGIN);
		gtk_adjustment_set_value (view->vadjustment, value);
	}

	adjustment = view->hadjustment;

	if (rect->x < adjustment->value) {
		value = MAX (adjustment->lower, rect->x - MARGIN);
		gtk_adjustment_set_value (view->hadjustment, value);
	} else if (rect->x + rect->height >
		   adjustment->value + widget->allocation.width) {
		value = MIN (adjustment->upper, rect->x + rect->width -
			     widget->allocation.width + MARGIN);
		gtk_adjustment_set_value (view->hadjustment, value);
	}

	gtk_widget_queue_resize (GTK_WIDGET (view));
}

/*** Geometry computations ***/

static void
compute_border (EvView *view, int width, int height, GtkBorder *border)
{
	ev_document_misc_get_page_border_size (width, height, border);
}

void
_get_page_size_for_scale_and_rotation (EvDocument *document,
				       gint        page,
				       gdouble     scale,
				       gint        rotation,
				       gint       *page_width,
				       gint       *page_height)
{
	gdouble w, h;
	gint    width, height;

	ev_document_get_page_size (document, page, &w, &h);

	width = (gint)(w * scale + 0.5);
	height = (gint)(h * scale + 0.5);

	if (page_width)
		*page_width = (rotation == 0 || rotation == 180) ? width : height;
	if (page_height)
		*page_height = (rotation == 0 || rotation == 180) ? height : width;
}

static void
ev_view_get_page_size (EvView *view,
		       gint    page,
		       gint   *page_width,
		       gint   *page_height)
{
	_get_page_size_for_scale_and_rotation (view->document,
					       page,
					       view->scale,
					       view->rotation,
					       page_width,
					       page_height);
}

static void
ev_view_get_max_page_size (EvView *view,
			   gint   *max_width,
			   gint   *max_height)
{
	double w, h;
	gint   width, height;

	ev_document_get_max_page_size (view->document, &w, &h);

	width = (gint)(w * view->scale + 0.5);
	height = (gint)(h * view->scale + 0.5);

	if (max_width)
		*max_width = (view->rotation == 0 || view->rotation == 180) ? width : height;
	if (max_height)
		*max_height = (view->rotation == 0 || view->rotation == 180) ? height : width;
}

static void
get_page_y_offset (EvView *view, int page, int *y_offset)
{
	int max_width, offset;
	GtkBorder border;

	g_return_if_fail (y_offset != NULL);

	ev_view_get_max_page_size (view, &max_width, NULL);
	compute_border (view, max_width, max_width, &border);

	if (view->dual_page) {
		ev_view_get_height_to_page (view, page, NULL, &offset);
		offset += ((page + get_dual_even_left (view)) / 2 + 1) * view->spacing +
			((page + get_dual_even_left (view)) / 2 ) * (border.top + border.bottom);
	} else {
		ev_view_get_height_to_page (view, page, &offset, NULL);
		offset += (page + 1) * view->spacing + page * (border.top + border.bottom);
	}

	*y_offset = offset;
	return;
}

static gboolean
get_page_extents (EvView       *view,
		  gint          page,
		  GdkRectangle *page_area,
		  GtkBorder    *border)
{
	GtkWidget *widget;
	int width, height;

	widget = GTK_WIDGET (view);

	/* Get the size of the page */
	ev_view_get_page_size (view, page, &width, &height);
	compute_border (view, width, height, border);
	page_area->width = width + border->left + border->right;
	page_area->height = height + border->top + border->bottom;

	if (view->continuous) {
		gint max_width;
		gint x, y;

		ev_view_get_max_page_size (view, &max_width, NULL);
		max_width = max_width + border->left + border->right;
		/* Get the location of the bounding box */
		if (view->dual_page) {
			x = view->spacing + ((page % 2 == get_dual_even_left (view)) ? 0 : 1) * (max_width + view->spacing);
			x = x + MAX (0, widget->allocation.width - (max_width * 2 + view->spacing * 3)) / 2;
			if (page % 2 == get_dual_even_left (view))
				x = x + (max_width - width - border->left - border->right);
		} else {
			x = view->spacing;
			x = x + MAX (0, widget->allocation.width - (width + view->spacing * 2)) / 2;
		}

		get_page_y_offset (view, page, &y);

		page_area->x = x;
		page_area->y = y;
	} else {
		gint x, y;
		if (view->dual_page) {
			gint width_2, height_2;
			gint max_width = width;
			gint max_height = height;
			GtkBorder overall_border;
			gint other_page;

			other_page = (page % 2 == get_dual_even_left (view)) ? page + 1: page - 1;

			/* First, we get the bounding box of the two pages */
			if (other_page < ev_document_get_n_pages (view->document)
			    && (0 <= other_page)) {
				ev_view_get_page_size (view, other_page,
						       &width_2, &height_2);
				if (width_2 > width)
					max_width = width_2;
				if (height_2 > height)
					max_height = height_2;
			}
			compute_border (view, max_width, max_height, &overall_border);

			/* Find the offsets */
			x = view->spacing;
			y = view->spacing;

			/* Adjust for being the left or right page */
			if (page % 2 == get_dual_even_left (view))
				x = x + max_width - width;
			else
				x = x + (max_width + overall_border.left + overall_border.right) + view->spacing;

			y = y + (max_height - height)/2;

			/* Adjust for extra allocation */
			x = x + MAX (0, widget->allocation.width -
				     ((max_width + overall_border.left + overall_border.right) * 2 + view->spacing * 3))/2;
			y = y + MAX (0, widget->allocation.height - (height + view->spacing * 2))/2;
		} else {
			x = view->spacing;
			y = view->spacing;

			/* Adjust for extra allocation */
			x = x + MAX (0, widget->allocation.width - (width + border->left + border->right + view->spacing * 2))/2;
			y = y + MAX (0, widget->allocation.height - (height + border->top + border->bottom +  view->spacing * 2))/2;
		}

		page_area->x = x;
		page_area->y = y;
	}

	return TRUE;
}

static void
get_doc_page_size (EvView  *view,
		   gint     page,
		   gdouble *width,
		   gdouble *height)
{
	double w, h;

	ev_document_get_page_size (view->document, page, &w, &h);
	if (view->rotation == 0 || view->rotation == 180) {
		if (width) *width = w;
		if (height) *height = h;
	} else {
		if (width) *width = h;
		if (height) *height = w;
	}
}

static void
view_point_to_doc_point (EvView *view,
			 GdkPoint *view_point,
			 GdkRectangle *page_area,
			 double  *doc_point_x,
			 double  *doc_point_y)
{
	*doc_point_x = (double) (view_point->x - page_area->x) / view->scale;
	*doc_point_y = (double) (view_point->y - page_area->y) / view->scale;
}

static void
view_rect_to_doc_rect (EvView *view,
		       GdkRectangle *view_rect,
		       GdkRectangle *page_area,
		       EvRectangle  *doc_rect)
{
	doc_rect->x1 = (double) (view_rect->x - page_area->x) / view->scale;
	doc_rect->y1 = (double) (view_rect->y - page_area->y) / view->scale;
	doc_rect->x2 = doc_rect->x1 + (double) view_rect->width / view->scale;
	doc_rect->y2 = doc_rect->y1 + (double) view_rect->height / view->scale;
}

static void
doc_point_to_view_point (EvView       *view,
                         int           page,
		         EvPoint      *doc_point,
		         GdkPoint     *view_point)
{
	GdkRectangle page_area;
	GtkBorder border;
	double x, y, view_x, view_y;
	gdouble width, height;

	get_doc_page_size (view, page, &width, &height);

	if (view->rotation == 0) {
		x = doc_point->x;
		y = doc_point->y;
	} else if (view->rotation == 90) {
		x = width - doc_point->y;
		y = doc_point->x;
	} else if (view->rotation == 180) {
		x = width - doc_point->x;
		y = height - doc_point->y;
	} else if (view->rotation == 270) {
		x = doc_point->y;
		y = height - doc_point->x;
	} else {
		g_assert_not_reached ();
	}

	get_page_extents (view, page, &page_area, &border);

	view_x = CLAMP (x * view->scale, 0, page_area.width);
	view_y = CLAMP (y * view->scale, 0, page_area.height);
	view_point->x = view_x + page_area.x;
	view_point->y = view_y + page_area.y;
}

static void
doc_rect_to_view_rect (EvView       *view,
                       int           page,
		       EvRectangle  *doc_rect,
		       GdkRectangle *view_rect)
{
	GdkRectangle page_area;
	GtkBorder border;
	double x, y, w, h;
	gdouble width, height;

	get_doc_page_size (view, page, &width, &height);

	if (view->rotation == 0) {
		x = doc_rect->x1;
		y = doc_rect->y1;
		w = doc_rect->x2 - doc_rect->x1;
		h = doc_rect->y2 - doc_rect->y1;
	} else if (view->rotation == 90) {
		x = width - doc_rect->y2;
		y = doc_rect->x1;
		w = doc_rect->y2 - doc_rect->y1;
		h = doc_rect->x2 - doc_rect->x1;
	} else if (view->rotation == 180) {
		x = width - doc_rect->x2;
		y = height - doc_rect->y2;
		w = doc_rect->x2 - doc_rect->x1;
		h = doc_rect->y2 - doc_rect->y1;
	} else if (view->rotation == 270) {
		x = doc_rect->y1;
		y = height - doc_rect->x2;
		w = doc_rect->y2 - doc_rect->y1;
		h = doc_rect->x2 - doc_rect->x1;
	} else {
		g_assert_not_reached ();
	}

	get_page_extents (view, page, &page_area, &border);

	view_rect->x = x * view->scale + page_area.x;
	view_rect->y = y * view->scale + page_area.y;
	view_rect->width = w * view->scale;
	view_rect->height = h * view->scale;
}

static gboolean
get_dual_even_left (EvView *view)
{
	return (ev_document_get_n_pages (view->document) > 2);
}

static void
find_page_at_location (EvView  *view,
		       gdouble  x,
		       gdouble  y,
		       gint    *page,
		       gint    *x_offset,
		       gint    *y_offset)
{
	int i;

	if (view->document == NULL)
		return;

	g_assert (page);
	g_assert (x_offset);
	g_assert (y_offset);

	for (i = view->start_page; i >= 0 && i <= view->end_page; i++) {
		GdkRectangle page_area;
		GtkBorder border;

		if (! get_page_extents (view, i, &page_area, &border))
			continue;

		if ((x >= page_area.x + border.left) &&
		    (x < page_area.x + page_area.width - border.right) &&
		    (y >= page_area.y + border.top) &&
		    (y < page_area.y + page_area.height - border.bottom)) {
			*page = i;
			*x_offset = x - (page_area.x + border.left);
			*y_offset = y - (page_area.y + border.top);
			return;
		}
	}

	*page = -1;
}

static gboolean
location_in_text (EvView  *view,
		  gdouble  x,
		  gdouble  y)
{
	GdkRegion *region;
	gint page = -1;
	gint x_offset = 0, y_offset = 0;

	find_page_at_location (view, x, y, &page, &x_offset, &y_offset);

	if (page == -1)
		return FALSE;
	
	region = ev_page_cache_get_text_mapping (view->page_cache, page);

	if (region)
		return gdk_region_point_in (region, x_offset / view->scale, y_offset / view->scale);
	else
		return FALSE;
}

static int
ev_view_get_width (EvView *view)
{
	return GTK_WIDGET (view)->allocation.width;
}

static int
ev_view_get_height (EvView *view)
{
	return GTK_WIDGET (view)->allocation.height;
}

static gboolean
location_in_selected_text (EvView  *view,
			   gdouble  x,
			   gdouble  y)
{
	gint page = -1;
	gint x_offset = 0, y_offset = 0;
	EvViewSelection *selection;
	GList *l = NULL;

	for (l = view->selection_info.selections; l != NULL; l = l->next) {
		selection = (EvViewSelection *)l->data;
		
		find_page_at_location (view, x, y, &page, &x_offset, &y_offset);
		
		if (page != selection->page)
			continue;
		
		if (selection->covered_region &&
		    gdk_region_point_in (selection->covered_region, x_offset, y_offset))
			return TRUE;
	}

	return FALSE;
}

static gboolean
get_doc_point_from_offset (EvView *view, 
			   gint    page, 
			   gint    x_offset, 
			   gint    y_offset, 
			   gint   *x_new, 
			   gint   *y_new)
{
        gdouble width, height;
	double x, y;

	get_doc_page_size (view, page, &width, &height);

	x_offset = x_offset / view->scale;
	y_offset = y_offset / view->scale;

        if (view->rotation == 0) {
                x = x_offset;
                y = y_offset;
        } else if (view->rotation == 90) {
                x = y_offset;
                y = width - x_offset;
        } else if (view->rotation == 180) {
                x = width - x_offset;
                y = height - y_offset;
        } else if (view->rotation == 270) {
                x = height - y_offset; 
                y = x_offset;
        } else {
                g_assert_not_reached ();
        }

	*x_new = x;
	*y_new = y;
	
	return TRUE;
}

static gboolean
get_doc_point_from_location (EvView  *view,
			     gdouble  x,
			     gdouble  y,
			     gint    *page,
			     gint    *x_new,
			     gint    *y_new)
{
	gint x_offset = 0, y_offset = 0;

	x += view->scroll_x;
	y += view->scroll_y;
	find_page_at_location (view, x, y, page, &x_offset, &y_offset);
	if (*page == -1)
		return FALSE;

	return get_doc_point_from_offset (view, *page, x_offset, y_offset, x_new, y_new);
}

static void
ev_view_get_area_from_mapping (EvView       *view,
			       guint         page,
			       GList        *mapping_list,
			       gconstpointer data,
			       GdkRectangle *area)
{
	EvMapping *mapping;

	mapping = ev_mapping_list_find (mapping_list, data);
	doc_rect_to_view_rect (view, page, &mapping->area, area);
	area->x -= view->scroll_x;
	area->y -= view->scroll_y;
}


/*** Hyperref ***/
static EvLink *
ev_view_get_link_at_location (EvView  *view,
  	         	      gdouble  x,
		              gdouble  y)
{
	gint page = -1;
	gint x_new = 0, y_new = 0;
	GList *link_mapping;

	if (!EV_IS_DOCUMENT_LINKS (view->document))
		return NULL;

	if (!get_doc_point_from_location (view, x, y, &page, &x_new, &y_new))
		return NULL;

	link_mapping = ev_page_cache_get_link_mapping (view->page_cache, page);

	if (link_mapping)
		return ev_mapping_list_get_data (link_mapping, x_new, y_new);
	else
		return NULL;
}

static void
goto_fitr_dest (EvView *view, EvLinkDest *dest)
{
	EvPoint doc_point;
	gdouble zoom, left, top;
	gboolean change_left, change_top;

	left = ev_link_dest_get_left (dest, &change_left);
	top = ev_link_dest_get_top (dest, &change_top);

	zoom = zoom_for_size_best_fit (ev_link_dest_get_right (dest) - left,
				       ev_link_dest_get_bottom (dest) - top,
				       ev_view_get_width (view),
				       ev_view_get_height (view));

	ev_document_model_set_sizing_mode (view->model, EV_SIZING_FREE);
	ev_document_model_set_scale (view->model, zoom);

	doc_point.x = change_left ? left : 0;
	doc_point.y = change_top ? top : 0;
	view->pending_point = doc_point;

	ev_view_change_page (view, ev_link_dest_get_page (dest));
}

static void
goto_fitv_dest (EvView *view, EvLinkDest *dest)
{
	EvPoint doc_point;
	gdouble doc_width, doc_height;
	gint page;
	double zoom, left;
	gboolean change_left;

	page = ev_link_dest_get_page (dest);
	ev_document_get_page_size (view->document, page, &doc_width, &doc_height);

	left = ev_link_dest_get_left (dest, &change_left);
	doc_point.x = change_left ? left : 0;
	doc_point.y = 0;

	zoom = zoom_for_size_fit_height (doc_width - doc_point.x , doc_height,
					 ev_view_get_width (view),
				         ev_view_get_height (view));

	ev_document_model_set_sizing_mode (view->model, EV_SIZING_FREE);
	ev_document_model_set_scale (view->model, zoom);

	view->pending_point = doc_point;

	ev_view_change_page (view, page);
}

static void
goto_fith_dest (EvView *view, EvLinkDest *dest)
{
	EvPoint doc_point;
	gdouble doc_width, doc_height;
	gint page;
	gdouble zoom, top;
	gboolean change_top;

	page = ev_link_dest_get_page (dest);
	ev_document_get_page_size (view->document, page, &doc_width, &doc_height);

	top = ev_link_dest_get_top (dest, &change_top);

	doc_point.x = 0;
	doc_point.y = change_top ? top : 0;

	zoom = zoom_for_size_fit_width (doc_width, top,
					ev_view_get_width (view),
				        ev_view_get_height (view));

	ev_document_model_set_sizing_mode (view->model, EV_SIZING_FIT_WIDTH);
	ev_document_model_set_scale (view->model, zoom);

	view->pending_point = doc_point;

	ev_view_change_page (view, page);
}

static void
goto_fit_dest (EvView *view, EvLinkDest *dest)
{
	double zoom;
	gdouble doc_width, doc_height;
	int page;

	page = ev_link_dest_get_page (dest);
	ev_document_get_page_size (view->document, page, &doc_width, &doc_height);

	zoom = zoom_for_size_best_fit (doc_width, doc_height,
				       ev_view_get_width (view),
				       ev_view_get_height (view));

	ev_document_model_set_sizing_mode (view->model, EV_SIZING_BEST_FIT);
	ev_document_model_set_scale (view->model, zoom);

	ev_view_change_page (view, page);
}

static void
goto_xyz_dest (EvView *view, EvLinkDest *dest)
{
	EvPoint doc_point;
	gint page;
	gdouble zoom, left, top;
	gboolean change_zoom, change_left, change_top; 

	zoom = ev_link_dest_get_zoom (dest, &change_zoom);
	page = ev_link_dest_get_page (dest);

	if (change_zoom && zoom > 1) {
		ev_document_model_set_sizing_mode (view->model, EV_SIZING_FREE);
		ev_document_model_set_scale (view->model, zoom);
	}

	left = ev_link_dest_get_left (dest, &change_left);
	top = ev_link_dest_get_top (dest, &change_top);

	doc_point.x = change_left ? left : 0;
	doc_point.y = change_top ? top : 0;
	view->pending_point = doc_point;

	ev_view_change_page (view, page);
}

static void
goto_dest (EvView *view, EvLinkDest *dest)
{
	EvLinkDestType type;
	int page, n_pages, current_page;

	page = ev_link_dest_get_page (dest);
	n_pages = ev_document_get_n_pages (view->document);

	if (page < 0 || page >= n_pages)
		return;

	current_page = view->current_page;
	
	type = ev_link_dest_get_dest_type (dest);

	switch (type) {
	        case EV_LINK_DEST_TYPE_PAGE:
			ev_document_model_set_page (view->model, page);
			break;
	        case EV_LINK_DEST_TYPE_FIT:
			goto_fit_dest (view, dest);
			break;
	        case EV_LINK_DEST_TYPE_FITH:
			goto_fith_dest (view, dest);
			break;
	        case EV_LINK_DEST_TYPE_FITV:
			goto_fitv_dest (view, dest);
			break;
	        case EV_LINK_DEST_TYPE_FITR:
			goto_fitr_dest (view, dest);
			break;
	        case EV_LINK_DEST_TYPE_XYZ:
			goto_xyz_dest (view, dest);
			break;
	        case EV_LINK_DEST_TYPE_PAGE_LABEL:
			ev_document_model_set_page_by_label (view->model, ev_link_dest_get_page_label (dest));
			break;
	        default:
			g_assert_not_reached ();
	}

	if (current_page != view->current_page)
		ev_document_model_set_page (view->model, view->current_page);
}

static void
ev_view_goto_dest (EvView *view, EvLinkDest *dest)
{
	EvLinkDestType type;

	type = ev_link_dest_get_dest_type (dest);

	if (type == EV_LINK_DEST_TYPE_NAMED) {
		EvLinkDest  *dest2;	
		const gchar *named_dest;

		named_dest = ev_link_dest_get_named_dest (dest);
		dest2 = ev_document_links_find_link_dest (EV_DOCUMENT_LINKS (view->document),
							  named_dest);
		if (dest2) {
			goto_dest (view, dest2);
			g_object_unref (dest2);
		}

		return;
	}

	goto_dest (view, dest);
}
	
void
ev_view_handle_link (EvView *view, EvLink *link)
{
	EvLinkAction    *action = NULL;
	EvLinkActionType type;

	action = ev_link_get_action (link);
	if (!action)
		return;

	type = ev_link_action_get_action_type (action);

	switch (type) {
	        case EV_LINK_ACTION_TYPE_GOTO_DEST: {
			EvLinkDest *dest;
			
			g_signal_emit (view, signals[SIGNAL_HANDLE_LINK], 0, link);
		
			dest = ev_link_action_get_dest (action);
			ev_view_goto_dest (view, dest);
		}
			break;
	        case EV_LINK_ACTION_TYPE_GOTO_REMOTE:
	        case EV_LINK_ACTION_TYPE_EXTERNAL_URI:
	        case EV_LINK_ACTION_TYPE_LAUNCH:
	        case EV_LINK_ACTION_TYPE_NAMED:
			g_signal_emit (view, signals[SIGNAL_EXTERNAL_LINK], 0, action);
			break;
	}
}

static char *
tip_from_action_named (EvLinkAction *action)
{
	const gchar *name = ev_link_action_get_name (action);
	
	if (g_ascii_strcasecmp (name, "FirstPage") == 0) {
		return g_strdup (_("Go to first page"));
	} else if (g_ascii_strcasecmp (name, "PrevPage") == 0) {
		return g_strdup (_("Go to previous page"));
	} else if (g_ascii_strcasecmp (name, "NextPage") == 0) {
		return g_strdup (_("Go to next page"));
	} else if (g_ascii_strcasecmp (name, "LastPage") == 0) {
		return g_strdup (_("Go to last page"));
	} else if (g_ascii_strcasecmp (name, "GoToPage") == 0) {
		return g_strdup (_("Go to page"));
	} else if (g_ascii_strcasecmp (name, "Find") == 0) {
		return g_strdup (_("Find"));
	}
	
	return NULL;
}

static char *
tip_from_link (EvView *view, EvLink *link)
{
	EvLinkAction *action;
	EvLinkActionType type;
	char *msg = NULL;
	char *page_label;
	const char *title;

	action = ev_link_get_action (link);
	title = ev_link_get_title (link);
	
	if (!action)
		return title ? g_strdup (title) : NULL;
		
	type = ev_link_action_get_action_type (action);

	switch (type) {
	        case EV_LINK_ACTION_TYPE_GOTO_DEST:
			page_label = ev_document_links_get_dest_page_label (EV_DOCUMENT_LINKS (view->document),
									    ev_link_action_get_dest (action));
			if (page_label) {
    				msg = g_strdup_printf (_("Go to page %s"), page_label);
				g_free (page_label);
			}
			break;
	        case EV_LINK_ACTION_TYPE_GOTO_REMOTE:
			if (title) {
				msg = g_strdup_printf (_("Go to %s on file “%s”"), title,
						       ev_link_action_get_filename (action));
			} else {
				msg = g_strdup_printf (_("Go to file “%s”"),
						       ev_link_action_get_filename (action));
			}
			break;
	        case EV_LINK_ACTION_TYPE_EXTERNAL_URI:
			msg = g_strdup (ev_link_action_get_uri (action));
			break;
	        case EV_LINK_ACTION_TYPE_LAUNCH:
			msg = g_strdup_printf (_("Launch %s"),
					       ev_link_action_get_filename (action));
			break;
	        case EV_LINK_ACTION_TYPE_NAMED:
			msg = tip_from_action_named (action);
			break;
	        default:
			if (title)
				msg = g_strdup (title);
			break;
	}
	
	return msg;
}

static void
ev_view_handle_cursor_over_xy (EvView *view, gint x, gint y)
{
	EvLink       *link;
	EvFormField  *field;
	EvAnnotation *annot = NULL;

	if (view->cursor == EV_VIEW_CURSOR_HIDDEN)
		return;

	if (view->drag_info.in_drag) {
		if (view->cursor != EV_VIEW_CURSOR_DRAG)
			ev_view_set_cursor (view, EV_VIEW_CURSOR_DRAG);
		return;
	}

	if (view->scroll_info.autoscrolling) {
		if (view->cursor != EV_VIEW_CURSOR_AUTOSCROLL)
			ev_view_set_cursor (view, EV_VIEW_CURSOR_AUTOSCROLL);
		return;
	}

	link = ev_view_get_link_at_location (view, x, y);
        if (link) {
		ev_view_set_cursor (view, EV_VIEW_CURSOR_LINK);
	} else if ((field = ev_view_get_form_field_at_location (view, x, y))) {
		if (field->is_read_only) {
			if (view->cursor == EV_VIEW_CURSOR_LINK ||
			    view->cursor == EV_VIEW_CURSOR_IBEAM ||
			    view->cursor == EV_VIEW_CURSOR_DRAG)
				ev_view_set_cursor (view, EV_VIEW_CURSOR_NORMAL);
		} else if (EV_IS_FORM_FIELD_TEXT (field)) {
			ev_view_set_cursor (view, EV_VIEW_CURSOR_IBEAM);
		} else {
			ev_view_set_cursor (view, EV_VIEW_CURSOR_LINK);
		}
	} else if ((annot = ev_view_get_annotation_at_location (view, x, y))) {
		ev_view_set_cursor (view, EV_VIEW_CURSOR_LINK);
	} else if (location_in_text (view, x + view->scroll_x, y + view->scroll_y)) {
		ev_view_set_cursor (view, EV_VIEW_CURSOR_IBEAM);
	} else {
		if (view->cursor == EV_VIEW_CURSOR_LINK ||
		    view->cursor == EV_VIEW_CURSOR_IBEAM ||
		    view->cursor == EV_VIEW_CURSOR_DRAG ||
		    view->cursor == EV_VIEW_CURSOR_AUTOSCROLL)
			ev_view_set_cursor (view, EV_VIEW_CURSOR_NORMAL);
	}

	if (link || annot)
		g_object_set (view, "has-tooltip", TRUE, NULL);
}

/*** Images ***/
static EvImage *
ev_view_get_image_at_location (EvView  *view,
			       gdouble  x,
			       gdouble  y)
{
	gint page = -1;
	gint x_new = 0, y_new = 0;
	GList *image_mapping;

	if (!EV_IS_DOCUMENT_IMAGES (view->document))
		return NULL;

	if (!get_doc_point_from_location (view, x, y, &page, &x_new, &y_new))
		return NULL;

	image_mapping = ev_page_cache_get_image_mapping (view->page_cache, page);

	if (image_mapping)
		return ev_mapping_list_get_data (image_mapping, x_new, y_new);
	else
		return NULL;
}

/*** Forms ***/
static EvFormField *
ev_view_get_form_field_at_location (EvView  *view,
				    gdouble  x,
				    gdouble  y)
{
	gint page = -1;
	gint x_new = 0, y_new = 0;
	GList *forms_mapping;
	
	if (!EV_IS_DOCUMENT_FORMS (view->document))
		return NULL;

	if (!get_doc_point_from_location (view, x, y, &page, &x_new, &y_new))
		return NULL;

	forms_mapping = ev_page_cache_get_form_field_mapping (view->page_cache, page);

	if (forms_mapping)
		return ev_mapping_list_get_data (forms_mapping, x_new, y_new);
	else
		return NULL;
}

static GdkRegion *
ev_view_form_field_get_region (EvView      *view,
			       EvFormField *field)
{
	GdkRectangle view_area;
	GList       *forms_mapping;

	forms_mapping = ev_page_cache_get_form_field_mapping (view->page_cache,
							      field->page->index);
	ev_view_get_area_from_mapping (view, field->page->index,
				       forms_mapping,
				       field, &view_area);

	return gdk_region_rectangle (&view_area);
}

static gboolean
ev_view_forms_remove_widgets (EvView *view)
{
	ev_view_remove_all (view);

	return FALSE;
}

static void
ev_view_form_field_destroy (GtkWidget *widget,
			    EvView    *view)
{
	g_idle_add ((GSourceFunc)ev_view_forms_remove_widgets, view);
}

static GtkWidget *
ev_view_form_field_button_create_widget (EvView      *view,
					 EvFormField *field)
{
	EvFormFieldButton *field_button = EV_FORM_FIELD_BUTTON (field);
	GdkRegion         *field_region = NULL;
	
	switch (field_button->type) {
	        case EV_FORM_FIELD_BUTTON_PUSH:
			return NULL;
	        case EV_FORM_FIELD_BUTTON_CHECK:
  	        case EV_FORM_FIELD_BUTTON_RADIO: {
			gboolean  state;
			GList    *forms_mapping, *l;

			state = ev_document_forms_form_field_button_get_state (EV_DOCUMENT_FORMS (view->document),
									       field);

			/* FIXME: it actually depends on NoToggleToOff flags */
			if (field_button->type == EV_FORM_FIELD_BUTTON_RADIO &&
			    state && field_button->state)
				return NULL;
			
			field_region = ev_view_form_field_get_region (view, field);

			/* For radio buttons and checkbox buttons that are in a set
			 * we need to update also the region for the current selected item
			 */
			forms_mapping = ev_page_cache_get_form_field_mapping (view->page_cache,
									      field->page->index);
			for (l = forms_mapping; l; l = g_list_next (l)) {
				EvFormField *button = ((EvMapping *)(l->data))->data;
				GdkRegion   *button_region;

				if (button->id == field->id)
					continue;

				/* FIXME: only buttons in the same group should be updated */
				if (!EV_IS_FORM_FIELD_BUTTON (button) ||
				    EV_FORM_FIELD_BUTTON (button)->type != field_button->type ||
				    EV_FORM_FIELD_BUTTON (button)->state != TRUE)
					continue;

				button_region = ev_view_form_field_get_region (view, button);
				gdk_region_union (field_region, button_region);
				gdk_region_destroy (button_region);
			}
			
			ev_document_forms_form_field_button_set_state (EV_DOCUMENT_FORMS (view->document),
								       field, !state);
			field_button->state = !state;
		}
			break;
	}

	ev_view_reload_page (view, field->page->index, field_region);
	gdk_region_destroy (field_region);
	
	return NULL;
}

static void
ev_view_form_field_text_save (EvView    *view,
			      GtkWidget *widget)
{
	EvFormField *field;

	if (!view->document)
		return;
	
	field = g_object_get_data (G_OBJECT (widget), "form-field");
	
	if (field->changed) {
		EvFormFieldText *field_text = EV_FORM_FIELD_TEXT (field);
		GdkRegion       *field_region;

		field_region = ev_view_form_field_get_region (view, field);
		
		ev_document_forms_form_field_text_set_text (EV_DOCUMENT_FORMS (view->document),
							    field, field_text->text);
		field->changed = FALSE;
		ev_view_reload_page (view, field->page->index, field_region);
		gdk_region_destroy (field_region);
	}
}

static void
ev_view_form_field_text_changed (GtkWidget   *widget,
				 EvFormField *field)
{
	EvFormFieldText *field_text = EV_FORM_FIELD_TEXT (field);
	gchar           *text = NULL;

	if (GTK_IS_ENTRY (widget)) {
		text = g_strdup (gtk_entry_get_text (GTK_ENTRY (widget)));
	} else if (GTK_IS_TEXT_BUFFER (widget)) {
		GtkTextIter start, end;

		gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (widget), &start, &end);
		text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (widget),
						 &start, &end, FALSE);
	}

	if (!field_text->text ||
	    (field_text->text && g_ascii_strcasecmp (field_text->text, text) != 0)) {
		g_free (field_text->text);
		field_text->text = text;
		field->changed = TRUE;
	}
}

static GtkWidget *
ev_view_form_field_text_create_widget (EvView      *view,
				       EvFormField *field)
{
	EvFormFieldText *field_text = EV_FORM_FIELD_TEXT (field);
	GtkWidget       *text = NULL;
	gchar           *txt;

	txt = ev_document_forms_form_field_text_get_text (EV_DOCUMENT_FORMS (view->document),
							  field);

	switch (field_text->type) {
	        case EV_FORM_FIELD_TEXT_FILE_SELECT:
			/* TODO */
	        case EV_FORM_FIELD_TEXT_NORMAL:
			text = gtk_entry_new ();
			gtk_entry_set_has_frame (GTK_ENTRY (text), FALSE);
			gtk_entry_set_max_length (GTK_ENTRY (text), field_text->max_len);
			gtk_entry_set_visibility (GTK_ENTRY (text), !field_text->is_password);
			
			if (txt) {
				gtk_entry_set_text (GTK_ENTRY (text), txt);
				g_free (txt);
			}

			g_signal_connect (text, "changed",
					  G_CALLBACK (ev_view_form_field_text_changed),
					  field);
			g_signal_connect_after (text, "activate",
						G_CALLBACK (ev_view_form_field_destroy),
						view);
			break;
	        case EV_FORM_FIELD_TEXT_MULTILINE: {
			GtkTextBuffer *buffer;
		
			text = gtk_text_view_new ();
			buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text));
			
			if (txt) {
				gtk_text_buffer_set_text (buffer, txt, -1);
				g_free (txt);
			}
			
			g_signal_connect (buffer, "changed",
					  G_CALLBACK (ev_view_form_field_text_changed),
					  field);
		}
			break;
	}			

	g_object_weak_ref (G_OBJECT (text),
			   (GWeakNotify)ev_view_form_field_text_save,
			   view);

	return text;
}

static void
ev_view_form_field_choice_save (EvView    *view,
				GtkWidget *widget)
{
	EvFormField *field;

	if (!view->document)
		return;
	
	field = g_object_get_data (G_OBJECT (widget), "form-field");

	if (field->changed) {
		GList             *l;
		EvFormFieldChoice *field_choice = EV_FORM_FIELD_CHOICE (field);
		GdkRegion         *field_region;

		field_region = ev_view_form_field_get_region (view, field);

		if (field_choice->is_editable) {
			ev_document_forms_form_field_choice_set_text (EV_DOCUMENT_FORMS (view->document),
								      field, field_choice->text);
		} else {
			ev_document_forms_form_field_choice_unselect_all (EV_DOCUMENT_FORMS (view->document), field);
			for (l = field_choice->selected_items; l; l = g_list_next (l)) {
				ev_document_forms_form_field_choice_select_item (EV_DOCUMENT_FORMS (view->document),
										 field,
										 GPOINTER_TO_INT (l->data));
			}
		}
		field->changed = FALSE;
		ev_view_reload_page (view, field->page->index, field_region);
		gdk_region_destroy (field_region);
	}
}

static void
ev_view_form_field_choice_changed (GtkWidget   *widget,
				   EvFormField *field)
{
	EvFormFieldChoice *field_choice = EV_FORM_FIELD_CHOICE (field);
	
	if (GTK_IS_COMBO_BOX (widget)) {
		gint item;
		
		item = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));
		if (!field_choice->selected_items ||
		    GPOINTER_TO_INT (field_choice->selected_items->data) != item) {
			g_list_free (field_choice->selected_items);
			field_choice->selected_items = NULL;
			field_choice->selected_items = g_list_prepend (field_choice->selected_items,
								       GINT_TO_POINTER (item));
			field->changed = TRUE;
		}

		if (GTK_IS_COMBO_BOX_ENTRY (widget)) {
			gchar *text;
			
			text = gtk_combo_box_get_active_text (GTK_COMBO_BOX (widget));
			if (!field_choice->text ||
			    (field_choice->text && g_ascii_strcasecmp (field_choice->text, text) != 0)) {
				g_free (field_choice->text);
				field_choice->text = text;
				field->changed = TRUE;
			}
		}
	} else if (GTK_IS_TREE_SELECTION (widget)) {
		GtkTreeSelection *selection = GTK_TREE_SELECTION (widget);
		GtkTreeModel     *model;
		GList            *items, *l;
		
		items = gtk_tree_selection_get_selected_rows (selection, &model);
		g_list_free (field_choice->selected_items);
		field_choice->selected_items = NULL;

		for (l = items; l && l->data; l = g_list_next (l)) {
			GtkTreeIter  iter;
			GtkTreePath *path = (GtkTreePath *)l->data;
			gint         item;
			
			gtk_tree_model_get_iter (model, &iter, path);
			gtk_tree_model_get (model, &iter, 1, &item, -1);

			field_choice->selected_items = g_list_prepend (field_choice->selected_items,
								       GINT_TO_POINTER (item));

			gtk_tree_path_free (path);
		}

		g_list_free (items);

		field->changed = TRUE;
	}
}

static GtkWidget *
ev_view_form_field_choice_create_widget (EvView      *view,
					 EvFormField *field)
{
	EvFormFieldChoice *field_choice = EV_FORM_FIELD_CHOICE (field);
	GtkWidget         *choice;
	GtkTreeModel      *model;
	gint               n_items, i;
	gint               selected_item = 0;

	n_items = ev_document_forms_form_field_choice_get_n_items (EV_DOCUMENT_FORMS (view->document),
								   field);
	model = GTK_TREE_MODEL (gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT));
	for (i = 0; i < n_items; i++) {
		GtkTreeIter iter;
		gchar      *item;

		item = ev_document_forms_form_field_choice_get_item (EV_DOCUMENT_FORMS (view->document),
								     field, i);
		if (ev_document_forms_form_field_choice_is_item_selected (
			    EV_DOCUMENT_FORMS (view->document), field, i)) {
			selected_item = i;
			/* FIXME: we need a get_selected_items function in poppler */
			field_choice->selected_items = g_list_prepend (field_choice->selected_items,
								       GINT_TO_POINTER (i));
		}
		
		if (item) {
			gtk_list_store_append (GTK_LIST_STORE (model), &iter);
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    0, item,
					    1, i,
					    -1);
			g_free (item);
		}
	}

	if (field_choice->type == EV_FORM_FIELD_CHOICE_LIST) {
		GtkCellRenderer  *renderer;
		GtkWidget        *tree_view;
		GtkTreeSelection *selection;

		tree_view = gtk_tree_view_new_with_model (model);
		gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree_view), FALSE);

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
		if (field_choice->multi_select) {
			gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
		}

		/* TODO: set selected items */

		renderer = gtk_cell_renderer_text_new ();
		gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tree_view),
							     0,
							     "choix", renderer,
							     "text", 0,
							     NULL);

		choice = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (choice),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_AUTOMATIC);
		gtk_container_add (GTK_CONTAINER (choice), tree_view);
		gtk_widget_show (tree_view);

		g_signal_connect (selection, "changed",
				  G_CALLBACK (ev_view_form_field_choice_changed),
				  field);
		g_signal_connect_after (selection, "changed",
					G_CALLBACK (ev_view_form_field_destroy),
					view);
	} else if (field_choice->is_editable) { /* ComboBoxEntry */
		gchar *text;
		
		choice = gtk_combo_box_entry_new_with_model (model, 0);
		text = ev_document_forms_form_field_choice_get_text (EV_DOCUMENT_FORMS (view->document), field);
		if (text) {
			gtk_entry_set_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (choice))), text);
			g_free (text);
		}

		g_signal_connect (choice, "changed",
				  G_CALLBACK (ev_view_form_field_choice_changed),
				  field);
		g_signal_connect_after (GTK_BIN(choice)->child, "activate",
					G_CALLBACK (ev_view_form_field_destroy),
					view);
	} else { /* ComboBoxText */
		GtkCellRenderer *renderer;

		choice = gtk_combo_box_new_with_model (model);
		renderer = gtk_cell_renderer_text_new ();
		gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (choice),
					    renderer, TRUE);
		gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (choice),
						renderer,
						"text", 0,
						NULL);
		gtk_combo_box_set_active (GTK_COMBO_BOX (choice), selected_item);
		gtk_combo_box_popup (GTK_COMBO_BOX (choice));
		
		g_signal_connect (choice, "changed",
				  G_CALLBACK (ev_view_form_field_choice_changed),
				  field);
		g_signal_connect_after (choice, "changed",
					G_CALLBACK (ev_view_form_field_destroy),
					view);
	}

	g_object_unref (model);

	g_object_weak_ref (G_OBJECT (choice),
			   (GWeakNotify)ev_view_form_field_choice_save,
			   view);

	return choice;
}

static void
ev_view_handle_form_field (EvView      *view,
			   EvFormField *field,
			   gdouble      x,
			   gdouble      y)
{
	GtkWidget   *field_widget = NULL;
	GList       *form_field_mapping;
	GdkRectangle view_area;

	if (field->is_read_only)
		return;
	
	if (EV_IS_FORM_FIELD_BUTTON (field)) {
		field_widget = ev_view_form_field_button_create_widget (view, field);
	} else if (EV_IS_FORM_FIELD_TEXT (field)) {
		field_widget = ev_view_form_field_text_create_widget (view, field);
	} else if (EV_IS_FORM_FIELD_CHOICE (field)) {
		field_widget = ev_view_form_field_choice_create_widget (view, field);
	} else if (EV_IS_FORM_FIELD_SIGNATURE (field)) {
		/* TODO */
	}

	/* Form field doesn't require a widget */
	if (!field_widget)
		return;

	g_object_set_data_full (G_OBJECT (field_widget), "form-field",
				g_object_ref (field),
				(GDestroyNotify)g_object_unref);

	form_field_mapping = ev_page_cache_get_form_field_mapping (view->page_cache,
								   field->page->index);
	ev_view_get_area_from_mapping (view, field->page->index,
				       form_field_mapping,
				       field, &view_area);

	gtk_layout_put (GTK_LAYOUT (view), field_widget, view_area.x, view_area.y);
	gtk_widget_show (field_widget);
	gtk_widget_grab_focus (field_widget);
}

/* Annotations */
static EvViewWindowChild *
ev_view_get_window_child (EvView    *view,
			  GtkWidget *window)
{
	GList *children = view->window_children;

	while (children) {
		EvViewWindowChild *child;

		child = (EvViewWindowChild *)children->data;
		children = children->next;

		if (child->window == window)
			return child;
	}

	return NULL;
}

static void
ev_view_window_child_move (EvView            *view,
			   EvViewWindowChild *child,
			   gint               x,
			   gint               y)
{
	child->x = x;
	child->y = y;
	gtk_window_move (GTK_WINDOW (child->window),
			 MAX (child->parent_x, x),
			 MAX (child->parent_y, y));
}

static void
ev_view_window_child_move_with_parent (EvView    *view,
				       GtkWidget *window)
{
	EvViewWindowChild *child;
	gint               root_x, root_y;

	child = ev_view_get_window_child (view, window);
	gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (view)),
			       &root_x, &root_y);
	if (root_x != child->parent_x || root_y != child->parent_y) {
		gint dest_x, dest_y;

		dest_x = child->x + (root_x - child->parent_x);
		dest_y = child->y + (root_y - child->parent_y);
		child->parent_x = root_x;
		child->parent_y = root_y;
		ev_view_window_child_move (view, child, dest_x, dest_y);
	}

	if (child->visible && !GTK_WIDGET_VISIBLE (window))
		gtk_widget_show (window);
}

static void
ev_view_window_child_put (EvView    *view,
			  GtkWidget *window,
			  guint      page,
			  gint       x,
			  gint       y,
			  gdouble    orig_x,
			  gdouble    orig_y)
{
	EvViewWindowChild *child;
	gint               root_x, root_y;

	gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (view)),
			       &root_x, &root_y);

	child = g_new0 (EvViewWindowChild, 1);
	child->window = window;
	child->page = page;
	child->orig_x = orig_x;
	child->orig_y = orig_y;
	child->parent_x = root_x;
	child->parent_y = root_y;
	child->visible = ev_annotation_window_is_open (EV_ANNOTATION_WINDOW (window));
	ev_view_window_child_move (view, child, x + root_x, y + root_y);

	if (child->visible)
		gtk_widget_show (window);
	else
		gtk_widget_hide (window);

	view->window_children = g_list_append (view->window_children, child);
}

static EvViewWindowChild *
ev_view_find_window_child_for_annot (EvView       *view,
				     guint         page,
				     EvAnnotation *annot)
{
	GList *children = view->window_children;

	while (children) {
		EvViewWindowChild *child;
		EvAnnotation      *wannot;

		child = (EvViewWindowChild *)children->data;
		children = children->next;

		if (child->page != page)
			continue;

		wannot = ev_annotation_window_get_annotation (EV_ANNOTATION_WINDOW (child->window));
		if (wannot == annot || strcmp (wannot->name, annot->name) == 0)
			return child;
	}

	return NULL;
}

static void
ev_view_window_children_free (EvView *view)
{
	GList *l;

	if (!view->window_children)
		return;

	for (l = view->window_children; l && l->data; l = g_list_next (l)) {
		EvViewWindowChild *child;

		child = (EvViewWindowChild *)l->data;
		gtk_widget_destroy (GTK_WIDGET (child->window));
		g_free (child);
	}
	g_list_free (view->window_children);
	view->window_children = NULL;
	view->window_child_focus = NULL;
}

static void
annotation_window_grab_focus (GtkWidget *widget,
			      EvView    *view)
{
	if (view->window_child_focus)
		ev_annotation_window_ungrab_focus (EV_ANNOTATION_WINDOW (view->window_child_focus->window));
	view->window_child_focus = ev_view_get_window_child (view, widget);
}

static void
annotation_window_closed (EvAnnotationWindow *window,
			  EvView             *view)
{
	EvViewWindowChild *child;

	child = ev_view_get_window_child (view, GTK_WIDGET (window));
	child->visible = FALSE;
}

static void
annotation_window_moved (EvAnnotationWindow *window,
			 gint                x,
			 gint                y,
			 EvView             *view)
{
	EvViewWindowChild *child;
	GdkRectangle       page_area;
	GtkBorder          border;
	GdkRectangle       view_rect;
	EvRectangle        doc_rect;
	gint               width, height;

	child = ev_view_get_window_child (view, GTK_WIDGET (window));
	if (child->x == x && child->y == y)
		return;

	child->moved = TRUE;
	child->x = x;
	child->y = y;

	/* Window has been moved by the user,
	 * we have to set a new origin in doc coords
	 */
	gtk_window_get_size (GTK_WINDOW (window), &width, &height);
	view_rect.x = (x - child->parent_x) + view->scroll_x;
	view_rect.y = (y - child->parent_y) + view->scroll_y;
	view_rect.width = width;
	view_rect.height = height;

	get_page_extents (view, child->page, &page_area, &border);
	view_rect_to_doc_rect (view, &view_rect, &page_area, &doc_rect);
	child->orig_x = doc_rect.x1;
	child->orig_y = doc_rect.y1;
}

static void
ev_view_annotation_save (EvView       *view,
			 EvAnnotation *annot)
{
	if (!view->document)
		return;

	if (!annot->changed)
		return;

	ev_document_annotations_annotation_set_contents (EV_DOCUMENT_ANNOTATIONS (view->document),
							 annot, annot->contents);
	annot->changed = FALSE;
}

static void
show_annotation_windows (EvView *view,
			 gint    page)
{
	GList     *annots, *l;
	GtkWindow *parent;

	parent = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (view)));

	annots = ev_page_cache_get_annot_mapping (view->page_cache, page);

	for (l = annots; l && l->data; l = g_list_next (l)) {
		EvAnnotation      *annot;
		EvViewWindowChild *child;
		GtkWidget         *window;
		EvRectangle       *doc_rect;
		GdkRectangle       view_rect;

		annot = ((EvMapping *)(l->data))->data;

		if (!EV_IS_ANNOTATION_MARKUP (annot))
			continue;

		if (!ev_annotation_markup_has_popup (EV_ANNOTATION_MARKUP (annot)))
			continue;

		window = g_object_get_data (G_OBJECT (annot), "popup");
		if (window) {
			ev_view_window_child_move_with_parent (view, window);
			continue;
		}

		/* Look if we already have a popup for this annot */
		child = ev_view_find_window_child_for_annot (view, page, annot);
		window = child ? child->window : NULL;
		if (window) {
			ev_annotation_window_set_annotation (EV_ANNOTATION_WINDOW (window), annot);
			g_object_set_data (G_OBJECT (annot), "popup", window);
			ev_view_window_child_move_with_parent (view, window);
		} else {
			window = ev_annotation_window_new (annot, parent);
			g_signal_connect (window, "grab_focus",
					  G_CALLBACK (annotation_window_grab_focus),
					  view);
			g_signal_connect (window, "closed",
					  G_CALLBACK (annotation_window_closed),
					  view);
			g_signal_connect (window, "moved",
					  G_CALLBACK (annotation_window_moved),
					  view);
			g_object_set_data (G_OBJECT (annot), "popup", window);

			doc_rect = (EvRectangle *)ev_annotation_window_get_rectangle (EV_ANNOTATION_WINDOW (window));
			doc_rect_to_view_rect (view, page, doc_rect, &view_rect);
			view_rect.x -= view->scroll_x;
			view_rect.y -= view->scroll_y;

			ev_view_window_child_put (view, window, page,
						  view_rect.x, view_rect.y,
						  doc_rect->x1, doc_rect->y1);

			g_object_weak_ref (G_OBJECT (annot),
					   (GWeakNotify)ev_view_annotation_save,
					   view);
		}
	}
}

static void
hide_annotation_windows (EvView *view,
			 gint    page)
{
	GList *annots, *l;

	annots = ev_page_cache_get_annot_mapping (view->page_cache, page);

	for (l = annots; l && l->data; l = g_list_next (l)) {
		EvAnnotation *annot;
		GtkWidget    *window;

		annot = ((EvMapping *)(l->data))->data;

		if (!EV_IS_ANNOTATION_MARKUP (annot))
			continue;

		window = g_object_get_data (G_OBJECT (annot), "popup");
		if (window)
			gtk_widget_hide (window);
	}
}

static EvAnnotation *
ev_view_get_annotation_at_location (EvView  *view,
				    gdouble  x,
				    gdouble  y)
{
	gint page = -1;
	gint x_new = 0, y_new = 0;
	GList *annotations_mapping;

	if (!EV_IS_DOCUMENT_ANNOTATIONS (view->document))
		return NULL;

	if (!get_doc_point_from_location (view, x, y, &page, &x_new, &y_new))
		return NULL;

	annotations_mapping = ev_page_cache_get_annot_mapping (view->page_cache, page);

	if (annotations_mapping)
		return ev_mapping_list_get_data (annotations_mapping, x_new, y_new);
	else
		return NULL;
}

static void
ev_view_handle_annotation (EvView       *view,
			   EvAnnotation *annot,
			   gdouble       x,
			   gdouble       y,
			   guint32       timestamp)
{
	if (EV_IS_ANNOTATION_MARKUP (annot)) {
		GtkWidget *window;

		window = g_object_get_data (G_OBJECT (annot), "popup");
		if (window) {
			EvViewWindowChild *child;

			child = ev_view_get_window_child (view, window);
			if (!child->visible) {
				child->visible = TRUE;
				ev_view_window_child_move (view, child, child->x, child->y);
				gtk_widget_show (window);
			}
		}
	}

	if (EV_IS_ANNOTATION_ATTACHMENT (annot)) {
		EvAttachment *attachment = EV_ANNOTATION_ATTACHMENT (annot)->attachment;

		if (attachment) {
			GError *error = NULL;

			ev_attachment_open (attachment,
					    gtk_widget_get_screen (GTK_WIDGET (view)),
					    timestamp,
					    &error);

			if (error) {
				g_warning ("%s", error->message);
				g_error_free (error);
			}
		}
	}
}

/*** GtkWidget implementation ***/

static void
ev_view_size_request_continuous_dual_page (EvView         *view,
			     	           GtkRequisition *requisition)
{
	gint n_pages;

	n_pages = ev_document_get_n_pages (view->document) + 1;
	get_page_y_offset (view, n_pages, &requisition->height);

	switch (view->sizing_mode) {
	        case EV_SIZING_FIT_WIDTH:
	        case EV_SIZING_BEST_FIT:
			requisition->width = 1;

			break;
	        case EV_SIZING_FREE: {
			gint max_width;
			GtkBorder border;

			ev_view_get_max_page_size (view, &max_width, NULL);
			compute_border (view, max_width, max_width, &border);
			requisition->width = (max_width + border.left + border.right) * 2 + (view->spacing * 3);
		}
			break;
	        default:
			g_assert_not_reached ();
	}
}

static void
ev_view_size_request_continuous (EvView         *view,
				 GtkRequisition *requisition)
{
	gint n_pages;

	n_pages = ev_document_get_n_pages (view->document);
	get_page_y_offset (view, n_pages, &requisition->height);

	switch (view->sizing_mode) {
	        case EV_SIZING_FIT_WIDTH:
	        case EV_SIZING_BEST_FIT:
			requisition->width = 1;

			break;
	        case EV_SIZING_FREE: {
			gint max_width;
			GtkBorder border;

			ev_view_get_max_page_size (view, &max_width, NULL);
			compute_border (view, max_width, max_width, &border);
			requisition->width = max_width + (view->spacing * 2) + border.left + border.right;
		}
			break;
	        default:
			g_assert_not_reached ();
	}
}

static void
ev_view_size_request_dual_page (EvView         *view,
				GtkRequisition *requisition)
{
	GtkBorder border;
	gint width, height;

	if (view->sizing_mode == EV_SIZING_BEST_FIT) {
		requisition->width = 1;
		requisition->height = 1;

		return;
	}

	/* Find the largest of the two. */
	ev_view_get_page_size (view,
			       view->current_page,
			       &width, &height);
	if (view->current_page + 1 < ev_document_get_n_pages (view->document)) {
		gint width_2, height_2;
		ev_view_get_page_size (view,
				       view->current_page + 1,
				       &width_2, &height_2);
		if (width_2 > width) {
			width = width_2;
			height = height_2;
		}
	}
	compute_border (view, width, height, &border);

	requisition->width = view->sizing_mode == EV_SIZING_FIT_WIDTH ? 1 :
		((width + border.left + border.right) * 2) + (view->spacing * 3);
	requisition->height = (height + border.top + border.bottom) + (view->spacing * 2);
}

static void
ev_view_size_request_single_page (EvView         *view,
				  GtkRequisition *requisition)
{
	GtkBorder border;
	gint width, height;

	if (view->sizing_mode == EV_SIZING_BEST_FIT) {
		requisition->width = 1;
		requisition->height = 1;

		return;
	}

	ev_view_get_page_size (view, view->current_page, &width, &height);
	compute_border (view, width, height, &border);

	requisition->width = view->sizing_mode == EV_SIZING_FIT_WIDTH ? 1 :
		width + border.left + border.right + (2 * view->spacing);
	requisition->height = height + border.top + border.bottom + (2 * view->spacing);
}

static void
ev_view_size_request (GtkWidget      *widget,
		      GtkRequisition *requisition)
{
	EvView *view = EV_VIEW (widget);
	
	if (view->document == NULL) {
		requisition->width = 1;
		requisition->height = 1;
		return;
	}

	if (view->continuous && view->dual_page)
		ev_view_size_request_continuous_dual_page (view, requisition);
	else if (view->continuous)
		ev_view_size_request_continuous (view, requisition);
	else if (view->dual_page)
		ev_view_size_request_dual_page (view, requisition);
	else
		ev_view_size_request_single_page (view, requisition);
}

static void
ev_view_size_allocate (GtkWidget      *widget,
		       GtkAllocation  *allocation)
{
	EvView *view = EV_VIEW (widget);
	GList  *children, *l;
	gint    root_x, root_y;

	GTK_WIDGET_CLASS (ev_view_parent_class)->size_allocate (widget, allocation);
	
	if (view->sizing_mode == EV_SIZING_FIT_WIDTH ||
	    view->sizing_mode == EV_SIZING_BEST_FIT) {
		ev_view_zoom_for_size (view,
				       allocation->width,
				       allocation->height);
		ev_view_size_request (widget, &widget->requisition);
	}
	
	view_set_adjustment_values (view, GTK_ORIENTATION_HORIZONTAL);
	view_set_adjustment_values (view, GTK_ORIENTATION_VERTICAL);

	if (view->document)
		view_update_range_and_current_page (view);

	view->pending_scroll = SCROLL_TO_KEEP_POSITION;
	view->pending_resize = FALSE;
	view->pending_point.x = 0;
	view->pending_point.y = 0;

	children = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l = children; l && l->data; l = g_list_next (l)) {
		EvFormField   *field;
		GdkRectangle   view_area;
		GList         *form_field_mapping;
		GtkAllocation  child_allocation;
		GtkRequisition child_requisition;
		GtkWidget     *child = (GtkWidget *)l->data;
		
		field = g_object_get_data (G_OBJECT (child), "form-field");
		if (!field)
			continue;

		form_field_mapping = ev_page_cache_get_form_field_mapping (view->page_cache,
									   field->page->index);
		ev_view_get_area_from_mapping (view, field->page->index,
					       form_field_mapping,
					       field, &view_area);

		gtk_widget_size_request (child, &child_requisition);
		if (child_requisition.width != view_area.width ||
		    child_requisition.height != view_area.height)
			gtk_widget_set_size_request (child, view_area.width, view_area.height);

		gtk_container_child_get (GTK_CONTAINER (widget),
					 child,
					 "x", &child_allocation.x,
					 "y", &child_allocation.y,
					 NULL);
		if (child_allocation.x != view_area.x ||
		    child_allocation.y != view_area.y) {
			gtk_layout_move (GTK_LAYOUT (widget), child, view_area.x, view_area.y);
		}
	}
	g_list_free (children);

	if (view->window_children)
		gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (view)),
				       &root_x, &root_y);

	for (l = view->window_children; l && l->data; l = g_list_next (l)) {
		EvViewWindowChild *child;
		EvRectangle        doc_rect;
		GdkRectangle       view_rect;

		child = (EvViewWindowChild *)l->data;

		doc_rect = *ev_annotation_window_get_rectangle (EV_ANNOTATION_WINDOW (child->window));
		if (child->moved) {
			doc_rect.x1 = child->orig_x;
			doc_rect.y1 = child->orig_y;
		}
		doc_rect_to_view_rect (view, child->page, &doc_rect, &view_rect);
		view_rect.x -= view->scroll_x;
		view_rect.y -= view->scroll_y;

		if (view_rect.x != child->orig_x || view_rect.y != child->orig_y) {
			child->parent_x = root_x;
			child->parent_y = root_y;
			ev_view_window_child_move (view, child, view_rect.x + root_x, view_rect.y + root_y);
		}
	}
}

static void
ev_view_realize (GtkWidget *widget)
{
	EvView *view = EV_VIEW (widget);

	if (GTK_WIDGET_CLASS (ev_view_parent_class)->realize)
		(* GTK_WIDGET_CLASS (ev_view_parent_class)->realize) (widget);

	gdk_window_set_events (view->layout.bin_window,
			       (gdk_window_get_events (view->layout.bin_window) | 
				GDK_EXPOSURE_MASK |
				GDK_BUTTON_PRESS_MASK |
				GDK_BUTTON_RELEASE_MASK |
				GDK_SCROLL_MASK |
				GDK_KEY_PRESS_MASK |
				GDK_POINTER_MOTION_MASK |
				GDK_POINTER_MOTION_HINT_MASK |
				GDK_ENTER_NOTIFY_MASK |
				GDK_LEAVE_NOTIFY_MASK));

	gdk_window_set_background (view->layout.bin_window, &widget->style->mid [GTK_STATE_NORMAL]);

	on_adjustment_value_changed (NULL, view);
}

static gboolean
ev_view_scroll_event (GtkWidget *widget, GdkEventScroll *event)
{
	EvView *view = EV_VIEW (widget);
	guint state;

	state = event->state & gtk_accelerator_get_default_mod_mask ();

	if (state == GDK_CONTROL_MASK) {
		ev_document_model_set_sizing_mode (view->model, EV_SIZING_FREE);
		if (event->direction == GDK_SCROLL_UP ||
		    event->direction == GDK_SCROLL_LEFT) {
			if (ev_view_can_zoom_in (view)) {
				ev_view_zoom_in (view);
			}
		} else {
			if (ev_view_can_zoom_out (view)) {
				ev_view_zoom_out (view);
			}
		}

		return TRUE;
	}

	view->jump_to_find_result = FALSE;

	/* Shift+Wheel scrolls the in the perpendicular direction */
	if (state & GDK_SHIFT_MASK) {
		if (event->direction == GDK_SCROLL_UP)
			event->direction = GDK_SCROLL_LEFT;
		else if (event->direction == GDK_SCROLL_LEFT)
			event->direction = GDK_SCROLL_UP;
		else if (event->direction == GDK_SCROLL_DOWN)
			event->direction = GDK_SCROLL_RIGHT;
		else if (event->direction == GDK_SCROLL_RIGHT)
			event->direction = GDK_SCROLL_DOWN;

		event->state &= ~GDK_SHIFT_MASK;
		state &= ~GDK_SHIFT_MASK;
	}

	if (state == 0 && view->sizing_mode == EV_SIZING_BEST_FIT && !view->continuous) {
		switch (event->direction) {
		        case GDK_SCROLL_DOWN:
		        case GDK_SCROLL_RIGHT:
				ev_view_next_page (view);	
				break;
		        case GDK_SCROLL_UP:
		        case GDK_SCROLL_LEFT:
				ev_view_previous_page (view);
				break;
		}

		return TRUE;
	}

	return FALSE;
}

static EvViewSelection *
find_selection_for_page (EvView *view,
			 gint    page)
{
	GList *list;

	for (list = view->selection_info.selections; list != NULL; list = list->next) {
		EvViewSelection *selection;

		selection = (EvViewSelection *) list->data;

		if (selection->page == page)
			return selection;
	}

	return NULL;
}

static gboolean
ev_view_expose_event (GtkWidget      *widget,
		      GdkEventExpose *event)
{
	EvView  *view = EV_VIEW (widget);
	cairo_t *cr;
	gint     i;

	if (view->loading) {
		GdkRectangle area = {0};
		
		area.width = widget->allocation.width;
		area.height = widget->allocation.height;

		draw_loading_text (view,
				   &area,
				   &(event->area));
	}

	if (view->document == NULL)
		return FALSE;

	cr = gdk_cairo_create (view->layout.bin_window);

	for (i = view->start_page; i >= 0 && i <= view->end_page; i++) {
		GdkRectangle page_area;
		GtkBorder border;
		gboolean page_ready = TRUE;

		if (!get_page_extents (view, i, &page_area, &border))
			continue;

		page_area.x -= view->scroll_x;
		page_area.y -= view->scroll_y;

		draw_one_page (view, i, cr, &page_area, &border, &(event->area), &page_ready);

		if (page_ready && view->find_pages && view->highlight_find_results)
			highlight_find_results (view, i);
		if (page_ready && EV_IS_DOCUMENT_ANNOTATIONS (view->document))
			show_annotation_windows (view, i);
	}

	cairo_destroy (cr);

	if (GTK_WIDGET_CLASS (ev_view_parent_class)->expose_event)
		(* GTK_WIDGET_CLASS (ev_view_parent_class)->expose_event) (widget, event);

	return FALSE;
}

static gboolean
ev_view_do_popup_menu (EvView *view,
		       gdouble x,
		       gdouble y)
{
	EvLink       *link;
	EvImage      *image;
	EvAnnotation *annot;

	image = ev_view_get_image_at_location (view, x, y);
	if (image) {
		g_signal_emit (view, signals[SIGNAL_POPUP_MENU], 0, image);
		return TRUE;
	}

	link = ev_view_get_link_at_location (view, x, y);
	if (link) {
		g_signal_emit (view, signals[SIGNAL_POPUP_MENU], 0, link);
		return TRUE;
	}

	annot = ev_view_get_annotation_at_location (view, x, y);
	if (annot) {
		g_signal_emit (view, signals[SIGNAL_POPUP_MENU], 0, annot);
		return TRUE;
	}

	g_signal_emit (view, signals[SIGNAL_POPUP_MENU], 0, NULL);

	return TRUE;
}

static gboolean
ev_view_popup_menu (GtkWidget *widget)
{
	gint x, y;
	
	gtk_widget_get_pointer (widget, &x, &y);
	return ev_view_do_popup_menu (EV_VIEW (widget), x, y);
}

static void
get_link_area (EvView       *view,
	       gint          x,
	       gint          y,
	       EvLink       *link,
	       GdkRectangle *area)
{
	GList *link_mapping;
	gint   page;
	gint   x_offset = 0, y_offset = 0;

	x += view->scroll_x;
	y += view->scroll_y;
	
	find_page_at_location (view, x, y, &page, &x_offset, &y_offset);
	
	link_mapping = ev_page_cache_get_link_mapping (view->page_cache, page);
	ev_view_get_area_from_mapping (view, page,
				       link_mapping,
				       link, area);
}

static void
get_annot_area (EvView       *view,
	       gint          x,
	       gint          y,
	       EvAnnotation *annot,
	       GdkRectangle *area)
{
	GList *annot_mapping;
	gint   page;
	gint   x_offset = 0, y_offset = 0;

	x += view->scroll_x;
	y += view->scroll_y;

	find_page_at_location (view, x, y, &page, &x_offset, &y_offset);

	annot_mapping = ev_page_cache_get_annot_mapping (view->page_cache, page);
	ev_view_get_area_from_mapping (view, page,
				       annot_mapping,
				       annot, area);
}

static gboolean
ev_view_query_tooltip (GtkWidget  *widget,
		       gint        x,
		       gint        y,
		       gboolean    keyboard_tip,
		       GtkTooltip *tooltip)
{
	EvView       *view = EV_VIEW (widget);
	EvLink       *link;
	EvAnnotation *annot;
	gchar        *text;

	annot = ev_view_get_annotation_at_location (view, x, y);
	if (annot && annot->contents) {
		GdkRectangle annot_area;

		get_annot_area (view, x, y, annot, &annot_area);
		gtk_tooltip_set_text (tooltip, annot->contents);
		gtk_tooltip_set_tip_area (tooltip, &annot_area);

		return TRUE;
	}

	link = ev_view_get_link_at_location (view, x, y);
	if (!link)
		return FALSE;

	text = tip_from_link (view, link);
	if (text && g_utf8_validate (text, -1, NULL)) {
		GdkRectangle link_area;

		get_link_area (view, x, y, link, &link_area);
		gtk_tooltip_set_text (tooltip, text);
		gtk_tooltip_set_tip_area (tooltip, &link_area);
	}
	g_free (text);

	return TRUE;
}

static void
start_selection_for_event (EvView         *view,
			   GdkEventButton *event)
{
	clear_selection (view);

	view->selection_info.start.x = event->x + view->scroll_x;
	view->selection_info.start.y = event->y + view->scroll_y;

	switch (event->type) {
	        case GDK_2BUTTON_PRESS:
			view->selection_info.style = EV_SELECTION_STYLE_WORD;
			break;
	        case GDK_3BUTTON_PRESS:
			view->selection_info.style = EV_SELECTION_STYLE_LINE;
			break;
	        default:
			view->selection_info.style = EV_SELECTION_STYLE_GLYPH;
			return;
	}

	/* In case of WORD or LINE, compute selections now */
	compute_selections (view,
			    view->selection_info.style,
			    &(view->selection_info.start),
			    &(view->selection_info.start));
}

static gboolean
ev_view_button_press_event (GtkWidget      *widget,
			    GdkEventButton *event)
{
	EvView *view = EV_VIEW (widget);

	if (!view->document)
		return FALSE;
	
	if (!GTK_WIDGET_HAS_FOCUS (widget)) {
		gtk_widget_grab_focus (widget);
	}

	if (view->window_child_focus) {
		EvAnnotationWindow *window;
		EvAnnotation       *annot;

		window = EV_ANNOTATION_WINDOW (view->window_child_focus->window);
		annot = ev_annotation_window_get_annotation (window);
		ev_annotation_window_ungrab_focus (window);
		ev_view_annotation_save (view, annot);
		view->window_child_focus = NULL;
	}
	
	view->pressed_button = event->button;
	view->selection_info.in_drag = FALSE;

	if (view->scroll_info.autoscrolling)
		return TRUE;
	
	switch (event->button) {
	        case 1: {
			EvImage *image;
			EvAnnotation *annot;
			EvFormField *field;

			if (EV_IS_SELECTION (view->document) && view->selection_info.selections) {
				if (event->type == GDK_3BUTTON_PRESS) {
					start_selection_for_event (view, event);
				} else if (location_in_selected_text (view,
							       event->x + view->scroll_x,
							       event->y + view->scroll_y)) {
					view->selection_info.in_drag = TRUE;
				} else {
					start_selection_for_event (view, event);
				}

				gtk_widget_queue_draw (widget);
			} else if ((annot = ev_view_get_annotation_at_location (view, event->x, event->y))) {
				ev_view_handle_annotation (view, annot, event->x, event->y, event->time);
			} else if ((field = ev_view_get_form_field_at_location (view, event->x, event->y))) {
				ev_view_remove_all (view);
				ev_view_handle_form_field (view, field, event->x, event->y);
			} else if (!location_in_text (view, event->x + view->scroll_x, event->y + view->scroll_y) &&
				   (image = ev_view_get_image_at_location (view, event->x, event->y))) {
				if (view->image_dnd_info.image)
					g_object_unref (view->image_dnd_info.image);
				view->image_dnd_info.image = g_object_ref (image);
				view->image_dnd_info.in_drag = TRUE;

				view->image_dnd_info.start.x = event->x + view->scroll_x;
				view->image_dnd_info.start.y = event->y + view->scroll_y;
			} else {
				ev_view_remove_all (view);
				
				if (EV_IS_SELECTION (view->document))
					start_selection_for_event (view, event);
			}
		}			
			return TRUE;
		case 2:
			/* use root coordinates as reference point because
			 * scrolling changes window relative coordinates */
			view->drag_info.start.x = event->x_root;
			view->drag_info.start.y = event->y_root;
			view->drag_info.hadj = gtk_adjustment_get_value (view->hadjustment);
			view->drag_info.vadj = gtk_adjustment_get_value (view->vadjustment);

			ev_view_set_cursor (view, EV_VIEW_CURSOR_DRAG);

			return TRUE;
		case 3:
			view->scroll_info.start_y = event->y;
			return ev_view_do_popup_menu (view, event->x, event->y);
	}
	
	return FALSE;
}

static void
ev_view_remove_all (EvView *view)
{
	GList *children, *child;

	children = gtk_container_get_children (GTK_CONTAINER (view));
	for (child = children; child && child->data; child = g_list_next (child)) {
		gtk_container_remove (GTK_CONTAINER (view),
				      GTK_WIDGET (child->data));
	}
	g_list_free (children);
}

/*** Drag and Drop ***/
static void
ev_view_drag_data_get (GtkWidget        *widget,
		       GdkDragContext   *context,
		       GtkSelectionData *selection_data,
		       guint             info,
		       guint             time)
{
	EvView *view = EV_VIEW (widget);

	switch (info) {
	        case TARGET_DND_TEXT:
			if (EV_IS_SELECTION (view->document) &&
			    view->selection_info.selections) {
				gchar *text;

				text = get_selected_text (view);
				gtk_selection_data_set_text (selection_data,
							     text,
							     strlen (text));
				g_free (text);
			}
			break;
	        case TARGET_DND_IMAGE:
			if (view->image_dnd_info.image) {
				GdkPixbuf *pixbuf;

				ev_document_doc_mutex_lock ();
				pixbuf = ev_document_images_get_image (EV_DOCUMENT_IMAGES (view->document),
								       view->image_dnd_info.image);
				ev_document_doc_mutex_unlock ();
				
				gtk_selection_data_set_pixbuf (selection_data, pixbuf);
				g_object_unref (pixbuf);
			}
			break;
	        case TARGET_DND_URI:
			if (view->image_dnd_info.image) {
				GdkPixbuf   *pixbuf;
				const gchar *tmp_uri;
				gchar       *uris[2];

				ev_document_doc_mutex_lock ();
				pixbuf = ev_document_images_get_image (EV_DOCUMENT_IMAGES (view->document),
								       view->image_dnd_info.image);
				ev_document_doc_mutex_unlock ();
				
				tmp_uri = ev_image_save_tmp (view->image_dnd_info.image, pixbuf);
				g_object_unref (pixbuf);

				uris[0] = (gchar *)tmp_uri;
                                uris[1] = NULL;
				gtk_selection_data_set_uris (selection_data, uris);
			}
	}
}

static gboolean
ev_view_drag_motion (GtkWidget      *widget,
		     GdkDragContext *context,
		     gint            x,
		     gint            y,
		     guint           time)
{
	if (gtk_drag_get_source_widget (context) == widget)
		gdk_drag_status (context, 0, time);
	else
		gdk_drag_status (context, context->suggested_action, time);
	
	return TRUE;
}
		     
static gboolean
selection_update_idle_cb (EvView *view)
{
	compute_selections (view,
			    view->selection_info.style,
			    &view->selection_info.start,
			    &view->motion);
	view->selection_update_id = 0;
	return FALSE;
}

static gboolean
selection_scroll_timeout_cb (EvView *view)
{	
	gint x, y, shift = 0;
	GtkWidget *widget = GTK_WIDGET (view);
	
	gtk_widget_get_pointer (widget, &x, &y);

	if (y > widget->allocation.height) {
		shift = (y - widget->allocation.height) / 2;
	} else if (y < 0) {
		shift = y / 2;
	}

	if (shift)
		gtk_adjustment_set_value (view->vadjustment,
					  CLAMP (view->vadjustment->value + shift,
					  view->vadjustment->lower,
					  view->vadjustment->upper -
					  view->vadjustment->page_size));	

	if (x > widget->allocation.width) {
		shift = (x - widget->allocation.width) / 2;
	} else if (x < 0) {
		shift = x / 2;
	}

	if (shift)
		gtk_adjustment_set_value (view->hadjustment,
					  CLAMP (view->hadjustment->value + shift,
					  view->hadjustment->lower,
					  view->hadjustment->upper -
					  view->hadjustment->page_size));	

	return TRUE;
}

static gboolean
ev_view_drag_update_momentum (EvView *view)
{
	int i;
	if (!view->drag_info.in_drag)
		return FALSE;
	
	for (i = DRAG_HISTORY - 1; i > 0; i--) {
		view->drag_info.buffer[i].x = view->drag_info.buffer[i-1].x;
		view->drag_info.buffer[i].y = view->drag_info.buffer[i-1].y;
	}

	/* Momentum is a moving average of 10ms granularity over
	 * the last 100ms with each 10ms stored in buffer. 
	 */
	
	view->drag_info.momentum.x = (view->drag_info.buffer[DRAG_HISTORY - 1].x - view->drag_info.buffer[0].x);
	view->drag_info.momentum.y = (view->drag_info.buffer[DRAG_HISTORY - 1].y - view->drag_info.buffer[0].y);

	return TRUE;
}

static gboolean
ev_view_scroll_drag_release (EvView *view)
{
	gdouble dhadj_value, dvadj_value;
	gdouble oldhadjustment, oldvadjustment;

	view->drag_info.momentum.x /= 1.2;
	view->drag_info.momentum.y /= 1.2; /* Alter these constants to change "friction" */

	dhadj_value = view->hadjustment->page_size *
		      (gdouble)view->drag_info.momentum.x / GTK_WIDGET (view)->allocation.width;
	dvadj_value = view->vadjustment->page_size *
		      (gdouble)view->drag_info.momentum.y / GTK_WIDGET (view)->allocation.height;

	oldhadjustment = gtk_adjustment_get_value (view->hadjustment);
	oldvadjustment = gtk_adjustment_get_value (view->vadjustment);

     /* When we reach the edges, we need either to absorb some momentum and bounce by
      * multiplying it on -0.5 or stop scrolling by setting momentum to 0. */	
     if (((oldhadjustment + dhadj_value) > (view->hadjustment->upper - view->hadjustment->page_size)) ||
	   ((oldhadjustment + dhadj_value) < 0))
		view->drag_info.momentum.x = 0;
	if (((oldvadjustment + dvadj_value) > (view->vadjustment->upper - view->vadjustment->page_size)) ||
	   ((oldvadjustment + dvadj_value) < 0))
		view->drag_info.momentum.y = 0;

	gtk_adjustment_set_value (view->hadjustment,
				MIN (oldhadjustment + dhadj_value,
				view->hadjustment->upper - view->hadjustment->page_size));
	gtk_adjustment_set_value (view->vadjustment,
				MIN (oldvadjustment + dvadj_value,
				view->vadjustment->upper - view->vadjustment->page_size));

	if (((view->drag_info.momentum.x < 1) && (view->drag_info.momentum.x > -1)) &&
	   ((view->drag_info.momentum.y < 1) && (view->drag_info.momentum.y > -1)))
		return FALSE;
	else
		return TRUE;
}

static gboolean
ev_view_motion_notify_event (GtkWidget      *widget,
			     GdkEventMotion *event)
{
	EvView *view = EV_VIEW (widget);
	gint x, y;

	if (!view->document)
		return FALSE;

        if (event->is_hint || event->window != view->layout.bin_window) {
	    gtk_widget_get_pointer (widget, &x, &y);
        } else {
	    x = event->x;
	    y = event->y;
	}

	if (view->scroll_info.autoscrolling) {
		view->scroll_info.last_y = y;
		return TRUE;
	}

	if (view->selection_info.in_drag) {
		if (gtk_drag_check_threshold (widget,
					      view->selection_info.start.x,
					      view->selection_info.start.y,
					      x, y)) {
			GtkTargetList *target_list = gtk_target_list_new (NULL, 0);

			gtk_target_list_add_text_targets (target_list, TARGET_DND_TEXT);

			gtk_drag_begin (widget, target_list,
					GDK_ACTION_COPY,
					1, (GdkEvent *)event);

			view->selection_info.in_drag = FALSE;

			gtk_target_list_unref (target_list);

			return TRUE;
		}
	} else if (view->image_dnd_info.in_drag) {
		if (gtk_drag_check_threshold (widget,
					      view->selection_info.start.x,
					      view->selection_info.start.y,
					      x, y)) {
			GtkTargetList *target_list = gtk_target_list_new (NULL, 0);

			gtk_target_list_add_uri_targets (target_list, TARGET_DND_URI);
			gtk_target_list_add_image_targets (target_list, TARGET_DND_IMAGE, TRUE);

			gtk_drag_begin (widget, target_list,
					GDK_ACTION_COPY,
					1, (GdkEvent *)event);

			view->image_dnd_info.in_drag = FALSE;

			gtk_target_list_unref (target_list);

			return TRUE;
		}
	}
	
	switch (view->pressed_button) {
	case 1:
		/* For the Evince 0.4.x release, we limit selection to un-rotated
		 * documents only.
		 */
		if (view->rotation != 0)
			return FALSE;

		/* Schedule timeout to scroll during selection and additionally 
		 * scroll once to allow arbitrary speed. */
		if (!view->selection_scroll_id)
		    view->selection_scroll_id = g_timeout_add (SCROLL_TIME,
							       (GSourceFunc)selection_scroll_timeout_cb,
							       view);
		else 
		    selection_scroll_timeout_cb (view);

		view->selection_info.in_selection = TRUE;
		view->motion.x = x + view->scroll_x;
		view->motion.y = y + view->scroll_y;

		/* Queue an idle to handle the motion.  We do this because	
		 * handling any selection events in the motion could be slower	
		 * than new motion events reach us.  We always put it in the	
		 * idle to make sure we catch up and don't visibly lag the	
		 * mouse. */
		if (!view->selection_update_id)
			view->selection_update_id = g_idle_add ((GSourceFunc)selection_update_idle_cb, view);

		return TRUE;
	case 2:
		if (!view->drag_info.in_drag) {
			gboolean start;
			int i;

			start = gtk_drag_check_threshold (widget,
							  view->drag_info.start.x,
							  view->drag_info.start.y,
							  event->x_root,
							  event->y_root);
			view->drag_info.in_drag = start;
			view->drag_info.drag_timeout_id = g_timeout_add (10,
				(GSourceFunc)ev_view_drag_update_momentum, view);
			/* Set 100 to choose how long it takes to build up momentum */
			/* Clear out previous momentum info: */
			for (i = 0; i < DRAG_HISTORY; i++) {
				view->drag_info.buffer[i].x = event->x;
				view->drag_info.buffer[i].y = event->y;
			}
			view->drag_info.momentum.x = 0;
			view->drag_info.momentum.y = 0;
		}

		if (view->drag_info.in_drag) {
			int dx, dy;
			gdouble dhadj_value, dvadj_value;

			view->drag_info.buffer[0].x = event->x;
			view->drag_info.buffer[0].y = event->y;

			dx = event->x_root - view->drag_info.start.x;
			dy = event->y_root - view->drag_info.start.y;

			dhadj_value = view->hadjustment->page_size *
				      (gdouble)dx / widget->allocation.width;
			dvadj_value = view->vadjustment->page_size *
				      (gdouble)dy / widget->allocation.height;

			/* clamp scrolling to visible area */
			gtk_adjustment_set_value (view->hadjustment,
						  MIN(view->drag_info.hadj - dhadj_value,
						      view->hadjustment->upper -
						      view->hadjustment->page_size));
			gtk_adjustment_set_value (view->vadjustment,
						  MIN(view->drag_info.vadj - dvadj_value,
						      view->vadjustment->upper -
						      view->vadjustment->page_size));

			return TRUE;
		}

		break;
	default:
		ev_view_handle_cursor_over_xy (view, x, y);
	} 

	return FALSE;
}

static gboolean
ev_view_button_release_event (GtkWidget      *widget,
			      GdkEventButton *event)
{
	EvView *view = EV_VIEW (widget);
	EvLink *link = NULL;

	view->image_dnd_info.in_drag = FALSE;

	if (view->scroll_info.autoscrolling) {
		ev_view_autoscroll_stop (view);
		view->pressed_button = -1;

		return TRUE;
	} 

	if (view->drag_info.in_drag) {
		view->drag_info.release_timeout_id =
			g_timeout_add (20,
				       (GSourceFunc)ev_view_scroll_drag_release, view);
	}

	if (view->document && !view->drag_info.in_drag && view->pressed_button != 3) {
		link = ev_view_get_link_at_location (view, event->x, event->y);
	}

	view->drag_info.in_drag = FALSE;

	if (view->pressed_button == 2) {
		ev_view_handle_cursor_over_xy (view, event->x, event->y);
	}

	view->pressed_button = -1;

	if (view->selection_scroll_id) {
	    g_source_remove (view->selection_scroll_id);
	    view->selection_scroll_id = 0;
	}
	if (view->selection_update_id) {
	    g_source_remove (view->selection_update_id);
	    view->selection_update_id = 0;
	}

	if (view->selection_info.selections) {
		clear_link_selected (view);
		ev_view_update_primary_selection (view);
		
		if (view->selection_info.in_drag) {
			clear_selection (view);
			gtk_widget_queue_draw (widget);
		}
		
		view->selection_info.in_drag = FALSE;
	} else if (link) {
		if (event->button == 2) {
			EvLinkAction    *action;
			EvLinkActionType type;

			action = ev_link_get_action (link);
			if (!action)
				return FALSE;

			type = ev_link_action_get_action_type (action);
			if (type == EV_LINK_ACTION_TYPE_GOTO_DEST) {
				g_signal_emit (view,
					       signals[SIGNAL_EXTERNAL_LINK],
					       0, action);
			}
		} else {
			ev_view_handle_link (view, link);
		}
	}

	return FALSE;
}

static gboolean
ev_view_key_press_event (GtkWidget   *widget,
			 GdkEventKey *event)
{
	EvView *view = EV_VIEW (widget);

	if (!view->document)
		return FALSE;

	if (!GTK_WIDGET_HAS_FOCUS (widget)) {
		/* Forward key events to current focused window child */
		if (view->window_child_focus) {
			GdkEventKey *new_event;
			gboolean     handled;

			new_event = (GdkEventKey *) gdk_event_copy ((GdkEvent *)event);
			g_object_unref (new_event->window);
			new_event->window = g_object_ref (view->window_child_focus->window->window);
			gtk_widget_realize (view->window_child_focus->window);
			handled = gtk_widget_event (view->window_child_focus->window, (GdkEvent *)new_event);
			gdk_event_free ((GdkEvent *)new_event);

			return handled;
		}

		return FALSE;
	}

	return gtk_bindings_activate_event (GTK_OBJECT (widget), event);
}

static gint
ev_view_focus_in (GtkWidget     *widget,
		  GdkEventFocus *event)
{
	if (EV_VIEW (widget)->pixbuf_cache)
		ev_pixbuf_cache_style_changed (EV_VIEW (widget)->pixbuf_cache);
	gtk_widget_queue_draw (widget);

	return FALSE;
}

static gint
ev_view_focus_out (GtkWidget     *widget,
		   GdkEventFocus *event)
{
	if (EV_VIEW (widget)->pixbuf_cache)
		ev_pixbuf_cache_style_changed (EV_VIEW (widget)->pixbuf_cache);
	gtk_widget_queue_draw (widget);

	return FALSE;
}

static gboolean
ev_view_leave_notify_event (GtkWidget *widget, GdkEventCrossing   *event)
{
	EvView *view = EV_VIEW (widget);

	if (view->cursor != EV_VIEW_CURSOR_NORMAL)
		ev_view_set_cursor (view, EV_VIEW_CURSOR_NORMAL);

	return FALSE;
}

static gboolean
ev_view_enter_notify_event (GtkWidget *widget, GdkEventCrossing   *event)
{
	EvView *view = EV_VIEW (widget);

	ev_view_handle_cursor_over_xy (view, event->x, event->y);
    
	return FALSE;
}

static void
ev_view_style_set (GtkWidget *widget,
		   GtkStyle  *old_style)
{
	if (EV_VIEW (widget)->pixbuf_cache)
		ev_pixbuf_cache_style_changed (EV_VIEW (widget)->pixbuf_cache);

	GTK_WIDGET_CLASS (ev_view_parent_class)->style_set (widget, old_style);
}

/*** Drawing ***/

static guint32
ev_gdk_color_to_rgb (const GdkColor *color)
{
  guint32 result;
  result = (0xff0000 | (color->red & 0xff00));
  result <<= 8;
  result |= ((color->green & 0xff00) | (color->blue >> 8));
  return result;
}

static void
draw_rubberband (GtkWidget *widget, GdkWindow *window,
		 const GdkRectangle *rect, guchar alpha)
{
	GdkGC *gc;
	GdkPixbuf *pixbuf;
	GdkColor *fill_color_gdk;
	guint fill_color;

	fill_color_gdk = gdk_color_copy (&GTK_WIDGET (widget)->style->base[GTK_STATE_SELECTED]);
	fill_color = ev_gdk_color_to_rgb (fill_color_gdk) << 8 | alpha;

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
				 rect->width, rect->height);
	gdk_pixbuf_fill (pixbuf, fill_color);

	gdk_draw_pixbuf (window, NULL, pixbuf,
			 0, 0,
			 rect->x - EV_VIEW (widget)->scroll_x, rect->y - EV_VIEW (widget)->scroll_y,
			 rect->width, rect->height,
			 GDK_RGB_DITHER_NONE,
			 0, 0);

	g_object_unref (pixbuf);

	gc = gdk_gc_new (window);
	gdk_gc_set_rgb_fg_color (gc, fill_color_gdk);
	gdk_draw_rectangle (window, gc, FALSE,
			    rect->x - EV_VIEW (widget)->scroll_x, rect->y - EV_VIEW (widget)->scroll_y,
			    rect->width - 1,
			    rect->height - 1);
	g_object_unref (gc);

	gdk_color_free (fill_color_gdk);
}


static void
highlight_find_results (EvView *view, int page)
{
	gint i, n_results = 0;

	n_results = ev_view_find_get_n_results (view, page);

	for (i = 0; i < n_results; i++) {
		EvRectangle *rectangle;
		GdkRectangle view_rectangle;
		guchar alpha;

		if (i == view->find_result && page == view->current_page) {
			alpha = 0x90;
		} else {
			alpha = 0x20;
		}

		rectangle = ev_view_find_get_result (view, page, i);
		doc_rect_to_view_rect (view, page, rectangle, &view_rectangle);
		draw_rubberband (GTK_WIDGET (view), view->layout.bin_window,
				 &view_rectangle, alpha);
        }
}

static void
draw_loading_text (EvView       *view,
		   GdkRectangle *page_area,
		   GdkRectangle *expose_area)
{
	cairo_t *cr;
	gint     width, height;

	if (!view->loading_text) {
		const gchar *loading_text = _("Loading…");
		PangoLayout *layout;
		PangoFontDescription *font_desc;
		PangoRectangle logical_rect;
		gint target_width;
		gdouble real_scale;

		ev_document_fc_mutex_lock ();

		layout = gtk_widget_create_pango_layout (GTK_WIDGET (view), loading_text);
		
		font_desc = pango_font_description_new ();
		
		/* We set the font to be 10 points, get the size, and scale appropriately */
		pango_font_description_set_size (font_desc, 10 * PANGO_SCALE);
		pango_layout_set_font_description (layout, font_desc);
		pango_layout_get_pixel_extents (layout, NULL, &logical_rect);

		target_width = MAX (page_area->width / 2, 1);
		real_scale = ((double)target_width / (double) logical_rect.width) * (PANGO_SCALE * 10);
		pango_font_description_set_size (font_desc, (int)real_scale);
		pango_layout_set_font_description (layout, font_desc);
		pango_layout_get_pixel_extents (layout, NULL, &logical_rect);

		view->loading_text = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
								 logical_rect.width,
								 logical_rect.height);
		cr = cairo_create (view->loading_text);
		cairo_set_source_rgb (cr,
				      155 / (double)255,
				      155 / (double)255,
				      155 / (double)255);
		pango_cairo_show_layout (cr, layout);
		cairo_destroy (cr);

		pango_font_description_free (font_desc);
		g_object_unref (layout);

		ev_document_fc_mutex_unlock ();
	}

	width = (page_area->width - cairo_image_surface_get_width (view->loading_text)) / 2;
	height = (page_area->height - cairo_image_surface_get_height (view->loading_text)) / 2;
	
	cr = gdk_cairo_create (view->layout.bin_window);
	cairo_translate (cr,
			 page_area->x + width,
			 page_area->y + height);
	cairo_set_source_surface (cr, view->loading_text, 0, 0);
	cairo_paint (cr);
	cairo_destroy (cr);
}

static void
draw_one_page (EvView       *view,
	       gint          page,
	       cairo_t      *cr,
	       GdkRectangle *page_area,
	       GtkBorder    *border,
	       GdkRectangle *expose_area,
	       gboolean     *page_ready)
{
	GdkRectangle overlap;
	GdkRectangle real_page_area;
	gint         current_page;

	g_assert (view->document);

	if (! gdk_rectangle_intersect (page_area, expose_area, &overlap))
		return;

	/* Render the document itself */
	real_page_area = *page_area;

	real_page_area.x += border->left;
	real_page_area.y += border->top;
	real_page_area.width -= (border->left + border->right);
	real_page_area.height -= (border->top + border->bottom);
	*page_ready = TRUE;

	current_page = ev_document_model_get_page (view->model);
	ev_document_misc_paint_one_page (view->layout.bin_window,
					 GTK_WIDGET (view),
					 page_area, border,
					 page == current_page);

	if (gdk_rectangle_intersect (&real_page_area, expose_area, &overlap)) {
		gint             width, height;
		gint             page_width, page_height;
		cairo_surface_t *page_surface = NULL;
		gint             selection_width, selection_height;
		cairo_surface_t *selection_surface = NULL;

		page_surface = ev_pixbuf_cache_get_surface (view->pixbuf_cache, page);

		if (!page_surface) {
			draw_loading_text (view,
					   &real_page_area,
					   expose_area);

			*page_ready = FALSE;

			return;
		}

		ev_view_get_page_size (view, page, &width, &height);

		page_width = cairo_image_surface_get_width (page_surface);
		page_height = cairo_image_surface_get_height (page_surface);

		cairo_save (cr);
		cairo_translate (cr, overlap.x, overlap.y);

		if (width != page_width || height != page_height) {
			cairo_pattern_set_filter (cairo_get_source (cr),
						  CAIRO_FILTER_FAST);
			cairo_scale (cr,
				     (gdouble)width / page_width,
				     (gdouble)height / page_height);
		}

		cairo_surface_set_device_offset (page_surface,
						 overlap.x - real_page_area.x,
						 overlap.y - real_page_area.y);
		cairo_set_source_surface (cr, page_surface, 0, 0);
		cairo_paint (cr);
		cairo_restore (cr);
		
		/* Get the selection pixbuf iff we have something to draw */
		if (find_selection_for_page (view, page) &&
		    view->selection_mode == EV_VIEW_SELECTION_TEXT) {
			selection_surface =
				ev_pixbuf_cache_get_selection_surface (view->pixbuf_cache,
								       page,
								       view->scale,
								       NULL);
		}

		if (!selection_surface) {
			return;
		}

		selection_width = cairo_image_surface_get_width (selection_surface);
		selection_height = cairo_image_surface_get_height (selection_surface);

		cairo_save (cr);
		cairo_translate (cr, overlap.x, overlap.y);

		if (width != selection_width || height != selection_height) {
			cairo_pattern_set_filter (cairo_get_source (cr),
						  CAIRO_FILTER_FAST);
			cairo_scale (cr,
				     (gdouble)width / selection_width,
				     (gdouble)height / selection_height);
		}

		cairo_surface_set_device_offset (selection_surface,
						 overlap.x - real_page_area.x,
						 overlap.y - real_page_area.y);
		cairo_set_source_surface (cr, selection_surface, 0, 0);
		cairo_paint (cr);
		cairo_restore (cr);
	}
}

/*** GObject functions ***/

static void
ev_view_finalize (GObject *object)
{
	EvView *view = EV_VIEW (object);

	clear_selection (view);
	clear_link_selected (view);

	if (view->image_dnd_info.image)
		g_object_unref (view->image_dnd_info.image);
	view->image_dnd_info.image = NULL;

	G_OBJECT_CLASS (ev_view_parent_class)->finalize (object);
}

static void
ev_view_destroy (GtkObject *object)
{
	EvView *view = EV_VIEW (object);

	if (view->model) {
		g_object_unref (view->model);
		view->model = NULL;
	}

	if (view->pixbuf_cache) {
		g_object_unref (view->pixbuf_cache);
		view->pixbuf_cache = NULL;
	}

	if (view->document) {
		g_object_unref (view->document);
		view->document = NULL;
	}

	if (view->page_cache) {
		g_object_unref (view->page_cache);
		view->page_cache = NULL;
	}

	ev_view_window_children_free (view);

	if (view->selection_scroll_id) {
	    g_source_remove (view->selection_scroll_id);
	    view->selection_scroll_id = 0;
	}

	if (view->selection_update_id) {
	    g_source_remove (view->selection_update_id);
	    view->selection_update_id = 0;
	}

	if (view->loading_text) {
		cairo_surface_destroy (view->loading_text);
		view->loading_text = NULL;
	}

	if (view->scroll_info.timeout_id) {
	    g_source_remove (view->scroll_info.timeout_id);
	    view->scroll_info.timeout_id = 0;
	}

	if (view->drag_info.drag_timeout_id) {
		g_source_remove (view->drag_info.drag_timeout_id);
		view->drag_info.drag_timeout_id = 0;
	}

	if (view->drag_info.release_timeout_id) {
		g_source_remove (view->drag_info.release_timeout_id);
		view->drag_info.release_timeout_id = 0;
	}

	ev_view_set_scroll_adjustments (GTK_LAYOUT (view), NULL, NULL);

	GTK_OBJECT_CLASS (ev_view_parent_class)->destroy (object);
}

static AtkObject *
ev_view_get_accessible (GtkWidget *widget)
{
	static gboolean first_time = TRUE;

	if (first_time)	{
		AtkObjectFactory *factory;
		AtkRegistry *registry;
		GType derived_type; 
		GType derived_atk_type; 

		/*
		 * Figure out whether accessibility is enabled by looking at the
		 * type of the accessible object which would be created for
		 * the parent type of EvView.
		 */
		derived_type = g_type_parent (EV_TYPE_VIEW);

		registry = atk_get_default_registry ();
		factory = atk_registry_get_factory (registry,
						    derived_type);
		derived_atk_type = atk_object_factory_get_accessible_type (factory);
		if (g_type_is_a (derived_atk_type, GTK_TYPE_ACCESSIBLE)) 
			atk_registry_set_factory_type (registry, 
						       EV_TYPE_VIEW,
						       ev_view_accessible_factory_get_type ());
		first_time = FALSE;
	} 
	return GTK_WIDGET_CLASS (ev_view_parent_class)->get_accessible (widget);
}

static void
ev_view_class_init (EvViewClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GtkObjectClass *gtk_object_class = GTK_OBJECT_CLASS (class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
	GtkLayoutClass *layout_class = GTK_LAYOUT_CLASS (class);
	GtkBindingSet *binding_set;

	object_class->finalize = ev_view_finalize;

	widget_class->expose_event = ev_view_expose_event;
	widget_class->button_press_event = ev_view_button_press_event;
	widget_class->motion_notify_event = ev_view_motion_notify_event;
	widget_class->button_release_event = ev_view_button_release_event;
	widget_class->key_press_event = ev_view_key_press_event;
	widget_class->focus_in_event = ev_view_focus_in;
	widget_class->focus_out_event = ev_view_focus_out;
 	widget_class->get_accessible = ev_view_get_accessible;
	widget_class->size_request = ev_view_size_request;
	widget_class->size_allocate = ev_view_size_allocate;
	widget_class->realize = ev_view_realize;
	widget_class->scroll_event = ev_view_scroll_event;
	widget_class->enter_notify_event = ev_view_enter_notify_event;
	widget_class->leave_notify_event = ev_view_leave_notify_event;
	widget_class->style_set = ev_view_style_set;
	widget_class->drag_data_get = ev_view_drag_data_get;
	widget_class->drag_motion = ev_view_drag_motion;
	widget_class->popup_menu = ev_view_popup_menu;
	widget_class->query_tooltip = ev_view_query_tooltip;

	gtk_object_class->destroy = ev_view_destroy;

	layout_class->set_scroll_adjustments = ev_view_set_scroll_adjustments;
	
	class->binding_activated = ev_view_scroll;

	signals[SIGNAL_BINDING_ACTIVATED] = g_signal_new ("binding_activated",
	  	         G_TYPE_FROM_CLASS (object_class),
		         G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		         G_STRUCT_OFFSET (EvViewClass, binding_activated),
		         NULL, NULL,
		         ev_view_marshal_VOID__ENUM_BOOLEAN,
		         G_TYPE_NONE, 2,
		         GTK_TYPE_SCROLL_TYPE,
		         G_TYPE_BOOLEAN);
	signals[SIGNAL_HANDLE_LINK] = g_signal_new ("handle-link",
	  	         G_TYPE_FROM_CLASS (object_class),
		         G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		         G_STRUCT_OFFSET (EvViewClass, handle_link),
		         NULL, NULL,
		         g_cclosure_marshal_VOID__OBJECT,
		         G_TYPE_NONE, 1,
			 G_TYPE_OBJECT);
	signals[SIGNAL_EXTERNAL_LINK] = g_signal_new ("external-link",
	  	         G_TYPE_FROM_CLASS (object_class),
		         G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		         G_STRUCT_OFFSET (EvViewClass, external_link),
		         NULL, NULL,
		         g_cclosure_marshal_VOID__OBJECT,
		         G_TYPE_NONE, 1,
			 G_TYPE_OBJECT);
	signals[SIGNAL_POPUP_MENU] = g_signal_new ("popup",
	  	         G_TYPE_FROM_CLASS (object_class),
		         G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		         G_STRUCT_OFFSET (EvViewClass, popup_menu),
		         NULL, NULL,
		         g_cclosure_marshal_VOID__OBJECT,
		         G_TYPE_NONE, 1,
			 G_TYPE_OBJECT);
	signals[SIGNAL_SELECTION_CHANGED] = g_signal_new ("selection-changed",
                         G_TYPE_FROM_CLASS (object_class),
                         G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                         G_STRUCT_OFFSET (EvViewClass, selection_changed),
                         NULL, NULL,
			 g_cclosure_marshal_VOID__VOID,
                         G_TYPE_NONE, 0,
                         G_TYPE_NONE);

	binding_set = gtk_binding_set_by_class (class);

	add_scroll_binding_keypad (binding_set, GDK_Left,  0, GTK_SCROLL_STEP_BACKWARD, TRUE);
	add_scroll_binding_keypad (binding_set, GDK_Right, 0, GTK_SCROLL_STEP_FORWARD,  TRUE);
	add_scroll_binding_keypad (binding_set, GDK_Left,  GDK_MOD1_MASK, GTK_SCROLL_STEP_DOWN, TRUE);
	add_scroll_binding_keypad (binding_set, GDK_Right, GDK_MOD1_MASK, GTK_SCROLL_STEP_UP,  TRUE);
	add_scroll_binding_keypad (binding_set, GDK_Up,    0, GTK_SCROLL_STEP_BACKWARD, FALSE);
	add_scroll_binding_keypad (binding_set, GDK_Down,  0, GTK_SCROLL_STEP_FORWARD,  FALSE);
	add_scroll_binding_keypad (binding_set, GDK_Up,    GDK_MOD1_MASK, GTK_SCROLL_STEP_DOWN, FALSE);
	add_scroll_binding_keypad (binding_set, GDK_Down,  GDK_MOD1_MASK, GTK_SCROLL_STEP_UP,  FALSE);
	gtk_binding_entry_add_signal (binding_set, GDK_H, 0, "binding_activated", 2, GTK_TYPE_SCROLL_TYPE,
				      GTK_SCROLL_STEP_BACKWARD, G_TYPE_BOOLEAN, TRUE);
	gtk_binding_entry_add_signal (binding_set, GDK_J, 0, "binding_activated", 2, GTK_TYPE_SCROLL_TYPE,
				      GTK_SCROLL_STEP_FORWARD, G_TYPE_BOOLEAN, FALSE);
	gtk_binding_entry_add_signal (binding_set, GDK_K, 0, "binding_activated", 2, GTK_TYPE_SCROLL_TYPE,
				      GTK_SCROLL_STEP_BACKWARD, G_TYPE_BOOLEAN, FALSE);
	gtk_binding_entry_add_signal (binding_set, GDK_L, 0, "binding_activated", 2, GTK_TYPE_SCROLL_TYPE,
				      GTK_SCROLL_STEP_FORWARD, G_TYPE_BOOLEAN, TRUE);
	
}

static void
ev_view_init (EvView *view)
{
	GTK_WIDGET_SET_FLAGS (view, GTK_CAN_FOCUS);

	view->start_page = -1;
	view->end_page = -1;
	view->spacing = 5;
	view->scale = 1.0;
	view->current_page = 0;
	view->pressed_button = -1;
	view->cursor = EV_VIEW_CURSOR_NORMAL;
	view->drag_info.in_drag = FALSE;
	view->scroll_info.autoscrolling = FALSE;
	view->selection_info.selections = NULL;
	view->selection_info.in_selection = FALSE;
	view->selection_info.in_drag = FALSE;
	view->selection_mode = EV_VIEW_SELECTION_TEXT;
	view->continuous = TRUE;
	view->dual_page = FALSE;
	view->fullscreen = FALSE;
	view->sizing_mode = EV_SIZING_FIT_WIDTH;
	view->pending_scroll = SCROLL_TO_KEEP_POSITION;
	view->jump_to_find_result = TRUE;
	view->highlight_find_results = FALSE;

	gtk_layout_set_hadjustment (GTK_LAYOUT (view), NULL);
	gtk_layout_set_vadjustment (GTK_LAYOUT (view), NULL);
}

/*** Callbacks ***/

static void
ev_view_change_page (EvView *view,
		     gint    new_page)
{
	gint x, y;

	view->current_page = new_page;
	view->pending_scroll = SCROLL_TO_PAGE_POSITION;

	gtk_widget_get_pointer (GTK_WIDGET (view), &x, &y);
	ev_view_handle_cursor_over_xy (view, x, y);

	gtk_widget_queue_resize (GTK_WIDGET (view));
}

static void
job_finished_cb (EvPixbufCache *pixbuf_cache,
		 GdkRegion     *region,
		 EvView        *view)
{
	if (region) {
		gdk_window_invalidate_region (view->layout.bin_window,
					      region, TRUE);
	} else {
		gtk_widget_queue_draw (GTK_WIDGET (view));
	}
}

static void
ev_view_page_changed_cb (EvDocumentModel *model,
			 gint             old_page,
			 gint             new_page,
			 EvView          *view)
{
	if (!view->document)
		return;

	if (view->current_page != new_page) {
		ev_view_change_page (view, new_page);
	} else {
		gtk_widget_queue_draw (GTK_WIDGET (view));
	}

	view->find_result = 0;
}

static void
on_adjustment_value_changed (GtkAdjustment *adjustment,
			     EvView        *view)
{
	int dx = 0, dy = 0;
	gint x, y;
	GList *children, *l;

	if (! GTK_WIDGET_REALIZED (view))
		return;

	if (view->hadjustment) {
		dx = view->scroll_x - (int) view->hadjustment->value;
		view->scroll_x = (int) view->hadjustment->value;
	} else {
		view->scroll_x = 0;
	}

	if (view->vadjustment) {
		dy = view->scroll_y - (int) view->vadjustment->value;
		view->scroll_y = (int) view->vadjustment->value;
	} else {
		view->scroll_y = 0;
	}

	children = gtk_container_get_children (GTK_CONTAINER (view));
	for (l = children; l && l->data; l = g_list_next (l)) {
		gint       child_x, child_y;
		GtkWidget *child = (GtkWidget *)l->data;
		
		gtk_container_child_get (GTK_CONTAINER (view),
					 child,
					 "x", &child_x,
					 "y", &child_y,
					 NULL);
		gtk_layout_move (GTK_LAYOUT (view), child, child_x + dx, child_y + dy);
	}
	g_list_free (children);

	for (l = view->window_children; l && l->data; l = g_list_next (l)) {
		EvViewWindowChild *child;

		child = (EvViewWindowChild *)l->data;

		ev_view_window_child_move (view, child, child->x + dx, child->y + dy);
	}
	
	if (view->pending_resize)
		gtk_widget_queue_draw (GTK_WIDGET (view));
	else
		gdk_window_scroll (view->layout.bin_window, dx, dy);
		
	gtk_widget_get_pointer (GTK_WIDGET (view), &x, &y);
	ev_view_handle_cursor_over_xy (view, x, y);

	if (view->document)
		view_update_range_and_current_page (view);
}

GtkWidget*
ev_view_new (void)
{
	GtkWidget *view;

	view = g_object_new (EV_TYPE_VIEW, NULL);

	return view;
}

static void
setup_caches (EvView *view)
{
	gboolean inverted_colors;

	view->height_to_page_cache = ev_view_get_height_to_page_cache (view);
	view->pixbuf_cache = ev_pixbuf_cache_new (GTK_WIDGET (view), view->document);
	view->page_cache = ev_page_cache_new (view->document);
	inverted_colors = ev_document_model_get_inverted_colors (view->model);
	ev_pixbuf_cache_set_inverted_colors (view->pixbuf_cache, inverted_colors);
	g_signal_connect (view->pixbuf_cache, "job-finished", G_CALLBACK (job_finished_cb), view);
}

static void
clear_caches (EvView *view)
{
	if (view->pixbuf_cache) {
		g_object_unref (view->pixbuf_cache);
		view->pixbuf_cache = NULL;
	}

	if (view->page_cache) {
		g_object_unref (view->page_cache);
		view->page_cache = NULL;
	}
}

void
ev_view_set_loading (EvView 	  *view,
		     gboolean      loading)
{
	view->loading = loading;
	gtk_widget_queue_draw (GTK_WIDGET (view));
}

static gboolean
ev_view_autoscroll_cb (EvView *view)
{
	gdouble speed, value;

	/* If the user stops autoscrolling, autoscrolling will be
	 * set to false but the timeout will continue; stop the timeout: */
	if (!view->scroll_info.autoscrolling) {
		view->scroll_info.timeout_id = 0;
		return FALSE;
	}
	
	if (view->scroll_info.last_y > view->scroll_info.start_y && 
		(view->scroll_info.last_y < view->scroll_info.start_y))
		return TRUE; 

	/* Replace 100 with your speed of choice: The lower the faster.
	 * Replace 3 with another speed of choice: The higher, the faster it accelerated
	 * 	based on the distance of the starting point from the mouse
	 * (All also effected by the timeout interval of this callback) */

	if (view->scroll_info.start_y > view->scroll_info.last_y)
		speed = -pow ((((gdouble)view->scroll_info.start_y - view->scroll_info.last_y) / 100), 3);
	else
		speed = pow ((((gdouble)view->scroll_info.last_y - view->scroll_info.start_y) / 100), 3);
	
	value = gtk_adjustment_get_value (view->vadjustment);
	value = CLAMP (value + speed, 0, view->vadjustment->upper - view->vadjustment->page_size);
	gtk_adjustment_set_value (view->vadjustment, value);
	
	return TRUE;

}

void
ev_view_autoscroll_start (EvView *view)
{
	gint x, y;
	
	g_return_if_fail (EV_IS_VIEW (view));

	if (view->scroll_info.autoscrolling)
		return;
	
	view->scroll_info.autoscrolling = TRUE;
	view->scroll_info.timeout_id =
		g_timeout_add (20, (GSourceFunc)ev_view_autoscroll_cb,
			       view);
	
	gtk_widget_get_pointer (GTK_WIDGET (view), &x, &y);
	ev_view_handle_cursor_over_xy (view, x, y);
}

void
ev_view_autoscroll_stop (EvView *view)
{
	gint x, y;
	
	g_return_if_fail (EV_IS_VIEW (view));

	if (!view->scroll_info.autoscrolling)
		return;

	view->scroll_info.autoscrolling = FALSE;
	if (view->scroll_info.timeout_id) {
		g_source_remove (view->scroll_info.timeout_id);
		view->scroll_info.timeout_id = 0;
	}

	gtk_widget_get_pointer (GTK_WIDGET (view), &x, &y);
	ev_view_handle_cursor_over_xy (view, x, y);
}

static void
ev_view_document_changed_cb (EvDocumentModel *model,
			     GParamSpec      *pspec,
			     EvView          *view)
{
	EvDocument *document = ev_document_model_get_document (model);

	view->loading = FALSE;

	if (document != view->document) {
		gint current_page;

		clear_caches (view);

		if (view->document) {
			g_object_unref (view->document);
                }

		view->document = document;
		view->find_result = 0;

		if (view->document) {
			g_object_ref (view->document);
			setup_caches (view);
                }

		current_page = ev_document_model_get_page (model);
		if (view->current_page != current_page) {
			ev_view_change_page (view, current_page);
		} else {
			view->pending_scroll = SCROLL_TO_KEEP_POSITION;
			gtk_widget_queue_resize (GTK_WIDGET (view));
		}
	}
}

static void
ev_view_rotation_changed_cb (EvDocumentModel *model,
			     GParamSpec      *pspec,
			     EvView          *view)
{
	gint rotation = ev_document_model_get_rotation (model);

	view->rotation = rotation;

	if (view->pixbuf_cache) {
		ev_pixbuf_cache_clear (view->pixbuf_cache);
		if (!ev_document_is_page_size_uniform (view->document))
			view->pending_scroll = SCROLL_TO_PAGE_POSITION;
		gtk_widget_queue_resize (GTK_WIDGET (view));
	}

	ev_view_remove_all (view);

	if (rotation != 0)
		clear_selection (view);
}

static void
ev_view_inverted_colors_changed_cb (EvDocumentModel *model,
				    GParamSpec      *pspec,
				    EvView          *view)
{
	if (view->pixbuf_cache) {
		gboolean inverted_colors;

		inverted_colors = ev_document_model_get_inverted_colors (model);
		ev_pixbuf_cache_set_inverted_colors (view->pixbuf_cache, inverted_colors);
		gtk_widget_queue_draw (GTK_WIDGET (view));
	}
}

static void
ev_view_sizing_mode_changed_cb (EvDocumentModel *model,
				GParamSpec      *pspec,
				EvView          *view)
{
	EvSizingMode mode = ev_document_model_get_sizing_mode (model);

	view->sizing_mode = mode;
	if (mode != EV_SIZING_FREE)
		gtk_widget_queue_resize (GTK_WIDGET (view));
}

#define EPSILON 0.0000001
static void
ev_view_scale_changed_cb (EvDocumentModel *model,
			  GParamSpec      *pspec,
			  EvView          *view)
{
	gdouble scale = ev_document_model_get_scale (model);

	if (ABS (view->scale - scale) < EPSILON)
		return;

	if (view->loading_text) {
		cairo_surface_destroy (view->loading_text);
		view->loading_text = NULL;
	}

	view->scale = scale;

	view->pending_resize = TRUE;
	if (view->sizing_mode == EV_SIZING_FREE)
		gtk_widget_queue_resize (GTK_WIDGET (view));
}

static void
ev_view_continuous_changed_cb (EvDocumentModel *model,
			       GParamSpec      *pspec,
			       EvView          *view)
{
	gboolean continuous = ev_document_model_get_continuous (model);

	view->continuous = continuous;
	view->pending_scroll = SCROLL_TO_PAGE_POSITION;
	gtk_widget_queue_resize (GTK_WIDGET (view));
}

static void
ev_view_dual_page_changed_cb (EvDocumentModel *model,
			      GParamSpec      *pspec,
			      EvView          *view)
{
	gboolean dual_page = ev_document_model_get_dual_page (model);

	view->dual_page = dual_page;
	view->pending_scroll = SCROLL_TO_PAGE_POSITION;
	/* FIXME: if we're keeping the pixbuf cache around, we should extend the
	 * preload_cache_size to be 2 if dual_page is set.
	 */
	gtk_widget_queue_resize (GTK_WIDGET (view));
}

static void
ev_view_fullscreen_changed_cb (EvDocumentModel *model,
			       GParamSpec      *pspec,
			       EvView          *view)
{
	gboolean fullscreen = ev_document_model_get_fullscreen (model);

	view->fullscreen = fullscreen;
	gtk_widget_queue_resize (GTK_WIDGET (view));
}

void
ev_view_set_model (EvView          *view,
		   EvDocumentModel *model)
{
	g_return_if_fail (EV_IS_VIEW (view));
	g_return_if_fail (EV_IS_DOCUMENT_MODEL (model));

	if (model == view->model)
		return;

	if (view->model) {
		g_signal_handlers_disconnect_by_func (view->model,
						      ev_view_document_changed_cb,
						      view);
		g_signal_handlers_disconnect_by_func (view->model,
						      ev_view_page_changed_cb,
						      view);
		g_object_unref (view->model);
	}
	view->model = g_object_ref (model);

	/* Initialize view from model */
	view->rotation = ev_document_model_get_rotation (view->model);
	view->sizing_mode = ev_document_model_get_sizing_mode (view->model);
	view->scale = ev_document_model_get_scale (view->model);
	view->continuous = ev_document_model_get_continuous (view->model);
	view->dual_page = ev_document_model_get_dual_page (view->model);
	view->fullscreen = ev_document_model_get_fullscreen (view->model);
	ev_view_document_changed_cb (view->model, NULL, view);

	g_signal_connect (view->model, "notify::document",
			  G_CALLBACK (ev_view_document_changed_cb),
			  view);
	g_signal_connect (view->model, "notify::rotation",
			  G_CALLBACK (ev_view_rotation_changed_cb),
			  view);
	g_signal_connect (view->model, "notify::inverted-colors",
			  G_CALLBACK (ev_view_inverted_colors_changed_cb),
			  view);
	g_signal_connect (view->model, "notify::sizing-mode",
			  G_CALLBACK (ev_view_sizing_mode_changed_cb),
			  view);
	g_signal_connect (view->model, "notify::scale",
			  G_CALLBACK (ev_view_scale_changed_cb),
			  view);
	g_signal_connect (view->model, "notify::continuous",
			  G_CALLBACK (ev_view_continuous_changed_cb),
			  view);
	g_signal_connect (view->model, "notify::dual-page",
			  G_CALLBACK (ev_view_dual_page_changed_cb),
			  view);
	g_signal_connect (view->model, "notify::fullscreen",
			  G_CALLBACK (ev_view_fullscreen_changed_cb),
			  view);
	g_signal_connect (view->model, "page-changed",
			  G_CALLBACK (ev_view_page_changed_cb),
			  view);
}

static void
ev_view_reload_page (EvView    *view,
		     gint       page,
		     GdkRegion *region)
{
	ev_pixbuf_cache_reload_page (view->pixbuf_cache,
				     region,
				     page,
				     view->rotation,
				     view->scale);
}

void
ev_view_reload (EvView *view)
{
	ev_pixbuf_cache_clear (view->pixbuf_cache);
	view_update_range_and_current_page (view);
}

/*** Zoom and sizing mode ***/

gboolean
ev_view_can_zoom_in (EvView *view)
{
	return view->scale * ZOOM_IN_FACTOR <= ev_document_model_get_max_scale (view->model);
}

gboolean
ev_view_can_zoom_out (EvView *view)
{
	return view->scale * ZOOM_OUT_FACTOR >= ev_document_model_get_min_scale (view->model);
}

void
ev_view_zoom_in (EvView *view)
{
	gdouble scale;

	g_return_if_fail (view->sizing_mode == EV_SIZING_FREE);

	view->pending_scroll = SCROLL_TO_CENTER;
	scale = ev_document_model_get_scale (view->model) * ZOOM_IN_FACTOR;
	ev_document_model_set_scale (view->model, scale);
}

void
ev_view_zoom_out (EvView *view)
{
	gdouble scale;

	g_return_if_fail (view->sizing_mode == EV_SIZING_FREE);

	view->pending_scroll = SCROLL_TO_CENTER;
	scale = ev_document_model_get_scale (view->model) * ZOOM_OUT_FACTOR;
	ev_document_model_set_scale (view->model, scale);
}

static double
zoom_for_size_fit_width (gdouble doc_width,
			 gdouble doc_height,
			 int     target_width,
			 int     target_height)
{
	return (double)target_width / doc_width;
}

static double
zoom_for_size_fit_height (gdouble doc_width,
			  gdouble doc_height,
			  int     target_width,
			  int     target_height)
{
	return (double)target_height / doc_height;
}

static double
zoom_for_size_best_fit (gdouble doc_width,
			gdouble doc_height,
			int     target_width,
			int     target_height)
{
	double w_scale;
	double h_scale;

	w_scale = (double)target_width / doc_width;
	h_scale = (double)target_height / doc_height;

	return MIN (w_scale, h_scale);
}

static void
ev_view_zoom_for_size_continuous_and_dual_page (EvView *view,
						int     width,
						int     height)
{
	gdouble doc_width, doc_height;
	GtkBorder border;
	gdouble scale;

	ev_document_get_max_page_size (view->document, &doc_width, &doc_height);
	if (view->rotation == 90 || view->rotation == 270) {
		gdouble tmp;

		tmp = doc_width;
		doc_width = doc_height;
		doc_height = tmp;
	}

	compute_border (view, doc_width, doc_height, &border);

	doc_width *= 2;
	width -= (2 * (border.left + border.right) + 3 * view->spacing);
	height -= (border.top + border.bottom + 2 * view->spacing - 1);

	if (view->sizing_mode == EV_SIZING_FIT_WIDTH) {
		gint sb_size;

		sb_size = ev_view_get_scrollbar_size (view, GTK_ORIENTATION_VERTICAL);
		scale = zoom_for_size_fit_width (doc_width, doc_height, width - sb_size, height);
	} else if (view->sizing_mode == EV_SIZING_BEST_FIT)
		scale = zoom_for_size_best_fit (doc_width, doc_height, width, height);
	else
		g_assert_not_reached ();

	ev_document_model_set_scale (view->model, scale);
}

static void
ev_view_zoom_for_size_continuous (EvView *view,
				  int     width,
				  int     height)
{
	gdouble doc_width, doc_height;
	GtkBorder border;
	gdouble scale;

	ev_document_get_max_page_size (view->document, &doc_width, &doc_height);
	if (view->rotation == 90 || view->rotation == 270) {
		gdouble tmp;

		tmp = doc_width;
		doc_width = doc_height;
		doc_height = tmp;
	}

	compute_border (view, doc_width, doc_height, &border);

	width -= (border.left + border.right + 2 * view->spacing);
	height -= (border.top + border.bottom + 2 * view->spacing - 1);

	if (view->sizing_mode == EV_SIZING_FIT_WIDTH) {
		gint sb_size;

		sb_size = ev_view_get_scrollbar_size (view, GTK_ORIENTATION_VERTICAL);
		scale = zoom_for_size_fit_width (doc_width, doc_height, width - sb_size, height);
	} else if (view->sizing_mode == EV_SIZING_BEST_FIT)
		scale = zoom_for_size_best_fit (doc_width, doc_height, width, height);
	else
		g_assert_not_reached ();

	ev_document_model_set_scale (view->model, scale);
}

static void
ev_view_zoom_for_size_dual_page (EvView *view,
				 int     width,
				 int     height)
{
	GtkBorder border;
	gdouble doc_width, doc_height;
	gdouble scale;
	gint other_page;

	other_page = view->current_page ^ 1;

	/* Find the largest of the two. */
	get_doc_page_size (view, view->current_page, &doc_width, &doc_height);
	if (other_page < ev_document_get_n_pages (view->document)) {
		gdouble width_2, height_2;

		get_doc_page_size (view, other_page, &width_2, &height_2);
		if (width_2 > doc_width)
			doc_width = width_2;
		if (height_2 > doc_height)
			doc_height = height_2;
	}
	compute_border (view, width, height, &border);

	doc_width = doc_width * 2;
	width -= ((border.left + border.right)* 2 + 3 * view->spacing);
	height -= (border.top + border.bottom + 2 * view->spacing);

	if (view->sizing_mode == EV_SIZING_FIT_WIDTH) {
		gint sb_size;

		sb_size = ev_view_get_scrollbar_size (view, GTK_ORIENTATION_VERTICAL);
		scale = zoom_for_size_fit_width (doc_width, doc_height, width - sb_size, height);
	} else if (view->sizing_mode == EV_SIZING_BEST_FIT)
		scale = zoom_for_size_best_fit (doc_width, doc_height, width, height);
	else
		g_assert_not_reached ();

	ev_document_model_set_scale (view->model, scale);
}

static void
ev_view_zoom_for_size_single_page (EvView *view,
				   int     width,
				   int     height)
{
	gdouble doc_width, doc_height;
	GtkBorder border;
	gdouble scale;

	get_doc_page_size (view, view->current_page, &doc_width, &doc_height);

	/* Get an approximate border */
	compute_border (view, width, height, &border);

	width -= (border.left + border.right + 2 * view->spacing);
	height -= (border.top + border.bottom + 2 * view->spacing);

	if (view->sizing_mode == EV_SIZING_FIT_WIDTH) {
		gint sb_size;

		sb_size = ev_view_get_scrollbar_size (view, GTK_ORIENTATION_VERTICAL);
		scale = zoom_for_size_fit_width (doc_width, doc_height, width - sb_size, height);
	} else if (view->sizing_mode == EV_SIZING_BEST_FIT)
		scale = zoom_for_size_best_fit (doc_width, doc_height, width, height);
	else
		g_assert_not_reached ();

	ev_document_model_set_scale (view->model, scale);
}

static void
ev_view_zoom_for_size (EvView *view,
		       int     width,
		       int     height)
{
	g_return_if_fail (EV_IS_VIEW (view));
	g_return_if_fail (view->sizing_mode == EV_SIZING_FIT_WIDTH ||
			  view->sizing_mode == EV_SIZING_BEST_FIT);
	g_return_if_fail (width >= 0);
	g_return_if_fail (height >= 0);

	if (view->document == NULL)
		return;

	if (view->continuous && view->dual_page)
		ev_view_zoom_for_size_continuous_and_dual_page (view, width, height);
	else if (view->continuous)
		ev_view_zoom_for_size_continuous (view, width, height);
	else if (view->dual_page)
		ev_view_zoom_for_size_dual_page (view, width, height);
	else
		ev_view_zoom_for_size_single_page (view, width, height);
}

/*** Find ***/
static gint
ev_view_find_get_n_results (EvView *view, gint page)
{
	return view->find_pages ? g_list_length (view->find_pages[page]) : 0;
}

static EvRectangle *
ev_view_find_get_result (EvView *view, gint page, gint result)
{
	return view->find_pages ? (EvRectangle *) g_list_nth_data (view->find_pages[page], result) : NULL;
}

static void
jump_to_find_result (EvView *view)
{
	gint n_results;
	gint page = view->current_page;

	n_results = ev_view_find_get_n_results (view, page);

	if (n_results > 0 && view->find_result < n_results) {
		EvRectangle *rect;
		GdkRectangle view_rect;

		rect = ev_view_find_get_result (view, page, view->find_result);
		doc_rect_to_view_rect (view, page, rect, &view_rect);
		ensure_rectangle_is_visible (view, &view_rect);
		view->jump_to_find_result = FALSE;
	}
}

/**
 * jump_to_find_page
 * @view: #EvView instance
 * @direction: Direction to look
 * @shift: Shift from current page
 *
 * Jumps to the first page that has occurences of searched word.
 * Uses a direction where to look and a shift from current page. 
 *
 */
static void
jump_to_find_page (EvView *view, EvViewFindDirection direction, gint shift)
{
	int n_pages, i;

	n_pages = ev_document_get_n_pages (view->document);

	for (i = 0; i < n_pages; i++) {
		int page;
		
		if (direction == EV_VIEW_FIND_NEXT)
			page = view->current_page + i;
		else
			page = view->current_page - i;		
		page += shift;
		
		if (page >= n_pages) {
			page = page - n_pages;
		} else if (page < 0) 
			page = page + n_pages;

		if (ev_view_find_get_n_results (view, page) > 0) {
			ev_document_model_set_page (view->model, page);
			break;
		}
	}
}

void
ev_view_find_changed (EvView *view, GList **results, gint page)
{
	view->find_pages = results;
	
	if (view->jump_to_find_result == TRUE) {
		jump_to_find_page (view, EV_VIEW_FIND_NEXT, 0);
		jump_to_find_result (view);
	}

	if (view->current_page == page)
		gtk_widget_queue_draw (GTK_WIDGET (view));
}

void
ev_view_find_next (EvView *view)
{
	gint n_results;

	n_results = ev_view_find_get_n_results (view, view->current_page);
	view->find_result++;

	if (view->find_result >= n_results) {
		view->find_result = 0;
		jump_to_find_page (view, EV_VIEW_FIND_NEXT, 1);
		jump_to_find_result (view);
	} else {
		jump_to_find_result (view);
		gtk_widget_queue_draw (GTK_WIDGET (view));
	}
}

void
ev_view_find_previous (EvView *view)
{
	view->find_result--;

	if (view->find_result < 0) {
		jump_to_find_page (view, EV_VIEW_FIND_PREV, -1);
		view->find_result = MAX (0, ev_view_find_get_n_results (view, view->current_page) - 1);
		jump_to_find_result (view);
	} else {
		jump_to_find_result (view);
		gtk_widget_queue_draw (GTK_WIDGET (view));
	}
}

void
ev_view_find_search_changed (EvView *view)
{
	/* search string has changed, focus on new search result */
	view->jump_to_find_result = TRUE;
	view->find_pages = NULL;
}

void
ev_view_find_set_highlight_search (EvView *view, gboolean value)
{
	view->highlight_find_results = value;
	gtk_widget_queue_draw (GTK_WIDGET (view));
}

void
ev_view_find_cancel (EvView *view)
{
	view->find_pages = NULL;
}

/*** Selections ***/

/* compute_new_selection_rect/text calculates the area currently selected by
 * view_rect.  each handles a different mode;
 */
static GList *
compute_new_selection_rect (EvView       *view,
			    GdkPoint     *start,
			    GdkPoint     *stop)
{
	GdkRectangle view_rect;
	int n_pages, i;
	GList *list = NULL;

	g_assert (view->selection_mode == EV_VIEW_SELECTION_RECTANGLE);
	
	view_rect.x = MIN (start->x, stop->x);
	view_rect.y = MIN (start->y, stop->y);
	view_rect.width = MAX (start->x, stop->x) - view_rect.x;
	view_rect.width = MAX (start->y, stop->y) - view_rect.y;

	n_pages = ev_document_get_n_pages (view->document);

	for (i = 0; i < n_pages; i++) {
		GdkRectangle page_area;
		GtkBorder border;
		
		if (get_page_extents (view, i, &page_area, &border)) {
			GdkRectangle overlap;

			if (gdk_rectangle_intersect (&page_area, &view_rect, &overlap)) {
				EvViewSelection *selection;

				selection = g_new0 (EvViewSelection, 1);
				selection->page = i;
				view_rect_to_doc_rect (view, &overlap, &page_area,
						       &(selection->rect));

				list = g_list_append (list, selection);
			}
		}
	}

	return list;
}

static gboolean
gdk_rectangle_point_in (GdkRectangle *rectangle,
			GdkPoint     *point)
{
	return rectangle->x <= point->x &&
		rectangle->y <= point->y &&
		point->x < rectangle->x + rectangle->width &&
		point->y < rectangle->y + rectangle->height;
}

static GList *
compute_new_selection_text (EvView          *view,
			    EvSelectionStyle style,
			    GdkPoint        *start,
			    GdkPoint        *stop)
{
	int n_pages, i, first, last;
	GList *list = NULL;
	EvViewSelection *selection;
	gdouble width, height;
	int start_page, end_page;

	g_assert (view->selection_mode == EV_VIEW_SELECTION_TEXT);

	n_pages = ev_document_get_n_pages (view->document);

	/* First figure out the range of pages the selection
	 * affects. */
	first = n_pages;
	last = 0;
	if (view->continuous) {
		start_page = 0;
		end_page = n_pages;
	} else if (view->dual_page) {
		start_page = view->start_page;
		end_page = view->end_page + 1;
	} else {
		start_page = view->current_page;
		end_page = view->current_page + 1;
	}

	for (i = start_page; i < end_page; i++) {
		GdkRectangle page_area;
		GtkBorder border;
		
		get_page_extents (view, i, &page_area, &border);
		if (gdk_rectangle_point_in (&page_area, start) || 
		    gdk_rectangle_point_in (&page_area, stop)) {
			if (first == n_pages)
				first = i;
			last = i;
		}

	}

	/* Now create a list of EvViewSelection's for the affected
	 * pages.  This could be an empty list, a list of just one
	 * page or a number of pages.*/
	for (i = first; i < last + 1; i++) {
		GdkRectangle page_area;
		GtkBorder border;
		GdkPoint *point;

		get_doc_page_size (view, i, &width, &height);

		selection = g_new0 (EvViewSelection, 1);
		selection->page = i;
		selection->style = style;
		selection->rect.x1 = selection->rect.y1 = 0;
		selection->rect.x2 = width;
		selection->rect.y2 = height;

		get_page_extents (view, i, &page_area, &border);

		if (gdk_rectangle_point_in (&page_area, start))
			point = start;
		else
			point = stop;

		if (i == first)
			view_point_to_doc_point (view, point, &page_area,
						 &selection->rect.x1,
						 &selection->rect.y1);

		/* If the selection is contained within just one page,
		 * make sure we don't write 'start' into both points
		 * in selection->rect. */
		if (first == last)
			point = stop;

		if (i == last)
			view_point_to_doc_point (view, point, &page_area,
						 &selection->rect.x2,
						 &selection->rect.y2);

		list = g_list_append (list, selection);
	}

	return list;
}

/* This function takes the newly calculated list, and figures out which regions
 * have changed.  It then queues a redraw approporiately.
 */
static void
merge_selection_region (EvView *view,
			GList  *new_list)
{
	GList *old_list;
	GList *new_list_ptr, *old_list_ptr;

	/* Update the selection */
	old_list = ev_pixbuf_cache_get_selection_list (view->pixbuf_cache);
	g_list_foreach (view->selection_info.selections, (GFunc)selection_free, NULL);
	g_list_free (view->selection_info.selections);
	view->selection_info.selections = new_list;
	ev_pixbuf_cache_set_selection_list (view->pixbuf_cache, new_list);
	g_signal_emit (view, signals[SIGNAL_SELECTION_CHANGED], 0, NULL);

	new_list_ptr = new_list;
	old_list_ptr = old_list;

	while (new_list_ptr || old_list_ptr) {
		EvViewSelection *old_sel, *new_sel;
		int cur_page;
		GdkRegion *region = NULL;

		new_sel = (new_list_ptr) ? (new_list_ptr->data) : NULL;
		old_sel = (old_list_ptr) ? (old_list_ptr->data) : NULL;

		/* Assume that the lists are in order, and we run through them
		 * comparing them, one page at a time.  We come out with the
		 * first page we see. */
		if (new_sel && old_sel) {
			if (new_sel->page < old_sel->page) {
				new_list_ptr = new_list_ptr->next;
				old_sel = NULL;
			} else if (new_sel->page > old_sel->page) {
				old_list_ptr = old_list_ptr->next;
				new_sel = NULL;
			} else {
				new_list_ptr = new_list_ptr->next;
				old_list_ptr = old_list_ptr->next;
			}
		} else if (new_sel) {
			new_list_ptr = new_list_ptr->next;
		} else if (old_sel) {
			old_list_ptr = old_list_ptr->next;
		}

		g_assert (new_sel || old_sel);

		/* is the page we're looking at on the screen?*/
		cur_page = new_sel ? new_sel->page : old_sel->page;
		if (cur_page < view->start_page || cur_page > view->end_page)
			continue;

		/* seed the cache with a new page.  We are going to need the new
		 * region too. */
		if (new_sel) {
			GdkRegion *tmp_region = NULL;

			ev_pixbuf_cache_get_selection_surface (view->pixbuf_cache,
							       cur_page,
							       view->scale,
							       &tmp_region);

			if (tmp_region && !gdk_region_empty (tmp_region)) {
				new_sel->covered_region = gdk_region_copy (tmp_region);
			}
		}

		/* Now we figure out what needs redrawing */
		if (old_sel && new_sel) {
			if (old_sel->covered_region && new_sel->covered_region) {
				/* We only want to redraw the areas that have
				 * changed, so we xor the old and new regions
				 * and redraw if it's different */
				region = gdk_region_copy (old_sel->covered_region);
				gdk_region_xor (region, new_sel->covered_region);
				if (gdk_region_empty (region)) {
					gdk_region_destroy (region);
					region = NULL;
				}
			} else if (old_sel->covered_region) {
				region = gdk_region_copy (old_sel->covered_region);
			} else if (new_sel->covered_region) {
				region = gdk_region_copy (new_sel->covered_region);
			}
		} else if (old_sel && !new_sel) {
			if (old_sel->covered_region && !gdk_region_empty (old_sel->covered_region)) {
				region = gdk_region_copy (old_sel->covered_region);
			}
		} else if (!old_sel && new_sel) {
			if (new_sel->covered_region && !gdk_region_empty (new_sel->covered_region)) {
				region = gdk_region_copy (new_sel->covered_region);
			}
		} else {
			g_assert_not_reached ();
		}

		/* Redraw the damaged region! */
		if (region) {
			GdkRectangle page_area;
			GtkBorder border;

			/* I don't know why but the region is smaller
			 * than expected. This hack fixes it, I guess
			 * 10 pixels more won't hurt
			 */
			gdk_region_shrink (region, -5, -5);

			get_page_extents (view, cur_page, &page_area, &border);
			gdk_region_offset (region,
					   page_area.x + border.left - view->scroll_x,
					   page_area.y + border.top - view->scroll_y);
			gdk_window_invalidate_region (view->layout.bin_window, region, TRUE);
			gdk_region_destroy (region);
		}
	}

	/* Free the old list, now that we're done with it. */
	g_list_foreach (old_list, (GFunc) selection_free, NULL);
	g_list_free (old_list);
}

static void
compute_selections (EvView          *view,
		    EvSelectionStyle style,
		    GdkPoint        *start,
		    GdkPoint        *stop)
{
	GList *list;

	if (view->selection_mode == EV_VIEW_SELECTION_RECTANGLE)
		list = compute_new_selection_rect (view, start, stop);
	else
		list = compute_new_selection_text (view, style, start, stop);
	merge_selection_region (view, list);
}

/* Free's the selection.  It's up to the caller to queue redraws if needed.
 */
static void
selection_free (EvViewSelection *selection)
{
	if (selection->covered_region)
		gdk_region_destroy (selection->covered_region);
	g_free (selection);
}

static void
clear_selection (EvView *view)
{
	if (view->selection_info.selections) {
		g_list_foreach (view->selection_info.selections, (GFunc)selection_free, NULL);
		g_list_free (view->selection_info.selections);
		view->selection_info.selections = NULL;

		g_signal_emit (view, signals[SIGNAL_SELECTION_CHANGED], 0, NULL);
	}
	view->selection_info.in_selection = FALSE;
	if (view->pixbuf_cache)
		ev_pixbuf_cache_set_selection_list (view->pixbuf_cache, NULL);
}

void
ev_view_select_all (EvView *view)
{
	GList *selections = NULL;
	int n_pages, i;

	/* Disable selection on rotated pages for the 0.4.0 series */
	if (view->rotation != 0)
		return;

	clear_selection (view);
	
	n_pages = ev_document_get_n_pages (view->document);
	for (i = 0; i < n_pages; i++) {
		gdouble width, height;
		EvViewSelection *selection;

		get_doc_page_size (view, i, &width, &height);

		selection = g_new0 (EvViewSelection, 1);
		selection->page = i;
		selection->style = EV_SELECTION_STYLE_GLYPH;
		selection->rect.x1 = selection->rect.y1 = 0;
		selection->rect.x2 = width;
		selection->rect.y2 = height;

		selections = g_list_append (selections, selection);
	}

	merge_selection_region (view, selections);
	gtk_widget_queue_draw (GTK_WIDGET (view));
}

gboolean
ev_view_get_has_selection (EvView *view)
{
	return view->selection_info.selections != NULL;
}

static char *
get_selected_text (EvView *view)
{
	GString *text;
	GList *l;
	gchar *normalized_text;
	EvRenderContext *rc;

	text = g_string_new (NULL);
	rc = ev_render_context_new (NULL, view->rotation, view->scale);

	ev_document_doc_mutex_lock ();

	for (l = view->selection_info.selections; l != NULL; l = l->next) {
		EvViewSelection *selection = (EvViewSelection *)l->data;
		EvPage *page;
		gchar *tmp;

		page = ev_document_get_page (view->document, selection->page);
		ev_render_context_set_page (rc, page);
		g_object_unref (page);
		
		tmp = ev_selection_get_selected_text (EV_SELECTION (view->document),
						      rc, selection->style,
						      &(selection->rect));

		g_string_append (text, tmp);
		g_free (tmp);
	}

	g_object_unref (rc);
	
	ev_document_doc_mutex_unlock ();
	
	normalized_text = g_utf8_normalize (text->str, text->len, G_NORMALIZE_NFKC);
	g_string_free (text, TRUE);
	return normalized_text;
}

static void
ev_view_clipboard_copy (EvView      *view,
			const gchar *text)
{
	GtkClipboard *clipboard;

	clipboard = gtk_widget_get_clipboard (GTK_WIDGET (view),
					      GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text (clipboard, text, -1);
}

void
ev_view_copy (EvView *ev_view)
{
	char *text;

	if (!EV_IS_SELECTION (ev_view->document))
		return;

	text = get_selected_text (ev_view);
	ev_view_clipboard_copy (ev_view, text);
	g_free (text);
}

static void
ev_view_primary_get_cb (GtkClipboard     *clipboard,
			GtkSelectionData *selection_data,
			guint             info,
			gpointer          data)
{
	EvView *ev_view = EV_VIEW (data);

	if (ev_view->link_selected) {
		gtk_selection_data_set_text (selection_data,
					     ev_link_action_get_uri (ev_view->link_selected),
					     -1);
	} else if (EV_IS_SELECTION (ev_view->document) &&
		   ev_view->selection_info.selections) {
		gchar *text;
		
		text = get_selected_text (ev_view);
		if (text) {
			gtk_selection_data_set_text (selection_data, text, -1);
			g_free (text);
		}
	}
}

static void
ev_view_primary_clear_cb (GtkClipboard *clipboard,
			  gpointer      data)
{
	EvView *view = EV_VIEW (data);

	clear_selection (view);
	clear_link_selected (view);
}

static void
ev_view_update_primary_selection (EvView *ev_view)
{
	GtkClipboard *clipboard;

	clipboard = gtk_widget_get_clipboard (GTK_WIDGET (ev_view),
                                              GDK_SELECTION_PRIMARY);

	if (ev_view->selection_info.selections || ev_view->link_selected) {
                GtkTargetList *target_list;
                GtkTargetEntry *targets;
                int n_targets;

                target_list = gtk_target_list_new (NULL, 0);
                gtk_target_list_add_text_targets (target_list, 0);
                targets = gtk_target_table_new_from_list (target_list, &n_targets);
                gtk_target_list_unref (target_list);
                
		if (!gtk_clipboard_set_with_owner (clipboard,
						   targets, n_targets,
						   ev_view_primary_get_cb,
						   ev_view_primary_clear_cb,
						   G_OBJECT (ev_view)))
			ev_view_primary_clear_cb (clipboard, ev_view);

                gtk_target_table_free (targets, n_targets);
	} else {
		if (gtk_clipboard_get_owner (clipboard) == G_OBJECT (ev_view))
			gtk_clipboard_clear (clipboard);
	}
}

static void
clear_link_selected (EvView *view)
{
	if (view->link_selected) {
		g_object_unref (view->link_selected);
		view->link_selected = NULL;
	}
}

void
ev_view_copy_link_address (EvView       *view,
			   EvLinkAction *action)
{
	clear_link_selected (view);
	
	ev_view_clipboard_copy (view, ev_link_action_get_uri (action));
	
	view->link_selected = g_object_ref (action);
	ev_view_update_primary_selection (view);
}

/*** Cursor operations ***/
static void
ev_view_set_cursor (EvView *view, EvViewCursor new_cursor)
{
	GdkCursor *cursor = NULL;
	GtkWidget *widget;

	if (view->cursor == new_cursor) {
		return;
	}

	view->cursor = new_cursor;

	widget = gtk_widget_get_toplevel (GTK_WIDGET (view));
	cursor = ev_view_cursor_new (gtk_widget_get_display (widget), new_cursor);
	gdk_window_set_cursor (view->layout.bin_window, cursor);
	gdk_flush ();
	if (cursor)
		gdk_cursor_unref (cursor);
}

void
ev_view_hide_cursor (EvView *view)
{
       ev_view_set_cursor (view, EV_VIEW_CURSOR_HIDDEN);
}

void
ev_view_show_cursor (EvView *view)
{
       ev_view_set_cursor (view, EV_VIEW_CURSOR_NORMAL);
}

gboolean
ev_view_next_page (EvView *view)
{
	int page, n_pages;

	g_return_val_if_fail (EV_IS_VIEW (view), FALSE);
	
	if (!view->document)
		return FALSE;

	page = ev_document_model_get_page (view->model);
	n_pages = ev_document_get_n_pages (view->document);

	if (view->dual_page)
	        page = page + 2; 
	else 
		page = page + 1;

	if (page < n_pages) {
		ev_document_model_set_page (view->model, page);
		return TRUE;
	} else if (view->dual_page && page == n_pages) {
		ev_document_model_set_page (view->model, page - 1);
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
ev_view_previous_page (EvView *view)
{
	int page;

	g_return_val_if_fail (EV_IS_VIEW (view), FALSE);

	if (!view->document)
		return FALSE;

	page = ev_document_model_get_page (view->model);

	if (view->dual_page)
	        page = page - 2; 
	else 
		page = page - 1;

	if (page >= 0) {
		ev_document_model_set_page (view->model, page);
		return TRUE;
	} else if (view->dual_page && page == -1) {
		ev_document_model_set_page (view->model, 0);
		return TRUE;
	} else {	
		return FALSE;
	}
}
		
static gint is_unchanged_uri_char(char c)
{
  return isalnum(c);
}

static void encode_uri(gchar *encoded_uri, gint bufsize, const gchar *uri)
{
  int i;
  int k;
  
  k = 0;
  assert(encoded_uri != NULL);
  assert(uri != NULL);
  for(i = 0; i < strlen(uri) ; i++) {
    if (is_unchanged_uri_char(uri[i])) {
      if (k + 2 >= bufsize)
        break;
      encoded_uri[k++] = uri[i];
    }
    else {
      char * hexa = "0123456789ABCDEF";
      
      if (k + 4 >= bufsize)
        break;
      encoded_uri[k++] = '%';
      encoded_uri[k++] = hexa[(unsigned char)uri[i] / 16];
      encoded_uri[k++] = hexa[(unsigned char)uri[i] % 16];
    }
  }
  encoded_uri[k] = 0;
}



void
ev_view_annotate (EvView *ev_view, gchar *uri, int page)
{
#define ANN_MAX_BUFFER_LEN	 1000
	char *text;
	char *tempSel;
	char *tempFileName;
	char *tempCommandLine;
	GError  *error = NULL;
	

	if (ev_view == NULL) 
		return;

	EvDocumentInfo  *p = ev_document_get_info(ev_view->document);

	tempSel = g_malloc(ANN_MAX_BUFFER_LEN);
	tempFileName = g_malloc(strlen(uri) * 4);

	if (!EV_IS_SELECTION (ev_view->document))  {
		strcmp(tempSel,  ""); 
		text = "";
	}
	else {
		text = get_selected_text (ev_view);
		encode_uri(tempSel, ANN_MAX_BUFFER_LEN, text);
	}
	/// encode filename
#define ANN_FILE_PREFIX "file://"
	if (strncmp(uri,ANN_FILE_PREFIX, strlen(ANN_FILE_PREFIX) ) == 0) {
		// skip the prefix
		encode_uri(tempFileName, 
			   ANN_MAX_BUFFER_LEN, uri+strlen(ANN_FILE_PREFIX));
	} else {
		encode_uri(tempFileName, ANN_MAX_BUFFER_LEN, uri);
	}

	tempCommandLine = g_malloc(strlen(tempSel) + strlen(tempFileName) + 200);

	printf("remember::::%s::::%s::::%s::::%d\n", p->title, uri, text, page);
	sprintf(tempCommandLine, "emacsclient 'org-protocol://remember://docview:%s::%d'", tempFileName, page+1);
	printf("temp: [%s]\n", tempCommandLine);

	if (!g_spawn_command_line_async (tempCommandLine, &error)) {
		g_printerr ("Cannot start emacsclient: %s\n", error->message);
		g_error_free (error);
	}

	g_free (text);
	g_free (tempSel);
	g_free (tempCommandLine);
	g_free (tempFileName);


#ifdef fork
	pid_t pID = fork();
	if (pID == 0) {
		// Code only executed by child process
		// this is the child
	} else if (pID < 0) {           // failed to fork		
		fprintf(stderr,"Failed to fork");
	} else {
      // Code only executed by parent process
	}
#endif	

}
