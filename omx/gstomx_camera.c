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
#  include <OMX_Index.h>
#  include <OMX_Component.h>
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
    ARG_SHUTTER,
    ARG_ZOOM,
    ARG_FOCUS,
    ARG_AWB,
    ARG_THUMBNAIL_WIDTH,
    ARG_THUMBNAIL_HEIGHT,
    ARG_CONTRAST,
    ARG_BRIGHTNESS,
#ifdef USE_OMXTICORE
    ARG_VNF,
    ARG_YUV_RANGE,
    ARG_VSTAB,
    ARG_ROTATION,
    ARG_MIRROR,
#endif
};
#define DEFAULT_ZOOM_LEVEL           100
#define MIN_ZOOM_LEVEL               100
#define MAX_ZOOM_LEVEL               600
#define CAM_ZOOM_IN_STEP             0x10000

#define DEFAULT_FOCUS       OMX_IMAGE_FocusControlOff
#define DEFAULT_AWB         OMX_WhiteBalControlOff

#define DEFAULT_THUMBNAIL_WIDTH       352
#define DEFAULT_THUMBNAIL_HEIGHT      288
#define MIN_THUMBNAIL_LEVEL           16
#define MAX_THUMBNAIL_LEVEL           1920

#define DEFAULT_CONTRAST_LEVEL        0
#define MIN_CONTRAST_LEVEL            -100
#define MAX_CONTRAST_LEVEL            100

#define DEFAULT_BRIGHTNESS_LEVEL      50
#define MIN_BRIGHTNESS_LEVEL          0
#define MAX_BRIGHTNESS_LEVEL          100

#ifdef USE_OMXTICORE
#  define DEFAULT_VNF          OMX_VideoNoiseFilterModeOn
#  define DEFAULT_YUV_RANGE    OMX_ITURBT601
#  define DEFAULT_ROTATION     180
#  define DEFAULT_MIRROR       OMX_MirrorNone
#endif


GSTOMX_BOILERPLATE (GstOmxCamera, gst_omx_camera, GstOmxBaseSrc, GST_OMX_BASE_SRC_TYPE);

#define USE_GSTOMXCAM_IMGSRCPAD
//#define USE_GSTOMXCAM_VIDSRCPAD
//#define USE_GSTOMXCAM_IN_PORT

/*
 * Mode table
 */
enum
{
    MODE_PREVIEW        = 0,
    MODE_VIDEO          = 1,
    MODE_VIDEO_IMAGE    = 2,
    MODE_IMAGE          = 3,
    MODE_IMAGE_HS       = 4,
};

/*
 * Shutter state
 */
enum
{
    SHUTTER_OFF         = 0,
    SHUTTER_HALF_PRESS  = 1,
    SHUTTER_FULL_PRESS  = 2,
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
} config[] = {
    /* MODE_PREVIEW */            PORT_PREVIEW,
    /* MODE_VIDEO */              PORT_VIDEO,
    /* MODE_VIDEO_IMAGE */        PORT_VIDEO | PORT_IMAGE,
    /* MODE_IMAGE */              PORT_PREVIEW | PORT_IMAGE,
    /* MODE_IMAGE_HS */           PORT_PREVIEW | PORT_IMAGE,
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
            {MODE_IMAGE_HS,       "Image Capture High Speed",   "image-hs"},
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraMode", vals);
    }

    return type;
}

#define GST_TYPE_OMX_CAMERA_SHUTTER (gst_omx_camera_shutter_get_type ())
static GType
gst_omx_camera_shutter_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static GEnumValue vals[] =
        {
            {SHUTTER_OFF,         "Off",                        "off"},
            {SHUTTER_HALF_PRESS,  "Half Press",                 "half-press"},
            {SHUTTER_FULL_PRESS,  "Full Press",                 "full-press"},
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraShutter", vals);
    }

    return type;
}

