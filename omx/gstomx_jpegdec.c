/*
 * Created on: Aug 17, 2009
 *
 * This is the first version for the jpeg decoder on gst-openmax
 *
 * Copyright (C) 2009 Texas Instruments - http://www.ti.com/
 *
 * Author:
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "gstomx_jpegdec.h"
#include "gstomx_base_filter.h"
#include "gstomx.h"

#include <string.h>
#include <stdlib.h>

GSTOMX_BOILERPLATE (GstOmxJpegDec, gst_omx_jpegdec, GstOmxBaseFilter, GST_OMX_BASE_FILTER_TYPE);


static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ( GST_VIDEO_CAPS_YUV ("{ UYVY }") )
);

static GstCaps *
generate_sink_template (void)
{
    GstCaps *caps;

    caps = gst_caps_new_simple ("image/jpeg",
                               "width", GST_TYPE_INT_RANGE, 16, 4096,
                               "height", GST_TYPE_INT_RANGE, 16, 4096,
                               "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
                               NULL);

    return caps;
}

static void
type_base_init (gpointer g_class)
{
    GstElementClass *element_class;

    element_class = GST_ELEMENT_CLASS (g_class);

    {
        GstElementDetails details;

        details.longname = "OpenMAX IL JPEG image decoder";
        details.klass = "Codec/Decoder/Image";
        details.description = "Decodes image in JPEG format with OpenMAX IL";
        details.author = "Texas Instrument";

        gst_element_class_set_details (element_class, &details);
    }

    {
        gst_element_class_add_pad_template (element_class,
                        gst_static_pad_template_get (&src_template));
    }

    {
        GstPadTemplate *template;

        template = gst_pad_template_new ("sink", GST_PAD_SINK,
                                         GST_PAD_ALWAYS,
                                         generate_sink_template ());

        gst_element_class_add_pad_template (element_class, template);
    }
}
/*The properties have not been implemented yet*/
/***
static void
set_property (GObject *obj,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    GstOmxJpegDec *self;

    self = GST_OMX_JPEGDEC (obj);

    switch (prop_id)
    {
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
    GstOmxJpegDec *self;

    self = GST_OMX_JPEGDEC (obj);

    switch (prop_id)
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
}
**/
static void
type_class_init (gpointer g_class,
                 gpointer class_data)
{
    GObjectClass *gobject_class;

    gobject_class = G_OBJECT_CLASS (g_class);

    /* Properties stuff */
    /*{
        gobject_class->set_property = set_property;
        gobject_class->get_property = get_property;
    }
    */
}

static void
settings_changed_cb (GOmxCore *core)
{
    GstOmxBaseFilter *omx_base;
    GstOmxJpegDec *self;
    guint width;
    guint height;
    guint32 format = 0;

    omx_base = core->object;
    self = GST_OMX_JPEGDEC (omx_base);

    GST_DEBUG_OBJECT (omx_base, "settings changed");

    {
        OMX_PARAM_PORTDEFINITIONTYPE param;
        G_OMX_PORT_GET_PARAM (omx_base->out_port, OMX_IndexParamPortDefinition, &param);

        width = param.format.image.nFrameWidth;
        height = param.format.image.nFrameHeight;

        switch (param.format.image.eColorFormat)
        {
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
    GstOmxBaseFilter *omx_base;
    GstOmxJpegDec *self;
    GOmxCore *gomx;
    OMX_PARAM_PORTDEFINITIONTYPE param;
    gint width = 0;
    gint height = 0;

    omx_base = GST_OMX_BASE_FILTER (GST_PAD_PARENT (pad));
    self = GST_OMX_JPEGDEC (omx_base);
    gomx = (GOmxCore *) omx_base->gomx;

    GST_INFO_OBJECT (omx_base, "setcaps (sink): %" GST_PTR_FORMAT, caps);

    g_return_val_if_fail (gst_caps_get_size (caps) == 1, FALSE);

    structure = gst_caps_get_structure (caps, 0);

    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);

    height = GST_ROUND_UP_16 ( height );
    width = GST_ROUND_UP_16 ( width );


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
        G_OMX_PORT_GET_PARAM (omx_base->in_port, OMX_IndexParamPortDefinition, &param);

        param.format.image.nFrameWidth = width;
        param.format.image.nFrameHeight = height;

        G_OMX_PORT_SET_PARAM (omx_base->in_port, OMX_IndexParamPortDefinition, &param);
    }

    return gst_pad_set_caps (pad, caps);

}

