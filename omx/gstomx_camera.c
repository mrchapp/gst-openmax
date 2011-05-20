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

#include "gstomx_camera_parameters.h"
#include "gstomx_camera.h"
#include "gstomx.h"

#include <gst/video/video.h>

#ifdef USE_OMXTICORE
#  include <OMX_TI_IVCommon.h>
#  include <OMX_TI_Index.h>
#endif

#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/timer-32k.h>
#include <OMX_CoreExt.h>
#include <OMX_IndexExt.h>

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
 * gst-launch omx_camera vstab=1 mode=2 vnf=1 name=cam cam.src ! queue ! v4l2sink \
 * cam.vidsrc ! "video/x-raw-yuv, format=(fourcc)UYVY, width=720, height=480, framerate=30/1" ! \
 * queue ! omx_h264enc matroskamux name=mux ! filesink location=capture.mkv ! \
 * alsasrc ! "audio/x-raw-int,rate=48000,channels=1, width=16, depth=16, endianness=1234" ! \
 * queue ! omx_aacenc bitrate=64000 profile=2 ! "audio/mpeg,mpegversion=4,rate=48000,channels=1" ! \
 * mux. cam.imgsrc ! "image/jpeg, width=720, height=480" ! filesink name=capture.jpg
 * ]|
 * </refsect2>
 */

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("Video OMX Camera Source",
    "Source/Video",
    "Reads frames from a OMX Camera Component",
    "Rob Clark <rob@ti.com>");


GSTOMX_BOILERPLATE (GstOmxCamera, gst_omx_camera, GstOmxBaseSrc, GST_OMX_BASE_SRC_TYPE);

#define USE_GSTOMXCAM_IMGSRCPAD
#define USE_GSTOMXCAM_VIDSRCPAD
#define USE_GSTOMXCAM_THUMBSRCPAD
//#define USE_GSTOMXCAM_IN_PORT


/*
 * Caps:
 */


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (
                "video/x-raw-rgb-strided, bpp=16, depth=16, red_mask=63488, "
                "green_mask=2016, blue_mask=31, endianness=1234, "
                "rowstride=(int)[1,max], width=(int)[1,max], height=(int)[1,max], "
                "framerate=(fraction)[0,max]; "
                GST_VIDEO_CAPS_YUV_STRIDED (GSTOMX_ALL_FORMATS, "[ 0, max ]"))
    );

static GstStaticPadTemplate imgsrc_template = GST_STATIC_PAD_TEMPLATE ("imgsrc",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        /* Note: imgsrc pad supports JPEG format, Bayer, as well as
           non-strided YUV. */
        GST_STATIC_CAPS (
                "image/jpeg, width=(int)[1,max], height=(int)[1,max]; "
                "video/x-raw-bayer, width=(int)[1,max], height=(int)[1,max]; "
                GST_VIDEO_CAPS_YUV (GSTOMX_ALL_FORMATS))
    );

static GstStaticPadTemplate vidsrc_template = GST_STATIC_PAD_TEMPLATE ("vidsrc",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (
                "video/x-raw-rgb-strided, bpp=16, depth=16, red_mask=63488, "
                "green_mask=2016, blue_mask=31, endianness=1234, "
                "rowstride=(int)[1,max], width=(int)[1,max], height=(int)[1,max], "
                "framerate=(fraction)[0,max]; "
                GST_VIDEO_CAPS_YUV_STRIDED (GSTOMX_ALL_FORMATS, "[ 0, max ]"))
    );

static GstStaticPadTemplate thumbsrc_template = GST_STATIC_PAD_TEMPLATE ("thumbsrc",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (
                "video/x-raw-bayer, width=(int)[1,max], height=(int)[1,max]; "
                GST_VIDEO_CAPS_RGB "; "
                GST_VIDEO_CAPS_RGB_16 "; "
                GST_VIDEO_CAPS_YUV (GSTOMX_ALL_FORMATS))
    );

#ifdef USE_GSTOMXCAM_IN_PORT
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("???")
    );
#endif