#define GST_TYPE_OMX_CAMERA_FOCUS (gst_omx_camera_focus_get_type ())
static GType
gst_omx_camera_focus_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static GEnumValue vals[] =
        {
            {OMX_IMAGE_FocusControlOff,      "off",              "off"},
            {OMX_IMAGE_FocusControlOn,       "on",               "on"},
            {OMX_IMAGE_FocusControlAuto,     "auto",             "auto"},
            {OMX_IMAGE_FocusControlAutoLock, "autolock",         "autolock"},
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraFocus", vals);
    }

    return type;
}

#define GST_TYPE_OMX_CAMERA_AWB (gst_omx_camera_awb_get_type ())
static GType
gst_omx_camera_awb_get_type ()
{
    static GType type = 0;

    if (!type)
    {
        static const GEnumValue vals[] =
        {
            {OMX_WhiteBalControlOff,           "Off",           "No balance"},
            {OMX_WhiteBalControlAuto,          "Auto",          "Auto balance"},
            {OMX_WhiteBalControlSunLight,      "Sunlight",      "Sun light"},
            {OMX_WhiteBalControlCloudy,        "Cloudy",        "Cloudy"},
            {OMX_WhiteBalControlShade,         "Shade",         "Shade"},
            {OMX_WhiteBalControlTungsten,      "Tungsten",      "Tungsten"},
            {OMX_WhiteBalControlFluorescent,   "Fluorescent",   "Fluorescent"},
            {OMX_WhiteBalControlIncandescent,  "Incandescent",  "Incandescent"},
            {OMX_WhiteBalControlFlash,         "Flash",         "Flash" },
            {OMX_WhiteBalControlHorizon,       "Horizon",       "Horizon" },
            {0, NULL, NULL },
        };

        type = g_enum_register_static ("GstOmxCameraWhiteBalance",vals);
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

#define GST_TYPE_OMX_CAMERA_MIRROR (gst_omx_camera_mirror_get_type ())
static GType
gst_omx_camera_mirror_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static GEnumValue vals[] =
        {
            {OMX_MirrorNone,        "off",              "off"},
            {OMX_MirrorVertical,    "Vertical",         "Vertical"},
            {OMX_MirrorHorizontal,  "Horizontal",       "Horizontal"},
            {OMX_MirrorBoth,        "Both"              "Both"},
            {OMX_MirrorMax,         "Max"               "Max"},
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraMirror", vals);
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

        G_OMX_PORT_GET_DEFINITION (omx_base->out_port, &param);

        param.format.video.eColorFormat = g_omx_gstvformat_to_colorformat (format);
        param.format.video.nFrameWidth  = width;
        param.format.video.nFrameHeight = height;
        param.format.video.nStride      = rowstride;
        param.nBufferSize = gst_video_format_get_size_strided (format, width, height, rowstride);

        /* special hack to work around OMX camera bug:
         */
        if (param.format.video.eColorFormat == OMX_COLOR_FormatYUV420PackedSemiPlanar)
            param.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;

        framerate = gst_structure_get_value (
                gst_caps_get_structure (caps, 0), "framerate");

        if (framerate)
        {
            framerate_num = gst_value_get_fraction_numerator (framerate);
            framerate_denom = gst_value_get_fraction_denominator (framerate);

            param.format.video.xFramerate = (framerate_num << 16) / framerate_denom;
        }

        /* At the moment we are only using preview port and not vid_port
         * From omx camera desing document we are missing
         * SetParam CommonSensormode -> bOneShot = FALSE ?
         */
        G_OMX_PORT_SET_DEFINITION (omx_base->out_port, &param);

#ifdef USE_GSTOMXCAM_VIDSRCPAD
        G_OMX_PORT_SET_DEFINITION (self->vid_port, &param);
#endif

        GST_INFO_OBJECT (omx_base, " Rowstride=%d, Width=%d, Height=%d, Color=%d, Buffersize=%d, framerate=%d",
            param.format.video.nStride, param.format.video.nFrameWidth, param.format.video.nFrameHeight, param.format.video.eColorFormat, param.nBufferSize,param.format.video.xFramerate );

        gst_pad_set_caps (GST_BASE_SRC (self)->srcpad, caps);
#if 0
        /* force the src pad and vidsrc pad to use the same caps: */
        if (pad == self->vidsrcpad)
        {
            gst_pad_set_caps (GST_BASE_SRC (self)->srcpad, caps);
        }
        else
        {
            gst_pad_set_caps (self->vidsrcpad, caps);
        }

        GST_INFO_OBJECT (omx_base, " exit setcaps src: %");
#endif
    }

    return TRUE;
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

    return TRUE;
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
        GST_INFO_OBJECT (self, "%"GST_PTR_FORMAT": pre-fixated caps: %" GST_PTR_FORMAT, pad, new_caps);
        gst_pad_fixate_caps (pad, new_caps);
    }

    GST_INFO_OBJECT (self, "%"GST_PTR_FORMAT": caps are: %" GST_PTR_FORMAT, pad, new_caps);
    GST_INFO_OBJECT (self, "%"GST_PTR_FORMAT": old caps are: %" GST_PTR_FORMAT, pad, GST_PAD_CAPS (pad));

    gst_pad_set_caps (pad, new_caps);
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
}

