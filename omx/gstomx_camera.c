/* GStreamer
 *
 * Copyright (C) 2009 Texas Instruments, Inc - http://www.ti.com/
 *
 * Description: OMX Camera element
 *  Created on: Aug 31, 2009
 *      Author: Rob Clark <rob@ti.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "gstomx_camera.h"
#include "gstomx.h"

#include <gst/video/video.h>

/**
 * SECTION:element-omx_camerasrc
 *
 * omx_camerasrc can be used to capture video and/or still frames from OMX
 * camera.  It can also be used as a filter to provide access to the camera's
 * memory-to-memory mode.
 * <p>
 * In total, the omx_camerasrc element exposes one optional input port, "sink",
 * one mandatory src pad, "src", and two optional src pads, "imgsrc" and
 * "vidsrc".  If "imgsrc" and/or "vidsrc" are linked, then viewfinder buffers
 * are pushed on the "src" pad.
 * <p>
 * In all modes, preview buffers are pushed on the "src" pad.  In video capture
 * mode, the same buffer is pushed on the "vidsrc" pad.  In image capture mode,
 * a separate full resolution image (either raw or jpg encoded) is pushed on
 * the "imgsrc" pad.
 * <p>
 * The camera pad_alloc()s buffers from the "src" pad, in order to allocate
 * memory from the video driver.  The "vidsrc" caps are slaved to the "src"
 * caps.  Although this should be considered an implementation detail.
 * <p>
 * TODO: for legacy mode support, as a replacement for v4l2src, can we push
 * buffers of the requested resolution on the "src" pad?  Can we configure the
 * OMX component for arbitrary resolution on the preview port, or do we need
 * to dynamically map the "src" pad to different ports depending on the config?
 * The OMX camera supports only video resolutions on the preview and video
 * ports, but supports higher resolution stills on the image port.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch ... TODO ...
 * ]| ... TODO ...
 * </refsect2>
 */

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("Video OMX Camera Source",
    "Source/Video",
    "Reads frames from a OMX Camera Component",
    "Rob Clark <rob@ti.com>");


enum
{
    ARG_0,
    /* TODO add props here... */
    ARG_
};

GSTOMX_BOILERPLATE (GstOmxCamera, gst_omx_camera, GstOmxBaseSrc, GST_OMX_BASE_SRC_TYPE);


static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV_STRIDED (GSTOMX_ALL_FORMATS, "[ 0, max ]"))
    );

static GstStaticPadTemplate imgsrc_template =
GST_STATIC_PAD_TEMPLATE ("imgsrc",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,   // XXX ???
        /* note: imgsrc pad supports jpg format, as well as non-strided YUV */
        GST_STATIC_CAPS (
                "image/jpeg, width=(int)[1,max], height=(int)[1,max]; "
                GST_VIDEO_CAPS_YUV (GSTOMX_ALL_FORMATS))
    );

static GstStaticPadTemplate vidsrc_template =
GST_STATIC_PAD_TEMPLATE ("vidsrc",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,   // XXX ???
        GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV_STRIDED (GSTOMX_ALL_FORMATS, "[ 0, max ]"))
    );

#if 0
static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("???")
    );
#endif

static GstCaps *
src_getcaps (GstPad *pad)
{
    GstOmxCamera *self = GST_OMX_CAMERA (GST_PAD_PARENT (pad));
    GST_INFO_OBJECT (self, "NYI");
    // TODO
    return NULL;
}

static gboolean
src_setcaps (GstPad *pad, GstCaps *caps)
{
    GstOmxCamera *self = GST_OMX_CAMERA (GST_PAD_PARENT (pad));

    GST_INFO_OBJECT (self, "setcaps (src): %" GST_PTR_FORMAT, caps);

    g_return_val_if_fail (gst_caps_get_size (caps) == 1, FALSE);

    // TODO configure port

    return TRUE;
}

static gboolean
imgsrc_setcaps (GstPad *pad, GstCaps *caps)
{
    GstOmxCamera *self = GST_OMX_CAMERA (GST_PAD_PARENT (pad));

    GST_INFO_OBJECT (self, "setcaps (imgsrc): %" GST_PTR_FORMAT, caps);

    g_return_val_if_fail (gst_caps_get_size (caps) == 1, FALSE);

    // TODO configure port

    return TRUE;
}


static void
settings_changed_cb (GOmxCore *core)
{
    GstOmxBaseSrc *omx_base;

    omx_base = core->object;

    GST_DEBUG_OBJECT (omx_base, "settings changed");

    /** @todo properly set the capabilities */
}

