/* Vivified
 * Copyright (C) 2007 Benjamin Otte <otte@gnome.org>
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

#include "vivi_function.h"
#include "vivi_function_list.h"

/* include vivi_function_list with special macro definition, so we get a nice
 * way to initialize it */
#undef VIVI_FUNCTION
#define VIVI_FUNCTION(name, fun) \
  { name, fun },
static const struct {
  const char *		name;
  SwfdecAsNative	fun;
} functions[] = {
#include "vivi_function_list.h"
  { NULL, NULL }
};
#undef VIVI_FUNCTION

/* defined in vivi_initialize.s */
extern const char vivi_initialize[];

void
vivi_function_init_context (ViviApplication *app)
{ 
  SwfdecAsContext *cx = SWFDEC_AS_CONTEXT (app);
  SwfdecAsObject *obj;
  SwfdecAsValue val;
  guint i;

  obj = swfdec_as_object_new (cx);
  if (obj == NULL)
    return;
  SWFDEC_AS_VALUE_SET_OBJECT (&val, obj);
  swfdec_as_object_set_variable (cx->global, 
      swfdec_as_context_get_string (cx, "Native"), &val);

  for (i = 0; functions[i].name; i++) {
    swfdec_as_object_add_function (obj,
      swfdec_as_context_get_string (cx, functions[i].name),
      0, functions[i].fun, 0);
  }
  vivi_applciation_execute (app, vivi_initialize);
}