static void
setup_ports (GstOmxBaseSrc *base_src)
{
    GstOmxCamera *self = GST_OMX_CAMERA (base_src);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    OMX_PARAM_PORTDEFINITIONTYPE param;

#ifdef USE_GSTOMXCAM_VIDSRCPAD
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
      timestamp = gst_clock_get_time (clock) - timestamp - (140 * GST_MSECOND);
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
#if 0
    /* Not utilized  because we are setting the preview port enable since the beginning*/

    if (config[self->mode] & PORT_PREVIEW)
    {
        GST_DEBUG_OBJECT (self, "enable preview port");
        g_omx_port_enable (omx_base->out_port);
    }
#endif

#ifdef USE_GSTOMXCAM_VIDSRCPAD
    if (config[self->mode] & PORT_VIDEO)
    {
        GST_DEBUG_OBJECT (self, "enable video port");
        gst_pad_set_active (self->vidsrcpad, TRUE);
        gst_element_add_pad (GST_ELEMENT_CAST (self), self->vidsrcpad);
        g_omx_port_enable (self->vid_port);
    }
#endif

#ifdef USE_GSTOMXCAM_IMGSRCPAD
    if (config[self->mode] & PORT_IMAGE)
    {
        GST_DEBUG_OBJECT (self, "enable image port");
        gst_pad_set_active (self->imgsrcpad, TRUE);
        gst_element_add_pad (GST_ELEMENT_CAST (self), self->imgsrcpad);

        if ((self->next_mode == MODE_IMAGE) || (self->next_mode == MODE_IMAGE_HS))
        {
            /* Select Usecase -> remove from this part */
            OMX_CONFIG_CAMOPERATINGMODETYPE mode;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&mode);

            if (self->next_mode == MODE_IMAGE)
            {
                mode.eCamOperatingMode =
                    OMX_CaptureImageProfileBase;
            }
            else if (self->next_mode == MODE_IMAGE_HS)
            {
                mode.eCamOperatingMode =
                    OMX_CaptureImageHighSpeedTemporalBracketing;
            }
            error_val = OMX_SetParameter (gomx->omx_handle,
                            OMX_IndexCameraOperatingMode, &mode);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "OMX_CaptureImageMode: set = %d",
                              mode.eCamOperatingMode);
        }

        g_omx_port_enable (self->img_port);

        GST_DEBUG_OBJECT (self, "image port set_capture set to  %d", TRUE);
        set_capture (self, TRUE);
    }
#endif
}