static void
setup_ports (GstOmxBaseSrc *base_src)
{
    GstOmxCamera *self = GST_OMX_CAMERA (base_src);
    OMX_PARAM_PORTDEFINITIONTYPE param;

    g_omx_port_get_config (self->vid_port, &param);
    g_omx_port_setup (self->vid_port, &param);

    g_omx_port_get_config (self->img_port, &param);
    g_omx_port_setup (self->img_port, &param);

#if 0
    g_omx_port_get_config (self->in_port, &param);
    g_omx_port_setup (self->in_port, &param);
#endif
}

/*
 * GstBaseSrc Methods:
 */

static GstFlowReturn
create (GstBaseSrc *gst_base,
        guint64 offset,
        guint length,
        GstBuffer **ret_buf)
{
    GstOmxCamera *self = GST_OMX_CAMERA (gst_base);
    GstFlowReturn ret;

    GST_DEBUG_OBJECT (self, "NYI");

    ret = GST_BASE_SRC_CLASS (parent_class)->create (gst_base, offset, length, ret_buf);

    return ret;
}

/*
 * GObject Methods:
 */

static void
set_property (GObject *obj,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    GstOmxCamera *self = GST_OMX_CAMERA (obj);

    GST_DEBUG_OBJECT (self, "NYI");

    switch (prop_id)
    {
        // TODO: add properties
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
}

static void
get_property (GObject *obj,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    GstOmxCamera *self = GST_OMX_CAMERA (obj);

    GST_DEBUG_OBJECT (self, "NYI");

    switch (prop_id)
    {
        // TODO: add properties
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
}

static void
type_base_init (gpointer g_class)
{
    GstElementClass *element_class;

    element_class = GST_ELEMENT_CLASS (g_class);

    gst_element_class_set_details (element_class, &element_details);

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&src_template));

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&imgsrc_template));

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&vidsrc_template));

#if 0
    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&sink_template));
#endif
}

static void
type_class_init (gpointer g_class,
                 gpointer class_data)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
    GstBaseSrcClass *gst_base_src_class = GST_BASE_SRC_CLASS (g_class);
    GstOmxBaseSrcClass *omx_base_class = GST_OMX_BASE_SRC_CLASS (g_class);

    omx_base_class->out_port_index = OMX_CAMERA_PORT_VIDEO_OUT_PREVIEW;

    /* GstBaseSrc methods: */
    gst_base_src_class->create = create;

    /* GObject methods: */
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    /* TODO install properties... */
}

static void
type_instance_init (GTypeInstance *instance,
                    gpointer g_class)
{
    GstOmxCamera   *self     = GST_OMX_CAMERA (instance);
    GstOmxBaseSrc  *omx_base = GST_OMX_BASE_SRC (self);
    GstBaseSrc     *basesrc  = GST_BASE_SRC (self);
    GstPadTemplate *pad_template;

    GST_DEBUG_OBJECT (omx_base, "begin");

    omx_base->setup_ports = setup_ports;

    omx_base->gomx->settings_changed_cb = settings_changed_cb;

    omx_base->gomx->use_timestamps = TRUE;

    self->vid_port = g_omx_core_get_port (omx_base->gomx, "vid",
            OMX_CAMERA_PORT_VIDEO_OUT_VIDEO);
    self->img_port = g_omx_core_get_port (omx_base->gomx, "img",
            OMX_CAMERA_PORT_IMAGE_OUT_IMAGE);
// TODO I think we need to pad_alloc on the img port to figure out if the downstream element wants jpg or raw..
//    self->img_port->buffer_alloc = img_buffer_alloc;

    /* setup src pad (already created by basesrc): */

    gst_pad_set_getcaps_function (basesrc->srcpad,
            GST_DEBUG_FUNCPTR (src_getcaps));
    gst_pad_set_setcaps_function (basesrc->srcpad,
            GST_DEBUG_FUNCPTR (src_setcaps));

    /* create/setup vidsrc pad: */
    pad_template = gst_element_class_get_pad_template (
            GST_ELEMENT_CLASS (g_class), "vidsrc");
    g_return_if_fail (pad_template != NULL);

    GST_DEBUG_OBJECT (basesrc, "creating vidsrc pad");
    self->vidsrcpad = gst_pad_new_from_template (pad_template, "vidsrc");

    /* create/setup imgsrc pad: */
    pad_template = gst_element_class_get_pad_template (
            GST_ELEMENT_CLASS (g_class), "imgsrc");
    g_return_if_fail (pad_template != NULL);

    GST_DEBUG_OBJECT (basesrc, "creating imgsrc pad");
    self->imgsrcpad = gst_pad_new_from_template (pad_template, "imgsrc");
    gst_pad_set_setcaps_function (self->imgsrcpad,
            GST_DEBUG_FUNCPTR (imgsrc_setcaps));


    GST_DEBUG_OBJECT (omx_base, "end");
}
