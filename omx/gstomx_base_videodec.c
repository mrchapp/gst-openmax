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
                GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV_STRIDED (
                        GSTOMX_ALL_FORMATS, "[ 0, max ]"))
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
    GstCaps *new_caps;

    omx_base = core->object;
    self = GST_OMX_BASE_VIDEODEC (omx_base);

    GST_DEBUG_OBJECT (omx_base, "settings changed");

    new_caps = gst_caps_intersect (gst_pad_get_caps (omx_base->srcpad),
           gst_pad_peer_get_caps (omx_base->srcpad));

    if (!gst_caps_is_fixed (new_caps))
    {
        gst_caps_do_simplify (new_caps);
        GST_INFO_OBJECT (omx_base, "pre-fixated caps: %" GST_PTR_FORMAT, new_caps);
        gst_pad_fixate_caps (omx_base->srcpad, new_caps);
    }

    GST_INFO_OBJECT (omx_base, "caps are: %" GST_PTR_FORMAT, new_caps);
    GST_INFO_OBJECT (omx_base, "old caps are: %" GST_PTR_FORMAT, GST_PAD_CAPS (omx_base->srcpad));

    gst_pad_set_caps (omx_base->srcpad, new_caps);
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

    g_return_val_if_fail (caps, FALSE);
    g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

    structure = gst_caps_get_structure (caps, 0);

    g_return_val_if_fail (structure, FALSE);

    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);

    {
        const GValue *framerate = NULL;
        framerate = gst_structure_get_value (structure, "framerate");
        if (framerate)
        {
            self->framerate_num = gst_value_get_fraction_numerator (framerate);
            self->framerate_denom = gst_value_get_fraction_denominator (framerate);

            omx_base->duration = gst_util_uint64_scale_int(GST_SECOND,
                    gst_value_get_fraction_denominator (framerate),
                    gst_value_get_fraction_numerator (framerate));
            GST_DEBUG_OBJECT (self, "Nominal frame duration =%"GST_TIME_FORMAT,
                                GST_TIME_ARGS (omx_base->duration));
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

        G_OMX_PORT_GET_DEFINITION (omx_base->in_port, &param);

        param.format.video.nFrameWidth = width;
        param.format.video.nFrameHeight = height;
        param.nBufferSize = width * height;

        G_OMX_PORT_SET_DEFINITION (omx_base->in_port, &param);
        GST_DEBUG_OBJECT (self, "G_OMX_PORT_SET_DEFINITION");
    }

    self->inport_configured = TRUE;

    if (self->sink_setcaps)
        self->sink_setcaps (pad, caps);

    return gst_pad_set_caps (pad, caps);
}