static void
omx_setup (GstOmxBaseFilter *omx_base)
{
    GstOmxJpegDec *self;
    GOmxCore *gomx;
    gint width, height;
    OMX_COLOR_FORMATTYPE color_format;
    OMX_INDEXTYPE index;

    self = GST_OMX_JPEGDEC (omx_base);
    gomx = (GOmxCore *) omx_base->gomx;

    GST_INFO_OBJECT (omx_base, "begin");

    {
        OMX_PARAM_PORTDEFINITIONTYPE param;

        /* Input port configuration. */
        {
            G_OMX_PORT_GET_PARAM (omx_base->in_port, OMX_IndexParamPortDefinition, &param);

            param.format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;

            param.format.image.nFrameWidth =GST_ROUND_UP_16 (param.format.image.nFrameWidth);
            param.format.image.nFrameHeight = GST_ROUND_UP_16 (param.format.image.nFrameHeight);

            width = param.format.image.nFrameWidth;
            height = param.format.image.nFrameHeight;

            param.nBufferCountActual = 1;

            /* this is against the standard; nBufferSize is read-only. */
            param.nBufferSize = (width * height) / 2;

            /*I am not sure if this is neccesary*/
            /*param.format.video.eColorFormat = OMX_COLOR_FormatCbYCrY;
            param.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;*/

            G_OMX_PORT_SET_PARAM (omx_base->in_port, OMX_IndexParamPortDefinition, &param);
        }

        /* the component should do this instead */
        {

            G_OMX_PORT_GET_PARAM (omx_base->out_port, OMX_IndexParamPortDefinition, &param);

            param.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
            param.format.image.nFrameWidth = width;
            param.format.image.nFrameHeight = height;

            param.nBufferCountActual = 1;

            /** @todo get this from the srcpad */
            param.format.image.eColorFormat = OMX_COLOR_FormatCbYCrY;
            color_format = param.format.image.eColorFormat;

            /* this is against the standard; nBufferSize is read-only. */
            switch (color_format)
            {

                case OMX_COLOR_FormatCbYCrY:
                    param.nBufferSize = (width * height) * 2;
                    break;
                case OMX_COLOR_FormatYUV420PackedPlanar:
                    param.nBufferSize = (width * height) * 3 / 2;
                    break;
                default:
                    break;
            }

            G_OMX_PORT_SET_PARAM (omx_base->out_port, OMX_IndexParamPortDefinition, &param);
        }
    }

    /*Set parameters*/
    {
        OMX_CUSTOM_RESOLUTION pMaxResolution;
#if 0
        /*By the moment properties don't have been added */
        OMX_CUSTOM_IMAGE_DECODE_SECTION pSectionDecode;
        OMX_CUSTOM_IMAGE_DECODE_SUBREGION pSubRegionDecode;
        OMX_CONFIG_SCALEFACTORTYPE* pScalefactor;

        /* Section decoding */
        memset (&pSectionDecode, 0, sizeof (pSectionDecode));
        pSectionDecode.nSize = sizeof (OMX_CUSTOM_IMAGE_DECODE_SECTION);

        OMX_GetExtensionIndex (gomx->omx_handle, "OMX.TI.JPEG.decode.Param.SectionDecode", &index);

        pSectionDecode.nMCURow = 0;
        pSectionDecode.bSectionsInput  = OMX_FALSE;
        pSectionDecode.bSectionsOutput = OMX_TRUE;

        OMX_SetParameter (gomx->omx_handle, index, &pSectionDecode);

        /* SubRegion decoding */
        memset (&pSubRegionDecode, 0, sizeof (pSubRegionDecode));
        pSubRegionDecode.nSize = sizeof (OMX_CUSTOM_IMAGE_DECODE_SUBREGION);

        OMX_GetExtensionIndex (gomx->omx_handle, "OMX.TI.JPEG.decode.Param.SubRegionDecode", &index);

        pSubRegionDecode.nXOrg = 0;
        pSubRegionDecode.nYOrg  = 0;
        pSubRegionDecode.nXLength = 0;
        pSubRegionDecode.nYLength = 0;

        OMX_SetParameter (gomx->omx_handle, index, &pSubRegionDecode);

        /*scale factor*/
    /*
        memset (&pScalefactor, 0, sizeof (pScalefactor));
        pScalefactor.nSize = sizeof (OMX_CONFIG_SCALEFACTORTYPE);

        OMX_GetExtensionIndex (gomx->omx_handle, "OMX.TI.JPEG.decode.Param.SetMaxResolution", &index);

        pScalefactor.xWidth = (OMX_S32) 100;
        pScalefactor.xHeight = (OMX_S32) 100;

        OMX_SetParameter (gomx->omx_handle, OMX_IndexConfigCommonScale, &pScalefactor);
    */
#endif
        /*Max resolution */
        memset (&pMaxResolution, 0, sizeof (pMaxResolution));

        OMX_GetExtensionIndex (gomx->omx_handle, "OMX.TI.JPEG.decode.Param.SetMaxResolution", &index);

        pMaxResolution.nWidth = width;
        pMaxResolution.nHeight = height;

        OMX_SetParameter (gomx->omx_handle, index, &pMaxResolution);
    }

    /*Set config*/
    {
        OMX_U32 nProgressive;
        /*Dinamic color change */
        OMX_GetExtensionIndex(gomx->omx_handle, "OMX.TI.JPEG.decode.Config.OutputColorFormat", &index);

        g_assert ( (OMX_SetConfig (gomx->omx_handle, index, &color_format )) == OMX_ErrorNone );

        /*Progressive image decode*/
        OMX_GetExtensionIndex(gomx->omx_handle, "OMX.TI.JPEG.decode.Config.ProgressiveFactor", &index);

        nProgressive=0; /*harcoded by the moment, wait from the parser*/

        OMX_SetConfig(gomx->omx_handle, index, &(nProgressive));

    }
    GST_INFO_OBJECT (omx_base, "end");
}

static void
type_instance_init (GTypeInstance *instance,
                    gpointer g_class)
{
    GstOmxBaseFilter *omx_base;
    GstOmxJpegDec *self;

    omx_base = GST_OMX_BASE_FILTER (instance);

    self = GST_OMX_JPEGDEC (instance);

    omx_base->omx_setup = omx_setup;

    omx_base->gomx->settings_changed_cb = settings_changed_cb;

    gst_pad_set_setcaps_function (omx_base->sinkpad, sink_setcaps);

    self->framerate_num = 0;
    self->framerate_denom = 1;

}

