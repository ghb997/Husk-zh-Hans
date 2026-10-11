/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HUSK_TL_FRAMEWORK_H
#define HUSK_TL_FRAMEWORK_H

#include "husk-tl-dex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native function handler type */
typedef bool (*tl_dex_native_func)(tl_dex_context *ctx, tl_dex_object *this_obj,
                                  tl_dex_val *args, int nargs, tl_dex_val *ret);

/* Look up a built-in / native framework method */
tl_dex_native_func tl_framework_lookup(const char *class_desc, const char *method_name, const char *shorty);

/*
 * Public fields of framework classes.
 *
 * Android exposes data as fields as often as through methods -- Rect.left,
 * Point.x, DisplayMetrics.widthPixels, Build.VERSION.SDK_INT -- and bytecode
 * reads and writes them directly. The framework classes are not loaded from
 * anywhere here, and their state lives in native wrappers, so without these the
 * interpreter had nothing to read the field from and returned zero. A game that
 * compares a rectangle's top with a ceiling then concludes the bird is at the
 * ceiling. Each returns false when it does not know the field.
 */
bool tl_framework_field_get(tl_dex_context *ctx, tl_dex_object *obj, const tl_dex_field *f, tl_dex_val *out);
bool tl_framework_field_set(tl_dex_context *ctx, tl_dex_object *obj, const tl_dex_field *f, tl_dex_val value);
bool tl_framework_static_get(tl_dex_context *ctx, const tl_dex_field *f, tl_dex_val *out);

/*
 * Keep the app's SharedPreferences in a file, loading what is there. Without
 * this they live in memory for the life of the context, so call it for a real
 * run -- a high score is only worth saving if it is still there next launch.
 */
bool tl_framework_attach_prefs(tl_dex_context *ctx, const char *path);

/* Framework initialisation and asset loading */
bool tl_framework_init(tl_dex_context *ctx);
void tl_framework_cleanup(tl_dex_context *ctx);

/* Direct canvas rendering for a frame */
void tl_framework_render_view(tl_dex_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* HUSK_TL_FRAMEWORK_H */
