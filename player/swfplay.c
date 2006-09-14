#include <gtk/gtk.h>
#include <math.h>
#include <swfdec.h>
#include <swfdec_buffer.h>

static void
queue_draw (GtkWidget *widget, SwfdecRect *inval)
{
  //g_print ("queing draw of %g %g  %g %g\n", inval.x0, inval.y0, inval.x1, inval.y1);
  gtk_widget_queue_draw_area (widget, floor (inval->x0), floor (inval->y0),
      ceil (inval->x1) - floor (inval->x0), ceil (inval->y1) - floor (inval->y0));
}

static gboolean
motion_notify (GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  int x, y;
  SwfdecRect inval;
  SwfdecDecoder *dec = data;

  gtk_widget_get_pointer (widget, &x, &y);

  swfdec_decoder_handle_mouse (dec, event->x, event->y,
      g_object_get_data (G_OBJECT (widget), "swfmousebutton") ? 1 : 0, &inval);
  queue_draw (widget, &inval);
  
  return FALSE;
}

static gboolean
leave_notify (GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
  SwfdecRect inval;
  SwfdecDecoder *dec = data;

  swfdec_decoder_handle_mouse (dec, event->x, event->y, 0, &inval);
  g_object_set_data (G_OBJECT (widget), "swfmousebutton", GINT_TO_POINTER (0));
  queue_draw (widget, &inval);
  return FALSE;
}

static gboolean
press_notify (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  SwfdecRect inval;
  SwfdecDecoder *dec = data;

  if (event->button == 1) {
    swfdec_decoder_handle_mouse (dec, event->x, event->y, 1, &inval);
    g_object_set_data (G_OBJECT (widget), "swfmousebutton", GINT_TO_POINTER (1));
  }
  queue_draw (widget, &inval);
  return FALSE;
}

static gboolean
release_notify (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  SwfdecRect inval;
  SwfdecDecoder *dec = data;

  if (event->button == 1) {
    swfdec_decoder_handle_mouse (dec, event->x, event->y, 0, &inval);
    g_object_set_data (G_OBJECT (widget), "swfmousebutton", GINT_TO_POINTER (0));
  }
  queue_draw (widget, &inval);
  return FALSE;
}

static gboolean
next_image (gpointer data)
{
  GtkWidget *win = data;
  SwfdecRect inval;
  SwfdecDecoder *dec = g_object_get_data (G_OBJECT (win), "swfdec");

  swfdec_decoder_iterate (dec, &inval);
  queue_draw (win, &inval);
  
  return TRUE;
}

static gboolean
draw_current_image (GtkWidget *win, GdkEventExpose *event, SwfdecDecoder *dec)
{
  SwfdecRect rect;
  cairo_t *cr;

  cr = gdk_cairo_create (win->window);
  swfdec_rect_init (&rect, event->area.x, event->area.y, event->area.width, event->area.height);
#if 0
  {
    cairo_t *crs;
    cairo_surface_t *surface;
    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 
	event->area.width, event->area.height);
    crs = cairo_create (surface);
    cairo_translate (crs, -event->area.x, -event->area.y);
    swfdec_decoder_render (dec, crs, &rect);
    cairo_destroy (crs);
    //cairo_surface_write_to_png (surface, "image.png");
    cairo_set_source_surface (cr, surface, event->area.x, event->area.y);
    cairo_paint (cr);
    cairo_surface_destroy (surface);
  }
#else
  swfdec_decoder_render (dec, cr, &rect);
#endif
  cairo_destroy (cr);

  return TRUE;
}

static void
view_swf (SwfdecDecoder *dec)
{
  GtkWidget *win;
  double rate;
  int width, height;
  
  /* create toplevel window and set its title */
  win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW(win), "Flash File");
  
  /* exit when 'X' is clicked */
  g_object_set_data (G_OBJECT (win), "swfdec", dec);
  g_object_set_data (G_OBJECT (win), "swfmousebutton", GINT_TO_POINTER (1));
  g_signal_connect (win, "destroy", G_CALLBACK (gtk_main_quit), NULL);
  g_signal_connect_after (win, "expose-event", G_CALLBACK (draw_current_image), dec);
  g_signal_connect_after (win, "motion-notify-event", G_CALLBACK (motion_notify), dec);
  g_signal_connect_after (win, "leave-notify-event", G_CALLBACK (leave_notify), dec);
  g_signal_connect_after (win, "button-press-event", G_CALLBACK (press_notify), dec);
  g_signal_connect_after (win, "button-release-event", G_CALLBACK (release_notify), dec);
  gtk_widget_add_events (win, GDK_LEAVE_NOTIFY_MASK
      | GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK
      | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
  
  swfdec_decoder_get_image_size (dec, &width, &height);
  gtk_window_set_default_size (GTK_WINDOW (win), width, height);
  gtk_widget_show_all (win);
  swfdec_decoder_get_rate (dec, &rate);
  g_timeout_add (1000 / rate, next_image, win);
  
  gtk_main ();
}

static void buffer_free (SwfdecBuffer *buffer, void *priv)
{
  g_free (buffer->data);
}

int main (int argc, char *argv[])
{
  gsize length;
  int ret;
  char *fn = "it.swf";
  SwfdecDecoder *s;
  char *contents;
  SwfdecBuffer *buffer;
  GError *error = NULL;

  swfdec_init ();
  gtk_init (&argc, &argv);

  if(argc>=2){
	fn = argv[1];
  }

  ret = g_file_get_contents (fn, &contents, &length, &error);
  if (!ret) {
    g_printerr ("Couldn't open file \"%s\": %s\n", fn, error->message);
    return 1;
  }

  s = swfdec_decoder_new();
  buffer = swfdec_buffer_new_with_data (contents, length);
  buffer->free = buffer_free;
  ret = swfdec_decoder_add_buffer(s, buffer);

  while (ret != SWF_EOF) {
    ret = swfdec_decoder_parse(s);
    if (ret == SWF_NEEDBITS) {
      swfdec_decoder_eof(s);
    }
    if (ret == SWF_ERROR) {
      g_print("error while parsing\n");
      return 1;
    }
    if (ret == SWF_IMAGE) {
      break;
    }
  }

  view_swf (s);

  swfdec_decoder_free(s);
  s=NULL;
  return 0;
}

