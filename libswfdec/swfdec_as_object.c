/* SwfdecAs
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

#include "swfdec_as_object.h"
#include "swfdec_as_context.h"
#include "swfdec_as_frame.h"
#include "swfdec_as_function.h"
#include "swfdec_debug.h"


G_DEFINE_TYPE (SwfdecAsObject, swfdec_as_object, G_TYPE_OBJECT)

static void
swfdec_as_object_dispose (GObject *gobject)
{
  SwfdecAsObject *object = SWFDEC_AS_OBJECT (gobject);

  g_assert (!SWFDEC_AS_OBJECT_HAS_CONTEXT (object));

  G_OBJECT_CLASS (swfdec_as_object_parent_class)->dispose (gobject);
}

static void
swfdec_as_object_mark_property (gpointer key, gpointer value, gpointer unused)
{
  swfdec_as_string_mark (key);
  swfdec_as_variable_mark (value);
}

static void
swfdec_as_object_do_mark (SwfdecAsObject *object)
{
  g_hash_table_foreach (object->properties, swfdec_as_object_mark_property, NULL);
}

static SwfdecAsVariable *
swfdec_as_object_do_get (SwfdecAsObject *object, const SwfdecAsValue *variable, 
    gboolean create)
{
  const char *s;
  SwfdecAsVariable *var;

  s = swfdec_as_value_to_string (object->context, variable);
  var = g_hash_table_lookup (object->properties, s);
  if (var != NULL || !create)
    return var;
  if (!swfdec_as_context_use_mem (object->context, sizeof (SwfdecAsVariable)))
    return NULL;
  var = g_slice_new0 (SwfdecAsVariable);
  g_hash_table_insert (object->properties, (gpointer) s, var);
  return var;
}

static void
swfdec_as_object_free_property (gpointer key, gpointer value, gpointer data)
{
  SwfdecAsObject *object = data;

  swfdec_as_context_unuse_mem (object->context, sizeof (SwfdecAsVariable));
  g_slice_free (SwfdecAsVariable, value);
}

static void
swfdec_as_object_do_delete (SwfdecAsObject *object, const SwfdecAsValue *variable)
{
  const char *s;
  SwfdecAsVariable *var;

  s = swfdec_as_value_to_string (object->context, variable);
  var = g_hash_table_lookup (object->properties, s);
  g_assert (var);
  swfdec_as_object_free_property (NULL, var, object);
}

static void
swfdec_as_object_class_init (SwfdecAsObjectClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = swfdec_as_object_dispose;

  klass->mark = swfdec_as_object_do_mark;
  klass->get = swfdec_as_object_do_get;
  klass->delete = swfdec_as_object_do_delete;
}

static void
swfdec_as_object_init (SwfdecAsObject *object)
{
}

/**
 * swfdec_as_object_new:
 * @context: a #SwfdecAsContext
 *
 * Allocates a new Object. This does the same as the Actionscript code 
 * "new Object()".
 * <warning>This function may run the garbage collector.</warning>
 *
 * Returns: the new object or NULL on out of memory.
 **/
SwfdecAsObject *
swfdec_as_object_new (SwfdecAsContext *context)
{
  SwfdecAsObject *object;

  g_return_val_if_fail (SWFDEC_IS_AS_CONTEXT (context), NULL);
  
  if (!swfdec_as_context_use_mem (context, sizeof (SwfdecAsObject)))
    return NULL;
  object = g_object_new (SWFDEC_TYPE_AS_OBJECT, NULL);
  swfdec_as_object_add (object, context, sizeof (SwfdecAsObject));
  return object;
}

/**
 * swfdec_as_object_add:
 * @object: #SwfdecAsObject to make garbage-collected
 * @context: #SwfdecAsContext that should manage the object
 * @size: size the object currently uses
 *
 * Takes over the reference to @object for the garbage collector of @context. 
 * The object may not already be part of a different context. The given @size 
 * must have been allocated before with swfdec_as_context_use_mem ().
 * Note that after swfdec_as_object_add() the garbage collector might hold the
 * only reference to @object.
 **/
void
swfdec_as_object_add (SwfdecAsObject *object, SwfdecAsContext *context, gsize size)
{
  g_return_if_fail (SWFDEC_IS_AS_OBJECT (object));
  g_return_if_fail (SWFDEC_IS_AS_CONTEXT (context));
  g_return_if_fail (!SWFDEC_AS_OBJECT_HAS_CONTEXT (object));

  object->context = context;
  object->size = size;
  g_hash_table_insert (context->objects, object, object);
  object->properties = g_hash_table_new (g_direct_hash, g_direct_equal);
}

void
swfdec_as_object_collect (SwfdecAsObject *object)
{
  g_return_if_fail (SWFDEC_IS_AS_OBJECT (object));
  g_return_if_fail (SWFDEC_AS_OBJECT_HAS_CONTEXT (object));

  g_hash_table_foreach (object->properties, swfdec_as_object_free_property, object);
  g_hash_table_destroy (object->properties);
  object->properties = NULL;
  swfdec_as_context_unuse_mem (object->context, object->size);
  g_object_unref (object);
}

void
swfdec_as_object_root (SwfdecAsObject *object)
{
  g_return_if_fail (SWFDEC_IS_AS_OBJECT (object));
  g_return_if_fail ((object->flags & SWFDEC_AS_GC_ROOT) == 0);

  object->flags |= SWFDEC_AS_GC_ROOT;
}

