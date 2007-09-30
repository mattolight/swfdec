/* Swfdec
 * Copyright (C) 2006-2007 Benjamin Otte <otte@gnome.org>
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

#include <math.h>

#include "swfdec_pattern.h"
#include "swfdec_bits.h"
#include "swfdec_color.h"
#include "swfdec_debug.h"
#include "swfdec_decoder.h"
#include "swfdec_image.h"
#include "swfdec_path.h"
#include "swfdec_stroke.h"

/*** PATTERN ***/

G_DEFINE_ABSTRACT_TYPE (SwfdecPattern, swfdec_pattern, SWFDEC_TYPE_DRAW);

static void
swfdec_pattern_compute_extents (SwfdecDraw *draw)
{
  swfdec_path_get_extents (&draw->path, &draw->extents);
}

static void
swfdec_pattern_paint (SwfdecDraw *draw, cairo_t *cr, const SwfdecColorTransform *trans)
{
  cairo_pattern_t *pattern;

  pattern = swfdec_pattern_get_pattern (SWFDEC_PATTERN (draw), trans);
  if (pattern == NULL)
    return;
  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_append_path (cr, &draw->path);
  cairo_set_source (cr, pattern);
  cairo_pattern_destroy (pattern);
  cairo_fill (cr);
}

static void
swfdec_pattern_morph (SwfdecDraw *dest, SwfdecDraw *source, guint ratio)
{
  SwfdecPattern *dpattern = SWFDEC_PATTERN (dest);
  SwfdecPattern *spattern = SWFDEC_PATTERN (source);

  swfdec_matrix_morph (&dpattern->start_transform,
      &spattern->start_transform, &spattern->end_transform, ratio);

  SWFDEC_DRAW_CLASS (swfdec_pattern_parent_class)->morph (dest, source, ratio);
}

static gboolean
swfdec_pattern_contains (SwfdecDraw *draw, cairo_t *cr, double x, double y)
{
  cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_append_path (cr, &draw->path);
  return cairo_in_fill (cr, x, y);
}

static void
swfdec_pattern_class_init (SwfdecPatternClass *klass)
{
  SwfdecDrawClass *draw_class = SWFDEC_DRAW_CLASS (klass);

  draw_class->morph = swfdec_pattern_morph;
  draw_class->paint = swfdec_pattern_paint;
  draw_class->compute_extents = swfdec_pattern_compute_extents;
  draw_class->contains = swfdec_pattern_contains;
}

static void
swfdec_pattern_init (SwfdecPattern *pattern)
{
  cairo_matrix_init_identity (&pattern->start_transform);
  cairo_matrix_init_identity (&pattern->end_transform);
}

/*** COLOR PATTERN ***/

typedef struct _SwfdecColorPattern SwfdecColorPattern;
typedef struct _SwfdecColorPatternClass SwfdecColorPatternClass;

#define SWFDEC_TYPE_COLOR_PATTERN                    (swfdec_color_pattern_get_type())
#define SWFDEC_IS_COLOR_PATTERN(obj)                 (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SWFDEC_TYPE_COLOR_PATTERN))
#define SWFDEC_IS_COLOR_PATTERN_CLASS(klass)         (G_TYPE_CHECK_CLASS_TYPE ((klass), SWFDEC_TYPE_COLOR_PATTERN))
#define SWFDEC_COLOR_PATTERN(obj)                    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SWFDEC_TYPE_COLOR_PATTERN, SwfdecColorPattern))
#define SWFDEC_COLOR_PATTERN_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST ((klass), SWFDEC_TYPE_COLOR_PATTERN, SwfdecColorPatternClass))
#define SWFDEC_COLOR_PATTERN_GET_CLASS(obj)          (G_TYPE_INSTANCE_GET_CLASS ((obj), SWFDEC_TYPE_COLOR_PATTERN, SwfdecColorPatternClass))

struct _SwfdecColorPattern
{
  SwfdecPattern		pattern;

  SwfdecColor		start_color;		/* color to paint with at the beginning */
  SwfdecColor		end_color;		/* color to paint with in the end */
};

