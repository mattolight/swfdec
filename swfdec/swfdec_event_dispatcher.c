/* Swfdec
 * Copyright (C) 2008 Benjamin Otte <otte@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "swfdec_event_dispatcher.h"
#include "swfdec_debug.h"


G_DEFINE_TYPE (SwfdecEventDispatcher, swfdec_event_dispatcher, SWFDEC_TYPE_GC_OBJECT)

static void
swfdec_event_dispatcher_class_init (SwfdecEventDispatcherClass *klass)
{
}

static void
swfdec_event_dispatcher_init (SwfdecEventDispatcher *event_dispatcher)
{
}