void
swfdec_as_object_unroot (SwfdecAsObject *object)
{
  g_return_if_fail (SWFDEC_IS_AS_OBJECT (object));
  g_return_if_fail ((object->flags & SWFDEC_AS_GC_ROOT) != 0);

  object->flags &= ~SWFDEC_AS_GC_ROOT;
}

static inline SwfdecAsVariable *
swfdec_as_object_lookup (SwfdecAsObject *object, const SwfdecAsValue *variable, gboolean create)
{
  SwfdecAsObjectClass *klass = SWFDEC_AS_OBJECT_GET_CLASS (object);

  return klass->get (object, variable, create);
}

void
swfdec_as_object_set_variable (SwfdecAsObject *object,
    const SwfdecAsValue *variable, const SwfdecAsValue *value)
{
  SwfdecAsVariable *var;

  g_return_if_fail (SWFDEC_IS_AS_OBJECT (object));
  g_return_if_fail (SWFDEC_IS_AS_VALUE (variable));
  g_return_if_fail (SWFDEC_IS_AS_VALUE (value));

  var = swfdec_as_object_lookup (object, variable, TRUE);
  if (var == NULL ||
      var->flags & SWFDEC_AS_VARIABLE_READONLY)
    return;
  g_assert ((var->flags & SWFDEC_AS_VARIABLE_NATIVE) == 0);
  var->value.value = *value;
}

void
swfdec_as_object_get_variable (SwfdecAsObject *object, 
    const SwfdecAsValue *variable, SwfdecAsValue *value)
{
  SwfdecAsVariable *var;
  guint i;

  g_return_if_fail (SWFDEC_IS_AS_OBJECT (object));
  g_return_if_fail (SWFDEC_IS_AS_VALUE (variable));
  g_return_if_fail (value != NULL);

  for (i = 0; i < 256 && object != NULL; i++) {
    var = swfdec_as_object_lookup (object, variable, FALSE);
    if (var == NULL) {
      object = object->prototype;
      continue;
    }
    g_assert ((var->flags & SWFDEC_AS_VARIABLE_NATIVE) == 0);
    *value = var->value.value;
    return;
  }
  if (i == 256) {
    swfdec_as_context_abort (object->context, "Prototype recursion limit exceeded");
    return;
  }
  SWFDEC_AS_VALUE_SET_UNDEFINED (value);
}

void
swfdec_as_object_delete_variable (SwfdecAsObject *object, 
    const SwfdecAsValue *variable)
{
  SwfdecAsObjectClass *klass;
  SwfdecAsVariable *var;
  guint i;

  g_return_if_fail (SWFDEC_IS_AS_OBJECT (object));
  g_return_if_fail (SWFDEC_IS_AS_VALUE (variable));

  for (i = 0; i < 256 && object != NULL; i++) {
    var = swfdec_as_object_lookup (object, variable, FALSE);
    if (var == NULL) {
      object = object->prototype;
      continue;
    }
    if (var->flags & SWFDEC_AS_VARIABLE_PERMANENT)
      return;
    klass = SWFDEC_AS_OBJECT_GET_CLASS (object);
    klass->delete (object, variable);
    return;
  }
  if (i == 256) {
    swfdec_as_context_abort (object->context, "Prototype recursion limit exceeded");
    return;
  }
}

SwfdecAsObject *
swfdec_as_object_find_variable (SwfdecAsObject *object,
    const SwfdecAsValue *variable)
{
  SwfdecAsVariable *var;
  guint i;

  g_return_val_if_fail (SWFDEC_IS_AS_OBJECT (object), NULL);
  g_return_val_if_fail (SWFDEC_IS_AS_VALUE (variable), NULL);

  for (i = 0; i < 256 && object != NULL; i++) {
    var = swfdec_as_object_lookup (object, variable, FALSE);
    if (var)
      return object;
    object = object->prototype;
  }
  if (i == 256) {
    swfdec_as_context_abort (object->context, "Prototype recursion limit exceeded");
    return NULL;
  }
  return NULL;
}

/**
 * swfdec_as_object_run:
 * @object: a #SwfdecAsObject
 * @script: script to execute
 *
 * Executes the given @script with @object as this pointer.
 **/
void
swfdec_as_object_run (SwfdecAsObject *object, SwfdecScript *script)
{
  g_return_if_fail (SWFDEC_IS_AS_OBJECT (object));
  g_return_if_fail (SWFDEC_AS_OBJECT_HAS_CONTEXT (object));
  g_return_if_fail (script != NULL);

  if (swfdec_as_frame_new (object, script))
    swfdec_as_context_run (object->context);
}

void
swfdec_as_object_call (SwfdecAsObject *object, const char *name, guint argc, SwfdecAsValue *argv)
{
  g_return_if_fail (SWFDEC_IS_AS_OBJECT (object));
  g_return_if_fail (name != NULL);
  g_return_if_fail (argc == 0 || argv != NULL);
}

gboolean
swfdec_as_object_has_function (SwfdecAsObject *object, const char *name)
{
  SwfdecAsValue val;

  g_return_val_if_fail (SWFDEC_IS_AS_OBJECT (object), FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  swfdec_as_object_get (object, name, &val);
  if (!SWFDEC_AS_VALUE_IS_OBJECT (&val))
    return FALSE;
  return SWFDEC_IS_AS_FUNCTION (SWFDEC_AS_VALUE_GET_OBJECT (&val));
}