struct _SwfdecColorPatternClass
{
  SwfdecPatternClass	pattern_class;
};

GType swfdec_color_pattern_get_type (void);
G_DEFINE_TYPE (SwfdecColorPattern, swfdec_color_pattern, SWFDEC_TYPE_PATTERN);

static void
swfdec_color_pattern_morph (SwfdecDraw *dest, SwfdecDraw *source, guint ratio)
{
  SwfdecColorPattern *dpattern = SWFDEC_COLOR_PATTERN (dest);
  SwfdecColorPattern *spattern = SWFDEC_COLOR_PATTERN (source);

  dpattern->start_color = swfdec_color_apply_morph (spattern->start_color, spattern->end_color, ratio);

  SWFDEC_DRAW_CLASS (swfdec_color_pattern_parent_class)->morph (dest, source, ratio);
}

static cairo_pattern_t *
swfdec_color_pattern_get_pattern (SwfdecPattern *pat, const SwfdecColorTransform *trans)
{
  SwfdecColor color = SWFDEC_COLOR_PATTERN (pat)->start_color;

  color = swfdec_color_apply_transform (color, trans);
  return cairo_pattern_create_rgba ( 
      SWFDEC_COLOR_R (color) / 255.0, SWFDEC_COLOR_G (color) / 255.0,
      SWFDEC_COLOR_B (color) / 255.0, SWFDEC_COLOR_A (color) / 255.0);
}

static void
swfdec_color_pattern_class_init (SwfdecColorPatternClass *klass)
{
  SWFDEC_DRAW_CLASS (klass)->morph = swfdec_color_pattern_morph;

  SWFDEC_PATTERN_CLASS (klass)->get_pattern = swfdec_color_pattern_get_pattern;
}

static void
swfdec_color_pattern_init (SwfdecColorPattern *pattern)
{
}

/*** IMAGE PATTERN ***/

typedef struct _SwfdecImagePattern SwfdecImagePattern;
typedef struct _SwfdecImagePatternClass SwfdecImagePatternClass;

#define SWFDEC_TYPE_IMAGE_PATTERN                    (swfdec_image_pattern_get_type())
#define SWFDEC_IS_IMAGE_PATTERN(obj)                 (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SWFDEC_TYPE_IMAGE_PATTERN))
#define SWFDEC_IS_IMAGE_PATTERN_CLASS(klass)         (G_TYPE_CHECK_CLASS_TYPE ((klass), SWFDEC_TYPE_IMAGE_PATTERN))
#define SWFDEC_IMAGE_PATTERN(obj)                    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SWFDEC_TYPE_IMAGE_PATTERN, SwfdecImagePattern))
#define SWFDEC_IMAGE_PATTERN_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST ((klass), SWFDEC_TYPE_IMAGE_PATTERN, SwfdecImagePatternClass))
#define SWFDEC_IMAGE_PATTERN_GET_CLASS(obj)          (G_TYPE_INSTANCE_GET_CLASS ((obj), SWFDEC_TYPE_IMAGE_PATTERN, SwfdecImagePatternClass))

struct _SwfdecImagePattern
{
  SwfdecPattern		pattern;

  SwfdecImage *		image;		/* image to paint */
  cairo_extend_t	extend;
  cairo_filter_t	filter;
};

struct _SwfdecImagePatternClass
{
  SwfdecPatternClass	pattern_class;
};

GType swfdec_image_pattern_get_type (void);
G_DEFINE_TYPE (SwfdecImagePattern, swfdec_image_pattern, SWFDEC_TYPE_PATTERN);

static void
swfdec_image_pattern_morph (SwfdecDraw *dest, SwfdecDraw *source, guint ratio)
{
  SwfdecImagePattern *dpattern = SWFDEC_IMAGE_PATTERN (dest);
  SwfdecImagePattern *spattern = SWFDEC_IMAGE_PATTERN (source);

  dpattern->image = g_object_ref (spattern->image);
  dpattern->extend = spattern->extend;
  dpattern->filter = spattern->filter;

  SWFDEC_DRAW_CLASS (swfdec_image_pattern_parent_class)->morph (dest, source, ratio);
}