static void
stop_ports (GstOmxCamera *self)
{

#if 0
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    if (config[self->mode] & PORT_PREVIEW)
    {
        GST_DEBUG_OBJECT (self, "disable preview port");
        g_omx_port_disable (omx_base->out_port);
    }
#endif

#ifdef USE_GSTOMXCAM_VIDSRCPAD
    if (config[self->mode] & PORT_VIDEO)
    {
        GST_DEBUG_OBJECT (self, "disable video port");
        gst_pad_set_active (self->vidsrcpad, FALSE);
        //gst_element_remove_pad (GST_ELEMENT_CAST (self), self->vidsrcpad);
        g_omx_port_disable (self->vid_port);
    }
#endif

#ifdef USE_GSTOMXCAM_IMGSRCPAD
    if (config[self->mode] & PORT_IMAGE)
    {
        GST_DEBUG_OBJECT (self, "disable image port");
        gst_pad_set_active (self->imgsrcpad, FALSE);
        //gst_element_remove_pad (GST_ELEMENT_CAST (self), self->imgsrcpad);
        g_omx_port_disable (self->img_port);
    }
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
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    GstBuffer *preview_buf = NULL;
    GstBuffer *vid_buf = NULL;
    GstBuffer *img_buf = NULL;
    GstFlowReturn ret = GST_FLOW_NOT_NEGOTIATED;
    GstClockTime timestamp;
    static guint cont;

    GST_DEBUG_OBJECT (self, "begin, mode=%d", self->mode);

    GST_LOG_OBJECT (self, "state: %d", omx_base->gomx->omx_state);

    if (omx_base->gomx->omx_state == OMX_StateLoaded)
    {
        GST_INFO_OBJECT (self, "omx: prepare");
        gst_omx_base_src_setup_ports (omx_base);
        g_omx_core_prepare (omx_base->gomx);
    }

    if (self->mode != self->next_mode)
    {
        if (self->mode != -1)
            stop_ports (self);
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
        if (ret != GST_FLOW_OK)
            goto fail;
    }

    if (config[self->mode] & PORT_VIDEO)
    {
        ret = gst_omx_base_src_create_from_port (omx_base,
                self->vid_port, &vid_buf);
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

    if (vid_buf && !preview_buf)
    {
        preview_buf = gst_buffer_ref (vid_buf);
    }

    timestamp = get_timestamp (self);
    cont ++;
    GST_DEBUG_OBJECT (self, "******** preview buffers cont = %d", cont);
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
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

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
            self->next_mode = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "mode: %d", self->next_mode);
            break;
        }
        case ARG_SHUTTER:
        {
            self->shutter = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "shutter: %d", self->shutter);
            break;
        }
        case ARG_ZOOM:
        {
            OMX_CONFIG_SCALEFACTORTYPE zoom_scalefactor;
            GOmxCore *gomx;
            OMX_U32 zoom_factor;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;
            gint32 zoom_value;
            zoom_value = g_value_get_int (value);
            gomx = (GOmxCore *) omx_base->gomx;
            zoom_factor = (OMX_U32)(zoom_value / 100);
            GST_DEBUG_OBJECT (self, "Set Property for zoom factor = %d", zoom_factor);

            _G_OMX_INIT_PARAM (&zoom_scalefactor);
            error_val = OMX_GetConfig (gomx->omx_handle, OMX_IndexConfigCommonDigitalZoom,
                                       &zoom_scalefactor);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "OMX_GetConfig Successful for zoom");
            zoom_scalefactor.xWidth = (CAM_ZOOM_IN_STEP * zoom_factor);
            zoom_scalefactor.xHeight = (CAM_ZOOM_IN_STEP * zoom_factor);
            GST_DEBUG_OBJECT (self, "zoom = x%d", zoom_scalefactor.xWidth / CAM_ZOOM_IN_STEP);
            error_val = OMX_SetConfig (gomx->omx_handle, OMX_IndexConfigCommonDigitalZoom,
                                       &zoom_scalefactor);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "OMX_SetConfig Successful for zoom");
            break;
        }
        case ARG_FOCUS:
        {
            OMX_IMAGE_CONFIG_FOCUSCONTROLTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig(gomx->omx_handle,
                                       OMX_IndexConfigFocusControl, &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nPortIndex = omx_base->out_port->port_index;
            config.eFocusControl = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "AF: param=%d port=%d", config.eFocusControl,
                                                            config.nPortIndex);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigFocusControl, &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_AWB:
        {
            OMX_CONFIG_WHITEBALCONTROLTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonWhiteBalance,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nPortIndex = omx_base->out_port->port_index;
            config.eWhiteBalControl = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "AWB: param=%d",
                                     config.eWhiteBalControl,
                                     config.nPortIndex);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonWhiteBalance,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_THUMBNAIL_WIDTH:
        {
            OMX_PARAM_THUMBNAILTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            error_val = OMX_GetParameter (gomx->omx_handle,
                                          OMX_IndexParamThumbnail,
                                          &param);
            g_assert (error_val == OMX_ErrorNone);
            self->img_thumbnail_width = g_value_get_int (value);
            param.nWidth = self->img_thumbnail_width;
            GST_DEBUG_OBJECT (self, "Thumbnail width=%d", param.nWidth);
            error_val = OMX_SetParameter (gomx->omx_handle,
                    OMX_IndexParamThumbnail,&param);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_THUMBNAIL_HEIGHT:
        {
            OMX_PARAM_THUMBNAILTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            error_val = OMX_GetParameter (gomx->omx_handle,
                                          OMX_IndexParamThumbnail,
                                          &param);
            g_assert (error_val == OMX_ErrorNone);
            self->img_thumbnail_height = g_value_get_int (value);
            param.nHeight = self->img_thumbnail_height;
            GST_DEBUG_OBJECT (self, "Thumbnail height=%d", param.nHeight);
            error_val = OMX_SetParameter (gomx->omx_handle,
                    OMX_IndexParamThumbnail,&param);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_CONTRAST:
        {
            OMX_CONFIG_CONTRASTTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonContrast, &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nContrast = g_value_get_int (value);
            GST_DEBUG_OBJECT (self, "Contrast: param=%d", config.nContrast);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonContrast, &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_BRIGHTNESS:
        {
            OMX_CONFIG_BRIGHTNESSTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonBrightness, &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nBrightness = g_value_get_int (value);
            GST_DEBUG_OBJECT (self, "Brightness: param=%d", config.nBrightness);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonBrightness, &config);
            g_assert (error_val == OMX_ErrorNone);
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
        case ARG_ROTATION:
        {
            OMX_CONFIG_ROTATIONTYPE  config;

            G_OMX_PORT_GET_CONFIG (self->img_port, OMX_IndexConfigCommonRotate, &config);

            config.nRotation = g_value_get_uint (value);
            GST_DEBUG_OBJECT (self, "Rotation: param=%d", config.nRotation);

            G_OMX_PORT_SET_CONFIG (self->img_port, OMX_IndexConfigCommonRotate, &config);
            break;
        }
        case ARG_MIRROR:
        {
            OMX_CONFIG_MIRRORTYPE  config;
            G_OMX_PORT_GET_CONFIG (self->img_port, OMX_IndexConfigCommonMirror, &config);

            config.eMirror = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "Mirror: param=%d", config.eMirror);

            G_OMX_PORT_SET_CONFIG (self->img_port, OMX_IndexConfigCommonMirror, &config);
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
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

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
        case ARG_SHUTTER:
        {
            GST_DEBUG_OBJECT (self, "shutter: %d", self->shutter);
            g_value_set_enum (value, self->shutter);
            break;
        }
        case ARG_ZOOM:
        {
            OMX_CONFIG_SCALEFACTORTYPE zoom_scalefactor;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;
            gomx = (GOmxCore *) omx_base->gomx;
            GST_DEBUG_OBJECT (self, "Get Property for zoom");

            _G_OMX_INIT_PARAM (&zoom_scalefactor);
            error_val = OMX_GetConfig (gomx->omx_handle, OMX_IndexConfigCommonDigitalZoom,
                                       &zoom_scalefactor);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_FOCUS:
        {
            OMX_IMAGE_CONFIG_FOCUSCONTROLTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;
            gomx = (GOmxCore *) omx_base->gomx;

            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                        OMX_IndexConfigFocusControl, &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nPortIndex = omx_base->out_port->port_index;
            GST_DEBUG_OBJECT (self, "AF: param=%d port=%d", config.eFocusControl,
                                                            config.nPortIndex);
            g_value_set_enum (value, config.eFocusControl);

            break;
        }
        case ARG_AWB:
        {
            OMX_CONFIG_WHITEBALCONTROLTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;
            gomx = (GOmxCore *) omx_base->gomx;

            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonWhiteBalance,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nPortIndex = omx_base->out_port->port_index;
            GST_DEBUG_OBJECT (self, "AWB: param=%d", config.eWhiteBalControl);
            g_value_set_enum (value, config.eWhiteBalControl);

            break;
        }
        case ARG_THUMBNAIL_WIDTH:
        {
            OMX_PARAM_THUMBNAILTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            error_val = OMX_GetParameter(gomx->omx_handle,
                                       OMX_IndexParamThumbnail,
                                       &param);
            g_assert (error_val == OMX_ErrorNone);
            self->img_thumbnail_width = param.nWidth;
            GST_DEBUG_OBJECT (self, "Thumbnail width=%d",
                                    self->img_thumbnail_width);
            break;
        }
        case ARG_THUMBNAIL_HEIGHT:
        {
            OMX_PARAM_THUMBNAILTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            error_val = OMX_GetParameter(gomx->omx_handle,
                                       OMX_IndexParamThumbnail,
                                       &param);
            g_assert (error_val == OMX_ErrorNone);
            self->img_thumbnail_height = param.nHeight;
            GST_DEBUG_OBJECT (self, "Thumbnail height=%d",
                                    self->img_thumbnail_height);
            break;
        }
        case ARG_CONTRAST:
        {
            OMX_CONFIG_CONTRASTTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonContrast, &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Contrast=%d", config.nContrast);
            break;
        }
        case ARG_BRIGHTNESS:
        {
            OMX_CONFIG_BRIGHTNESSTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonBrightness, &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Brightness=%d", config.nBrightness);
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
        case ARG_ROTATION:
        {
            OMX_CONFIG_ROTATIONTYPE  config;

            G_OMX_PORT_GET_CONFIG (self->img_port, OMX_IndexConfigCommonRotate, &config);

            GST_DEBUG_OBJECT (self, "Rotation: param=%d", config.nRotation);
            g_value_set_uint (value, config.nRotation);
            break;
        }
        case ARG_MIRROR:
        {
            OMX_CONFIG_MIRRORTYPE  config;
            G_OMX_PORT_GET_CONFIG (self->img_port, OMX_IndexConfigCommonMirror, &config);

            GST_DEBUG_OBJECT (self, "Mirror: param=%d", config.eMirror);
            g_value_set_enum (value, config.eMirror);
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
        gst_static_pad_template_get (&vidsrc_template));

    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get (&imgsrc_template));

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
                    MODE_PREVIEW,
                    G_PARAM_READWRITE));

    g_object_class_install_property (gobject_class, ARG_SHUTTER,
            g_param_spec_enum ("shutter", "Shutter State",
                    "shutter button state",
                    GST_TYPE_OMX_CAMERA_SHUTTER,
                    SHUTTER_OFF,
                    G_PARAM_READWRITE));

    g_object_class_install_property (gobject_class, ARG_ZOOM,
            g_param_spec_int ("zoom", "Digital Zoom",
                    "digital zoom factor/level",
                    MIN_ZOOM_LEVEL, MAX_ZOOM_LEVEL, DEFAULT_ZOOM_LEVEL,
                    G_PARAM_READWRITE));

    g_object_class_install_property (gobject_class, ARG_FOCUS,
            g_param_spec_enum ("focus", "Auto Focus",
                    "auto focus state",
                    GST_TYPE_OMX_CAMERA_FOCUS,
                    DEFAULT_FOCUS,
                    G_PARAM_READWRITE));

    g_object_class_install_property (gobject_class, ARG_AWB,
            g_param_spec_enum ("awb", "Auto White Balance",
                    "auto white balance state",
                    GST_TYPE_OMX_CAMERA_AWB,
                    DEFAULT_AWB,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_THUMBNAIL_WIDTH,
            g_param_spec_int ("thumb-width", "Thumbnail width",
                    "Thumbnail width in pixels", MIN_THUMBNAIL_LEVEL,
                    MAX_THUMBNAIL_LEVEL, DEFAULT_THUMBNAIL_WIDTH,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_THUMBNAIL_HEIGHT,
            g_param_spec_int ("thumb-height", "Thumbnail height",
                    "Thumbnail height in pixels", MIN_THUMBNAIL_LEVEL,
                    MAX_THUMBNAIL_LEVEL, DEFAULT_THUMBNAIL_HEIGHT,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_CONTRAST,
            g_param_spec_int ("contrast", "Contrast",
                    "contrast level", MIN_CONTRAST_LEVEL,
                    MAX_CONTRAST_LEVEL, DEFAULT_CONTRAST_LEVEL,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_BRIGHTNESS,
            g_param_spec_int ("brightness", "Brightness",
                    "brightness level", MIN_BRIGHTNESS_LEVEL,
                    MAX_BRIGHTNESS_LEVEL, DEFAULT_BRIGHTNESS_LEVEL,
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

    g_object_class_install_property (gobject_class, ARG_ROTATION,
            g_param_spec_uint ("rotation", "Rotation",
                    "Image rotation",
                    0, 270, DEFAULT_ROTATION , G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_MIRROR,
            g_param_spec_enum ("mirror", "Mirror",
                    "Mirror image",
                    GST_TYPE_OMX_CAMERA_MIRROR,
                    DEFAULT_MIRROR,
                    G_PARAM_READWRITE));
#endif
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

    omx_base->gomx->use_timestamps = TRUE;

    self->vid_port = g_omx_core_get_port (omx_base->gomx, "vid",
            OMX_CAMERA_PORT_VIDEO_OUT_VIDEO);
    self->img_port = g_omx_core_get_port (omx_base->gomx, "img",
            OMX_CAMERA_PORT_IMAGE_OUT_IMAGE);
    self->img_port->buffer_alloc = img_buffer_alloc;
#if 0
    self->in_port = g_omx_core_get_port (omx_base->gomx, "in"
            OMX_CAMERA_PORT_VIDEO_IN_VIDEO);
#endif

    gst_base_src_set_live (basesrc, TRUE);

    /* setup src pad (already created by basesrc): */

    gst_pad_set_setcaps_function (basesrc->srcpad,
            GST_DEBUG_FUNCPTR (src_setcaps));

    /* create/setup vidsrc pad: */
    pad_template = gst_element_class_get_pad_template (
            GST_ELEMENT_CLASS (g_class), "vidsrc");
    g_return_if_fail (pad_template != NULL);

    GST_DEBUG_OBJECT (basesrc, "creating vidsrc pad");
    self->vidsrcpad = gst_pad_new_from_template (pad_template, "vidsrc");

    /* src and vidsrc pads have same caps: */
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
#if 0
    /* disable all ports to begin with: */
    g_omx_port_disable (self->in_port);
    g_omx_port_disable (omx_base->out_port);
#endif
    g_omx_port_disable (self->vid_port);
    g_omx_port_disable (self->img_port);
    GST_DEBUG_OBJECT (omx_base, "end");
}
