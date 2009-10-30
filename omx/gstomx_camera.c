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

#ifdef USE_OMXTICORE
#  include <OMX_TI_IVCommon.h>
#  include <OMX_TI_Index.h>
#endif

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


enum
{
    ARG_0,
    ARG_NUM_IMAGE_OUTPUT_BUFFERS,
    ARG_NUM_VIDEO_OUTPUT_BUFFERS,
    ARG_MODE,
#ifdef USE_OMXTICORE
    ARG_VNF,
    ARG_YUV_RANGE,
    ARG_VSTAB,
#endif
};

#define DEFAULT_MODE         MODE_PREVIEW
#define DEFAULT_VNF          OMX_VideoNoiseFilterModeOn
#define DEFAULT_YUV_RANGE    OMX_ITURBT601


GSTOMX_BOILERPLATE (GstOmxCamera, gst_omx_camera, GstOmxBaseSrc, GST_OMX_BASE_SRC_TYPE);

/*
 * Mode table
 */
enum
{
    MODE_PREVIEW        = 0,
    MODE_VIDEO          = 1,
    MODE_VIDEO_IMAGE    = 2,
    MODE_IMAGE          = 3,
    MODE_IMAGE_TEMPO_BR = 4,
};

/**
 * Table mapping mode to features and ports.  The mode is used as an index
 * into this table to determine which ports and features are used in that
 * particular mode.  Since there is some degree of overlap between various
 * modes, this is to simplify the code to not care about modes, but instead
 * just which bits are set in the config.
 */
static const enum
{
    /* ports that can be used: */
    PORT_PREVIEW  = 0x01,
    PORT_VIDEO    = 0x02,
    PORT_IMAGE    = 0x04,

    /* features that can be used: */
    FEAT_TEMPO_BR = 0x10,
} config[] = {
    /* MODE_PREVIEW */            PORT_PREVIEW,
    /* MODE_VIDEO */              PORT_VIDEO,
    /* MODE_VIDEO_IMAGE */        PORT_VIDEO | PORT_IMAGE,
    /* MODE_IMAGE */              PORT_PREVIEW | PORT_IMAGE,
    /* MODE_IMAGE_TEMPO_BR */     PORT_PREVIEW | PORT_IMAGE | FEAT_TEMPO_BR,
};



/*
 * Enums:
 */

#define GST_TYPE_OMX_CAMERA_MODE (gst_omx_camera_mode_get_type ())
static GType
gst_omx_camera_mode_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static GEnumValue vals[] =
        {
            {MODE_PREVIEW,        "Preview",                    "preview"},
            {MODE_VIDEO,          "Video Capture",              "video"},
            {MODE_VIDEO_IMAGE,    "Video+Image Capture",        "video-image"},
            {MODE_IMAGE,          "Image Capture",              "image"},
            {MODE_IMAGE_TEMPO_BR, "Image (Temporal Bracket)",   "image-tb"},
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraMode", vals);
    }

    return type;
}

#ifdef USE_OMXTICORE
#define GST_TYPE_OMX_CAMERA_VNF (gst_omx_camera_vnf_get_type ())
static GType
gst_omx_camera_vnf_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static GEnumValue vals[] =
        {
            {OMX_VideoNoiseFilterModeOff,   "off",              "off"},
            {OMX_VideoNoiseFilterModeOn,    "on",               "on"},
            {OMX_VideoNoiseFilterModeAuto,  "auto",             "auto"},
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraVnf", vals);
    }

    return type;
}


#define GST_TYPE_OMX_CAMERA_YUV_RANGE (gst_omx_camera_yuv_range_get_type ())
static GType
gst_omx_camera_yuv_range_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static GEnumValue vals[] =
        {
            {OMX_ITURBT601,       "OMX_ITURBT601",              "OMX_ITURBT601"},
            {OMX_Full8Bit,        "OMX_Full8Bit",               "OMX_Full8Bit"},
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraYuvRange", vals);
    }

    return type;
}
#endif


/*
 * Caps:
 */


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV_STRIDED (GSTOMX_ALL_FORMATS, "[ 0, max ]"))
    );

static GstStaticPadTemplate imgsrc_template = GST_STATIC_PAD_TEMPLATE ("imgsrc",
        GST_PAD_SRC,
        GST_PAD_SOMETIMES,
        /* note: imgsrc pad supports jpg format, as well as non-strided YUV */
        GST_STATIC_CAPS (
                "image/jpeg, width=(int)[1,max], height=(int)[1,max]; "
                GST_VIDEO_CAPS_YUV (GSTOMX_ALL_FORMATS))
    );

