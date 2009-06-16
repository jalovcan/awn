/*
 *  Copyright (C) 2007 Michal Hruby <michal.mhr@gmail.com>
 *  Copyright (C) 2008 Rodney Cryderman <rcryderman@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "awn-effects.h"
#include "awn-effects-ops-new.h"
#include "awn-enum-types.h"
#include "awn-overlay.h"

#include "awn-config-client.h"
#include "awn-config-bridge.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <cairo/cairo-xlib.h>

#include "gseal-transition.h"

#include "anims/awn-effects-shared.h"

#include "anims/awn-effect-bounce.h"
#include "anims/awn-effect-desaturate.h"
#include "anims/awn-effect-fade.h"
#include "anims/awn-effect-glow.h"
#include "anims/awn-effect-simple.h"
#include "anims/awn-effect-spotlight.h"
#include "anims/awn-effect-spotlight3d.h"
#include "anims/awn-effect-squish.h"
#include "anims/awn-effect-turn.h"
#include "anims/awn-effect-zoom.h"

G_DEFINE_TYPE(AwnEffects, awn_effects, G_TYPE_OBJECT)

#define AWN_EFFECTS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj),\
  AWN_TYPE_EFFECTS, \
  AwnEffectsPrivate))

/* if someone wants faster/slower animations add a speed multiplier
 * property (and use it in the animations) but don't change fps
 */
#define AWN_FRAMES_PER_SECOND(fx) (25)
#define AWN_ANIMATIONS_PER_BUNDLE 5

#define AWN_INTERNAL_ICON "__awn_internal_"

#define AWN_INTERNAL_SPOTLIGHT	AWN_INTERNAL_ICON "spotlight"
#define AWN_INTERNAL_ARROW1	AWN_INTERNAL_ICON "arrow1"
#define AWN_INTERNAL_ARROW2	AWN_INTERNAL_ICON "arrow2"

typedef gboolean (*_AwnAnimation)(AwnEffectsAnimation*);

enum
{
  ANIMATION_START,
  ANIMATION_END,

  LAST_SIGNAL
};
static guint32 _effects_signals[LAST_SIGNAL] = { 0 };

enum {
  PROP_0,
  PROP_WIDGET,
  PROP_NO_CLEAR,
  PROP_INDIRECT_PAINT,
  PROP_ORIENTATION,
  PROP_CURRENT_EFFECTS,
  PROP_ICON_OFFSET,
  PROP_ICON_ALPHA,
  PROP_REFLECTION_OFFSET,
  PROP_REFLECTION_ALPHA,
  PROP_REFLECTION_VISIBLE,
  PROP_MAKE_SHADOW,
  PROP_LABEL,
  PROP_IS_ACTIVE,
  PROP_PROGRESS,
  PROP_BORDER_CLIP,
  PROP_SPOTLIGHT_ICON,
  PROP_ARROW_ICON,
  PROP_ARROWS_COUNT,
  PROP_CUSTOM_ACTIVE_ICON
};

/* FORWARDS */
static void awn_effects_prop_changed (GObject *object, GParamSpec *pspec);

static void
awn_effects_dispose (GObject *object)
{
  AwnEffects *fx = AWN_EFFECTS (object);
  
  /* destroy animation timer */
  if (fx->priv->timer_id)
  {
    g_source_remove (fx->priv->timer_id);
    fx->priv->timer_id = 0;
  }

  /* unref overlays in our overlay list */
  if (fx->priv->overlays)
  {
    for (GList *iter = fx->priv->overlays; iter != NULL;
         iter = g_list_next (iter))
    {
      AwnOverlay *overlay = iter->data;
      g_signal_handlers_disconnect_by_func (overlay,
        G_CALLBACK (awn_effects_prop_changed), fx);
      g_object_unref (overlay);
    }
    g_list_free (fx->priv->overlays);
    fx->priv->overlays = NULL;
  }

  G_OBJECT_CLASS (awn_effects_parent_class)->dispose(object);
}

static void
awn_effects_finalize (GObject *object)
{
  AwnEffects *fx = AWN_EFFECTS (object);

  fx->widget = NULL;

  /* free effect queue and associated AwnEffectsPriv */
  if (fx->priv->effect_queue)
  {
    g_list_foreach (fx->priv->effect_queue, (GFunc)g_free, NULL);
    g_list_free (fx->priv->effect_queue);
    fx->priv->effect_queue = NULL;
  }

  if (fx->label)
  {
    g_free (fx->label);
    fx->label = NULL;
  }

  G_OBJECT_CLASS (awn_effects_parent_class)->finalize(object);
}

/* it'd be so much easier if this wasn't compiled into AWN */
typedef struct _mem_reader_t {
  void *data;
  size_t size;
  unsigned int offset;
} mem_reader_t;

static cairo_status_t
internal_reader(void *closure, unsigned char *data,
                               unsigned int length)
{
  mem_reader_t *mem_reader = (mem_reader_t*)closure;
  unsigned char *in_data = mem_reader->data;

  if (mem_reader->offset + length <= mem_reader->size)
  {
    memcpy(data, in_data + mem_reader->offset, length);
    mem_reader->offset += length;
    return CAIRO_STATUS_SUCCESS;
  }

  return CAIRO_STATUS_READ_ERROR;
}

