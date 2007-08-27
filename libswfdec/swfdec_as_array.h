/* Swfdec
 * Copyright (C) 2007 Benjamin Otte <otte@gnome.org>
 *		 2007 Pekka Lampila <pekka.lampila@iki.fi>
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

#ifndef _SWFDEC_AS_ARRAY_H_
#define _SWFDEC_AS_ARRAY_H_

#include <libswfdec/swfdec_as_object.h>
#include <libswfdec/swfdec_as_types.h>

G_BEGIN_DECLS

typedef struct _SwfdecAsArrayClass SwfdecAsArrayClass;

#define SWFDEC_TYPE_AS_ARRAY                    (swfdec_as_array_get_type())
#define SWFDEC_IS_AS_ARRAY(obj)                 (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SWFDEC_TYPE_AS_ARRAY))
#define SWFDEC_IS_AS_ARRAY_CLASS(klass)         (G_TYPE_CHECK_CLASS_TYPE ((klass), SWFDEC_TYPE_AS_ARRAY))
#define SWFDEC_AS_ARRAY(obj)                    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SWFDEC_TYPE_AS_ARRAY, SwfdecAsArray))
#define SWFDEC_AS_ARRAY_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST ((klass), SWFDEC_TYPE_AS_ARRAY, SwfdecAsArrayClass))
#define SWFDEC_AS_ARRAY_GET_CLASS(obj)          (G_TYPE_INSTANCE_GET_CLASS ((obj), SWFDEC_TYPE_AS_ARRAY, SwfdecAsArrayClass))

struct _SwfdecAsArray {
  /*< private >*/
  SwfdecAsObject	object;
};

struct _SwfdecAsArrayClass {
  SwfdecAsObjectClass	object_class;
};

GType		swfdec_as_array_get_type	(void);

SwfdecAsObject *swfdec_as_array_new		(SwfdecAsContext *	context);

#define		swfdec_as_array_push(array,value) \
  swfdec_as_array_append ((array), 1, (value))
void		swfdec_as_array_append		(SwfdecAsArray *	array,
						 guint			n,
						 const SwfdecAsValue *	values);
gint32		swfdec_as_array_length		(SwfdecAsArray *	array);
void		swfdec_as_array_get_value	(SwfdecAsArray *	array,
						 gint32			i,
						 SwfdecAsValue *	value);


G_END_DECLS
#endif