static cairo_pattern_t *
swfdec_image_pattern_get_pattern (SwfdecPattern *pat, const SwfdecColorTransform *trans)
{
  SwfdecImagePattern *image = SWFDEC_IMAGE_PATTERN (pat);
  cairo_pattern_t *pattern;
  cairo_surface_t *surface;
  
  surface = swfdec_image_create_surface_transformed (image->image, trans);
  if (surface == NULL)
    return NULL;
  pattern = cairo_pattern_create_for_surface (surface);
  cairo_surface_destroy (surface);
  cairo_pattern_set_matrix (pattern, &pat->start_transform);
  cairo_pattern_set_extend (pattern, image->extend);
  cairo_pattern_set_filter (pattern, image->filter);
  return pattern;
}

static void
swfdec_image_pattern_class_init (SwfdecImagePatternClass *klass)
{
  SWFDEC_DRAW_CLASS (klass)->morph = swfdec_image_pattern_morph;

  SWFDEC_PATTERN_CLASS (klass)->get_pattern = swfdec_image_pattern_get_pattern;
}

static void
swfdec_image_pattern_init (SwfdecImagePattern *pattern)
{
}

/*** GRADIENT PATTERN ***/

typedef struct _SwfdecGradientPattern SwfdecGradientPattern;
typedef struct _SwfdecGradientPatternClass SwfdecGradientPatternClass;

#define SWFDEC_TYPE_GRADIENT_PATTERN                    (swfdec_gradient_pattern_get_type())
#define SWFDEC_IS_GRADIENT_PATTERN(obj)                 (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SWFDEC_TYPE_GRADIENT_PATTERN))
#define SWFDEC_IS_GRADIENT_PATTERN_CLASS(klass)         (G_TYPE_CHECK_CLASS_TYPE ((klass), SWFDEC_TYPE_GRADIENT_PATTERN))
#define SWFDEC_GRADIENT_PATTERN(obj)                    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SWFDEC_TYPE_GRADIENT_PATTERN, SwfdecGradientPattern))
#define SWFDEC_GRADIENT_PATTERN_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST ((klass), SWFDEC_TYPE_GRADIENT_PATTERN, SwfdecGradientPatternClass))
#define SWFDEC_GRADIENT_PATTERN_GET_CLASS(obj)          (G_TYPE_INSTANCE_GET_CLASS ((obj), SWFDEC_TYPE_GRADIENT_PATTERN, SwfdecGradientPatternClass))

struct _SwfdecGradientPattern
{
  SwfdecPattern		pattern;

  SwfdecGradient *	gradient;		/* gradient to paint */
  SwfdecGradient *	end_gradient;		/* end gradient for morphs */
  gboolean		radial;			/* TRUE for radial gradient, FALSE for linear gradient */
  double		focus;			/* focus point */
};

struct _SwfdecGradientPatternClass
{
  SwfdecPatternClass	pattern_class;
};

GType swfdec_gradient_pattern_get_type (void);
G_DEFINE_TYPE (SwfdecGradientPattern, swfdec_gradient_pattern, SWFDEC_TYPE_PATTERN);

static void
swfdec_gradient_pattern_morph (SwfdecDraw *dest, SwfdecDraw *source, guint ratio)
{
  SwfdecGradientPattern *dpattern = SWFDEC_GRADIENT_PATTERN (dest);
  SwfdecGradientPattern *spattern = SWFDEC_GRADIENT_PATTERN (source);

  g_return_if_fail (spattern->end_gradient == NULL);
  dpattern->gradient = swfdec_gradient_morph (spattern->gradient, 
      spattern->end_gradient, ratio);
  dpattern->radial = spattern->radial;
  dpattern->focus = spattern->focus;

  SWFDEC_DRAW_CLASS (swfdec_gradient_pattern_parent_class)->morph (dest, source, ratio);
}