static GstStaticPadTemplate vidsrc_template = GST_STATIC_PAD_TEMPLATE ("vidsrc",
        GST_PAD_SRC,
        GST_PAD_SOMETIMES,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV_STRIDED (GSTOMX_ALL_FORMATS, "[ 0, max ]"))
    );

#if 0
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
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
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    GstVideoFormat format;
    gint width, height, rowstride;

    GST_INFO_OBJECT (omx_base, "setcaps (src/vidsrc): %" GST_PTR_FORMAT, caps);

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
        G_OMX_PORT_SET_DEFINITION (self->vid_port, &param);
    }

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
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    OMX_PARAM_PORTDEFINITIONTYPE param;

    G_OMX_PORT_GET_DEFINITION (self->vid_port, &param);
    g_omx_port_setup (self->vid_port, &param);

    G_OMX_PORT_GET_DEFINITION (self->img_port, &param);
    g_omx_port_setup (self->img_port, &param);

#if 0
    G_OMX_PORT_GET_DEFINITION (self->in_port, &param);
    g_omx_port_setup (self->in_port, &param);
#endif

    omx_base->out_port->share_buffer = TRUE;
    self->vid_port->share_buffer = TRUE;
    self->img_port->share_buffer = TRUE;
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
    GstFlowReturn ret = GST_FLOW_OK;
    GstClockTime timestamp;

    GST_DEBUG_OBJECT (self, "begin, mode=%d", self->mode);

    GST_LOG_OBJECT (self, "state: %d", omx_base->gomx->omx_state);

    if (omx_base->gomx->omx_state == OMX_StateLoaded)
    {
        GST_INFO_OBJECT (self, "omx: prepare");

        gst_omx_base_src_setup_ports (omx_base);
        g_omx_core_prepare (omx_base->gomx);
    }

    if (config[self->mode] & PORT_PREVIEW)
    {
        ret = gst_omx_base_src_create_from_port (omx_base,
                omx_base->out_port, &preview_buf);
        if (ret != GST_FLOW_OK) goto fail;
    }

    if (config[self->mode] & PORT_VIDEO)
    {
        ret = gst_omx_base_src_create_from_port (omx_base,
                self->vid_port, &vid_buf);
        if (ret != GST_FLOW_OK) goto fail;
    }

    if (config[self->mode] & PORT_IMAGE)
    {
        ret = gst_omx_base_src_create_from_port (omx_base,
                self->img_port, &img_buf);
        if (ret != GST_FLOW_OK) goto fail;
    }

    if (vid_buf && !preview_buf)
    {
        preview_buf = gst_buffer_ref (vid_buf);
    }

    if (config[self->mode] & FEAT_TEMPO_BR)
    {
        // TODO
        GST_DEBUG_OBJECT (self, "temporal bracketing not implemented yet");
    }

    timestamp = get_timestamp (self);

    GST_BUFFER_TIMESTAMP (preview_buf) = timestamp;

    *ret_buf = preview_buf;

    if (vid_buf)
    {
        GST_DEBUG_OBJECT (self, "pushing vid_buf");
        GST_BUFFER_TIMESTAMP (vid_buf) = timestamp;
        gst_pad_push (self->vidsrcpad, vid_buf);
    }

    if (img_buf)
    {
        GST_DEBUG_OBJECT (self, "pushing img_buf");
        GST_BUFFER_TIMESTAMP (img_buf) = timestamp;
        gst_pad_push (self->imgsrcpad, img_buf);
    }

    GST_DEBUG_OBJECT (self, "end, ret=%d", ret);

    return GST_FLOW_OK;

fail:
    if (preview_buf) gst_buffer_unref (preview_buf);
    if (vid_buf)     gst_buffer_unref (vid_buf);
    if (img_buf)     gst_buffer_unref (img_buf);

    return ret;
}

static gboolean
start (GstBaseSrc *gst_base)
{
    GstOmxCamera *self = GST_OMX_CAMERA (gst_base);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    gboolean ret = TRUE;

    GST_DEBUG_OBJECT (self, "begin, mode=%d", self->mode);

    if (config[self->mode] & PORT_PREVIEW)
    {
        g_omx_port_enable (omx_base->out_port);
    }
    if (config[self->mode] & PORT_VIDEO)
    {
        gst_pad_set_active (self->vidsrcpad, TRUE);
        gst_element_add_pad (GST_ELEMENT_CAST (self), self->vidsrcpad);
        g_omx_port_enable (self->vid_port);
    }
    if (config[self->mode] & PORT_IMAGE)
    {
        gst_pad_set_active (self->vidsrcpad, TRUE);
        gst_element_add_pad (GST_ELEMENT_CAST (self), self->vidsrcpad);
        g_omx_port_enable (self->img_port);
    }

    ret = GST_BASE_SRC_CLASS (parent_class)->start (gst_base);

    GST_DEBUG_OBJECT (self, "end, ret=%d", ret);

    return ret;
}

