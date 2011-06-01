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

#include "gstomx_camera_parameters.h"
#include "gstomx.h"


#ifdef USE_OMXTICORE
#  include <OMX_TI_IVCommon.h>
#  include <OMX_TI_Index.h>
#endif

#define GST_USE_UNSTABLE_API TRUE

#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/timer-32k.h>
#include <OMX_CoreExt.h>
#include <OMX_IndexExt.h>
#include <omx/OMX_IVCommon.h>
#include <gst/interfaces/photography.h>

/*
 * Enums:
 */

GType
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

/*
 * OMX Structure wrappers
 */
GType
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

GType
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
#ifdef USE_OMXTICORE
            {OMX_IMAGE_FocusControlAutoMacro,         "AutoMacro",      "automacro"},
            {OMX_IMAGE_FocusControlAutoInfinity,      "AutoInfinity",   "autoinfinity"},
            {OMX_IMAGE_FocusControlHyperfocal,        "Hyperfocal",     "hyperfocal"},
            {OMX_IMAGE_FocusControlPortrait,          "Portrait",       "portrait"},
            {OMX_IMAGE_FocusControlExtended,          "Extended",       "extended"},
            {OMX_IMAGE_FocusControlContinousNormal,   "Cont-Normal",    "cont-normal"},
            {OMX_IMAGE_FocusControlContinousExtended, "Cont-Extended",  "cont-extended"},
#endif
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraFocus", vals);
    }

    return type;
}

GType
gst_omx_camera_awb_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static const GEnumValue vals[] =
        {
            {OMX_WhiteBalControlOff,           "Balance off",    "off"},
            {OMX_WhiteBalControlAuto,          "Auto balance",   "auto"},
            {OMX_WhiteBalControlSunLight,      "Sun light",      "sunlight"},
            {OMX_WhiteBalControlCloudy,        "Cloudy",         "cloudy"},
            {OMX_WhiteBalControlShade,         "Shade",          "shade"},
            {OMX_WhiteBalControlTungsten,      "Tungsten",       "tungsten"},
            {OMX_WhiteBalControlFluorescent,   "Fluorescent",    "fluorescent"},
            {OMX_WhiteBalControlIncandescent,  "Incandescent",   "incandescent"},
            {OMX_WhiteBalControlFlash,         "Flash",          "flash" },
            {OMX_WhiteBalControlHorizon,       "Horizon",        "horizon" },
            {0, NULL, NULL },
        };

        type = g_enum_register_static ("GstOmxCameraWhiteBalance",vals);
    }

    return type;
}

GType
gst_omx_camera_exposure_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static const GEnumValue vals[] =
        {
            {OMX_ExposureControlOff,             "Exposure control off",     "off"},
            {OMX_ExposureControlAuto,            "Auto exposure",            "auto"},
            {OMX_ExposureControlNight,           "Night exposure",           "night"},
            {OMX_ExposureControlBackLight,       "Backlight exposure",       "backlight"},
            {OMX_ExposureControlSpotLight,       "SportLight exposure",      "sportlight"},
            {OMX_ExposureControlSports,          "Sports exposure",          "sports"},
            {OMX_ExposureControlSnow,            "Snow exposure",            "snow"},
            {OMX_ExposureControlBeach,           "Beach exposure",           "beach"},
            {OMX_ExposureControlLargeAperture,   "Large aperture exposure",  "large-aperture"},
            {OMX_ExposureControlSmallApperture,  "Small aperture exposure",  "small-aperture"},
            {0, NULL, NULL },
        };

        type = g_enum_register_static ("GstOmxCameraExposureControl", vals);
    }

    return type;
}

GType
gst_omx_camera_mirror_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static GEnumValue vals[] =
        {
            {OMX_MirrorNone,        "Off",              "off"},
            {OMX_MirrorVertical,    "Vertical",         "vertical"},
            {OMX_MirrorHorizontal,  "Horizontal",       "horizontal"},
            {OMX_MirrorBoth,        "Both",             "both"},
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraMirror", vals);
    }

    return type;
}


#ifdef USE_OMXTICORE

GType
gst_omx_camera_flicker_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static const GEnumValue vals[] =
        {
            {OMX_FlickerCancelOff,  "Flicker control off",       "off"},
            {OMX_FlickerCancelAuto, "Auto flicker control",      "auto"},
            {OMX_FlickerCancel50,   "Flicker control for 50Hz",  "flick-50hz"},
            {OMX_FlickerCancel60,   "Flicker control for 60Hz",  "flick-60hz"},
            {0, NULL, NULL },
        };

        type = g_enum_register_static ("GstOmxCameraFlickerCancel", vals);
    }

    return type;
}

GType
gst_omx_camera_scene_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static const GEnumValue vals[] =
        {
            {OMX_Manual,         "Manual settings",         "manual"},
            {OMX_Closeup,        "Closeup settings",        "closeup"},
            {OMX_Portrait,       "Portrait settings",       "portrait"},
            {OMX_Landscape,      "Landscape settings",      "landscape"},
            {OMX_Underwater,     "Underwater settings",     "underwater"},
            {OMX_Sport,          "Sport settings",          "sport"},
            {OMX_SnowBeach,      "SnowBeach settings",      "snowbeach"},
            {OMX_Mood,           "Mood settings",           "mood"},
#if 0       /* The following options are not yet enabled at OMX level */
            {OMX_NightPortrait,  "NightPortrait settings",  "night-portrait"},
            {OMX_NightIndoor,    "NightIndoor settings",    "night-indoor"},
            {OMX_Fireworks,      "Fireworks settings",      "fireworks"},
            /* for still image: */
            {OMX_Document,       "Document settings",       "document"},
            {OMX_Barcode,        "Barcode settings",        "barcode"},
            /* for video: */
            {OMX_SuperNight,     "SuperNight settings",     "supernight"},
            {OMX_Cine,           "Cine settings",           "cine"},
            {OMX_OldFilm,        "OldFilm settings",        "oldfilm"},
#endif
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraScene", vals);
    }

    return type;
}

