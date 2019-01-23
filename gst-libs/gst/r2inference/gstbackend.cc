/*
 * GStreamer
 * Copyright (C) 2018 RidgeRun
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "gstbackend.h"
#include "gstbackendsubclass.h"

#include <r2i/r2i.h>

#include <cstring>
#include <memory>

GST_DEBUG_CATEGORY_STATIC (gst_backend_debug_category);
#define GST_CAT_DEFAULT gst_backend_debug_category

typedef struct _GstBackendPrivate GstBackendPrivate;
struct _GstBackendPrivate
{
  r2i::FrameworkCode code;
  std::shared_ptr < r2i::IEngine > engine;
  std::shared_ptr < r2i::ILoader > loader;
  std::shared_ptr < r2i::IModel > model;
  std::unique_ptr < r2i::IParameters > params;
  std::unique_ptr < r2i::IFrameworkFactory > factory;
};

G_DEFINE_TYPE_WITH_CODE (GstBackend, gst_backend, G_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_backend_debug_category, "backend", 0,
        "debug category for backend parameters"); G_ADD_PRIVATE (GstBackend));

#define GST_BACKEND_PRIVATE(self) \
  (GstBackendPrivate *)(gst_backend_get_instance_private (self))

static GParamSpec *gst_backend_param_to_spec (r2i::ParameterMeta * param);
static int gst_backend_param_flags (int flags);

#define GST_BACKEND_ERROR gst_backend_error_quark()

static void
gst_backend_class_init (GstBackendClass * klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->set_property = gst_backend_set_property;
  oclass->get_property = gst_backend_get_property;

}

static void
gst_backend_init (GstBackend * self)
{
}

void
gst_backend_install_properties (GstBackendClass * klass,
    r2i::FrameworkCode code)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  r2i::RuntimeError error;
  static std::vector < r2i::ParameterMeta > params;
  static gint nprop = 1;

  klass->props = g_hash_table_new (g_direct_hash, g_direct_equal);

  auto factory = r2i::IFrameworkFactory::MakeFactory (code, error);
  auto pfactory = factory->MakeParameters (error);
  error = pfactory->List (params);

  for (auto & param:params) {
    GParamSpec *spec = gst_backend_param_to_spec (&param);

    g_hash_table_insert (klass->props, GINT_TO_POINTER (nprop),
        (gpointer) (param.name.c_str ()));
    g_object_class_install_property (oclass, nprop, spec);
    nprop++;
  }
}

static int
gst_backend_param_flags (int flags)
{
  int pflags = G_PARAM_STATIC_STRINGS;

  if (r2i::ParameterMeta::Flags::READ & flags) {
    pflags += G_PARAM_READABLE;
  }

  if (r2i::ParameterMeta::Flags::WRITE & flags) {
    pflags += G_PARAM_WRITABLE;
  }

  return pflags;
}

static GParamSpec *
gst_backend_param_to_spec (r2i::ParameterMeta * param)
{
  GParamSpec *spec = NULL;

  switch (param->type) {
    case (r2i::ParameterMeta::Type::INTEGER):{
      spec = g_param_spec_int (param->name.c_str (),
          param->name.c_str (),
          param->description.c_str (),
          G_MININT,
          G_MAXINT, 0, (GParamFlags) gst_backend_param_flags (param->flags));
      break;
    }
    case (r2i::ParameterMeta::Type::STRING):{
      spec = g_param_spec_string (param->name.c_str (),
          param->name.c_str (),
          param->description.c_str (),
          NULL, (GParamFlags) gst_backend_param_flags (param->flags));
      break;
    }
    default:
      g_return_val_if_reached (NULL);
  }

  return spec;
}

void
gst_backend_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBackendClass *klass = GST_BACKEND_GET_CLASS (object);
  GstBackend *self = GST_BACKEND (object);
  const gchar *param;

  GST_DEBUG_OBJECT (self, "set_property");

  param = (const gchar *) g_hash_table_lookup (klass->props,
      GINT_TO_POINTER (property_id));
  if (NULL == param) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    return;
  }
}

void
gst_backend_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstBackendClass *klass = GST_BACKEND_GET_CLASS (object);
  GstBackend *self = GST_BACKEND (object);
  const gchar *param;

  GST_DEBUG_OBJECT (self, "get_property");

  param = (const gchar *) g_hash_table_lookup (klass->props,
      GINT_TO_POINTER (property_id));
  if (NULL == param) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    return;
  }
}

gboolean
gst_backend_start (GstBackend * self, const gchar * model_location, GError **err)
{
  GstBackendPrivate *priv = GST_BACKEND_PRIVATE (self);
  r2i::RuntimeError error;
  
  g_return_val_if_fail (priv, FALSE);
  g_return_val_if_fail (model_location, FALSE);
  g_return_val_if_fail (err, FALSE);

  priv->factory = r2i::IFrameworkFactory::MakeFactory (priv->code,
      error);
  if (error.IsError ()) {
    goto error;
  }

  priv->engine = priv->factory->MakeEngine (error);
  if (error.IsError ()) {
    goto error;
  }

  priv->loader = priv->factory->MakeLoader (error);
  if (error.IsError ()) {
    goto error;
  }

  priv->model = priv->loader->Load (model_location, error);
  if (error.IsError ()) {
    goto error;
  }

  error = priv->engine->SetModel (priv->model);
  if (error.IsError ()) {
    goto error;
  }

  error = priv->engine->Start ();
  if (error.IsError ()) {
    goto error;
  }
  
  return TRUE;
error:
  g_set_error (err, GST_BACKEND_ERROR, error.GetCode (),
      "R2Inference Error: (Code:%d) %s", error.GetCode (),
      error.GetDescription ().c_str ());
  return FALSE;
}

gboolean
gst_backend_process_frame (GstBackend * self, GstVideoFrame * input_frame,
    gpointer *prediction_data, gsize *prediction_size, GError **err)
{
  GstBackendPrivate *priv = GST_BACKEND_PRIVATE (self);
  std::shared_ptr < r2i::IPrediction > prediction;
  std::shared_ptr < r2i::IFrame > frame;
  r2i::RuntimeError error;
 
  g_return_val_if_fail (priv, FALSE);
  g_return_val_if_fail (input_frame, FALSE);
  g_return_val_if_fail (prediction_data, FALSE);
  g_return_val_if_fail (prediction_size, FALSE);
  g_return_val_if_fail (err, FALSE);

  frame = priv->factory->MakeFrame (error);
  if (error.IsError ()) {
    goto error;
  }

  GST_LOG_OBJECT (self, "Processing Frame of size %d x %d",
                    input_frame->info.width , input_frame->info.height);

  error =
      frame->Configure (input_frame->data[0], input_frame->info.width,
      input_frame->info.height, r2i::ImageFormat::Id::RGB);
  if (error.IsError ()) {
    goto error;
  }

  prediction = priv->engine->Predict (frame, error);
  if (error.IsError ()) {
    goto error;
  }
  
  *prediction_size = prediction->GetResultSize ();

  /*could we avoid memory copy ?*/
  *prediction_data = g_malloc(*prediction_size);
  memcpy(*prediction_data , prediction->GetResultData(),*prediction_size);

  GST_LOG_OBJECT (self, "Size of prediction %p is %lu",
                  *prediction_data, *prediction_size);

  return TRUE;
error:
  g_set_error (err, GST_BACKEND_ERROR, error.GetCode (),
      "R2Inference Error: (Code:%d) %s", error.GetCode (),
      error.GetDescription ().c_str ());
  return FALSE;  
}

gboolean
gst_backend_set_framework_code (GstBackend * backend, r2i::FrameworkCode code)
{
  GstBackendPrivate *priv = GST_BACKEND_PRIVATE (backend);
  g_return_val_if_fail (priv, FALSE);

  priv->code = code;
  return TRUE;

}

guint
gst_backend_get_framework_code (GstBackend * backend)
{
  GstBackendPrivate *priv = GST_BACKEND_PRIVATE (backend);
  g_return_val_if_fail (priv, -1);

  return priv->code;
}

GQuark
gst_backend_error_quark(void)
{
  static GQuark q = 0;

  if (0 == q) {
    q = g_quark_from_static_string("gst-backend-error-quark");
  }
  
  return q;
}