static cairo_pattern_t *
swfdec_gradient_pattern_get_pattern (SwfdecPattern *pat, const SwfdecColorTransform *trans)
{
  guint i;
  cairo_pattern_t *pattern;
  SwfdecColor color;
  double offset;
  SwfdecGradientPattern *gradient = SWFDEC_GRADIENT_PATTERN (pat);

#if 0
  /* use this when https://bugs.freedesktop.org/show_bug.cgi?id=8341 is fixed */
  if (gradient->radial)
    pattern = cairo_pattern_create_radial (0, 0, 0, 0, 0, 16384);
  else
    pattern = cairo_pattern_create_linear (-16384.0, 0, 16384.0, 0);
  cairo_pattern_set_matrix (pattern, &pat->transform);
#else
  {
    cairo_matrix_t mat = pat->start_transform;
    if (gradient->radial) {
      pattern = cairo_pattern_create_radial ((16384.0 / 256.0) * gradient->focus, 
	  0, 0, 0, 0, 16384 / 256.0);
    } else {
      pattern = cairo_pattern_create_linear (-16384.0 / 256.0, 0, 16384.0 / 256.0, 0);
    }
    cairo_matrix_scale (&mat, 1 / 256.0, 1 / 256.0);
    mat.x0 /= 256.0;
    mat.y0 /= 256.0;
    cairo_pattern_set_matrix (pattern, &mat);
  }
#endif
  for (i = 0; i < gradient->gradient->n_gradients; i++){
    color = swfdec_color_apply_transform (gradient->gradient->array[i].color,
	trans);
    offset = gradient->gradient->array[i].ratio / 255.0;
    cairo_pattern_add_color_stop_rgba (pattern, offset,
	SWFDEC_COLOR_R(color) / 255.0, SWFDEC_COLOR_G(color) / 255.0,
	SWFDEC_COLOR_B(color) / 255.0, SWFDEC_COLOR_A(color) / 255.0);
  }
  return pattern;
}

static void
swfdec_gradient_pattern_dispose (GObject *object)
{
  SwfdecGradientPattern *gradient = SWFDEC_GRADIENT_PATTERN (object);

  g_free (gradient->gradient);
  gradient->gradient = NULL;
  g_free (gradient->end_gradient);
  gradient->end_gradient = NULL;

  G_OBJECT_CLASS (swfdec_gradient_pattern_parent_class)->dispose (object);
}

static void
swfdec_gradient_pattern_class_init (SwfdecGradientPatternClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = swfdec_gradient_pattern_dispose;

  SWFDEC_DRAW_CLASS (klass)->morph = swfdec_gradient_pattern_morph;

  SWFDEC_PATTERN_CLASS (klass)->get_pattern = swfdec_gradient_pattern_get_pattern;
}

static void
swfdec_gradient_pattern_init (SwfdecGradientPattern *pattern)
{
}

/*** EXPORTED API ***/