static gboolean
src_setcaps (GstPad *pad, GstCaps *caps)
{
    GstOmxCamera *self = GST_OMX_CAMERA (GST_PAD_PARENT (pad));
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    GstVideoFormat format;
    gint width, height, rowstride;
    gint framerate_num, framerate_denom;
    const GValue *framerate = NULL;
    OMX_ERRORTYPE err;

    if (!self)
    {
        GST_DEBUG_OBJECT (pad, "pad has no parent (yet?)");
        return TRUE;  // ???
    }

    GST_INFO_OBJECT (omx_base, "setcaps (src/vidsrc): %" GST_PTR_FORMAT, caps);

    g_return_val_if_fail (caps, FALSE);
    g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

    if (gst_video_format_parse_caps_strided (caps,
            &format, &width, &height, &rowstride))
    {
        /* Output port configuration: */
        OMX_PARAM_PORTDEFINITIONTYPE param;
        gboolean configure_port = FALSE;

        G_OMX_PORT_GET_DEFINITION (omx_base->out_port, &param);

        if ((param.format.video.nFrameWidth != width) ||
           (param.format.video.nFrameHeight != height) ||
           (param.format.video.nStride != rowstride))
        {
            param.format.video.nFrameWidth  = width;
            param.format.video.nFrameHeight = height;
            param.format.video.nStride      = self->rowstride = rowstride;
            configure_port = TRUE;
        }

        param.nBufferSize = gst_video_format_get_size_strided (format, width, height, rowstride);

        /* special hack to work around OMX camera bug:
         */
        if (param.format.video.eColorFormat != g_omx_gstvformat_to_colorformat (format))
        {
            if (g_omx_gstvformat_to_colorformat (format) == OMX_COLOR_FormatYUV420PackedSemiPlanar)
            {
                if (param.format.video.eColorFormat != OMX_COLOR_FormatYUV420SemiPlanar)
                {
                    param.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
                    configure_port = TRUE;
                }
            }
            else
            {
                param.format.video.eColorFormat = g_omx_gstvformat_to_colorformat (format);
                configure_port = TRUE;
            }
        }

        framerate = gst_structure_get_value (
                gst_caps_get_structure (caps, 0), "framerate");

        if (framerate)
        {
            guint32 xFramerate;
            framerate_num = gst_value_get_fraction_numerator (framerate);
            framerate_denom = gst_value_get_fraction_denominator (framerate);

            xFramerate = (framerate_num << 16) / framerate_denom;

            if (param.format.video.xFramerate != xFramerate)
            {
                param.format.video.xFramerate = xFramerate;
                configure_port = TRUE;
            }
         }

        /* At the moment we are only using preview port and not vid_port
         * From omx camera desing document we are missing
         * SetParam CommonSensormode -> bOneShot = FALSE ?
         */

        if (configure_port)
        {
            gboolean port_enabled = FALSE;

            if (omx_base->out_port->enabled && (omx_base->gomx->omx_state != OMX_StateLoaded))
            {
                g_omx_port_disable (omx_base->out_port);
                port_enabled = TRUE;
            }

            err = G_OMX_PORT_SET_DEFINITION (omx_base->out_port, &param);
            if (err != OMX_ErrorNone)
                return FALSE;

            if (port_enabled)
                g_omx_port_enable (omx_base->out_port);
        }

        GST_INFO_OBJECT (omx_base, " Rowstride=%d, Width=%d, Height=%d, Color=%d, Buffersize=%d, framerate=%d",
            param.format.video.nStride, param.format.video.nFrameWidth, param.format.video.nFrameHeight, param.format.video.eColorFormat, param.nBufferSize,param.format.video.xFramerate );

#ifdef USE_OMXTICORE
        self->img_regioncenter_x = (param.format.video.nFrameWidth / 2);
        self->img_regioncenter_y = (param.format.video.nFrameHeight / 2);
#endif

        if  (!gst_pad_set_caps (GST_BASE_SRC (self)->srcpad, caps))
            return FALSE;

        GST_INFO_OBJECT (omx_base, " exit setcaps src: %");
    }

    return TRUE;
}

static void
src_fixatecaps (GstPad *pad, GstCaps *caps)
{
    GstStructure *structure;
    const GValue *value;
    gint width;

    structure = gst_caps_get_structure (caps, 0);

    gst_structure_fixate_field_nearest_int (structure, "width", 864);
    gst_structure_fixate_field_nearest_int (structure, "height", 480);
    gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);

    value = gst_structure_get_value (structure, "rowstride");
    if (value == NULL || G_VALUE_TYPE (value) == GST_TYPE_INT_RANGE) {
        gst_structure_get_int (structure, "width", &width);
        gst_caps_set_simple (caps, "rowstride",
            G_TYPE_INT, width, NULL);
    }
}