static GQuark
awn_effects_set_custom_icon(AwnEffects *fx, const gchar *path)
{
  if (path == NULL || path[0] == '\0') return 0;
  /* handle Awn's internal icons */
  else if (g_str_has_prefix (path, AWN_INTERNAL_ICON))
  {
    if (g_strcmp0 (path, AWN_INTERNAL_ARROW1) == 0)
      fx->priv->arrow_type = AWN_ARROW_TYPE_1;
    else if (g_strcmp0 (path, AWN_INTERNAL_ARROW2) == 0)
      fx->priv->arrow_type = AWN_ARROW_TYPE_2;

    /* did we already initialize data for this string? */
    GQuark q = g_quark_try_string (path);
    if (q) return q;

    q = g_quark_from_string(path);
    cairo_surface_t *surface = NULL;

    if (g_strcmp0(path, AWN_INTERNAL_SPOTLIGHT) == 0)
    {
      #include "../data/active/spotlight_png_inline.c"

      mem_reader_t mem_reader;
      mem_reader.data = spotlight_bin_data;
      mem_reader.size = sizeof(spotlight_bin_data);
      mem_reader.offset = 0;

      surface = cairo_image_surface_create_from_png_stream (internal_reader,
                                                            &mem_reader);
      if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Error while trying to read internal PNG icon!");
        cairo_surface_destroy(surface);
        surface = NULL;
      }
    }

    GData **icons = &(AWN_EFFECTS_GET_CLASS(fx)->custom_icons);

    g_datalist_id_set_data(icons, q, surface);

    return q;
  }
  else
  {
    GQuark q = g_quark_try_string(path);
    if (!q) {
      /* needs to be added to class' custom_icons */
      q = g_quark_from_string(path);

      cairo_surface_t *surface = cairo_image_surface_create_from_png(path);
      if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        g_warning("Error while trying to read PNG icon \"%s\"", path);
        cairo_surface_destroy(surface);
        surface = NULL;
      }
      GData **icons = &(AWN_EFFECTS_GET_CLASS(fx)->custom_icons);

      g_datalist_id_set_data(icons, q, surface);
    }
    return q;
  }
}

