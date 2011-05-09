/* GStreamer
 *
 * Copyright (C) 2009 Texas Instruments, Inc - http://www.ti.com/
 *
 * Description: OMX Camera element
 *  Created on: Mar 22, 2011
 *      Author: Joaquin Castellanos <jcastellanos@ti.com>
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
    /* MODE_VIDEO */              PORT_PREVIEW,
    /* MODE_VIDEO_IMAGE */        PORT_PREVIEW | PORT_IMAGE,
    /* MODE_IMAGE */              PORT_PREVIEW | PORT_IMAGE,
    /* MODE_IMAGE_HS */           PORT_PREVIEW | PORT_IMAGE,
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

/*
 * Argument table
 */
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
    ARG_CONTRAST,
    ARG_BRIGHTNESS,
    ARG_EXPOSURE,
    ARG_ISO,
    ARG_ROTATION,
    ARG_MIRROR,
    ARG_SATURATION,
    ARG_EXPOSUREVALUE,
    ARG_MANUALFOCUS,
    ARG_QFACTORJPEG,
#ifdef USE_OMXTICORE
    ARG_THUMBNAIL_WIDTH,
    ARG_THUMBNAIL_HEIGHT,
    ARG_FLICKER,
    ARG_SCENE,
    ARG_VNF,
    ARG_YUV_RANGE,
    ARG_VSTAB,
    ARG_DEVICE,
    ARG_LDC,
    ARG_NSF,
    ARG_MTIS,
    ARG_SENSOR_OVERCLOCK,
    ARG_WB_COLORTEMP,
    ARG_FOCUSSPOT_WEIGHT,
    ARG_SHARPNESS,
    ARG_CAC,
    ARG_GBCE,
    ARG_GLBCE,
#endif
};

/*
 * Camera initial values and limits
 */
#define DEFAULT_ZOOM_LEVEL          100
#define MIN_ZOOM_LEVEL              100
#define MAX_ZOOM_LEVEL              800
#define CAM_ZOOM_IN_STEP            65536
#define DEFAULT_FOCUS               OMX_IMAGE_FocusControlOff
#define DEFAULT_AWB                 OMX_WhiteBalControlOff
#define DEFAULT_EXPOSURE            OMX_ExposureControlOff
#define DEFAULT_CONTRAST_LEVEL      0
#define MIN_CONTRAST_LEVEL          -100
#define MAX_CONTRAST_LEVEL          100
#define DEFAULT_BRIGHTNESS_LEVEL    50
#define MIN_BRIGHTNESS_LEVEL        0
#define MAX_BRIGHTNESS_LEVEL        100
#define DEFAULT_ISO_LEVEL           0
#define MIN_ISO_LEVEL               0
#define MAX_ISO_LEVEL               1600
#define DEFAULT_ROTATION            180
#define DEFAULT_MIRROR              OMX_MirrorNone
#define MIN_SATURATION_VALUE        -100
#define MAX_SATURATION_VALUE        100
#define DEFAULT_SATURATION_VALUE    0
#define MIN_EXPOSURE_VALUE          -3.0
#define MAX_EXPOSURE_VALUE          3.0
#define DEFAULT_EXPOSURE_VALUE      0.0
#define MIN_MANUALFOCUS             0
#define MAX_MANUALFOCUS             100
#define DEFAULT_MANUALFOCUS         50
#define MIN_QFACTORJPEG             1
#define MAX_QFACTORJPEG             100
#define DEFAULT_QFACTORJPEG         75
#ifdef USE_OMXTICORE
#  define DEFAULT_THUMBNAIL_WIDTH   352
#  define DEFAULT_THUMBNAIL_HEIGHT  288
#  define MIN_THUMBNAIL_LEVEL       16
#  define MAX_THUMBNAIL_LEVEL       1920
#  define DEFAULT_FLICKER           OMX_FlickerCancelOff
#  define DEFAULT_SCENE             OMX_Manual
#  define DEFAULT_VNF               OMX_VideoNoiseFilterModeOn
#  define DEFAULT_YUV_RANGE         OMX_ITURBT601
#  define DEFAULT_DEVICE            OMX_PrimarySensor
#  define DEFAULT_NSF               OMX_ISONoiseFilterModeOff
#  define DEFAULT_WB_COLORTEMP_VALUE  5000
#  define MIN_WB_COLORTEMP_VALUE    2020
#  define MAX_WB_COLORTEMP_VALUE    7100
#  define DEFAULT_FOCUSSPOT_WEIGHT  OMX_FocusSpotDefault
#  define MIN_SHARPNESS_VALUE       -100
#  define MAX_SHARPNESS_VALUE       100
#  define DEFAULT_SHARPNESS_VALUE   0
#  define DEFAULT_GBCE              OMX_TI_BceModeOff
#  define DEFAULT_GLBCE             OMX_TI_BceModeOff
#endif


/*
 * Enums:
 */

#define GST_TYPE_OMX_CAMERA_MODE (gst_omx_camera_mode_get_type ())
GType gst_omx_camera_mode_get_type (void);


/*
 * OMX Structure wrappers
 */
#define GST_TYPE_OMX_CAMERA_SHUTTER (gst_omx_camera_shutter_get_type ())
GType gst_omx_camera_shutter_get_type (void);

#define GST_TYPE_OMX_CAMERA_FOCUS (gst_omx_camera_focus_get_type ())
GType gst_omx_camera_focus_get_type (void);

#define GST_TYPE_OMX_CAMERA_AWB (gst_omx_camera_awb_get_type ())
GType gst_omx_camera_awb_get_type (void);

#define GST_TYPE_OMX_CAMERA_EXPOSURE (gst_omx_camera_exposure_get_type ())
GType gst_omx_camera_exposure_get_type (void);

#define GST_TYPE_OMX_CAMERA_MIRROR (gst_omx_camera_mirror_get_type ())
GType gst_omx_camera_mirror_get_type (void);


#ifdef USE_OMXTICORE

#define GST_TYPE_OMX_CAMERA_FLICKER (gst_omx_camera_flicker_get_type ())
GType gst_omx_camera_flicker_get_type (void);

#define GST_TYPE_OMX_CAMERA_SCENE (gst_omx_camera_scene_get_type ())
GType gst_omx_camera_scene_get_type (void);

#define GST_TYPE_OMX_CAMERA_VNF (gst_omx_camera_vnf_get_type ())
GType gst_omx_camera_vnf_get_type (void);

#define GST_TYPE_OMX_CAMERA_YUV_RANGE (gst_omx_camera_yuv_range_get_type ())
GType gst_omx_camera_yuv_range_get_type (void);

#define GST_TYPE_OMX_CAMERA_DEVICE (gst_omx_camera_device_get_type ())
GType gst_omx_camera_device_get_type (void);

#define GST_TYPE_OMX_CAMERA_NSF (gst_omx_camera_nsf_get_type ())
GType gst_omx_camera_nsf_get_type (void);

#define GST_TYPE_OMX_CAMERA_FOCUSSPOT_WEIGHT (gst_omx_camera_focusspot_weight_get_type ())
GType gst_omx_camera_focusspot_weight_get_type (void);

#define GST_TYPE_OMX_CAMERA_BCE (gst_omx_camera_bce_get_type ())
GType gst_omx_camera_bce_get_type (void);

#endif

/*
 *  Methods:
 */
void set_camera_operating_mode (GstOmxCamera *self);
void set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
void get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
void install_camera_properties(GObjectClass *gobject_class);