static gboolean
imgsrc_setcaps (GstPad *pad, GstCaps *caps)
{
    GstOmxCamera *self = GST_OMX_CAMERA (GST_PAD_PARENT (pad));
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    GstVideoFormat format;
    gint width, height, rowstride;
    GstStructure *s;

    GST_INFO_OBJECT (omx_base, "setcaps (imgsrc): %" GST_PTR_FORMAT, caps);

    g_return_val_if_fail (caps, FALSE);
    g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

    if (gst_video_format_parse_caps_strided (caps,
            &format, &width, &height, &rowstride))
    {
        /* Output port configuration for YUV: */
        OMX_PARAM_PORTDEFINITIONTYPE param;

        GST_DEBUG_OBJECT (self, "set raw format");

        G_OMX_PORT_GET_DEFINITION (self->img_port, &param);

        param.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
        param.format.image.eColorFormat = g_omx_gstvformat_to_colorformat (format);
        param.format.image.nFrameWidth  = width;
        param.format.image.nFrameHeight = height;
        param.format.image.nStride      = rowstride;

        /* special hack to work around OMX camera bug:
         */
        if (param.format.video.eColorFormat == OMX_COLOR_FormatYUV420PackedSemiPlanar)
            param.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;

        G_OMX_PORT_SET_DEFINITION (self->img_port, &param);
    }
    else if (gst_structure_has_name (s=gst_caps_get_structure (caps, 0), "image/jpeg"))
    {
        /* Output port configuration for JPEG: */
        OMX_PARAM_PORTDEFINITIONTYPE param;

        GST_DEBUG_OBJECT (self, "set JPEG format");

        G_OMX_PORT_GET_DEFINITION (self->img_port, &param);

        gst_structure_get_int (s, "width", &width);
        gst_structure_get_int (s, "height", &height);

        param.format.image.eColorFormat = OMX_COLOR_FormatCbYCrY;
        param.format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
        param.format.image.nFrameWidth  = width;
        param.format.image.nFrameHeight = height;
        param.format.image.nStride      = 0;

        GST_INFO_OBJECT (self, "Rowstride=%d, Width=%d, Height=%d, Buffersize=%d, num-buffer=%d",
            param.format.image.nStride, param.format.image.nFrameWidth, param.format.image.nFrameHeight, param.nBufferSize, param.nBufferCountActual);

        G_OMX_PORT_SET_DEFINITION (self->img_port, &param);
    }
    else if (gst_structure_has_name (s=gst_caps_get_structure (caps, 0),
                     "video/x-raw-bayer"))
    {
        /* Output port configuration for Bayer: */
        OMX_PARAM_PORTDEFINITIONTYPE param;

        GST_DEBUG_OBJECT (self, "set Raw-Bayer format");

        G_OMX_PORT_GET_DEFINITION (self->img_port, &param);

        gst_structure_get_int (s, "width", &width);
        gst_structure_get_int (s, "height", &height);

        param.format.image.eColorFormat = OMX_COLOR_FormatRawBayer10bit;
        param.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
        param.format.image.nFrameWidth  = width;
        param.format.image.nFrameHeight = height;
        param.format.image.nStride      = width * 2;

        GST_INFO_OBJECT (self, "Rowstride=%d, Width=%d, Height=%d, "
            "Buffersize=%d, num-buffer=%d", param.format.image.nStride,
            param.format.image.nFrameWidth, param.format.image.nFrameHeight,
            param.nBufferSize, param.nBufferCountActual);

        G_OMX_PORT_SET_DEFINITION (self->img_port, &param);
    }

    return TRUE;
}

static void
imgsrc_fixatecaps (GstPad *pad, GstCaps *caps)
{
    GstStructure *structure;

    structure = gst_caps_get_structure (caps, 0);

    gst_structure_fixate_field_nearest_int (structure, "width", 864);
    gst_structure_fixate_field_nearest_int (structure, "height", 480);
}

static gboolean
thumbsrc_setcaps (GstPad *pad, GstCaps *caps)
{
    GstOmxCamera *self = GST_OMX_CAMERA (GST_PAD_PARENT (pad));
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    GstVideoFormat format;
    gint width, height;
    GstStructure *s;

    GST_INFO_OBJECT (omx_base, "setcaps (thumbsrc): %" GST_PTR_FORMAT, caps);

    g_return_val_if_fail (caps, FALSE);
    g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

    if (gst_video_format_parse_caps (caps, &format, &width, &height))
    {
        /* Output port configuration for RAW: */
        OMX_PARAM_PORTDEFINITIONTYPE param;

        GST_DEBUG_OBJECT (self, "set YUV/RGB raw format");

        G_OMX_PORT_GET_DEFINITION (self->vid_port, &param);

        param.format.image.eCompressionFormat = OMX_VIDEO_CodingUnused;
        param.format.image.eColorFormat = g_omx_gstvformat_to_colorformat (format);
        param.format.image.nFrameWidth  = width;
        param.format.image.nFrameHeight = height;

        /* special hack to work around OMX camera bug:
         */
        if (param.format.video.eColorFormat == OMX_COLOR_FormatYUV420PackedSemiPlanar)
            param.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;

        G_OMX_PORT_SET_DEFINITION (self->vid_port, &param);
    }
    else if (gst_structure_has_name (s=gst_caps_get_structure (caps, 0),
                     "video/x-raw-bayer"))
    {
        /* Output port configuration for Bayer: */
        OMX_PARAM_PORTDEFINITIONTYPE param;

        GST_DEBUG_OBJECT (self, "set Raw-Bayer format");

        G_OMX_PORT_GET_DEFINITION (self->vid_port, &param);

        gst_structure_get_int (s, "width", &width);
        gst_structure_get_int (s, "height", &height);

        param.format.image.eColorFormat = OMX_COLOR_FormatRawBayer10bit;
        param.format.image.eCompressionFormat = OMX_VIDEO_CodingUnused;
        param.format.image.nFrameWidth  = width;
        param.format.image.nFrameHeight = height;

        GST_INFO_OBJECT (self, "Width=%d, Height=%d, Buffersize=%d, num-buffer=%d",
            param.format.image.nFrameWidth, param.format.image.nFrameHeight,
            param.nBufferSize, param.nBufferCountActual);

        G_OMX_PORT_SET_DEFINITION (self->vid_port, &param);
    }

    return TRUE;
}