static SwfdecDraw *
swfdec_pattern_do_parse (SwfdecBits *bits, SwfdecSwfDecoder *dec, gboolean rgba)
{
  guint paint_style_type;
  SwfdecPattern *pattern;

  paint_style_type = swfdec_bits_get_u8 (bits);
  SWFDEC_LOG ("    type 0x%02x", paint_style_type);

  if (paint_style_type == 0x00) {
    pattern = g_object_new (SWFDEC_TYPE_COLOR_PATTERN, NULL);
    if (rgba) {
      SWFDEC_COLOR_PATTERN (pattern)->start_color = swfdec_bits_get_rgba (bits);
    } else {
      SWFDEC_COLOR_PATTERN (pattern)->start_color = swfdec_bits_get_color (bits);
    }
    SWFDEC_COLOR_PATTERN (pattern)->end_color = SWFDEC_COLOR_PATTERN (pattern)->start_color;
    SWFDEC_LOG ("    color %08x", SWFDEC_COLOR_PATTERN (pattern)->start_color);
  } else if (paint_style_type == 0x10 || paint_style_type == 0x12 || paint_style_type == 0x13) {
    SwfdecGradientPattern *gradient;
    pattern = g_object_new (SWFDEC_TYPE_GRADIENT_PATTERN, NULL);
    gradient = SWFDEC_GRADIENT_PATTERN (pattern);
    swfdec_bits_get_matrix (bits, &pattern->start_transform, NULL);
    pattern->end_transform = pattern->start_transform;
    if (rgba) {
      gradient->gradient = swfdec_bits_get_gradient_rgba (bits);
    } else {
      gradient->gradient = swfdec_bits_get_gradient (bits);
    }
    gradient->radial = (paint_style_type != 0x10);
    /* FIXME: need a way to ensure 0x13 only happens in Flash 8 */
    if (paint_style_type == 0x13) {
      gradient->focus = swfdec_bits_get_s16 (bits) / 256.0;
    }
  } else if (paint_style_type >= 0x40 && paint_style_type <= 0x43) {
    guint paint_id = swfdec_bits_get_u16 (bits);
    SWFDEC_LOG ("   background paint id = %d (type 0x%02x)",
	paint_id, paint_style_type);
    if (paint_id == 65535) {
      /* FIXME: someone explain this magic paint id here */
      pattern = g_object_new (SWFDEC_TYPE_COLOR_PATTERN, NULL);
      SWFDEC_COLOR_PATTERN (pattern)->start_color = SWFDEC_COLOR_COMBINE (0, 255, 255, 255);
      SWFDEC_COLOR_PATTERN (pattern)->end_color = SWFDEC_COLOR_PATTERN (pattern)->start_color;
      swfdec_bits_get_matrix (bits, &pattern->start_transform, NULL);
      pattern->end_transform = pattern->start_transform;
    } else {
      pattern = g_object_new (SWFDEC_TYPE_IMAGE_PATTERN, NULL);
      swfdec_bits_get_matrix (bits, &pattern->start_transform, NULL);
      pattern->end_transform = pattern->start_transform;
      SWFDEC_IMAGE_PATTERN (pattern)->image = swfdec_swf_decoder_get_character (dec, paint_id);
      if (!SWFDEC_IS_IMAGE (SWFDEC_IMAGE_PATTERN (pattern)->image)) {
	g_object_unref (pattern);
	SWFDEC_ERROR ("could not find image with id %u for pattern", paint_id);
	return NULL;
      }
      if (paint_style_type == 0x40 || paint_style_type == 0x42) {
	SWFDEC_IMAGE_PATTERN (pattern)->extend = CAIRO_EXTEND_REPEAT;
      } else {
#if 0
	/* not implemented yet in cairo */
	SWFDEC_IMAGE_PATTERN (pattern)->extend = CAIRO_EXTEND_PAD;
#else
	SWFDEC_FIXME ("CAIRO_EXTEND_PAD is not yet implemented");
	SWFDEC_IMAGE_PATTERN (pattern)->extend = CAIRO_EXTEND_NONE;
#endif
      }
      if (paint_style_type == 0x40 || paint_style_type == 0x41) {
	SWFDEC_IMAGE_PATTERN (pattern)->filter = CAIRO_FILTER_BILINEAR;
      } else {
	SWFDEC_IMAGE_PATTERN (pattern)->filter = CAIRO_FILTER_NEAREST;
      }
    }
  } else {
    SWFDEC_ERROR ("unknown paint style type 0x%02x", paint_style_type);
    return NULL;
  }
  if (cairo_matrix_invert (&pattern->start_transform)) {
    SWFDEC_ERROR ("paint transform matrix not invertible, resetting");
    cairo_matrix_init_identity (&pattern->start_transform);
    cairo_matrix_init_identity (&pattern->end_transform);
  }
  swfdec_bits_syncbits (bits);
  return SWFDEC_DRAW (pattern);
}

/**
 * swfdec_pattern_parse:
 * @bits: the bits to parse from
 * @dec: a #SwfdecDecoder to take context from
 * @rgba: TRUE if colors are RGBA, FALSE if they're just RGB
 *
 * Continues parsing @dec into a new #SwfdecPattern
 *
 * Returns: a new #SwfdecPattern or NULL on error
 **/
SwfdecDraw *
swfdec_pattern_parse (SwfdecBits *bits, SwfdecSwfDecoder *dec)
{
  g_return_val_if_fail (bits != NULL, NULL);
  g_return_val_if_fail (SWFDEC_IS_SWF_DECODER (dec), NULL);

  return swfdec_pattern_do_parse (bits, dec, FALSE);
}