GType
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

GType
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

GType
gst_omx_camera_device_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static GEnumValue vals[] =
        {
            {OMX_PrimarySensor,     "Primary",          "primary"},
            {OMX_SecondarySensor,   "Secondary",        "secondary"},
            {OMX_TI_StereoSensor,   "Stereo",           "stereo"},
            {0, NULL, NULL},
        };

        type = g_enum_register_static ("GstOmxCameraDevice", vals);
    }

    return type;
}

GType
gst_omx_camera_nsf_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static const GEnumValue vals[] =
        {
            {OMX_ISONoiseFilterModeOff,     "nsf control off",    "off"},
            {OMX_ISONoiseFilterModeOn,      "nsf control on",     "on"},
            {OMX_ISONoiseFilterModeAuto,    "nsf control auto",   "auto"},
            {0, NULL, NULL },
        };

        type = g_enum_register_static ("GstOmxCameraISONoiseFilter", vals);
    }

    return type;
}

GType
gst_omx_camera_focusspot_weight_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static const GEnumValue vals[] =
        {
            {OMX_FocusSpotDefault,        "Common focus region",  "default"},
            {OMX_FocusSpotSinglecenter,   "Single center",        "center"},
            {OMX_FocusSpotMultiNormal,    "Multi normal",         "multinormal"},
            {OMX_FocusSpotMultiAverage,   "Multi average",        "multiaverage"},
            {OMX_FocusSpotMultiCenter,    "Multi center",         "multicenter"},
            {0, NULL, NULL },
        };

        type = g_enum_register_static ("GstOmxCameraFocusSpotWeight", vals);
    }

    return type;
}

GType
gst_omx_camera_bce_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        static const GEnumValue vals[] =
        {
            {OMX_TI_BceModeOff,     "bce control off",    "off"},
            {OMX_TI_BceModeOn,      "bce control on",     "on"},
            {OMX_TI_BceModeAuto,    "bce control auto",   "auto"},
            {0, NULL, NULL },
        };

        type = g_enum_register_static ("GstOmxCameraBrightnessContrastEnhance", vals);
    }

    return type;
}

#endif


/*
 *  Methods:
 */

GstPhotoCaps
gst_omx_camera_photography_get_capabilities (GstPhotography *photo)
{
  return GST_PHOTOGRAPHY_CAPS_EV_COMP |
         GST_PHOTOGRAPHY_CAPS_ISO_SPEED |
         GST_PHOTOGRAPHY_CAPS_WB_MODE |
         GST_PHOTOGRAPHY_CAPS_SCENE |
         GST_PHOTOGRAPHY_CAPS_ZOOM;
}

gboolean
gst_omx_camera_photography_get_ev_compensation (GstPhotography *photo,
        gfloat *evcomp)
{
    OMX_CONFIG_EXPOSUREVALUETYPE config;
    GOmxCore *gomx;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    gomx = (GOmxCore *) omx_base->gomx;
    _G_OMX_INIT_PARAM (&config);
    error_val = OMX_GetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonExposureValue, &config);
    g_assert (error_val == OMX_ErrorNone);
    GST_DEBUG_OBJECT (self, "xEVCompensation: EVCompensation=%d",
            config.xEVCompensation);

    return TRUE;
}

gboolean
gst_omx_camera_photography_get_iso_speed (GstPhotography *photo,
        guint *iso_speed)
{
    OMX_CONFIG_EXPOSUREVALUETYPE config;
    GOmxCore *gomx;
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (photo);
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);

    gomx = (GOmxCore *) omx_base->gomx;

    _G_OMX_INIT_PARAM (&config);
    error_val = OMX_GetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonExposureValue, &config);
    g_assert (error_val == OMX_ErrorNone);
    GST_DEBUG_OBJECT (self, "ISO Speed: param=%d", config.nSensitivity);
    *iso_speed = config.nSensitivity;

    return TRUE;
}

static void
gst_omx_camera_get_white_balance_mode (GstPhotography *photo,
        OMX_WHITEBALCONTROLTYPE *wb_mode)
{
    OMX_CONFIG_WHITEBALCONTROLTYPE config;
    GOmxCore *gomx;
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (photo);
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);

    gomx = (GOmxCore *) omx_base->gomx;

    _G_OMX_INIT_PARAM (&config);
    error_val = OMX_GetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonWhiteBalance,
            &config);
    g_assert (error_val == OMX_ErrorNone);
    config.nPortIndex = omx_base->out_port->port_index;
    GST_DEBUG_OBJECT (self, "AWB: param=%d", config.eWhiteBalControl);
    *wb_mode = config.eWhiteBalControl;
}

gboolean
gst_omx_camera_photography_get_white_balance_mode (GstPhotography *photo,
        GstWhiteBalanceMode *wb_mode)
{
    OMX_WHITEBALCONTROLTYPE omx_wb;
    gint convert_wb;

    gst_omx_camera_get_white_balance_mode (photo, &omx_wb);
    convert_wb = omx_wb - 1;
    if (convert_wb < 0 || convert_wb > 6)
        return FALSE;

    *wb_mode = convert_wb;
    return TRUE;
}