static gboolean
src_query (GstPad *pad, GstQuery *query)
{
    GstOmxCamera *self = GST_OMX_CAMERA (GST_PAD_PARENT (pad));
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    gboolean ret = FALSE;

    GST_DEBUG_OBJECT (self, "Begin");

    switch (GST_QUERY_TYPE (query))
    {
        case GST_QUERY_BUFFERS:
        {
            const GstCaps *caps;
            OMX_ERRORTYPE err;
            OMX_PARAM_PORTDEFINITIONTYPE param;

            _G_OMX_INIT_PARAM (&param);

            gst_query_parse_buffers_caps (query, &caps);

            /* ensure the caps we are querying are the current ones, otherwise
             * results are meaningless..
             *
             * @todo should we save and restore current caps??
             */
#if 0
            /* FIXME: why is this needed? it breaks renegotiation happening inside
             * camerabin2 */
            src_setcaps (pad, (GstCaps *)caps);
#endif

            param.nPortIndex = omx_base->out_port->port_index;
            err = OMX_GetParameter (omx_base->gomx->omx_handle,
                    OMX_IndexParamPortDefinition, &param);
            g_assert (err == OMX_ErrorNone);

            GST_DEBUG_OBJECT (self, "Actual buffers: %d", param.nBufferCountActual);

            gst_query_set_buffers_count (query, param.nBufferCountActual);

#ifdef USE_OMXTICORE
            {
                OMX_CONFIG_RECTTYPE rect;
                _G_OMX_INIT_PARAM (&rect);

                rect.nPortIndex = omx_base->out_port->port_index;
                err = OMX_GetParameter (omx_base->gomx->omx_handle,
                        OMX_TI_IndexParam2DBufferAllocDimension, &rect);
                if (err == OMX_ErrorNone)
                {
                    GST_DEBUG_OBJECT (self, "Min dimensions: %dx%d",
                            rect.nWidth, rect.nHeight);

                    gst_query_set_buffers_dimensions (query,
                            rect.nWidth, rect.nHeight);
                }
            }
#endif

            ret = TRUE;
            break;
        }

        case GST_QUERY_LATENCY:
        {
            GstClockTime min, max;

            /* FIXME this is hardcoded for now but we should try to do better */
            min = 0;
            max = GST_CLOCK_TIME_NONE;
            gst_query_set_latency (query, TRUE, min, max);

            ret = TRUE;
            break;
        }

        default:
            ret = GST_BASE_SRC_CLASS (parent_class)->query (GST_BASE_SRC (self), query);
    }

    GST_DEBUG_OBJECT (self, "End -> %d", ret);

    return ret;
}

/* note.. maybe this should be moved somewhere common... GstOmxBaseVideoDec has
 * almost same logic..
 */
static void
settings_changed (GstElement *self, GstPad *pad)
{
    GstCaps *new_caps;

    if (!gst_pad_is_linked (pad))
    {
        GST_DEBUG_OBJECT (self, "%"GST_PTR_FORMAT": pad is not linked", pad);
        return;
    }

    new_caps = gst_caps_intersect (gst_pad_get_caps (pad),
           gst_pad_peer_get_caps (pad));

    if (!gst_caps_is_fixed (new_caps))
    {
        gst_caps_do_simplify (new_caps);

        if (gst_caps_is_subset (GST_PAD_CAPS(pad), new_caps))
        {
            gst_caps_replace (&new_caps, GST_PAD_CAPS(pad));
        }

        GST_INFO_OBJECT (self, "%"GST_PTR_FORMAT": pre-fixated caps: %" GST_PTR_FORMAT, pad, new_caps);
        gst_pad_fixate_caps (pad, new_caps);
    }

    GST_INFO_OBJECT (self, "%"GST_PTR_FORMAT": caps are: %" GST_PTR_FORMAT, pad, new_caps);
    GST_INFO_OBJECT (self, "%"GST_PTR_FORMAT": old caps are: %" GST_PTR_FORMAT, pad, GST_PAD_CAPS (pad));

    gst_pad_set_caps (pad, new_caps);
    gst_caps_unref (new_caps);
}

static void
settings_changed_cb (GOmxCore *core)
{
    GstOmxCamera *self = core->object;

    GST_DEBUG_OBJECT (self, "settings changed");

    settings_changed (GST_ELEMENT (self), GST_BASE_SRC (self)->srcpad);

#ifdef USE_GSTOMXCAM_VIDSRCPAD
    settings_changed (GST_ELEMENT (self), self->vidsrcpad);
#endif
#ifdef USE_GSTOMXCAM_IMGSRCPAD
    settings_changed (GST_ELEMENT (self), self->imgsrcpad);
#endif
#ifdef USE_GSTOMXCAM_THUMBSRCPAD
    settings_changed (GST_ELEMENT (self), self->thumbsrcpad);
#endif
}