static void
awn_effects_get_property (GObject      *object,
                          guint         prop_id,
                          GValue *value,
                          GParamSpec   *pspec)
{
  AwnEffects *fx = AWN_EFFECTS(object);

  switch (prop_id) {
    case PROP_WIDGET:
      g_value_set_object(value, fx->widget);
      break;
    case PROP_NO_CLEAR:
      g_value_set_boolean(value, fx->no_clear);
      break;
    case PROP_INDIRECT_PAINT:
      g_value_set_boolean(value, fx->indirect_paint);
      break;
    case PROP_ORIENTATION:
      g_value_set_int(value, fx->orientation);
      break;
    case PROP_CURRENT_EFFECTS:
      g_value_set_uint(value, fx->set_effects);
      break;
    case PROP_ICON_OFFSET:
      g_value_set_int(value, fx->icon_offset);
      break;
    case PROP_ICON_ALPHA:
      g_value_set_float(value, fx->icon_alpha);
      break;
    case PROP_REFLECTION_OFFSET:
      g_value_set_int(value, fx->refl_offset);
      break;
    case PROP_REFLECTION_ALPHA:
      g_value_set_float(value, fx->refl_alpha);
      break;
    case PROP_REFLECTION_VISIBLE:
      g_value_set_boolean(value, fx->do_reflection);
      break;
    case PROP_MAKE_SHADOW:
      g_value_set_boolean(value, fx->make_shadow);
      break;
    case PROP_LABEL:
      g_value_set_string(value, fx->label);
      break;
    case PROP_IS_ACTIVE:
      g_value_set_boolean(value, fx->is_active);
      break;
    case PROP_ARROWS_COUNT:
      g_value_set_int(value, fx->arrows_count);
      break;
    case PROP_PROGRESS:
      g_value_set_float(value, fx->progress);
      break;
    case PROP_BORDER_CLIP:
      g_value_set_int(value, fx->border_clip);
      break;
    case PROP_SPOTLIGHT_ICON:
      g_value_set_string(value, g_quark_to_string(fx->spotlight_icon));
      break;
    case PROP_ARROW_ICON:
      g_value_set_string(value, g_quark_to_string(fx->arrow_icon));
      break;
    case PROP_CUSTOM_ACTIVE_ICON:
      g_value_set_string(value, g_quark_to_string(fx->custom_active_icon));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
awn_effects_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  AwnEffects *fx = AWN_EFFECTS(object);

  switch (prop_id) {
    case PROP_WIDGET:
      fx->widget = g_value_get_object(value);
      break;
    case PROP_NO_CLEAR:
      fx->no_clear = g_value_get_boolean(value);
      break;
    case PROP_INDIRECT_PAINT:
      fx->indirect_paint = g_value_get_boolean(value);
      break;
    case PROP_ORIENTATION:
      /* make sure we set correct orient */
      switch (g_value_get_int(value)) {
        case AWN_EFFECT_ORIENT_TOP:
        case AWN_EFFECT_ORIENT_RIGHT:
        case AWN_EFFECT_ORIENT_BOTTOM:
        case AWN_EFFECT_ORIENT_LEFT:
          fx->orientation = g_value_get_int(value);
          break;
        default:
          fx->orientation = AWN_EFFECT_ORIENT_BOTTOM;
          break;
      }
      break;
    case PROP_CURRENT_EFFECTS:
      fx->set_effects = g_value_get_uint(value);
      break;
    case PROP_ICON_OFFSET:
      fx->icon_offset = g_value_get_int(value);
      break;
    case PROP_ICON_ALPHA:
      fx->icon_alpha = g_value_get_float(value);
      break;
    case PROP_REFLECTION_OFFSET:
      fx->refl_offset = g_value_get_int(value);
      break;
    case PROP_REFLECTION_ALPHA:
      fx->refl_alpha = g_value_get_float(value);
      break;
    case PROP_REFLECTION_VISIBLE:
      fx->do_reflection = g_value_get_boolean(value);
      break;
    case PROP_MAKE_SHADOW:
      fx->make_shadow = g_value_get_boolean(value);
      break;
    case PROP_LABEL:
      if (fx->label) g_free(fx->label);
      fx->label = g_value_dup_string(value);
      break;
    case PROP_IS_ACTIVE:
      fx->is_active = g_value_get_boolean(value);
      break;
    case PROP_ARROWS_COUNT:
      fx->arrows_count = g_value_get_int(value);
      break;
    case PROP_PROGRESS:
      fx->progress = g_value_get_float(value);
      break;
    case PROP_BORDER_CLIP:
      fx->border_clip = g_value_get_int(value);
      break;
    case PROP_SPOTLIGHT_ICON:
      fx->spotlight_icon =
        awn_effects_set_custom_icon (fx, g_value_get_string (value));
      break;
    case PROP_ARROW_ICON:
      /* arrow_type will be set by set_custom_icon if we use internal icon */
      fx->priv->arrow_type = AWN_ARROW_TYPE_CUSTOM;
      fx->arrow_icon = 
        awn_effects_set_custom_icon (fx, g_value_get_string (value));
      break;
    case PROP_CUSTOM_ACTIVE_ICON:
      fx->custom_active_icon = 
        awn_effects_set_custom_icon (fx, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
awn_effects_prop_changed(GObject *object, GParamSpec *pspec)
{
  AwnEffects *fx = AWN_EFFECTS(object);

  awn_effects_redraw (fx);
}

void
awn_effects_redraw(AwnEffects *fx)
{
  if (fx->widget && GTK_WIDGET_DRAWABLE (fx->widget))
  {
    gtk_widget_queue_draw (fx->widget);
  }
}

static void
awn_effects_register_effect_bundle(AwnEffectsClass *klass,
                                   _AwnAnimation opening,
                                   _AwnAnimation closing,
                                   _AwnAnimation hover,
                                   _AwnAnimation launching,
                                   _AwnAnimation attention)
{
  /* make sure there are exactly AWN_ANIMATIONS_PER_BUNDLE items */
  GPtrArray *anims = klass->animations;

  g_ptr_array_add(anims, opening);
  g_ptr_array_add(anims, closing);
  g_ptr_array_add(anims, hover);
  g_ptr_array_add(anims, launching);
  g_ptr_array_add(anims, attention);
}

static void
awn_effects_class_init(AwnEffectsClass *klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS(klass);

  obj_class->set_property = awn_effects_set_property;
  obj_class->get_property = awn_effects_get_property;
  obj_class->notify = awn_effects_prop_changed;
  obj_class->dispose = awn_effects_dispose;
  obj_class->finalize = awn_effects_finalize;

  /**
   * AwnEffects::animation-start:
   *
   * @fx: The #AwnEffects instance which received the signal.
   * @effect: Effect type which was started.
   *
   * The ::animation-start signal is emitted when the animation starts playing
   * (ie. all other animations queued before it finish playing).
   * You also have to explicitely ask AwnEffects to emit this signal using 
   * #awn_effects_start_ex method.
   * Note: Signal is emitted only once even if another animation with higher
   *  priority interrupts the animation and this animation is started again
   *  later (ie. no signal on this second start).
   */
  _effects_signals[ANIMATION_START] =
    g_signal_new ("animation-start", G_OBJECT_CLASS_TYPE(obj_class),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET(AwnEffectsClass, animation_start),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__ENUM,
                  G_TYPE_NONE, 1, AWN_TYPE_EFFECT);
  /**
   * AwnEffects::animation-end:
   *
   * @fx: The #AwnEffects instance which received the signal.
   * @effect: Effect type which just finished.
   *
   * The ::animation-end signal is emitted when the animation finishes, and
   * as ::animation-start signal is emitted only once for the entire
   * animation (see the note in description for ::animation-start signal).
   * You also have to explicitely ask AwnEffects to emit this signal using
   * #awn_effects_start_ex method.
   */
  _effects_signals[ANIMATION_END] =
    g_signal_new ("animation-end", G_OBJECT_CLASS_TYPE(obj_class),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET(AwnEffectsClass, animation_end),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__ENUM,
                  G_TYPE_NONE, 1, AWN_TYPE_EFFECT);

  g_type_class_add_private (obj_class, sizeof (AwnEffectsPrivate));

  klass->animations = g_ptr_array_sized_new(AWN_ANIMATIONS_PER_BUNDLE * 9); /* 5 animations per bundle, 9 effect bundles */

  awn_effects_register_effect_bundle(klass,
    NULL,
    NULL,
    simple_hover_effect,
    simple_attention_effect,
    simple_attention_effect
  );
  awn_effects_register_effect_bundle(klass,
    bounce_opening_effect,
    fade_out_effect,
    bounce_hover_effect,
    bounce_effect,
    bounce_effect
  );
  awn_effects_register_effect_bundle(klass,
    zoom_opening_effect,
    zoom_closing_effect,
    fading_effect,
    fading_effect,
    fading_effect
  );
  awn_effects_register_effect_bundle(klass,
    spotlight_opening_effect,
    spotlight_closing_effect,
    spotlight_hover_effect,
    spotlight_half_fade_effect,
    spotlight_half_fade_effect
  );
  awn_effects_register_effect_bundle(klass,
    zoom_opening_effect,
    zoom_closing_effect,
    zoom_effect,
    zoom_attention_effect,
    zoom_attention_effect
  );
  awn_effects_register_effect_bundle(klass,
    bounce_squish_opening_effect,
    bounce_squish_closing_effect,
    /*bounce_squish_hover_effect,*/
    bounce_squish_effect,
    bounce_squish_effect,
    bounce_squish_attention_effect
  );
  awn_effects_register_effect_bundle(klass,
    turn_opening_effect,
    turn_closing_effect,
    turn_hover_effect,
    turn_effect,
    turn_effect
  );
  awn_effects_register_effect_bundle(klass,
    spotlight3D_opening_effect,
    spotlight3D_closing_effect,
    spotlight3D_hover_effect,
    spotlight_half_fade_effect,
    spotlight3D_effect
  );
  awn_effects_register_effect_bundle(klass,
    glow_opening_effect,
    glow_closing_effect,
    glow_effect,
    glow_attention_effect,
    glow_attention_effect
  );

  /* association between icon paths and cairo image surfaces */
  g_datalist_init(&klass->custom_icons);

  /**
   * AwnEffects:widget:
   *
   * Determines which widget is animated by this instance of #AwnEffects.
   */
  g_object_class_install_property (
    obj_class, PROP_WIDGET,
    g_param_spec_object ("widget",
                         "Widget",
                         "Widget to draw to",
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE));
  /**
   * AwnEffects:no-clear:
   *
   * Whether the context should be cleared when painting.
   * Set no-clear to FALSE to improve performace if you're using
   * background of proper color on the widget.
   */
  g_object_class_install_property(
    obj_class, PROP_NO_CLEAR,
    g_param_spec_boolean("no-clear",
                         "No context clear",
                         "Determines whether to clear the context when drawing",
                         TRUE,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:indirect-paint:
   *
   * Determines whether transformations are applied directly to the widget, or
   * to an offscreen buffer before being painted to target widget.
   * Set to FALSE to improve performance, but will cause artifacts if used on
   * non-transparent window.
   */
  g_object_class_install_property(
    obj_class, PROP_INDIRECT_PAINT,
    g_param_spec_boolean("indirect-paint",
                         "Indirect paint",
                         "Determines whether to apply transforms directly on "
                         "the window or paint to a buffer instead",
                         TRUE,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:orientation:
   *
   * Determines orientation of the widget.
   */
  g_object_class_install_property(
    obj_class, PROP_ORIENTATION,
    /* keep in sync with AwnOrientation */
    g_param_spec_int("orientation",
                     "Orientation",
                     "Icon orientation",
                     0, 3, AWN_EFFECT_ORIENT_BOTTOM,
                     G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:effects:
   *
   * Set a bitmask to specify which kind of effects are used on the widget.
   */
  g_object_class_install_property(
    obj_class, PROP_CURRENT_EFFECTS,
    g_param_spec_uint("effects",
                      "Current effects",
                      "Active effects set for this instance",
                      0, G_MAXUINT, 0, /* set to classic (bouncing) */
                      G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:icon-offset:
   *
   * Determines offset of the icon from border of the widget's window.
   */
  g_object_class_install_property(
    obj_class, PROP_ICON_OFFSET,
    g_param_spec_int("icon-offset",
                     "Icon offset",
                     "Offset of drawn icon to window border",
                     G_MININT, G_MAXINT, 0,
                     G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:icon-alpha:
   *
   * Determines alpha value of the drawn icon.
   */
  g_object_class_install_property(
    obj_class, PROP_ICON_ALPHA,
    g_param_spec_float("icon-alpha",
                       "Icon alpha",
                       "Alpha value of drawn icon",
                       0.0, 1.0, 1.0,
                       G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:reflection-offset:
   *
   * Determines offset of the reflection from the drawn icon.
   */
  g_object_class_install_property(
    obj_class, PROP_REFLECTION_OFFSET,
    g_param_spec_int("reflection-offset",
                     "Reflection offset",
                     "Offset of drawn reflection to icon",
                     G_MININT, G_MAXINT, 0,
                     G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:reflection-alpha:
   *
   * Determines alpha value of the reflected icon.
   */
  g_object_class_install_property(
    obj_class, PROP_REFLECTION_ALPHA,
    g_param_spec_float("reflection-alpha",
                       "Reflection alpha",
                       "Alpha value of drawn reflection",
                       0.0, 1.0, 0.25,
                       G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:reflection-visible:
   *
   * Determines whether the reflection is visible.
   */
  g_object_class_install_property(
    obj_class, PROP_REFLECTION_VISIBLE,
    g_param_spec_boolean("reflection-visible",
                         "Reflection visibility",
                         "Determines whether reflection is visible",
                         TRUE,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:make-shadow:
   *
   * Determines whether to draw a shadow around the icon.
   */
  g_object_class_install_property(
    obj_class, PROP_MAKE_SHADOW,
    g_param_spec_boolean("make-shadow",
                         "Create shadow",
                         "Determines whether shadow is drawn around icon",
                         FALSE,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:active:
   *
   * Determines whether to draw the active rectangle behind the icon.
   */
  g_object_class_install_property(
    obj_class, PROP_IS_ACTIVE,
    g_param_spec_boolean("active",
                         "Active",
                         "Determines whether to draw active hint around icon",
                         FALSE,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:arrows-count:
   *
   * Determines the number of arrows drawn.
   */
  g_object_class_install_property(
    obj_class, PROP_ARROWS_COUNT,
    g_param_spec_int("arrows-count",
                     "Arrows count",
                     "Number of arrows to draw",
                     0, G_MAXINT, 0,
                     G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:border-clip:
   *
   * Determines amount of clipping of the icon edge. (suitable for offset
   * on the 3D bar)
   */
  g_object_class_install_property(
    obj_class, PROP_BORDER_CLIP,
    g_param_spec_int("border-clip",
                     "Active",
                     "Clips border of the icon",
                     0, G_MAXINT, 0,
                     G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:label:
   *
   * The extra label painted on top of the icon.
   */
  g_object_class_install_property(
    obj_class, PROP_LABEL,
    g_param_spec_string("label",
                        "Label",
                        "Extra label drawn on top of the icon",
                        NULL,
                        G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:progress:
   *
   * Extra progress pie painted on the pie and percentage value it represents.
   * Set to 1.0 to hide the progress pie.
   */
  g_object_class_install_property(
    obj_class, PROP_PROGRESS,
    g_param_spec_float("progress",
                       "Progress",
                       "Value displayed on extra progress pie"
                       " drawn on the icon",
                       0.0, 1.0, 1.0,
                       G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:spotlight-png:
   *
   * Path to png icon which will be used for the spotlight effect.
   */
  g_object_class_install_property(
    obj_class, PROP_SPOTLIGHT_ICON,
    g_param_spec_string("spotlight-png",
                        "Spotlight Icon",
                        "Icon to draw for the spotlight effect",
                        AWN_INTERNAL_SPOTLIGHT,
                        G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:arrow-icon:
   *
   * Path to a png icon which will be painted when the property
   * #AwnEffects:arrows-count is more than 0.
   */
  g_object_class_install_property(
    obj_class, PROP_ARROW_ICON,
    g_param_spec_string("arrow-png",
                        "Arrow Icon",
                        "Icon to draw when arrows-count is more than 0",
                        AWN_INTERNAL_ARROW1,
                        G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  /**
   * AwnEffects:custom-active-png:
   *
   * Path to a custom png icon which will be painted when the property
   * #AwnEffects:active is set to TRUE.
   */
  g_object_class_install_property(
    obj_class, PROP_CUSTOM_ACTIVE_ICON,
    g_param_spec_string("custom-active-png",
                        "Custom active Icon",
                        "Custom icon to draw when in active state",
                        NULL,
                        G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
}

static void
awn_effects_init(AwnEffects * fx)
{
  fx->priv = AWN_EFFECTS_GET_PRIVATE (fx);
  /* the entire structure is zeroed in allocation, define only non-zero vars
   * which are not properties
   */

  fx->priv->icon_width = 48;
  fx->priv->icon_height = 48;

  fx->priv->width_mod = 1.0;
  fx->priv->height_mod = 1.0;
  fx->priv->alpha = 1.0;
  fx->priv->saturation = 1.0;
}

AwnEffects* awn_effects_new_for_widget(GtkWidget * widget)
{
  g_return_val_if_fail(GTK_IS_WIDGET(widget), NULL);

  AwnEffects *fx = g_object_new(AWN_TYPE_EFFECTS, "widget", widget, NULL);

  /*
   * we will use weak reference on the widget, because we want the widget
   * to destroy us when it dies
   * though we could also ref the widget and unref it in our dispose
   */

  return fx;
}

/* we don't really need to expose this enum */
typedef enum
{
  AWN_EFFECT_PRIORITY_HIGHEST,
  AWN_EFFECT_PRIORITY_HIGH,
  AWN_EFFECT_PRIORITY_ABOVE_NORMAL,
  AWN_EFFECT_PRIORITY_NORMAL,
  AWN_EFFECT_PRIORITY_BELOW_NORMAL,
  AWN_EFFECT_PRIORITY_LOW,
  AWN_EFFECT_PRIORITY_LOWEST
} AwnEffectPriority;

static AwnEffectPriority
awn_effects_get_priority(const AwnEffect effect)
{
  switch (effect)
  {

    case AWN_EFFECT_OPENING:
      return AWN_EFFECT_PRIORITY_HIGH;

    case AWN_EFFECT_LAUNCHING:
      return AWN_EFFECT_PRIORITY_ABOVE_NORMAL;

    case AWN_EFFECT_HOVER:
      return AWN_EFFECT_PRIORITY_LOW;

    case AWN_EFFECT_ATTENTION:
      return AWN_EFFECT_PRIORITY_NORMAL;

    case AWN_EFFECT_CLOSING:
      return AWN_EFFECT_PRIORITY_HIGHEST;

    default:
      return AWN_EFFECT_PRIORITY_BELOW_NORMAL;
  }
}

static gint
awn_effects_sort(gconstpointer a, gconstpointer b)
{
  const AwnEffectsAnimation *data1 = (AwnEffectsAnimation *) a;
  const AwnEffectsAnimation *data2 = (AwnEffectsAnimation *) b;
  return (gint)(awn_effects_get_priority(data1->this_effect) - 
    awn_effects_get_priority(data2->this_effect));
}

void
awn_effects_start(AwnEffects * fx, const AwnEffect effect)
{
  awn_effects_start_ex(fx, effect, 0, FALSE, FALSE);
}

void
awn_effects_start_ex(AwnEffects * fx, const AwnEffect effect, gint max_loops,
                     gboolean signal_start, gboolean signal_end)
{
  if (effect == AWN_EFFECT_NONE || fx->widget == NULL)
  {
    return;
  }

  AwnEffectsAnimation *queue_item;

  GList *queue = fx->priv->effect_queue;

  /* dont start the effect if already in queue */

  while (queue)
  {
    queue_item = queue->data;

    if (queue_item->this_effect == effect)
    {
      return;
    }

    queue = g_list_next(queue);
  }

  AwnEffectsAnimation *anim = g_new(AwnEffectsAnimation, 1);

  anim->effects = fx;
  anim->this_effect = effect;
  anim->max_loops = max_loops;
  anim->signal_start = signal_start;
  anim->signal_end = signal_end;

  fx->priv->effect_queue = g_list_insert_sorted(fx->priv->effect_queue, anim,
                                                awn_effects_sort);
  awn_effects_main_effect_loop(fx);
}

void
awn_effects_stop(AwnEffects * fx, const AwnEffect effect)
{
  if (effect == AWN_EFFECT_NONE)
  {
    return;
  }

  AwnEffectsAnimation *queue_item;

  GList *queue = fx->priv->effect_queue;

  /* find the effect in queue */

  while (queue)
  {
    queue_item = queue->data;

    if (queue_item->this_effect == effect)
    {
      break;
    }

    queue = g_list_next(queue);
  }

  if (queue)
  {
    /* remove the effect from effect_queue and free the struct if the effect */

    /* is not in the middle of animation currently */
    gboolean dispose = queue_item->this_effect != fx->priv->current_effect;
    fx->priv->effect_queue = g_list_remove(fx->priv->effect_queue, queue_item);

    if (dispose)
    {
      g_free(queue_item);
    } 
    else if (fx->priv->sleeping_func)
    {
      /* wake up sleeping effect */
      fx->priv->timer_id = g_timeout_add(1000 / AWN_FRAMES_PER_SECOND(fx),
                                         fx->priv->sleeping_func, queue_item);
      fx->priv->sleeping_func = NULL;
    }
  }
}

static gpointer get_animation(AwnEffectsAnimation *topEffect, guint fxNum)
{
  switch (topEffect->this_effect)
  {
    case AWN_EFFECT_DESATURATE:
      return desaturate_effect;

    default:
      break;
  }

  GPtrArray *anims = AWN_EFFECTS_GET_CLASS(topEffect->effects)->animations;

  guint increment = topEffect->this_effect - 1;
  if (fxNum*AWN_ANIMATIONS_PER_BUNDLE + increment >= anims->len) {
    return NULL;
  }

  return g_ptr_array_index(anims, fxNum*AWN_ANIMATIONS_PER_BUNDLE + increment);
}

void
awn_effects_main_effect_loop(AwnEffects * fx)
{
  if (fx->priv->current_effect != AWN_EFFECT_NONE
      || fx->priv->effect_queue == NULL)
  {
    if (fx->priv->sleeping_func && fx->priv->effect_queue)
    {
      /* check if sleeping effect is still on top */
      AwnEffectsAnimation *queue_item = fx->priv->effect_queue->data;
      if (fx->priv->current_effect == queue_item->this_effect) return;

      /* sleeping effect is not on top -> wake & play it */

      /* find correct effectsPrivate starting with the second item */
      GList *queue = g_list_next(fx->priv->effect_queue);
      while (queue)
      {
        queue_item = queue->data;

        if (queue_item->this_effect == fx->priv->current_effect) break;

        queue = g_list_next(queue);
      }

      g_return_if_fail(queue_item);

      fx->priv->timer_id = g_timeout_add(1000 / AWN_FRAMES_PER_SECOND(fx),
                                         fx->priv->sleeping_func, queue_item);
      fx->priv->sleeping_func = NULL;
    }
    return;
  }

  AwnEffectsAnimation *topEffect =

    (AwnEffectsAnimation *)(fx->priv->effect_queue->data);

  /* FIXME: simplifing the index to (topEffect->thisEffect-1) changed
   *  the gconf key a bit -> update awn-manager's custom effect setting
   *  to reflect this change.
   */

  gint i = topEffect->this_effect - 1;
  guint effect = fx->set_effects & (0xF << (i * 4));
  effect >>= i * 4;

  GSourceFunc animation = (GSourceFunc) get_animation(topEffect, effect);

  if (animation)
  {
    fx->priv->timer_id = g_timeout_add(1000 / AWN_FRAMES_PER_SECOND(fx),
                                       animation, topEffect);
    fx->priv->current_effect = topEffect->this_effect;
    fx->priv->effect_lock = FALSE;
  }
  else
  {
    /* emit the signals */
    awn_effect_emit_anim_start(topEffect);
    awn_effect_emit_anim_end(topEffect);

    /* dispose AwnEffectsAnimation */
    awn_effects_stop(fx, topEffect->this_effect);
  }
}

void awn_effects_emit_anim_start(AwnEffects *fx, AwnEffect effect)
{
  g_signal_emit( fx, _effects_signals[ANIMATION_START], 0, effect);
}

void awn_effects_emit_anim_end(AwnEffects *fx, AwnEffect effect)
{
  g_signal_emit( fx, _effects_signals[ANIMATION_END], 0, effect);
}

void
awn_effects_set_icon_size(AwnEffects * fx, gint width, gint height,
                          gboolean requestSize)
{
  AwnEffectsPrivate *priv = fx->priv;

  gint old_width = priv->icon_width;
  gint old_height = priv->icon_height;

  if (width <= 0) width = 1;
  if (height <= 0) height = 1;

  priv->icon_width = width;
  priv->icon_height = height;

  if (priv->clip)
  {
    /* we're in middle of animation, let's update the clip region */
    priv->clip_region.x =
      priv->clip_region.x / (float)old_width * width;
    priv->clip_region.y =
      priv->clip_region.y / (float)old_height * height;
    priv->clip_region.width =
      priv->clip_region.width / (float)old_width * width;
    priv->clip_region.height =
      priv->clip_region.height / (float)old_height * height;
  }

  if (requestSize && fx->widget)
  {
    /* this should be only used without AwnIcon,
     * AwnIcon handles size_requests well enough
     */

    gint inc = fx->icon_offset;
    switch (fx->orientation) {
      case AWN_EFFECT_ORIENT_TOP:
      case AWN_EFFECT_ORIENT_BOTTOM:
        gtk_widget_set_size_request(fx->widget, width*6/5, height + inc);
        break;
      case AWN_EFFECT_ORIENT_RIGHT:
      case AWN_EFFECT_ORIENT_LEFT:
        gtk_widget_set_size_request(fx->widget, width + inc, height*6/5);
        break;
    }
  }
}

cairo_t *awn_effects_cairo_create(AwnEffects *fx)
{
  return awn_effects_cairo_create_clipped(fx, NULL);
}

cairo_t *awn_effects_cairo_create_clipped(AwnEffects *fx,
                                          GdkRegion *region)
{
  g_return_val_if_fail(AWN_IS_EFFECTS(fx) && fx->widget, NULL);

  AwnEffectsPrivate *priv = fx->priv;
  cairo_t *cr;

  cr = gdk_cairo_create (gtk_widget_get_window (fx->widget));
  g_return_val_if_fail(cairo_status(cr) == CAIRO_STATUS_SUCCESS, NULL);
  fx->window_ctx = cr;

  /* 
   * Oh right, first we used cairo_xlib_surface_get_width/height, but we
   * discovered that is causes artifacts, so now we use simple allocation
   * which seems to work just fine.
   */
  priv->window_width = fx->widget->allocation.width;
  priv->window_height = fx->widget->allocation.height;

  if (fx->no_clear == FALSE)
    awn_effects_pre_op_clear(fx, cr, NULL, NULL);
  if (region && gdk_region_empty(region) == FALSE)
  {
    /* Python apps pass empty region... interesting, I guess they should use
     * cairo_create instead of cairo_create_clipped, but we'll be nice
     */

    /* clip the region */
    gdk_cairo_region (cr, region);
    cairo_clip (cr);
  }

#if 0
  g_debug("Icon size: %dx%d, Surface size: %dx%d", 
          priv->icon_width, priv->icon_height,
          priv->window_width, priv->window_height);
  g_debug("Allocation size: (%d,%d) (%dx%d)",
          fx->widget->allocation.x, fx->widget->allocation.y,
          fx->widget->allocation.width, fx->widget->allocation.height);
#endif

  if (fx->indirect_paint)
  {
    cairo_surface_t *targetSurface = cairo_get_target(cr);
    /* we'll give to user virtual context and later paint everything on real one */
    targetSurface = cairo_surface_create_similar(targetSurface,
                                                 CAIRO_CONTENT_COLOR_ALPHA,
                                                 priv->window_width,
                                                 priv->window_height
                                                );
    g_return_val_if_fail(
      cairo_surface_status(targetSurface) == CAIRO_STATUS_SUCCESS, NULL);
    cr = cairo_create(targetSurface);
  }
  /* if we're painting directly virtual_ctx == window_ctx */
  fx->virtual_ctx = cr;

  /* FIXME: make GtkAllocation AwnEffects member, so it's accessible in both
   * pre and post ops (can be then used for optimizations)
   * Then the param should be also removed from all ops functions.
   */
  GtkAllocation ds;
  ds.width = priv->icon_width;
  ds.height = priv->icon_height;
  ds.x = (priv->window_width - ds.width) / 2;
  ds.y = (priv->window_height - ds.height); /* sit on bottom by default */

  /* put actual transformations here (no drawing)
   * FIXME: put the functions in some kind of list/array
   */
  awn_effects_pre_op_translate(fx, cr, &ds, NULL);
  awn_effects_pre_op_clip(fx, cr, &ds, NULL);
  awn_effects_pre_op_scale(fx, cr, &ds, NULL);
  awn_effects_pre_op_rotate(fx, cr, &ds, NULL);
  awn_effects_pre_op_flip(fx, cr, &ds, NULL);

  return cr;
}

void awn_effects_cairo_destroy(AwnEffects *fx)
{
  cairo_t *cr = fx->virtual_ctx;

  /* FIXME: divide overlays into two lists - those where effects should be
   *  applied and where they shouldn't
   */
  GList *overlays_with_effects = NULL;
  GList *overlays_wo_effects = NULL;

  for (GList *iter=g_list_first (fx->priv->overlays); iter != NULL;
       iter=g_list_next (iter))
  {
    GList **target = awn_overlay_get_apply_effects (AWN_OVERLAY (iter->data)) ?
      &overlays_with_effects : &overlays_wo_effects;
    *target = g_list_append (*target, iter->data);
  }

  /* Now paint the overlays which should have effects applied */
  for (GList *iter=g_list_first (overlays_with_effects); iter != NULL;
       iter=g_list_next (iter))
  {
    AwnOverlay *overlay = iter->data;
    awn_overlay_render (overlay, fx->widget, cr,
                        fx->priv->icon_width, fx->priv->icon_height);
  }

  cairo_reset_clip(cr);
  cairo_identity_matrix(cr);

  /* put surface operations here
   * FIXME: put the functions in some kind of list/array
   */
  awn_effects_post_op_clip(fx, cr, NULL, NULL);
  awn_effects_post_op_depth(fx, cr, NULL, NULL);
  awn_effects_post_op_shadow(fx, cr, NULL, NULL);
  awn_effects_post_op_saturate(fx, cr, NULL, NULL);
  awn_effects_post_op_glow(fx, cr, NULL, NULL);
  awn_effects_post_op_alpha(fx, cr, NULL, NULL);
  awn_effects_post_op_reflection(fx, cr, NULL, NULL);
  awn_effects_post_op_active (fx, cr, NULL, NULL);
  awn_effects_post_op_spotlight(fx, cr, NULL, NULL);
  awn_effects_post_op_arrow (fx, cr, NULL, NULL);
  awn_effects_post_op_progress(fx, cr, NULL, NULL);
  /* TODO: we're missing op to paint label */

  if (overlays_wo_effects != NULL)
  {
    double x, y;
    awn_effects_get_base_coords (fx, &x, &y);
    cairo_translate (cr, x, y);

    /* Now paint the overlays which should NOT have effects applied */
    for (GList *iter=g_list_first (overlays_wo_effects); iter != NULL;
         iter=g_list_next (iter))
    {
      AwnOverlay *overlay = iter->data;
      awn_overlay_render (overlay, fx->widget, cr,
                          fx->priv->icon_width, fx->priv->icon_height);
    }
  }

  if (fx->indirect_paint)
  {
    cairo_set_operator(fx->window_ctx, CAIRO_OPERATOR_OVER);
    cairo_set_source_surface(fx->window_ctx, cairo_get_target(cr), 0, 0);
    cairo_paint(fx->window_ctx);

    cairo_surface_destroy(cairo_get_target(cr));
    cairo_destroy(fx->virtual_ctx);
  }
  cairo_destroy(fx->window_ctx);

  g_list_free (overlays_with_effects);
  g_list_free (overlays_wo_effects);

  fx->window_ctx = NULL;
  fx->virtual_ctx = NULL;
}

void
awn_effects_add_overlay (AwnEffects *fx, AwnOverlay *overlay)
{
  g_return_if_fail (AWN_IS_EFFECTS (fx));

  AwnEffectsPrivate *priv = fx->priv;

  if (g_list_find (priv->overlays, overlay) == NULL)
  {
    priv->overlays = g_list_append (priv->overlays, g_object_ref (overlay));
    awn_effects_redraw (fx);
    g_signal_connect_swapped (overlay, "notify",
                              G_CALLBACK (awn_effects_prop_changed), fx);
  }
  else
  {
    g_warning ("%s: Attempt to add overlay that is already in overlays list!",
               __func__);
  }
}

void
awn_effects_remove_overlay (AwnEffects *fx, AwnOverlay *overlay)
{
  g_return_if_fail (AWN_IS_EFFECTS (fx));

  AwnEffectsPrivate *priv = fx->priv;
  GList *elem;

  if ((elem = (g_list_find (priv->overlays, overlay))) != NULL)
  {
    g_signal_handlers_disconnect_by_func (overlay,
      G_CALLBACK (awn_effects_prop_changed), fx);
    priv->overlays = g_list_delete_link (priv->overlays, elem);
    g_object_unref (overlay);
    awn_effects_redraw (fx);
  }
  else
  {
    g_warning ("%s: Attempt to remove overlay that is not in overlays list!",
               __func__);
  }
}

GList*
awn_effects_get_overlays (AwnEffects *fx)
{
  g_return_val_if_fail (AWN_IS_EFFECTS (fx), NULL);

  return g_list_copy (fx->priv->overlays);
}