static void
gst_omx_camera_get_scene_mode (GstPhotography *photo,
        OMX_SCENEMODETYPE *scene_mode)
{
    OMX_CONFIG_SCENEMODETYPE config;
    GOmxCore *gomx;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    gomx = (GOmxCore *) omx_base->gomx;
    _G_OMX_INIT_PARAM (&config);
    error_val = OMX_GetConfig (gomx->omx_handle,
            OMX_TI_IndexConfigSceneMode, &config);
    g_assert (error_val == OMX_ErrorNone);
    GST_DEBUG_OBJECT (self, "Scene mode = %d", config.eSceneMode);
    *scene_mode = config.eSceneMode;
}

gboolean
gst_omx_camera_photography_get_scene_mode (GstPhotography *photo,
        GstSceneMode *scene_mode)
{
    OMX_SCENEMODETYPE scene_omx_camera;

    gst_omx_camera_get_scene_mode (photo, &scene_omx_camera);
    if (scene_omx_camera <= 3)
        *scene_mode = scene_omx_camera;
    else if (scene_omx_camera == 5)
        *scene_mode = GST_PHOTOGRAPHY_SCENE_MODE_SPORT;
    else
        /* scene does not exist in photography */
        return FALSE;

    return TRUE;
}

static void
gst_omx_camera_get_zoom (GstPhotography *photo, guint *zoom)
{
    OMX_CONFIG_SCALEFACTORTYPE zoom_scalefactor;
    GOmxCore *gomx;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    *zoom = 100;
    gomx = (GOmxCore *) omx_base->gomx;
    GST_DEBUG_OBJECT (self, "Get Property for zoom");
    _G_OMX_INIT_PARAM (&zoom_scalefactor);
    error_val = OMX_GetConfig (gomx->omx_handle,
                               OMX_IndexConfigCommonDigitalZoom,
                               &zoom_scalefactor);
    g_assert (error_val == OMX_ErrorNone);
}

gboolean
gst_omx_camera_photography_get_zoom (GstPhotography *photo, gfloat *zoom)
{
    guint zoom_int_value;

    gst_omx_camera_get_zoom (photo, &zoom_int_value);
    *zoom = zoom_int_value / 700.0 * 9.0;
    return TRUE;
}


gboolean
gst_omx_camera_photography_set_ev_compensation (GstPhotography *photo,
        gfloat evcomp)
{
    OMX_CONFIG_EXPOSUREVALUETYPE config;
    GOmxCore *gomx;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    gomx = (GOmxCore *) omx_base->gomx;
    _G_OMX_INIT_PARAM (&config);
    error_val = OMX_GetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonExposureValue, &config);
    g_assert (error_val == OMX_ErrorNone);
    /* Converting into Q16 ( X << 16  = X*65536 ) */
    config.xEVCompensation = (OMX_S32) (evcomp * 65536);
    GST_DEBUG_OBJECT (self, "xEVCompensation: value=%f EVCompensation=%d",
            evcomp, config.xEVCompensation);

    error_val = OMX_SetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonExposureValue, &config);
    g_assert (error_val == OMX_ErrorNone);

    return TRUE;
}

gboolean
gst_omx_camera_photography_set_iso_speed (GstPhotography *photo,
        guint iso_speed)
{
    OMX_CONFIG_EXPOSUREVALUETYPE config;
    GOmxCore *gomx;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    gomx = (GOmxCore *) omx_base->gomx;
    _G_OMX_INIT_PARAM (&config);
    error_val = OMX_GetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonExposureValue, &config);
    g_assert (error_val == OMX_ErrorNone);
    if (iso_speed > 1600)
      return FALSE;
    config.bAutoSensitivity = (iso_speed < 100) ? OMX_TRUE : OMX_FALSE;
    if (config.bAutoSensitivity == OMX_FALSE)
    {
        config.nSensitivity = iso_speed;
    }
    GST_DEBUG_OBJECT (self, "ISO Speed: Auto=%d Sensitivity=%d",
            config.bAutoSensitivity, config.nSensitivity);

    error_val = OMX_SetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonExposureValue, &config);
    g_assert (error_val == OMX_ErrorNone);

    return TRUE;
}

static void
gst_omx_camera_set_white_balance_mode (GstPhotography *photo,
        OMX_WHITEBALCONTROLTYPE wb_mode)
{
    OMX_CONFIG_WHITEBALCONTROLTYPE config;
    GOmxCore *gomx;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    gomx = (GOmxCore *) omx_base->gomx;
    _G_OMX_INIT_PARAM (&config);
    error_val = OMX_GetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonWhiteBalance,
            &config);
    g_assert (error_val == OMX_ErrorNone);
    config.nPortIndex = omx_base->out_port->port_index;

    config.eWhiteBalControl = wb_mode;

    GST_DEBUG_OBJECT (self, "AWB: param=%d",
            config.eWhiteBalControl,
            config.nPortIndex);

    error_val = OMX_SetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonWhiteBalance,
            &config);
    g_assert (error_val == OMX_ErrorNone);
}

gboolean
gst_omx_camera_photography_set_white_balance_mode (GstPhotography *photo,
        GstWhiteBalanceMode wb_mode)
{
    OMX_WHITEBALCONTROLTYPE wb_omx_camera;

    wb_omx_camera = wb_mode + 1;
    gst_omx_camera_set_white_balance_mode (photo, wb_omx_camera);
    return TRUE;
}

static void
gst_omx_camera_set_scene_mode (GstPhotography *photo,
        OMX_SCENEMODETYPE scene_mode)
{
    OMX_CONFIG_SCENEMODETYPE config;
    GOmxCore *gomx;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    gomx = (GOmxCore *) omx_base->gomx;
    _G_OMX_INIT_PARAM (&config);
    error_val = OMX_GetConfig (gomx->omx_handle,
            OMX_TI_IndexConfigSceneMode,
            &config);
    g_assert (error_val == OMX_ErrorNone);
    config.eSceneMode = scene_mode;
    GST_DEBUG_OBJECT (self, "Scene mode = %d",
            config.eSceneMode);

    error_val = OMX_SetConfig (gomx->omx_handle,
            OMX_TI_IndexConfigSceneMode,
            &config);
    g_assert (error_val == OMX_ErrorNone);
}