static GstCaps *
src_getcaps (GstPad *pad)
{
    GstCaps *caps;
    GstOmxBaseVideoDec *self   = GST_OMX_BASE_VIDEODEC (GST_PAD_PARENT (pad));
    GstOmxBaseFilter *omx_base = GST_OMX_BASE_FILTER (self);

    if (omx_base->gomx->omx_state != OMX_StateLoaded)
    {
        /* currently, we cannot change caps once out of loaded..  later this
         * could possibly be supported by enabling/disabling the port..
         */
        return GST_PAD_CAPS (pad);
    }

    if (self->inport_configured)
    {
        /* if we already have src-caps, we want to take the already configured
         * width/height/etc.  But we can still support any option of rowstride,
         * so we still don't want to return fixed caps
         */
        OMX_PARAM_PORTDEFINITIONTYPE inparam, outparam;
        int i;

        G_OMX_PORT_GET_DEFINITION (omx_base->out_port, &outparam);
        G_OMX_PORT_GET_DEFINITION (omx_base->in_port, &inparam);

        /**** BEGIN WORKAROUND **************************/
        /* The following calculation of padding and number of reference frames is
         * specific to H264..  should be moved into h264dec component..
         */
        {
            gint width = inparam.format.video.nFrameWidth;
            gint height = inparam.format.video.nFrameHeight;
            gint spec_computation, ref_frames;

            /* decoder needs some padding:
             */
#define PADX    32
#define PADY    24
            width = (width + (2*PADX) + 127) & 0xFFFFFF80;
            height = height + (4*PADY);

            /* decoder needs a minimum number of buffers for reference frames based on
             * the resolution:
             */
            /* 12288 is the value for Profile 4.1 */
            spec_computation = (1024 * 12288) / ((width/16)*(height/16)*384);
            ref_frames = (spec_computation > 16) ? 16 : spec_computation;

            outparam.format.video.nFrameWidth = width;
            outparam.format.video.nFrameHeight = height;
            outparam.nBufferCountMin = ref_frames + 3;
            outparam.nBufferCountActual = outparam.nBufferCountMin + 4;

            /* oh, and don't assume decoder has sane rowstride configured:
             */
            outparam.format.video.nStride = 4096;

            /* configure encoder with new sanitized parameters:
             */
            G_OMX_PORT_SET_DEFINITION (omx_base->out_port, &outparam);
        }
        /**** END WORKAROUND ****************************/

        caps = gst_caps_new_empty ();

        for (i=0; i<2; i++)
        {
            GstStructure *struc = gst_structure_new (
                    (i ? "video/x-raw-yuv-strided" : "video/x-raw-yuv"),
                    "width",  G_TYPE_INT, outparam.format.video.nFrameWidth,
                    "height", G_TYPE_INT, outparam.format.video.nFrameHeight,
                    NULL);

            if(i)
            {
                /* if buffer sharing is used, we let the upstream that allocates
                 * the buffer dictate stride, otherwise we let the OMX component
                 * decide on the stride
                 */
                if (omx_base->out_port->share_buffer)
                {
                    gst_structure_set (struc,
                            "rowstride", GST_TYPE_INT_RANGE, 1, G_MAXINT,
                            NULL);
                }
                else
                {
                    gst_structure_set (struc,
                            "rowstride", G_TYPE_INT, outparam.format.video.nStride,
                            NULL);
                }
            }

            if (self->framerate_denom)
            {
                gst_structure_set (struc,
                        "framerate", GST_TYPE_FRACTION, self->framerate_num, self->framerate_denom,
                        NULL);
            }

            gst_caps_append_structure (caps, struc);
        }
    }
    else
    {
        /* we don't have valid width/height/etc yet, so just use the template.. */
        caps = gst_static_pad_template_get_caps (&src_template);
        GST_DEBUG_OBJECT (self, "caps=%"GST_PTR_FORMAT, caps);
    }

    caps = g_omx_port_set_video_formats (omx_base->out_port, caps);

    GST_DEBUG_OBJECT (self, "caps=%"GST_PTR_FORMAT, caps);

    return caps;
}


static gboolean
src_setcaps (GstPad *pad, GstCaps *caps)
{
    GstOmxBaseFilter *omx_base;

    GstVideoFormat format;
    gint width, height, rowstride;

    omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));

    GST_INFO_OBJECT (omx_base, "setcaps (src): %" GST_PTR_FORMAT, caps);

    g_return_val_if_fail (caps, FALSE);
    g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

    if (gst_video_format_parse_caps_strided (caps,
            &format, &width, &height, &rowstride))
    {
        /* Output port configuration: */
        OMX_PARAM_PORTDEFINITIONTYPE param;

        G_OMX_PORT_GET_DEFINITION (omx_base->out_port, &param);

        param.format.video.eColorFormat = g_omx_fourcc_to_colorformat (
                gst_video_format_to_fourcc (format));
        param.format.video.nFrameWidth  = width;
        param.format.video.nFrameHeight = height;
        param.format.video.nStride      = rowstride;

        G_OMX_PORT_SET_DEFINITION (omx_base->out_port, &param);
        GST_INFO_OBJECT (omx_base,"G_OMX_PORT_SET_DEFINITION");
    }

    return TRUE;
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
        G_OMX_PORT_GET_DEFINITION (omx_base->in_port, &param);

        param.format.video.eCompressionFormat = self->compression_format;

        G_OMX_PORT_SET_DEFINITION (omx_base->in_port, &param);
        GST_DEBUG_OBJECT (self, "G_OMX_PORT_SET_DEFINITION 1!!!");
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

    gst_pad_set_getcaps_function (omx_base->srcpad,
            GST_DEBUG_FUNCPTR (src_getcaps));
    gst_pad_set_setcaps_function (omx_base->srcpad,
            GST_DEBUG_FUNCPTR (src_setcaps));
}

