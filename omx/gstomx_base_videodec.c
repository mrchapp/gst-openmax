/*
 * Copyright (C) 2007-2009 Nokia Corporation.
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
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
 *
 */

#include "gstomx_base_videodec.h"
#include "gstomx.h"

#include <gst/video/video.h>

#include <string.h> /* for memset */

GSTOMX_BOILERPLATE (GstOmxBaseVideoDec, gst_omx_base_videodec, GstOmxBaseFilter, GST_OMX_BASE_FILTER_TYPE);


static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV_STRIDED ("{ I420, YUY2, UYVY }", "[ 0, max ]"))
    );

static void
type_base_init (gpointer g_class)
{
    GstElementClass *element_class;

    element_class = GST_ELEMENT_CLASS (g_class);

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&src_template));
}

static void
type_class_init (gpointer g_class,
                 gpointer class_data)
{
}

static void
settings_changed_cb (GOmxCore *core)
{
    GstOmxBaseFilter *omx_base;
    GstOmxBaseVideoDec *self;
    guint width;
    guint height;
    guint32 format = 0;

    omx_base = core->object;
    self = GST_OMX_BASE_VIDEODEC (omx_base);

    GST_DEBUG_OBJECT (omx_base, "settings changed");

    {
        OMX_PARAM_PORTDEFINITIONTYPE param;

        g_omx_port_get_config (omx_base->out_port, &param);

        width = param.format.video.nFrameWidth;
        height = param.format.video.nFrameHeight;
        switch (param.format.video.eColorFormat)
        {
            case OMX_COLOR_FormatYUV420PackedPlanar:
                format = GST_MAKE_FOURCC ('I', '4', '2', '0'); break;
            case OMX_COLOR_FormatYCbYCr:
                format = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'); break;
            case OMX_COLOR_FormatCbYCrY:
                format = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'); break;
            default:
                break;
        }
    }

    {
        GstCaps *new_caps;

        new_caps = gst_caps_new_simple ("video/x-raw-yuv",
                                        "width", G_TYPE_INT, width,
                                        "height", G_TYPE_INT, height,
                                        "framerate", GST_TYPE_FRACTION,
                                        self->framerate_num, self->framerate_denom,
                                        "format", GST_TYPE_FOURCC, format,
                                        NULL);

        GST_INFO_OBJECT (omx_base, "caps are: %" GST_PTR_FORMAT, new_caps);
        gst_pad_set_caps (omx_base->srcpad, new_caps);
    }
}

static gboolean
sink_setcaps (GstPad *pad,
              GstCaps *caps)
{
    GstStructure *structure;
    GstOmxBaseVideoDec *self;
    GstOmxBaseFilter *omx_base;
    GOmxCore *gomx;

    gint width = 0;
    gint height = 0;

    self = GST_OMX_BASE_VIDEODEC (GST_PAD_PARENT (pad));
    omx_base = GST_OMX_BASE_FILTER (self);

    gomx = (GOmxCore *) omx_base->gomx;

    GST_INFO_OBJECT (self, "setcaps (sink): %" GST_PTR_FORMAT, caps);

    g_return_val_if_fail (gst_caps_get_size (caps) == 1, FALSE);

    structure = gst_caps_get_structure (caps, 0);

    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);

    {
        const GValue *framerate = NULL;
        framerate = gst_structure_get_value (structure, "framerate");
        if (framerate)
        {
            self->framerate_num = gst_value_get_fraction_numerator (framerate);
            self->framerate_denom = gst_value_get_fraction_denominator (framerate);
        }
    }

    {
        const GValue *codec_data;
        GstBuffer *buffer;

        codec_data = gst_structure_get_value (structure, "codec_data");
        if (codec_data)
        {
            buffer = gst_value_get_buffer (codec_data);
            omx_base->codec_data = buffer;
            gst_buffer_ref (buffer);
        }
    }

    /* Input port configuration. */
    {
        OMX_PARAM_PORTDEFINITIONTYPE param;

        g_omx_port_get_config (omx_base->in_port, &param);

        param.format.video.nFrameWidth = width;
        param.format.video.nFrameHeight = height;

        g_omx_port_set_config (omx_base->in_port, &param);
    }

    if (self->sink_setcaps)
        self->sink_setcaps (pad, caps);

    return gst_pad_set_caps (pad, caps);
}

static void
omx_setup (GstOmxBaseFilter *omx_base)
{
    GstOmxBaseVideoDec *self;
    GOmxCore *gomx;

    self = GST_OMX_BASE_VIDEODEC (omx_base);
    gomx = (GOmxCore *) omx_base->gomx;

    GST_INFO_OBJECT (omx_base, "begin");

    {
        OMX_PARAM_PORTDEFINITIONTYPE param;

        /* Input port configuration. */
        {
            g_omx_port_get_config (omx_base->in_port, &param);

            param.format.video.eCompressionFormat = self->compression_format;

            g_omx_port_set_config (omx_base->in_port, &param);
        }

        /* some workarounds required for TI components. */
        {
            OMX_COLOR_FORMATTYPE color_format;
            gint width, height;

            {
                g_omx_port_get_config (omx_base->in_port, &param);

                width = param.format.video.nFrameWidth;
                height = param.format.video.nFrameHeight;

                /* this is against the standard; nBufferSize is read-only. */
                param.nBufferSize = (width * height) / 2;

                g_omx_port_set_config (omx_base->in_port, &param);
            }

            /* the component should do this instead */
            {
                g_omx_port_get_config (omx_base->out_port, &param);

                param.format.video.nFrameWidth = width;
                param.format.video.nFrameHeight = height;

                /** @todo get this from the srcpad. */
                param.format.video.eColorFormat = OMX_COLOR_FormatCbYCrY;

                color_format = param.format.video.eColorFormat;

                /* this is against the standard; nBufferSize is read-only. */
                switch (color_format)
                {
                    case OMX_COLOR_FormatYCbYCr:
                    case OMX_COLOR_FormatCbYCrY:
                        param.nBufferSize = (width * height) * 2;
                        break;
                    case OMX_COLOR_FormatYUV420PackedPlanar:
                        param.nBufferSize = (width * height) * 3 / 2;
                        break;
                    default:
                        break;
                }

                g_omx_port_set_config (omx_base->out_port, &param);
            }
        }
    }

    GST_INFO_OBJECT (omx_base, "end");
}

static void
type_instance_init (GTypeInstance *instance,
                    gpointer g_class)
{
    GstOmxBaseFilter *omx_base;

    omx_base = GST_OMX_BASE_FILTER (instance);

    omx_base->omx_setup = omx_setup;

    omx_base->gomx->settings_changed_cb = settings_changed_cb;

    gst_pad_set_setcaps_function (omx_base->sinkpad,
            GST_DEBUG_FUNCPTR (sink_setcaps));
}