gboolean
gst_omx_camera_photography_set_scene_mode (GstPhotography *photo,
        GstSceneMode scene_mode)
{
    OMX_SCENEMODETYPE scene_omx_camera;

    if (scene_mode <= 3)
        scene_omx_camera = scene_mode;
    else if (scene_mode == GST_PHOTOGRAPHY_SCENE_MODE_SPORT)
        scene_omx_camera = 5;
    else
        /* scene does not exist in omx_camera */
        return FALSE;

    gst_omx_camera_set_scene_mode (photo, scene_omx_camera);
    return TRUE;
}

static void
gst_omx_camera_set_zoom (GstPhotography *photo, guint zoom)
{
    OMX_CONFIG_SCALEFACTORTYPE zoom_scalefactor;
    GOmxCore *gomx;
    OMX_U32 zoom_factor;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;
    GstOmxCamera *self = GST_OMX_CAMERA (photo);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);

    gomx = (GOmxCore *) omx_base->gomx;
    zoom_factor = (OMX_U32)((CAM_ZOOM_IN_STEP * zoom) / 100);
    GST_DEBUG_OBJECT (self, "Set Property for zoom factor = %d", zoom);

    _G_OMX_INIT_PARAM (&zoom_scalefactor);
    error_val = OMX_GetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonDigitalZoom, &zoom_scalefactor);
    g_assert (error_val == OMX_ErrorNone);
    GST_DEBUG_OBJECT (self, "OMX_GetConfig Successful for zoom");
    zoom_scalefactor.xWidth = (zoom_factor);
    zoom_scalefactor.xHeight = (zoom_factor);
    GST_DEBUG_OBJECT (self, "zoom_scalefactor = %d", zoom_scalefactor.xHeight);
    error_val = OMX_SetConfig (gomx->omx_handle,
            OMX_IndexConfigCommonDigitalZoom,
            &zoom_scalefactor);
    g_assert (error_val == OMX_ErrorNone);
    GST_DEBUG_OBJECT (self, "OMX_SetConfig Successful for zoom");
}

gboolean
gst_omx_camera_photography_set_zoom (GstPhotography *photo, gfloat zoom)
{
    guint zoom_int_value;

    zoom_int_value = abs (zoom * 900.0 / 7.0);
    gst_omx_camera_set_zoom (photo, zoom_int_value);
    return TRUE;
}

void
set_camera_operating_mode (GstOmxCamera *self)
{
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    OMX_CONFIG_CAMOPERATINGMODETYPE mode;
    GOmxCore *gomx;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;

    gomx = (GOmxCore *) omx_base->gomx;
    _G_OMX_INIT_PARAM (&mode);

    switch (self->next_mode)
    {
        case MODE_VIDEO:
            mode.eCamOperatingMode = OMX_CaptureVideo;
            break;
        case MODE_PREVIEW:
        case MODE_IMAGE:
            mode.eCamOperatingMode = OMX_CaptureImageProfileBase;
            break;
        case MODE_VIDEO_IMAGE:     /* @todo check this */
        case MODE_IMAGE_HS:
            mode.eCamOperatingMode =
                OMX_CaptureImageHighSpeedTemporalBracketing;
            break;
        default:
            g_assert_not_reached ();
    }
    GST_DEBUG_OBJECT (self, "OMX_CaptureImageMode: set = %d",
            mode.eCamOperatingMode);
    error_val = OMX_SetParameter (gomx->omx_handle,
            OMX_IndexCameraOperatingMode, &mode);
    g_assert (error_val == OMX_ErrorNone);
}