static gboolean
stop (GstBaseSrc *gst_base)
{
    GstOmxCamera *self = GST_OMX_CAMERA (gst_base);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    gboolean ret = TRUE;

    GST_LOG_OBJECT (self, "begin");

    g_omx_port_disable (omx_base->out_port);
    g_omx_port_disable (self->img_port);
    g_omx_port_disable (self->vid_port);

    // XXX remove/deactivate unused pads..

    ret = GST_BASE_SRC_CLASS (parent_class)->stop (gst_base);

    GST_LOG_OBJECT (self, "end, ret=%d", ret);

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
#ifdef USE_OMXTICORE
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
#endif

    switch (prop_id)
    {
        case ARG_NUM_IMAGE_OUTPUT_BUFFERS:
        case ARG_NUM_VIDEO_OUTPUT_BUFFERS:
        {
            OMX_PARAM_PORTDEFINITIONTYPE param;
            OMX_U32 nBufferCountActual = g_value_get_uint (value);
            GOmxPort *port = (prop_id == ARG_NUM_IMAGE_OUTPUT_BUFFERS) ?
                    self->img_port : self->vid_port;

            G_OMX_PORT_GET_DEFINITION (port, &param);

            g_return_if_fail (nBufferCountActual >= param.nBufferCountMin);
            param.nBufferCountActual = nBufferCountActual;

            G_OMX_PORT_SET_DEFINITION (port, &param);

            break;
        }
        case ARG_MODE:
        {
            self->mode = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "mode: %d", self->mode);
            break;
        }
#ifdef USE_OMXTICORE
        case ARG_VNF:
        {
            OMX_PARAM_VIDEONOISEFILTERTYPE param;

            G_OMX_PORT_GET_PARAM (omx_base->out_port, OMX_IndexParamVideoNoiseFilter, &param);

            param.eMode = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "vnf: param=%d", param.eMode);

            G_OMX_PORT_SET_PARAM (omx_base->out_port, OMX_IndexParamVideoNoiseFilter, &param);

            break;
        }
        case ARG_YUV_RANGE:
        {
            OMX_PARAM_VIDEOYUVRANGETYPE param;

            G_OMX_PORT_GET_PARAM (omx_base->out_port, OMX_IndexParamVideoCaptureYUVRange, &param);

            param.eYUVRange = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "yuv-range: param=%d", param.eYUVRange);

            G_OMX_PORT_SET_PARAM (omx_base->out_port, OMX_IndexParamVideoCaptureYUVRange, &param);

            break;
        }
        case ARG_VSTAB:
        {
            OMX_CONFIG_BOOLEANTYPE param;
            OMX_CONFIG_FRAMESTABTYPE config;

            G_OMX_CORE_GET_PARAM (omx_base->gomx, OMX_IndexParamFrameStabilisation, &param);
            G_OMX_CORE_GET_CONFIG (omx_base->gomx, OMX_IndexConfigCommonFrameStabilisation, &config);

            param.bEnabled = config.bStab = g_value_get_boolean (value);
            GST_DEBUG_OBJECT (self, "vstab: param=%d, config=%d", param.bEnabled, config.bStab);

            G_OMX_CORE_SET_PARAM (omx_base->gomx, OMX_IndexParamFrameStabilisation, &param);
            G_OMX_CORE_SET_CONFIG (omx_base->gomx, OMX_IndexConfigCommonFrameStabilisation, &config);

            break;
        }
#endif
        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
        }
    }
}