static void
autofocus_cb (GstOmxCamera *self)
{
    guint32 autofocus_cb_time;

    GstStructure *structure = gst_structure_new ("omx_camera",
            "auto-focus", G_TYPE_BOOLEAN, TRUE, NULL);

    GstMessage *message = gst_message_new_element (GST_OBJECT (self),
            structure);

    gst_element_post_message (GST_ELEMENT (self), message);

    autofocus_cb_time = omap_32k_readraw ();
    GST_CAT_INFO_OBJECT (gstomx_ppm, GST_OBJECT (self), "%d Autofocus locked",
                         autofocus_cb_time);
}

static void
index_settings_changed_cb (GOmxCore *core, gint data1, gint data2)
{
    GstOmxCamera *self = core->object;

    if (data2 == OMX_IndexConfigCommonFocusStatus)
        autofocus_cb (self);
}

static void
setup_ports (GstOmxBaseSrc *base_src)
{
    GstOmxCamera *self = GST_OMX_CAMERA (base_src);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    OMX_PARAM_PORTDEFINITIONTYPE param;

#ifdef USE_GSTOMXCAM_THUMBSRCPAD
    G_OMX_PORT_GET_DEFINITION (self->vid_port, &param);
    g_omx_port_setup (self->vid_port, &param);
#endif

#ifdef USE_GSTOMXCAM_IMGSRCPAD
    G_OMX_PORT_GET_DEFINITION (self->img_port, &param);
    g_omx_port_setup (self->img_port, &param);
#endif

#ifdef USE_GSTOMXCAM_IN_PORT
    G_OMX_PORT_GET_DEFINITION (self->in_port, &param);
    g_omx_port_setup (self->in_port, &param);
#endif

/*   Not supported yet
    self->vid_port->share_buffer = TRUE;
    self->img_port->share_buffer = TRUE;
*/
    omx_base->out_port->omx_allocate = FALSE;
    omx_base->out_port->share_buffer = TRUE;

#ifdef USE_GSTOMXCAM_IMGSRCPAD
    self->img_port->omx_allocate = TRUE;
    self->img_port->share_buffer = FALSE;
#endif

#ifdef USE_GSTOMXCAM_THUMBSRCPAD
    self->vid_port->omx_allocate = TRUE;
    self->vid_port->share_buffer = FALSE;
#endif
}


static GstClockTime
get_timestamp (GstOmxCamera *self)
{
    GstClock *clock;
    GstClockTime timestamp;

    /* timestamps, LOCK to get clock and base time. */
    GST_OBJECT_LOCK (self);
    if ((clock = GST_ELEMENT_CLOCK (self))) {
      /* we have a clock, get base time and ref clock */
      timestamp = GST_ELEMENT (self)->base_time;
      gst_object_ref (clock);
    } else {
      /* no clock, can't set timestamps */
      timestamp = GST_CLOCK_TIME_NONE;
    }
    GST_OBJECT_UNLOCK (self);

    if (clock) {
      /* the time now is the time of the clock minus the base time */
      /* Hack: Need to subtract the extra lag that is causing problems to AV sync */
      timestamp = gst_clock_get_time (clock) - timestamp;
      gst_object_unref (clock);

      /* if we have a framerate adjust timestamp for frame latency */
#if 0
      if (self->fps_n > 0 && self->fps_d > 0)
      {
        GstClockTime latency;

        latency = gst_util_uint64_scale_int (GST_SECOND, self->fps_d, self->fps_n);

        if (timestamp > latency)
          timestamp -= latency;
        else
          timestamp = 0;
      }
#endif
    }

    return timestamp;
}

#ifdef USE_GSTOMXCAM_IMGSRCPAD
/** This function configure the camera component on capturing/no capturing mode **/
static void
set_capture (GstOmxCamera *self, gboolean capture_mode)
{
    OMX_CONFIG_BOOLEANTYPE param;
    GOmxCore *gomx;
    OMX_ERRORTYPE err;
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    gomx = (GOmxCore *) omx_base->gomx;

    _G_OMX_INIT_PARAM (&param);

    param.bEnabled = (capture_mode == TRUE) ? OMX_TRUE : OMX_FALSE;

    err = G_OMX_CORE_SET_CONFIG (gomx, OMX_IndexConfigCapturing, &param);
    g_warn_if_fail (err == OMX_ErrorNone);

    GST_DEBUG_OBJECT (self, "Capture = %d", param.bEnabled);
}
#endif


static void
start_ports (GstOmxCamera *self)
{
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    if (config[self->mode] & PORT_PREVIEW)
    {
        GST_DEBUG_OBJECT (self, "enable preview port");
        g_omx_port_enable (omx_base->out_port);
    }

#ifdef USE_GSTOMXCAM_THUMBSRCPAD
    if (config[self->mode] & PORT_VIDEO)
    {
        GST_DEBUG_OBJECT (self, "enable video port");
        g_omx_port_enable (self->vid_port);
    }
#endif

#ifdef USE_GSTOMXCAM_IMGSRCPAD
    if (config[self->mode] & PORT_IMAGE)
    {
        guint32 capture_start_time;

        GST_DEBUG_OBJECT (self, "enable image port");

        /* WORKAROUND: Image capture set only in LOADED state */
        /* set_camera_operating_mode (self); */
        g_omx_port_enable (self->img_port);

        GST_DEBUG_OBJECT (self, "image port set_capture set to  %d", TRUE);

        capture_start_time = omap_32k_readraw();
        GST_CAT_INFO_OBJECT (gstomx_ppm, self, "%d Start Image Capture",
                             capture_start_time);

        set_capture (self, TRUE);
    }
#endif
}