void
set_property (GObject *obj,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    GstOmxCamera *self = GST_OMX_CAMERA (obj);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    GstPhotography *photo = GST_PHOTOGRAPHY (self);

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
            gint32 zoom_value;
            zoom_value = g_value_get_int (value);
            gst_omx_camera_set_zoom (photo, zoom_value);

            break;
        }
        case ARG_FOCUS:
        {
            OMX_IMAGE_CONFIG_FOCUSCONTROLTYPE config;
            OMX_CONFIG_CALLBACKREQUESTTYPE focusreq_cb;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            _G_OMX_INIT_PARAM (&focusreq_cb);
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

            if (config.eFocusControl == OMX_IMAGE_FocusControlAutoLock)
                focusreq_cb.bEnable = OMX_TRUE;
            else
                focusreq_cb.bEnable = OMX_FALSE;

            if (omx_base->gomx->omx_state == OMX_StateExecuting)
            {
                guint32 autofocus_start_time;

                focusreq_cb.nPortIndex = OMX_ALL;
                focusreq_cb.nIndex = OMX_IndexConfigCommonFocusStatus;

                error_val = OMX_SetConfig (gomx->omx_handle,
                                           OMX_IndexConfigCallbackRequest,
                                           &focusreq_cb);
                g_assert (error_val == OMX_ErrorNone);
                GST_DEBUG_OBJECT (self, "AF_cb: enable=%d port=%d",
                                  focusreq_cb.bEnable, focusreq_cb.nPortIndex);

                if (config.eFocusControl == OMX_IMAGE_FocusControlAutoLock)
                {
                    GstStructure *structure = gst_structure_new ("omx_camera",
                            "auto-focus", G_TYPE_BOOLEAN, FALSE, NULL);

                    GstMessage *message = gst_message_new_element (
                            GST_OBJECT (self), structure);

                    gst_element_post_message (GST_ELEMENT (self), message);

                    autofocus_start_time = omap_32k_readraw ();
                    GST_CAT_INFO_OBJECT (gstomx_ppm, self,
                            "%d Autofocus started", autofocus_start_time);
                }
            }
            break;
        }
        case ARG_AWB:
        {
            OMX_WHITEBALCONTROLTYPE wb_enum_value;

            wb_enum_value = g_value_get_enum (value);
            gst_omx_camera_set_white_balance_mode (photo, wb_enum_value);
            break;
        }
        case ARG_WHITE_BALANCE:
        {
            GstWhiteBalanceMode wb_enum_value;

            wb_enum_value = g_value_get_enum (value);
            gst_omx_camera_photography_set_white_balance_mode (photo,
                                                               wb_enum_value);
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
        case ARG_EXPOSURE:
        {
            OMX_CONFIG_EXPOSURECONTROLTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonExposure,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            config.eExposureControl = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "Exposure control = %d",
                              config.eExposureControl);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonExposure,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_ISO:
        {
            OMX_U32 iso_requested;

            iso_requested = g_value_get_uint (value);
            gst_omx_camera_photography_set_iso_speed (photo, iso_requested);
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
        case ARG_SATURATION:
        {
            OMX_CONFIG_SATURATIONTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonSaturation, &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nSaturation = g_value_get_int (value);
            GST_DEBUG_OBJECT (self, "Saturation: param=%d", config.nSaturation);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonSaturation, &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_EXPOSUREVALUE:
        {
            gfloat exposure_float_value;
            exposure_float_value = g_value_get_float (value);

            gst_omx_camera_photography_set_ev_compensation (photo,
                    exposure_float_value);

            break;
        }
        case ARG_MANUALFOCUS:
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
            config.eFocusControl = OMX_IMAGE_FocusControlOn;
            config.nFocusSteps = g_value_get_uint (value);
            GST_DEBUG_OBJECT (self, "Manual AF: param=%d port=%d value=%d",
                              config.eFocusControl,
                              config.nPortIndex,
                              config.nFocusSteps);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigFocusControl, &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_QFACTORJPEG:
        {
            OMX_IMAGE_PARAM_QFACTORTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            param.nPortIndex = self->img_port->port_index;
            error_val = OMX_GetParameter (gomx->omx_handle,
                                          OMX_IndexParamQFactor, &param);
            GST_DEBUG_OBJECT (self, "Q Factor JPEG Error = %lu", error_val);
            g_assert (error_val == OMX_ErrorNone);
            param.nPortIndex = self->img_port->port_index;
            param.nQFactor = g_value_get_uint (value);
            GST_DEBUG_OBJECT (self, "Q Factor JPEG: port=%d value=%d",
                              param.nPortIndex,
                              param.nQFactor);

            error_val = OMX_SetParameter (gomx->omx_handle,
                                          OMX_IndexParamQFactor, &param);
            GST_DEBUG_OBJECT (self, "Q Factor JPEG Error = %lu", error_val);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
#ifdef USE_OMXTICORE
        case ARG_THUMBNAIL_WIDTH:
        {
            OMX_PARAM_THUMBNAILTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            param.nPortIndex = self->img_port->port_index;
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
            param.nPortIndex = self->img_port->port_index;
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
        case ARG_FLICKER:
        {
            OMX_CONFIG_FLICKERCANCELTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigFlickerCancel,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            config.eFlickerCancel = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "Flicker control = %d", config.eFlickerCancel);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigFlickerCancel,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_SCENE:
        {
            OMX_SCENEMODETYPE scene_enum;

            scene_enum = g_value_get_enum (value);
            gst_omx_camera_set_scene_mode (photo, scene_enum);
            break;
        }
        case ARG_SCENE_MODE:
        {
            GstSceneMode scene_enum;

            scene_enum = g_value_get_enum (value);
            gst_omx_camera_photography_set_scene_mode (photo, scene_enum);
            break;
        }
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
        case ARG_DEVICE:
        {
            OMX_CONFIG_SENSORSELECTTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigSensorSelect, &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nPortIndex = omx_base->out_port->port_index;
            config.eSensor = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "Device src=%d, port=%d", config.eSensor,
                              config.nPortIndex);
            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigSensorSelect, &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_LDC:
        {
            OMX_CONFIG_BOOLEANTYPE param;

            G_OMX_CORE_GET_PARAM (omx_base->gomx,
                                  OMX_IndexParamLensDistortionCorrection, &param);

            param.bEnabled = g_value_get_boolean (value);
            GST_DEBUG_OBJECT (self, "Lens Distortion Correction: param=%d",
                              param.bEnabled);
            G_OMX_CORE_SET_PARAM (omx_base->gomx,
                                  OMX_IndexParamLensDistortionCorrection, &param);
            break;
        }
        case ARG_NSF:
        {
            OMX_PARAM_ISONOISEFILTERTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            error_val = OMX_GetParameter (gomx->omx_handle,
                                          OMX_IndexParamHighISONoiseFiler,
                                          &param);
            g_assert (error_val == OMX_ErrorNone);
            param.eMode = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "ISO Noise Filter (NSF)=%d", param.eMode);
            error_val = OMX_SetParameter (gomx->omx_handle,
                                          OMX_IndexParamHighISONoiseFiler,
                                          &param);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_MTIS:
        {
            OMX_CONFIG_BOOLEANTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigMotionTriggeredImageStabilisation,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);

            config.bEnabled = g_value_get_boolean (value);
            GST_DEBUG_OBJECT (self, "Motion Triggered Image Stabilisation = %d",
                              config.bEnabled);
            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigMotionTriggeredImageStabilisation,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_SENSOR_OVERCLOCK:
        {
            OMX_CONFIG_BOOLEANTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            error_val = OMX_GetParameter (gomx->omx_handle,
                                          OMX_TI_IndexParamSensorOverClockMode,
                                          &param);
            g_assert (error_val == OMX_ErrorNone);

            param.bEnabled = g_value_get_boolean (value);
            GST_DEBUG_OBJECT (self, "Sensor OverClock Mode: param=%d",
                              param.bEnabled);
            error_val = OMX_SetParameter (gomx->omx_handle,
                                          OMX_TI_IndexParamSensorOverClockMode,
                                          &param);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_WB_COLORTEMP:
        {
            OMX_TI_CONFIG_WHITEBALANCECOLORTEMPTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigWhiteBalanceManualColorTemp,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nColorTemperature = g_value_get_uint (value);
            GST_DEBUG_OBJECT (self, "White balance color temperature = %d",
                              config.nColorTemperature);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigWhiteBalanceManualColorTemp,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_FOCUSSPOT_WEIGHT:
        {
            OMX_TI_CONFIG_FOCUSSPOTWEIGHTINGTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigFocusSpotWeighting,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            config.eMode = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "Focus spot weighting = %d", config.eMode);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigFocusSpotWeighting,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_SHARPNESS:
        {
            OMX_IMAGE_CONFIG_PROCESSINGLEVELTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigSharpeningLevel, &config);
            g_assert (error_val == OMX_ErrorNone);
            config.nPortIndex = omx_base->out_port->port_index;
            config.nLevel = g_value_get_int (value);
            if (config.nLevel == 0)
                config.bAuto = OMX_TRUE;
            else
                config.bAuto = OMX_FALSE;
            GST_DEBUG_OBJECT (self, "Sharpness: value=%d", config.nLevel);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_IndexConfigSharpeningLevel, &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_CAC:
        {
            OMX_CONFIG_BOOLEANTYPE param;

            G_OMX_CORE_GET_PARAM (omx_base->gomx,
                                  OMX_IndexConfigChromaticAberrationCorrection,
                                  &param);

            param.bEnabled = g_value_get_boolean (value);
            GST_DEBUG_OBJECT (self, "Chromatic Aberration Correction: param=%d",
                              param.bEnabled);
            G_OMX_CORE_SET_PARAM (omx_base->gomx,
                                  OMX_IndexConfigChromaticAberrationCorrection,
                                  &param);
            break;
        }
        case ARG_GBCE:
        {
            OMX_TI_CONFIG_LOCAL_AND_GLOBAL_BRIGHTNESSCONTRASTTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigGlobalBrightnessContrastEnhance,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            config.eControl = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "Global Brightness Contrast Enhance mode = %d",
                              config.eControl);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigGlobalBrightnessContrastEnhance,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            break;
        }
        case ARG_GLBCE:
        {
            OMX_TI_CONFIG_LOCAL_AND_GLOBAL_BRIGHTNESSCONTRASTTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigLocalBrightnessContrastEnhance,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            config.eControl = g_value_get_enum (value);
            GST_DEBUG_OBJECT (self, "Local Brightness Contrast Enhance mode = %d",
                              config.eControl);

            error_val = OMX_SetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigLocalBrightnessContrastEnhance,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
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

void
get_property (GObject *obj,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    GstOmxCamera *self = GST_OMX_CAMERA (obj);
    GstOmxBaseSrc *omx_base = GST_OMX_BASE_SRC (self);
    GstPhotography *photo = GST_PHOTOGRAPHY (self);

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
            guint zoom_int_value;
            gst_omx_camera_get_zoom (photo, &zoom_int_value);

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
            OMX_WHITEBALCONTROLTYPE wb_enum_value;

            gst_omx_camera_get_white_balance_mode (photo, &wb_enum_value);
            g_value_set_enum (value, wb_enum_value);
            break;
        }
        case ARG_WHITE_BALANCE:
        {
            GstWhiteBalanceMode wb_enum_value;

            gst_omx_camera_photography_get_white_balance_mode (photo,
                                                               &wb_enum_value);
            g_value_set_enum (value, wb_enum_value);
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
        case ARG_EXPOSURE:
        {
            OMX_CONFIG_EXPOSURECONTROLTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;
            gomx = (GOmxCore *) omx_base->gomx;

            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonExposure,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Exposure control = %d",
                              config.eExposureControl);
            g_value_set_enum (value, config.eExposureControl);

            break;
        }
        case ARG_ISO:
        {
            guint iso_uint_value;

            gst_omx_camera_photography_get_iso_speed (photo, &iso_uint_value);
            g_value_set_uint (value, iso_uint_value);
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
        case ARG_SATURATION:
        {
            OMX_CONFIG_SATURATIONTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigCommonSaturation, &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Saturation=%d", config.nSaturation);
            break;
        }
        case ARG_EXPOSUREVALUE:
        {
            gfloat float_value = 0;
            gst_omx_camera_photography_get_ev_compensation (photo,
                    &float_value);
            break;
        }
        case ARG_MANUALFOCUS:
        {
            OMX_IMAGE_CONFIG_FOCUSCONTROLTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigFocusControl, &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Manual AF: param=%d port=%d value=%d",
                              config.eFocusControl,
                              config.nPortIndex,
                              config.nFocusSteps);
            g_value_set_uint (value, config.nFocusSteps);
            break;
        }
        case ARG_QFACTORJPEG:
        {
            OMX_IMAGE_PARAM_QFACTORTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            param.nPortIndex = self->img_port->port_index;
            error_val = OMX_GetParameter (gomx->omx_handle,
                                          OMX_IndexParamQFactor, &param);
            GST_DEBUG_OBJECT (self, "Q Factor JPEG Error: port=%lu", error_val);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Q Factor JPEG: port=%d value=%d",
                              param.nPortIndex,
                              param.nQFactor);
            g_value_set_uint (value, param.nQFactor);
            break;
        }
#ifdef USE_OMXTICORE
        case ARG_THUMBNAIL_WIDTH:
        {
            OMX_PARAM_THUMBNAILTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            param.nPortIndex = self->img_port->port_index;
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
            param.nPortIndex = self->img_port->port_index;
            error_val = OMX_GetParameter(gomx->omx_handle,
                                       OMX_IndexParamThumbnail,
                                       &param);
            g_assert (error_val == OMX_ErrorNone);
            self->img_thumbnail_height = param.nHeight;
            GST_DEBUG_OBJECT (self, "Thumbnail height=%d",
                                    self->img_thumbnail_height);
            break;
        }
        case ARG_FLICKER:
        {
            OMX_CONFIG_FLICKERCANCELTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;
            gomx = (GOmxCore *) omx_base->gomx;

            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigFlickerCancel,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Flicker control = %d", config.eFlickerCancel);
            g_value_set_enum (value, config.eFlickerCancel);

            break;
        }
        case ARG_SCENE:
        {
            OMX_SCENEMODETYPE scene_enum;

            gst_omx_camera_get_scene_mode (photo, &scene_enum);
            g_value_set_enum (value, scene_enum);
            break;
        }
        case ARG_SCENE_MODE:
        {
            GstSceneMode scene_enum;

            gst_omx_camera_photography_get_scene_mode (photo, &scene_enum);
            g_value_set_enum (value, scene_enum);
            break;
        }
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
        case ARG_DEVICE:
        {
            OMX_CONFIG_SENSORSELECTTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigSensorSelect, &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Device src=%d", config.eSensor);
            g_value_set_enum (value, config.eSensor);

            break;
        }
        case ARG_LDC:
        {
            OMX_CONFIG_BOOLEANTYPE param;

            G_OMX_CORE_GET_PARAM (omx_base->gomx,
                                  OMX_IndexParamLensDistortionCorrection, &param);
            GST_DEBUG_OBJECT (self, "Lens Distortion Correction: param=%d",
                              param.bEnabled);
            g_value_set_boolean (value, param.bEnabled);
            break;
        }
        case ARG_NSF:
        {
            OMX_PARAM_ISONOISEFILTERTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            error_val = OMX_GetParameter (gomx->omx_handle,
                                          OMX_IndexParamHighISONoiseFiler,
                                          &param);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "ISO Noise Filter (NSF)=%d", param.eMode);
            g_value_set_enum (value, param.eMode);

            break;
        }
        case ARG_MTIS:
        {
            OMX_CONFIG_BOOLEANTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigMotionTriggeredImageStabilisation,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Motion Triggered Image Stabilisation = %d",
                              config.bEnabled);
            g_value_set_boolean (value, config.bEnabled);
            break;
        }
        case ARG_SENSOR_OVERCLOCK:
        {
            OMX_CONFIG_BOOLEANTYPE param;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&param);
            error_val = OMX_GetParameter (gomx->omx_handle,
                                          OMX_TI_IndexParamSensorOverClockMode,
                                          &param);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Sensor OverClock Mode: param=%d",
                              param.bEnabled);
            g_value_set_boolean (value, param.bEnabled);
            break;
        }
        case ARG_WB_COLORTEMP:
        {
            OMX_TI_CONFIG_WHITEBALANCECOLORTEMPTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigWhiteBalanceManualColorTemp,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "White balance color temperature = %d",
                              config.nColorTemperature);
            g_value_set_uint (value, config.nColorTemperature);
            break;
        }
        case ARG_FOCUSSPOT_WEIGHT:
        {
            OMX_TI_CONFIG_FOCUSSPOTWEIGHTINGTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigFocusSpotWeighting,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Focus spot weighting = %d", config.eMode);
            g_value_set_enum (value, config.eMode);
            break;
        }
        case ARG_SHARPNESS:
        {
            OMX_IMAGE_CONFIG_PROCESSINGLEVELTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);

            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_IndexConfigSharpeningLevel, &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Sharpness: value=%d  bAuto=%d",
                              config.nLevel, config.bAuto);
            g_value_set_int (value, config.nLevel);
            break;
        }
        case ARG_CAC:
        {
            OMX_CONFIG_BOOLEANTYPE param;

            G_OMX_CORE_GET_PARAM (omx_base->gomx,
                                  OMX_IndexConfigChromaticAberrationCorrection,
                                  &param);
            GST_DEBUG_OBJECT (self, "Chromatic Aberration Correction: param=%d",
                              param.bEnabled);
            g_value_set_boolean (value, param.bEnabled);
            break;
        }
        case ARG_GBCE:
        {
            OMX_TI_CONFIG_LOCAL_AND_GLOBAL_BRIGHTNESSCONTRASTTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigGlobalBrightnessContrastEnhance,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Global Brightness Contrast Enhance mode = %d",
                              config.eControl);
            g_value_set_enum (value, config.eControl);

            break;
        }
        case ARG_GLBCE:
        {
            OMX_TI_CONFIG_LOCAL_AND_GLOBAL_BRIGHTNESSCONTRASTTYPE config;
            GOmxCore *gomx;
            OMX_ERRORTYPE error_val = OMX_ErrorNone;

            gomx = (GOmxCore *) omx_base->gomx;
            _G_OMX_INIT_PARAM (&config);
            error_val = OMX_GetConfig (gomx->omx_handle,
                                       OMX_TI_IndexConfigLocalBrightnessContrastEnhance,
                                       &config);
            g_assert (error_val == OMX_ErrorNone);
            GST_DEBUG_OBJECT (self, "Local Brightness Contrast Enhance mode = %d",
                              config.eControl);
            g_value_set_enum (value, config.eControl);

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


void
install_camera_properties(GObjectClass *gobject_class)
{
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
    g_object_class_install_property (gobject_class, ARG_WHITE_BALANCE,
            g_param_spec_enum ("white-balance-mode",
                    "GstPhotography White Balance",
                    "Auto white balance state as defined in GstPhotography",
                    GST_TYPE_WHITE_BALANCE_MODE,
                    GST_PHOTOGRAPHY_WB_MODE_AUTO,
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
    g_object_class_install_property (gobject_class, ARG_EXPOSURE,
            g_param_spec_enum ("exposure", "Exposure Control",
                    "exposure control mode",
                    GST_TYPE_OMX_CAMERA_EXPOSURE,
                    DEFAULT_EXPOSURE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_ISO,
            g_param_spec_uint ("iso-speed", "ISO Speed",
                    "ISO speed level", MIN_ISO_LEVEL,
                    MAX_ISO_LEVEL, DEFAULT_ISO_LEVEL,
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
    g_object_class_install_property (gobject_class, ARG_SATURATION,
            g_param_spec_int ("saturation", "Saturation",
                    "Saturation level", MIN_SATURATION_VALUE,
                    MAX_SATURATION_VALUE, DEFAULT_SATURATION_VALUE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_EXPOSUREVALUE,
            g_param_spec_float ("exposure-value", "Exposure value",
                    "EVCompensation level", MIN_EXPOSURE_VALUE,
                    MAX_EXPOSURE_VALUE, DEFAULT_EXPOSURE_VALUE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_MANUALFOCUS,
            g_param_spec_uint ("manual-focus", "Manual Focus",
                    "Manual focus level, 0:Infinity  100:Macro",
                    MIN_MANUALFOCUS, MAX_MANUALFOCUS, DEFAULT_MANUALFOCUS,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_QFACTORJPEG,
            g_param_spec_uint ("qfactor", "Q Factor JPEG",
                    "JPEG Q Factor level, 1:Highest compression  100:Best quality",
                    MIN_QFACTORJPEG, MAX_QFACTORJPEG, DEFAULT_QFACTORJPEG,
                    G_PARAM_READWRITE));
#ifdef USE_OMXTICORE
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
    g_object_class_install_property (gobject_class, ARG_FLICKER,
            g_param_spec_enum ("flicker", "Flicker Control",
                    "flicker control state",
                    GST_TYPE_OMX_CAMERA_FLICKER,
                    DEFAULT_FLICKER,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_SCENE,
            g_param_spec_enum ("scene", "Scene Mode",
                    "Scene mode",
                    GST_TYPE_OMX_CAMERA_SCENE,
                    DEFAULT_SCENE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_SCENE_MODE,
            g_param_spec_enum ("scene-mode", "GstPhotography Scene Mode",
                    "Scene mode as in GstPhotography",
                    GST_TYPE_SCENE_MODE,
                    GST_PHOTOGRAPHY_SCENE_MODE_AUTO,
                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
    g_object_class_install_property (gobject_class, ARG_DEVICE,
            g_param_spec_enum ("device", "Camera sensor",
                    "Image and video stream source",
                    GST_TYPE_OMX_CAMERA_DEVICE,
                    DEFAULT_DEVICE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_LDC,
            g_param_spec_boolean ("ldc", "Lens Distortion Correction",
                    "Lens Distortion Correction state",
                    FALSE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_NSF,
            g_param_spec_enum ("nsf", "ISO noise suppression filter",
                    "low light environment noise filter",
                    GST_TYPE_OMX_CAMERA_NSF,
                    DEFAULT_NSF,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_MTIS,
            g_param_spec_boolean ("mtis", "Motion triggered image stabilisation mode",
                    "Motion triggered image stabilisation mode",
                    FALSE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_SENSOR_OVERCLOCK,
            g_param_spec_boolean ("overclock", "Sensor over-clock mode",
                    "Sensor over-clock mode",
                    FALSE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_WB_COLORTEMP,
            g_param_spec_uint ("wb-colortemp",
                    "White Balance Color Temperature",
                    "White balance color temperature", MIN_WB_COLORTEMP_VALUE,
                    MAX_WB_COLORTEMP_VALUE, DEFAULT_WB_COLORTEMP_VALUE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_FOCUSSPOT_WEIGHT,
            g_param_spec_enum ("focusweight", "Focus Spot Weight mode",
                    "Focus spot weight mode",
                    GST_TYPE_OMX_CAMERA_FOCUSSPOT_WEIGHT,
                    DEFAULT_FOCUSSPOT_WEIGHT,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_SHARPNESS,
            g_param_spec_int ("sharpness", "Sharpness value",
                    "Sharpness value, 0:automatic mode)", MIN_SHARPNESS_VALUE,
                    MAX_SHARPNESS_VALUE, DEFAULT_SHARPNESS_VALUE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_CAC,
            g_param_spec_boolean ("cac", "Chromatic Aberration Correction",
                    "Chromatic Aberration Correction state",
                    FALSE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_GBCE,
            g_param_spec_enum ("gbce", "Global Brightness Contrast Enhance",
                    "global brightness contrast enhance",
                    GST_TYPE_OMX_CAMERA_BCE,
                    DEFAULT_GBCE,
                    G_PARAM_READWRITE));
    g_object_class_install_property (gobject_class, ARG_GLBCE,
            g_param_spec_enum ("lbce", "Local Brightness Contrast Enhance",
                    "local brightness contrast enhance",
                    GST_TYPE_OMX_CAMERA_BCE,
                    DEFAULT_GLBCE,
                    G_PARAM_READWRITE));
#endif

}





