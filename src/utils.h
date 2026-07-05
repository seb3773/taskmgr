#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef WITHOUT_GTK
#include <glib.h>
#else
#include <gtk/gtk.h>
#endif
#include <stdarg.h>

G_BEGIN_DECLS

void show_error( const char* format, ... );
void show_info( const char* format, ... );
gboolean confirm( const char* question );
char* size_to_string( char* buf, guint64 size );
guint64 string_to_size(char *s);

G_END_DECLS

#endif
