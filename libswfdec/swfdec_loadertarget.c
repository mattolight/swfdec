/* Swfdec
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

#include "swfdec_loadertarget.h"

static void
swfdec_loader_target_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (G_UNLIKELY (!initialized)) {
    initialized = TRUE;
  }
}

GType
swfdec_loader_target_get_type ()
{
  static GType loader_target_type = 0;
  
  if (!loader_target_type) {
    static const GTypeInfo loader_target_info = {
      sizeof (SwfdecLoaderTargetInterface),
      swfdec_loader_target_base_init,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
    };
    
    loader_target_type = g_type_register_static (G_TYPE_INTERFACE,
        "SwfdecLoaderTarget", &loader_target_info, 0);
    g_type_interface_add_prerequisite (loader_target_type, G_TYPE_OBJECT);
  }
  
  return loader_target_type;
}

SwfdecPlayer *
swfdec_loader_target_get_player (SwfdecLoaderTarget *target)
{
  SwfdecLoaderTargetInterface *iface;
  
  g_return_val_if_fail (SWFDEC_IS_LOADER_TARGET (target), NULL);

  iface = SWFDEC_LOADER_TARGET_GET_INTERFACE (target);
  g_assert (iface->get_player != NULL);
  return iface->get_player (target);
}

void
swfdec_loader_target_parse_default (SwfdecLoaderTarget *target, SwfdecLoader *loader)
{
  SwfdecDecoder *dec;
  SwfdecDecoderClass *klass;

  dec = swfdec_loader_target_get_decoder (target);
  if (dec == NULL) {
    SwfdecPlayer *player;
    if (!swfdec_decoder_can_detect (loader->queue))
      return;
    player = swfdec_loader_target_get_player (target);
    dec = swfdec_decoder_new (player, loader->queue);
    if (dec == NULL) {
      swfdec_loader_error (loader, "Unknown format");
      return;
    }
    if (!swfdec_loader_target_set_decoder (target, dec)) {
      swfdec_loader_error (loader, "Internal error");
      return;
    }
  }
  klass = SWFDEC_DECODER_GET_CLASS (dec);
  g_return_if_fail (klass->parse);
  while (TRUE) {
    SwfdecStatus status = klass->parse (dec);
    switch (status) {
      case SWFDEC_STATUS_ERROR:
	swfdec_loader_error (loader, "parsing error");
	return;
      case SWFDEC_STATUS_OK:
	break;
      case SWFDEC_STATUS_NEEDBITS:
	return;
      case SWFDEC_STATUS_IMAGE:
	if (!swfdec_loader_target_image (target)) {
	  swfdec_loader_error (loader, "Internal error");
	  return;
	}
	break;
      case SWFDEC_STATUS_INIT:
	g_assert (dec->width > 0);
	g_assert (dec->height > 0);
	if (!swfdec_loader_target_init (target)) {
	  swfdec_loader_error (loader, "Internal error");
	  return;
	}
	break;
      case SWFDEC_STATUS_EOF:
	return;
      default:
	g_assert_not_reached ();
	return;
    }
  }
}

void
swfdec_loader_target_parse (SwfdecLoaderTarget *target, SwfdecLoader *loader)
{
  SwfdecLoaderTargetInterface *iface;
  
  g_return_if_fail (SWFDEC_IS_LOADER_TARGET (target));
  g_return_if_fail (SWFDEC_IS_LOADER (loader));

  if (loader->error)
    return;

  iface = SWFDEC_LOADER_TARGET_GET_INTERFACE (target);
  if (iface->parse == NULL) {
    swfdec_loader_target_parse_default (target, loader);
  } else {
    iface->parse (target, loader);
  }
}

SwfdecDecoder *
swfdec_loader_target_get_decoder (SwfdecLoaderTarget *target)
{
  SwfdecLoaderTargetInterface *iface;
  
  g_return_val_if_fail (SWFDEC_IS_LOADER_TARGET (target), NULL);

  iface = SWFDEC_LOADER_TARGET_GET_INTERFACE (target);
  g_assert (iface->get_decoder != NULL);
  return iface->get_decoder (target);
}

gboolean
swfdec_loader_target_set_decoder (SwfdecLoaderTarget *target, SwfdecDecoder *decoder)
{
  SwfdecLoaderTargetInterface *iface;
  
  g_return_val_if_fail (SWFDEC_IS_LOADER_TARGET (target), FALSE);

  iface = SWFDEC_LOADER_TARGET_GET_INTERFACE (target);
  g_assert (iface->set_decoder != NULL);
  return iface->set_decoder (target, decoder);
}

gboolean
swfdec_loader_target_init (SwfdecLoaderTarget *target)
{
  SwfdecLoaderTargetInterface *iface;
  
  g_return_val_if_fail (SWFDEC_IS_LOADER_TARGET (target), FALSE);

  iface = SWFDEC_LOADER_TARGET_GET_INTERFACE (target);
  if (iface->init == NULL)
    return TRUE;
  return iface->init (target);
}

gboolean
swfdec_loader_target_image (SwfdecLoaderTarget *target)
{
  SwfdecLoaderTargetInterface *iface;
  
  g_return_val_if_fail (SWFDEC_IS_LOADER_TARGET (target), FALSE);

  iface = SWFDEC_LOADER_TARGET_GET_INTERFACE (target);
  if (iface->image == NULL)
    return TRUE;
  return iface->image (target);
}