static void
get_property (GObject *obj,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    GstOmxCamera *self = GST_OMX_CAMERA (obj);
#ifdef USE_OMXTICORE
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
#endif

    switch (prop_id)
    {
        case ARG_NUM_IMAGE_OUTPUT_BUFFERS:
        case ARG_NUM_VIDEO_OUTPUT_BUFFERS:
        {
            OMX_PARAM_PORTDEFINITIONTYPE param;
            GOmxPort *port = (prop_id == ARG_NUM_IMAGE_OUTPUT_BUFFERS) ?
                    self->img_port : self->vid_port;

            G_OMX_PORT_GET_DEFINITION (port, &param);

            g_value_set_uint (value, param.nBufferCountActual);

            break;
        }
        case ARG_MODE:
        {
            GST_DEBUG_OBJECT (self, "mode: %d", self->mode);
            g_value_set_enum (value, self->mode);
            break;
        }
#ifdef USE_OMXTICORE
        case ARG_VNF:
        {
            OMX_PARAM_VIDEONOISEFILTERTYPE param;

            G_OMX_PORT_GET_PARAM (omx_base->out_port, OMX_IndexParamVideoNoiseFilter, &param);

            GST_DEBUG_OBJECT (self, "vnf: param=%d", param.eMode);
            g_value_set_enum (value, param.eMode);

            break;
        }
        case ARG_YUV_RANGE:
        {
            OMX_PARAM_VIDEOYUVRANGETYPE param;

            G_OMX_PORT_GET_PARAM (omx_base->out_port, OMX_IndexParamVideoCaptureYUVRange, &param);

            GST_DEBUG_OBJECT (self, "yuv-range: param=%d", param.eYUVRange);
            g_value_set_enum (value, param.eYUVRange);

            break;
        }
        case ARG_VSTAB:
        {
            OMX_CONFIG_BOOLEANTYPE param;
            OMX_CONFIG_FRAMESTABTYPE config;

            G_OMX_CORE_GET_PARAM (omx_base->gomx, OMX_IndexParamFrameStabilisation, &param);
            G_OMX_CORE_GET_CONFIG (omx_base->gomx, OMX_IndexConfigCommonFrameStabilisation, &config);

            GST_DEBUG_OBJECT (self, "vstab: param=%d, config=%d", param.bEnabled, config.bStab);
            g_value_set_boolean (value, param.bEnabled && config.bStab);

            break;
        }
#endif
        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
        }
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
    gst_base_src_class->start = start;
    gst_base_src_class->stop = stop;

    /* GObject methods: */
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    /* install properties: */
    g_object_class_install_property (gobject_class, ARG_NUM_IMAGE_OUTPUT_BUFFERS,
            g_param_spec_uint ("image-output-buffers", "Image port output buffers",
                    "The number of OMX image port output buffers",
                    1, 10, 4, G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_NUM_VIDEO_OUTPUT_BUFFERS,
            g_param_spec_uint ("video-output-buffers", "Video port output buffers",
                    "The number of OMX video port output buffers",
                    1, 10, 4, G_PARAM_READWRITE));

    g_object_class_install_property (gobject_class, ARG_MODE,
            g_param_spec_enum ("mode", "Camera Mode",
                    "image capture, video capture, or both",
                    GST_TYPE_OMX_CAMERA_MODE,
                    DEFAULT_MODE,
                    G_PARAM_READWRITE));

#ifdef USE_OMXTICORE
    g_object_class_install_property (gobject_class, ARG_VNF,
            g_param_spec_enum ("vnf", "Video Noise Filter",
                    "is video noise filter algorithm enabled?",
                    GST_TYPE_OMX_CAMERA_VNF,
                    DEFAULT_VNF,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_YUV_RANGE,
            g_param_spec_enum ("yuv-range", "YUV Range",
                    "YUV Range",
                    GST_TYPE_OMX_CAMERA_YUV_RANGE,
                    DEFAULT_YUV_RANGE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_VSTAB,
            g_param_spec_boolean ("vstab", "Video Frame Stabilization",
                    "is video stabilization algorithm enabled?",
                    TRUE,
                    G_PARAM_READWRITE));
#endif
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
#if 0
    self->in_port = g_omx_core_get_port (omx_base->gomx, "in"
            OMX_CAMERA_PORT_VIDEO_IN_VIDEO);
#endif

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

    /* src and vidsrc pads have same caps: */
    gst_pad_set_getcaps_function (self->vidsrcpad,
            GST_DEBUG_FUNCPTR (src_getcaps));
    gst_pad_set_setcaps_function (self->vidsrcpad,
            GST_DEBUG_FUNCPTR (src_setcaps));

    /* create/setup imgsrc pad: */
    pad_template = gst_element_class_get_pad_template (
            GST_ELEMENT_CLASS (g_class), "imgsrc");
    g_return_if_fail (pad_template != NULL);

    GST_DEBUG_OBJECT (basesrc, "creating imgsrc pad");
    self->imgsrcpad = gst_pad_new_from_template (pad_template, "imgsrc");
    gst_pad_set_setcaps_function (self->imgsrcpad,
            GST_DEBUG_FUNCPTR (imgsrc_setcaps));

    /* disable all ports to begin with: */
    g_omx_port_disable (omx_base->out_port);
    g_omx_port_disable (self->img_port);
    g_omx_port_disable (self->vid_port);
#if 0
    g_omx_port_disable (self->in_port);
#endif

    GST_DEBUG_OBJECT (omx_base, "end");
}
