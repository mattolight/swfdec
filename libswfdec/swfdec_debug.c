
#include <stdio.h>
#include "swfdec_internal.h"

static const char *swfdec_debug_level_names[] = {
  "NONE",
  "ERROR",
  "WARNING",
  "INFO",
  "DEBUG",
  "LOG"
};

static int swfdec_debug_level = SWFDEC_LEVEL_ERROR;

void
swfdec_debug_log (int level, const char *file, const char *function,
    int line, const char *format, ...)
{
  va_list varargs;
  char *s;

  if (level > swfdec_debug_level) return;

  va_start (varargs, format);
  s = g_strdup_vprintf(format, varargs);
  va_end (varargs);

  fprintf(stderr, "SWFDEC: %s: %s(%d): %s: %s\n",
      swfdec_debug_level_names[level], file, line, function, s);
  g_free (s);
}

void
swfdec_debug_set_level (int level)
{
  swfdec_debug_level = level;
}

int
swfdec_debug_get_level (void)
{
  return swfdec_debug_level;
}

void
art_warn (const char *fmt, ...)
{
  SWFDEC_DEBUG("caught art_warn");
}




