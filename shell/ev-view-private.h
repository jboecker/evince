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

#ifndef __EV_VIEW_PRIVATE_H__
#define __EV_VIEW_PRIVATE_H__

#include "ev-view.h"
#include "ev-pixbuf-cache.h"
#include "ev-page-cache.h"

/* Information for middle clicking and moving around the doc */
typedef struct {
        gboolean in_drag;
	GdkPoint start;
	gdouble hadj;
	gdouble vadj;
} DragInfo;

/* Information for handling selection */
typedef struct {
	gboolean in_selection;
	gboolean in_drag;
	GdkPoint start;
	GList *selections;
} SelectionInfo;

typedef enum {
	SCROLL_TO_KEEP_POSITION,
	SCROLL_TO_CURRENT_PAGE,
	SCROLL_TO_CENTER
} PendingScroll;

typedef enum {
	EV_VIEW_CURSOR_NORMAL,
	EV_VIEW_CURSOR_IBEAM,
	EV_VIEW_CURSOR_LINK,
	EV_VIEW_CURSOR_WAIT,
	EV_VIEW_CURSOR_HIDDEN,
	EV_VIEW_CURSOR_DRAG
} EvViewCursor;

struct _EvView {
	GtkWidget parent_instance;

	EvDocument *document;

	char *status;
	char *find_status;

	/* Scrolling */
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;

	gint scroll_x;
	gint scroll_y;

	/* Information for middle clicking and dragging around. */
	DragInfo drag_info;

	/* Selection */
	gint motion_x;
	gint motion_y;
	guint selection_update_id;

	EvViewSelectionMode selection_mode;
	SelectionInfo selection_info;

	int pressed_button;
	EvViewCursor cursor;
	GtkWidget *link_tooltip;
	EvLink *hovered_link;

	EvPageCache *page_cache;
	EvPixbufCache *pixbuf_cache;

	gint start_page;
	gint end_page;
	gint current_page;

	EvJobRender *current_job;

	int find_page;
	int find_result;
	int spacing;

	int rotation;
	double scale;

	gboolean continuous;
	gboolean dual_page;
	gboolean fullscreen;
	gboolean presentation;
	EvSizingMode sizing_mode;

	PendingScroll pending_scroll;
	gboolean pending_resize;
};

struct _EvViewClass {
	GtkWidgetClass parent_class;

	void	(*set_scroll_adjustments) (EvView         *view,
					   GtkAdjustment  *hadjustment,
					   GtkAdjustment  *vadjustment);
	void    (*binding_activated)	  (EvView         *view,
					   GtkScrollType   scroll,
					   gboolean        horizontal);
	void    (*zoom_invalid)		  (EvView         *view);
	void    (*external_link)	  (EvView         *view,
					   EvLink         *link);
};

#endif  /* __EV_VIEW_PRIVATE_H__ */
