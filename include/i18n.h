#ifndef RASTERLAB_I18N_H
#define RASTERLAB_I18N_H

#include <locale.h>

#ifdef HAVE_GETTEXT
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#endif

#define N_(String) (String)

#endif /* RASTERLAB_I18N_H */