static void
stop_ports (GstOmxCamera *self)
{

    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    if (config[self->mode] & PORT_PREVIEW)
    {
        GST_DEBUG_OBJECT (self, "disable preview port");
        g_omx_port_disable (omx_base->out_port);
    }

#ifdef USE_GSTOMXCAM_THUMBSRCPAD
    if (config[self->mode] & PORT_VIDEO)
    {
        GST_DEBUG_OBJECT (self, "disable video port");
        g_omx_port_disable (self->vid_port);
    }
#endif

#ifdef USE_GSTOMXCAM_IMGSRCPAD
    if (config[self->mode] & PORT_IMAGE)
    {
        GST_DEBUG_OBJECT (self, "disable image port");
        g_omx_port_disable (self->img_port);
        set_capture (self, FALSE);
    }
#endif
}

#define CALC_RELATIVE(mult, image_size, chunk_size) ((mult * chunk_size) / image_size)


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
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    GstBuffer *preview_buf = NULL;
    GstBuffer *vid_buf = NULL;
    GstBuffer *img_buf = NULL;
    GstBuffer *thumb_buf = NULL;
    GstFlowReturn ret = GST_FLOW_NOT_NEGOTIATED;
    GstClockTime timestamp;
    GstEvent *vstab_evt = NULL;
    gboolean pending_eos;
    guint n_offset = 0;
    static guint cont;

    pending_eos = g_atomic_int_compare_and_exchange (&self->pending_eos, TRUE, FALSE);

    GST_DEBUG_OBJECT (self, "begin, mode=%d, pending_eos=%d", self->mode, pending_eos);

    GST_LOG_OBJECT (self, "state: %d", omx_base->gomx->omx_state);

    if (self->mode != self->next_mode)
    {
        if (self->mode != -1)
        {
            stop_ports (self);
            g_omx_core_stop (omx_base->gomx);
            g_omx_core_unload (omx_base->gomx);
        }

        set_camera_operating_mode (self);
        gst_omx_base_src_setup_ports (omx_base);
        g_omx_core_prepare (omx_base->gomx);
        self->mode = self->next_mode;
        start_ports (self);

        /* @todo for now just capture one image... later let the user config
         * this to the number of desired burst mode images
         */
        if (self->mode == MODE_IMAGE)
            self->img_count = 1;
        if (self->mode == MODE_IMAGE_HS)
            self->img_count = self->img_port->num_buffers;
    }

    if (config[self->mode] & PORT_PREVIEW)
    {
        ret = gst_omx_base_src_create_from_port (omx_base,
                omx_base->out_port, &preview_buf);
        n_offset = omx_base->out_port->n_offset;
        if (ret != GST_FLOW_OK)
            goto fail;
        if (self->mode == MODE_VIDEO)
        {
            vid_buf = gst_buffer_ref (preview_buf);
        }
    }

    if (config[self->mode] & PORT_VIDEO)
    {
        ret = gst_omx_base_src_create_from_port (omx_base,
                self->vid_port, &thumb_buf);
        n_offset = self->vid_port->n_offset;
        if (ret != GST_FLOW_OK)
            goto fail;
    }

    if (config[self->mode] & PORT_IMAGE)
    {
        ret = gst_omx_base_src_create_from_port (omx_base,
                self->img_port, &img_buf);
        if (ret != GST_FLOW_OK)
            goto fail;

        if (--self->img_count == 0)
        {
            self->next_mode = MODE_PREVIEW;
            GST_DEBUG_OBJECT (self, "image port set_capture set to %d", FALSE);
            set_capture (self, FALSE);
        }
        GST_DEBUG_OBJECT (self, "### img_count = %d ###", self->img_count);
    }

    timestamp = get_timestamp (self);
    cont ++;
    GST_DEBUG_OBJECT (self, "******** preview buffers cont = %d", cont);
    GST_BUFFER_TIMESTAMP (preview_buf) = timestamp;

    *ret_buf = preview_buf;

    if (n_offset)
    {
        vstab_evt = gst_event_new_crop (n_offset / self->rowstride, /* top */
                n_offset % self->rowstride, /* left */
                -1, -1); /* width/height: we can just give invalid for now */
        gst_pad_push_event (GST_BASE_SRC (self)->srcpad,
                gst_event_ref (vstab_evt));
    }

    if (vid_buf)
    {
        GST_DEBUG_OBJECT (self, "pushing vid_buf");
        GST_BUFFER_TIMESTAMP (vid_buf) = timestamp;
        if (vstab_evt)
            gst_pad_push_event (self->vidsrcpad, gst_event_ref (vstab_evt));
        gst_buffer_set_caps (vid_buf, GST_PAD_CAPS (gst_base->srcpad));
        gst_pad_push (self->vidsrcpad, vid_buf);
        if (G_UNLIKELY (pending_eos))
            gst_pad_push_event (self->vidsrcpad, gst_event_new_eos ());
    }

    if (img_buf)
    {
        GST_DEBUG_OBJECT (self, "pushing img_buf");
        GST_BUFFER_TIMESTAMP (img_buf) = timestamp;
        gst_pad_push (self->imgsrcpad, img_buf);
        if (G_UNLIKELY (pending_eos))
            gst_pad_push_event (self->imgsrcpad, gst_event_new_eos ());
    }

    if (thumb_buf)
    {
        GST_DEBUG_OBJECT (self, "pushing thumb_buf");
        GST_BUFFER_TIMESTAMP (thumb_buf) = timestamp;
        gst_pad_push (self->thumbsrcpad, thumb_buf);
        if (G_UNLIKELY (pending_eos))
            gst_pad_push_event (self->thumbsrcpad, gst_event_new_eos ());
    }

    if (vstab_evt)
    {
        gst_event_unref (vstab_evt);
    }

    if (G_UNLIKELY (pending_eos))
    {
         /* now send eos event, which was previously deferred, to parent
          * class this will trigger basesrc's eos logic.  Unfortunately we
          * can't call parent->send_event() directly from here to pass along
          * the eos, which would be a more obvious approach, because that
          * would deadlock when it tries to acquire live-lock.. but live-
          * lock is already held when calling create().
          */
          return GST_FLOW_UNEXPECTED;
    }

    GST_DEBUG_OBJECT (self, "end, ret=%d", ret);

    return GST_FLOW_OK;