SwfdecDraw *
swfdec_pattern_parse_rgba (SwfdecBits *bits, SwfdecSwfDecoder *dec)
{
  g_return_val_if_fail (bits != NULL, NULL);
  g_return_val_if_fail (SWFDEC_IS_SWF_DECODER (dec), NULL);

  return swfdec_pattern_do_parse (bits, dec, TRUE);
}

/**
 * swfdec_pattern_parse_morph:
 * @dec: a #SwfdecDecoder to parse from
 *
 * Continues parsing @dec into a new #SwfdecPattern. This function is used by
 * morph shapes.
 *
 * Returns: a new #SwfdecPattern or NULL on error
 **/
SwfdecDraw *
swfdec_pattern_parse_morph (SwfdecBits *bits, SwfdecSwfDecoder *dec)
{
  guint paint_style_type;
  SwfdecPattern *pattern;

  g_return_val_if_fail (bits != NULL, NULL);
  g_return_val_if_fail (SWFDEC_IS_SWF_DECODER (dec), NULL);

  paint_style_type = swfdec_bits_get_u8 (bits);
  SWFDEC_LOG ("    type 0x%02x", paint_style_type);

  if (paint_style_type == 0x00) {
    pattern = g_object_new (SWFDEC_TYPE_COLOR_PATTERN, NULL);
    SWFDEC_COLOR_PATTERN (pattern)->start_color = swfdec_bits_get_rgba (bits);
    SWFDEC_COLOR_PATTERN (pattern)->end_color = swfdec_bits_get_rgba (bits);
    SWFDEC_LOG ("    color %08x => %08x", SWFDEC_COLOR_PATTERN (pattern)->start_color,
	SWFDEC_COLOR_PATTERN (pattern)->end_color);
  } else if (paint_style_type == 0x10 || paint_style_type == 0x12) {
    SwfdecGradientPattern *gradient;
    pattern = g_object_new (SWFDEC_TYPE_GRADIENT_PATTERN, NULL);
    gradient = SWFDEC_GRADIENT_PATTERN (pattern);
    swfdec_bits_get_matrix (bits, &pattern->start_transform, NULL);
    swfdec_bits_get_matrix (bits, &pattern->end_transform, NULL);
    swfdec_bits_get_morph_gradient (bits, &gradient->gradient, &gradient->end_gradient);
    SWFDEC_GRADIENT_PATTERN (pattern)->radial = (paint_style_type == 0x12);
  } else if (paint_style_type >= 0x40 && paint_style_type <= 0x43) {
    guint paint_id = swfdec_bits_get_u16 (bits);
    SWFDEC_LOG ("   background paint id = %d (type 0x%02x)",
	paint_id, paint_style_type);
    if (paint_id == 65535) {
      /* FIXME: someone explain this magic paint id here */
      pattern = g_object_new (SWFDEC_TYPE_COLOR_PATTERN, NULL);
      SWFDEC_COLOR_PATTERN (pattern)->start_color = SWFDEC_COLOR_COMBINE (0, 255, 255, 255);
      SWFDEC_COLOR_PATTERN (pattern)->end_color = SWFDEC_COLOR_PATTERN (pattern)->start_color;
      swfdec_bits_get_matrix (bits, &pattern->start_transform, NULL);
      swfdec_bits_get_matrix (bits, &pattern->end_transform, NULL);
    } else {
      pattern = g_object_new (SWFDEC_TYPE_IMAGE_PATTERN, NULL);
      swfdec_bits_get_matrix (bits, &pattern->start_transform, NULL);
      swfdec_bits_get_matrix (bits, &pattern->end_transform, NULL);
      SWFDEC_IMAGE_PATTERN (pattern)->image = swfdec_swf_decoder_get_character (dec, paint_id);
      if (!SWFDEC_IS_IMAGE (SWFDEC_IMAGE_PATTERN (pattern)->image)) {
	g_object_unref (pattern);
	SWFDEC_ERROR ("could not find image with id %u for pattern", paint_id);
	return NULL;
      }
      if (paint_style_type == 0x40 || paint_style_type == 0x42) {
	SWFDEC_IMAGE_PATTERN (pattern)->extend = CAIRO_EXTEND_REPEAT;
      } else {
	SWFDEC_IMAGE_PATTERN (pattern)->extend = CAIRO_EXTEND_NONE;
      }
      if (paint_style_type == 0x40 || paint_style_type == 0x41) {
	SWFDEC_IMAGE_PATTERN (pattern)->filter = CAIRO_FILTER_BILINEAR;
      } else {
	SWFDEC_IMAGE_PATTERN (pattern)->filter = CAIRO_FILTER_NEAREST;
      }
    }
  } else {
    SWFDEC_ERROR ("unknown paint style type 0x%02x", paint_style_type);
    return NULL;
  }
  if (cairo_matrix_invert (&pattern->start_transform)) {
    SWFDEC_ERROR ("paint start transform matrix not invertible, resetting");
    cairo_matrix_init_identity (&pattern->start_transform);
  }
  if (cairo_matrix_invert (&pattern->end_transform)) {
    SWFDEC_ERROR ("paint end transform matrix not invertible, resetting");
    cairo_matrix_init_identity (&pattern->end_transform);
  }
  swfdec_bits_syncbits (bits);
  return SWFDEC_DRAW (pattern);
}