fail:
    if (preview_buf) gst_buffer_unref (preview_buf);
    if (vid_buf)     gst_buffer_unref (vid_buf);
    if (img_buf)     gst_buffer_unref (img_buf);
    if (thumb_buf)   gst_buffer_unref (thumb_buf);

    return ret;
}

static gboolean
send_event (GstElement * element, GstEvent * event)
{
    GstOmxCamera *self = GST_OMX_CAMERA (element);

    GST_DEBUG_OBJECT (self, "received %s event", GST_EVENT_TYPE_NAME (event));

    switch (GST_EVENT_TYPE (event))
    {
        case GST_EVENT_EOS:
            /* note: we don't pass the eos event on to basesrc until
             * we have a chance to handle it ourselves..
             */
            g_atomic_int_set (&self->pending_eos, TRUE);
            gst_event_unref (event);
            return TRUE;
        default:
            return GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
    }
}


/*
 * Initialization:
 */

static void
type_base_init (gpointer g_class)
{
    GstElementClass *element_class;

    element_class = GST_ELEMENT_CLASS (g_class);

    gst_element_class_set_details (element_class, &element_details);

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&src_template));

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&vidsrc_template));

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&imgsrc_template));

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&thumbsrc_template));

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
    GstElementClass *gst_element_class = GST_ELEMENT_CLASS (g_class);
    GstBaseSrcClass *gst_base_src_class = GST_BASE_SRC_CLASS (g_class);
    GstOmxBaseSrcClass *omx_base_class = GST_OMX_BASE_SRC_CLASS (g_class);

    omx_base_class->out_port_index = OMX_CAMERA_PORT_VIDEO_OUT_PREVIEW;

    /* GstBaseSrc methods: */
    gst_base_src_class->create = GST_DEBUG_FUNCPTR (create);

    /* GstElement methods: */
    gst_element_class->send_event = GST_DEBUG_FUNCPTR (send_event);

    /* GObject methods: */
    gobject_class->set_property = GST_DEBUG_FUNCPTR (set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR (get_property);

    /* install properties: */
    install_camera_properties (gobject_class);

}


void check_settings (GOmxPort *port, GstPad *pad);


/**
 * overrides the default buffer allocation for img_port to allow
 * pad_alloc'ing from the imgsrcpad
 */
static GstBuffer *
img_buffer_alloc (GOmxPort *port, gint len)
{
    GstOmxCamera *self = port->core->object;
    GstBuffer *buf;
    GstFlowReturn ret;

    GST_DEBUG_OBJECT (self, "img_buffer_alloc begin");
    check_settings (self->img_port, self->imgsrcpad);

    ret = gst_pad_alloc_buffer_and_set_caps (
            self->imgsrcpad, GST_BUFFER_OFFSET_NONE,
            len, GST_PAD_CAPS (self->imgsrcpad), &buf);

    if (ret == GST_FLOW_OK) return buf;

    return NULL;
}


/**
 * overrides the default buffer allocation for thumb_port to allow
 * pad_alloc'ing from the thumbsrcpad
 */
static GstBuffer *
thumb_buffer_alloc (GOmxPort *port, gint len)
{
    GstOmxCamera *self = port->core->object;
    GstBuffer *buf;
    GstFlowReturn ret;

    GST_DEBUG_OBJECT (self, "thumb_buffer_alloc begin");
    check_settings (self->vid_port, self->thumbsrcpad);

    ret = gst_pad_alloc_buffer_and_set_caps (
            self->thumbsrcpad, GST_BUFFER_OFFSET_NONE,
            len, GST_PAD_CAPS (self->thumbsrcpad), &buf);

    if (ret == GST_FLOW_OK) return buf;

    return NULL;
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

    self->mode = -1;
    self->next_mode = MODE_PREVIEW;

    omx_base->setup_ports = setup_ports;

    omx_base->gomx->settings_changed_cb = settings_changed_cb;
    omx_base->gomx->index_settings_changed_cb = index_settings_changed_cb;

    omx_base->gomx->use_timestamps = TRUE;

    self->vid_port = g_omx_core_get_port (omx_base->gomx, "vid",
            OMX_CAMERA_PORT_VIDEO_OUT_VIDEO);
    self->img_port = g_omx_core_get_port (omx_base->gomx, "img",
            OMX_CAMERA_PORT_IMAGE_OUT_IMAGE);
    self->in_port = g_omx_core_get_port (omx_base->gomx, "in",
            OMX_CAMERA_PORT_OTHER_IN);
    self->in_vid_port = g_omx_core_get_port (omx_base->gomx, "in_vid",
            OMX_CAMERA_PORT_VIDEO_IN_VIDEO);
    self->msr_port = g_omx_core_get_port (omx_base->gomx, "msr",
            OMX_CAMERA_PORT_VIDEO_OUT_MEASUREMENT);

    self->img_port->buffer_alloc = img_buffer_alloc;
    self->vid_port->buffer_alloc = thumb_buffer_alloc;
#if 0
    self->in_port = g_omx_core_get_port (omx_base->gomx, "in"
            OMX_CAMERA_PORT_VIDEO_IN_VIDEO);
#endif

    gst_base_src_set_live (basesrc, TRUE);

    /* setup src pad (already created by basesrc): */

    gst_pad_set_setcaps_function (basesrc->srcpad,
            GST_DEBUG_FUNCPTR (src_setcaps));
    gst_pad_set_fixatecaps_function (basesrc->srcpad,
            GST_DEBUG_FUNCPTR (src_fixatecaps));

    /* create/setup vidsrc pad: */
    pad_template = gst_element_class_get_pad_template (
            GST_ELEMENT_CLASS (g_class), "vidsrc");
    g_return_if_fail (pad_template != NULL);

    GST_DEBUG_OBJECT (basesrc, "creating vidsrc pad");
    self->vidsrcpad = gst_pad_new_from_template (pad_template, "vidsrc");
    gst_element_add_pad (GST_ELEMENT_CAST (self), self->vidsrcpad);

    /* create/setup imgsrc pad: */
    pad_template = gst_element_class_get_pad_template (
            GST_ELEMENT_CLASS (g_class), "imgsrc");
    g_return_if_fail (pad_template != NULL);

    GST_DEBUG_OBJECT (basesrc, "creating imgsrc pad");
    self->imgsrcpad = gst_pad_new_from_template (pad_template, "imgsrc");
    gst_element_add_pad (GST_ELEMENT_CAST (self), self->imgsrcpad);
    gst_pad_set_setcaps_function (self->imgsrcpad,
            GST_DEBUG_FUNCPTR (imgsrc_setcaps));
    gst_pad_set_fixatecaps_function (self->imgsrcpad,
            GST_DEBUG_FUNCPTR (imgsrc_fixatecaps));

    /* create/setup thumbsrc pad: */
    pad_template = gst_element_class_get_pad_template (
            GST_ELEMENT_CLASS (g_class), "thumbsrc");
    g_return_if_fail (pad_template != NULL);

    GST_DEBUG_OBJECT (basesrc, "creating thumbsrc pad");
    self->thumbsrcpad = gst_pad_new_from_template (pad_template, "thumbsrc");
    gst_element_add_pad (GST_ELEMENT_CAST (self), self->thumbsrcpad);
    gst_pad_set_setcaps_function (self->thumbsrcpad,
            GST_DEBUG_FUNCPTR (thumbsrc_setcaps));

    gst_pad_set_query_function (basesrc->srcpad,
            GST_DEBUG_FUNCPTR (src_query));
    gst_pad_set_query_function (self->vidsrcpad,
            GST_DEBUG_FUNCPTR (src_query));

#if 0
    /* disable all ports to begin with: */
    g_omx_port_disable (self->in_port);
#endif
    g_omx_port_disable (omx_base->out_port);
    g_omx_port_disable (self->vid_port);
    g_omx_port_disable (self->img_port);
    g_omx_port_disable (self->in_port);
    g_omx_port_disable (self->in_vid_port);
    g_omx_port_disable (self->msr_port);

    GST_DEBUG_OBJECT (omx_base, "end");
}