/**
 * swfdec_pattern_new_color:
 * @color: color to paint in
 *
 * Creates a new pattern to paint with the given color
 *
 * Returns: a new @SwfdecPattern to paint with
 */
SwfdecPattern *	
swfdec_pattern_new_color (SwfdecColor color)
{
  SwfdecPattern *pattern = g_object_new (SWFDEC_TYPE_COLOR_PATTERN, NULL);

  SWFDEC_COLOR_PATTERN (pattern)->start_color = color;
  SWFDEC_COLOR_PATTERN (pattern)->end_color = color;

  return pattern;
}

char *
swfdec_pattern_to_string (SwfdecPattern *pattern)
{
  g_return_val_if_fail (SWFDEC_IS_PATTERN (pattern), NULL);

  if (SWFDEC_IS_IMAGE_PATTERN (pattern)) {
    SwfdecImagePattern *image = SWFDEC_IMAGE_PATTERN (pattern);
    if (image->image->width == 0)
      cairo_surface_destroy (swfdec_image_create_surface (image->image));
    return g_strdup_printf ("%ux%u image %u (%s, %s)", image->image->width,
	image->image->height, SWFDEC_CHARACTER (image->image)->id,
	image->extend == CAIRO_EXTEND_REPEAT ? "repeat" : "no repeat",
	image->filter == CAIRO_FILTER_BILINEAR ? "bilinear" : "nearest");
  } else if (SWFDEC_IS_COLOR_PATTERN (pattern)) {
    if (SWFDEC_COLOR_PATTERN (pattern)->start_color == SWFDEC_COLOR_PATTERN (pattern)->end_color)
      return g_strdup_printf ("color #%08X", SWFDEC_COLOR_PATTERN (pattern)->start_color);
    else
      return g_strdup_printf ("color #%08X => #%08X", SWFDEC_COLOR_PATTERN (pattern)->start_color,
	  SWFDEC_COLOR_PATTERN (pattern)->end_color);
  } else if (SWFDEC_IS_GRADIENT_PATTERN (pattern)) {
    SwfdecGradientPattern *gradient = SWFDEC_GRADIENT_PATTERN (pattern);
    return g_strdup_printf ("%s gradient (%u colors)", gradient->radial ? "radial" : "linear",
	gradient->gradient->n_gradients);
  } else {
    return g_strdup_printf ("%s", G_OBJECT_TYPE_NAME (pattern));
  }
}

cairo_pattern_t *
swfdec_pattern_get_pattern (SwfdecPattern *pattern, const SwfdecColorTransform *trans)
{
  SwfdecPatternClass *klass;

  g_return_val_if_fail (SWFDEC_IS_PATTERN (pattern), NULL);
  g_return_val_if_fail (trans != NULL, NULL);

  klass = SWFDEC_PATTERN_GET_CLASS (pattern);
  g_assert (klass->get_pattern);
  return klass->get_pattern (pattern, trans);
}

