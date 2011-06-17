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

#include "gstomx_h264dec.h"
#include "gstomx.h"

GSTOMX_BOILERPLATE (GstOmxH264Dec, gst_omx_h264dec, GstOmxBaseVideoDec, GST_OMX_BASE_VIDEODEC_TYPE);

#define MIN_H264_TAG_SIZE    7


typedef enum
{
    NAL_UNKNOWN      = 0,
    NAL_SLICE        = 1,
    NAL_SLICE_DPA    = 2,
    NAL_SLICE_DPB    = 3,
    NAL_SLICE_DPC    = 4,
    NAL_SLICE_IDR    = 5,
    NAL_SEI          = 6,
    NAL_SPS          = 7,
    NAL_PPS          = 8,
    NAL_AU_DELIMITER = 9,
    NAL_SEQ_END      = 10,
    NAL_STREAM_END   = 11,
    NAL_FILTER_DATA  = 12
} GstNalUnitType;

typedef enum
{
    HEADER_UNKNOWN     = 0,
    BYTESTREAM_3BYTES  = 1,
    BYTESTREAM_4BYTES  = 2,
    NALU_3BYTES        = 3,
    NALU_4BYTES        = 4
} GstNalHeaderType;

typedef enum
{
    AVC_BASE_PROFILE      = 66,
    AVC_MAIN_PROFILE      = 77,
    AVC_EXTENDED_PROFILE  = 88,
    AVC_HIGH_PROFILE      = 100,
    AVC_HIGH_10_PROFILE   = 110,
    AVC_HIGH_422_PROFILE  = 122,
    AVC_HIGH_444_PROFILE  = 244
} GstVideoAVCProfileType;

typedef enum
{
    VIDEO_AVCLevel1b   = 9,    /**< Level 1b */
    VIDEO_AVCLevel1   = 10,    /**< Level 1 */
    VIDEO_AVCLevel11  = 11,    /**< Level 1.1 */
    VIDEO_AVCLevel12  = 12,    /**< Level 1.2 */
    VIDEO_AVCLevel13  = 13,    /**< Level 1.3 */
    VIDEO_AVCLevel2   = 20,    /**< Level 2 */
    VIDEO_AVCLevel21  = 21,    /**< Level 2.1 */
    VIDEO_AVCLevel22  = 22,    /**< Level 2.2 */
    VIDEO_AVCLevel3   = 30,    /**< Level 3 */
    VIDEO_AVCLevel31  = 31,    /**< Level 3.1 */
    VIDEO_AVCLevel32  = 32,    /**< Level 3.2 */
    VIDEO_AVCLevel4   = 40,    /**< Level 4 */
    VIDEO_AVCLevel41  = 41,    /**< Level 4.1 */
    VIDEO_AVCLevel42  = 42,    /**< Level 4.2 */
    VIDEO_AVCLevel5   = 50,    /**< Level 5 */
    VIDEO_AVCLevel51  = 51     /**< Level 5.1 */
} GstVideoAVCLevelType;

static GstCaps *
generate_sink_template (void)
{
    GstCaps *caps;
    GstStructure *struc;

    caps = gst_caps_new_empty ();

    struc = gst_structure_new ("video/x-h264",
                               "width", GST_TYPE_INT_RANGE, 16, 4096,
                               "height", GST_TYPE_INT_RANGE, 16, 4096,
                               "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
                               NULL);

    gst_caps_append_structure (caps, struc);

    return caps;
}

static void
gst_h264_configure (GstOmxH264Dec *self, gint profile, gint level)
{
    GstOmxBaseFilter *omx_base = GST_OMX_BASE_FILTER (self);
    OMX_VIDEO_PARAM_AVCTYPE param;
    GOmxCore *gomx;
    OMX_ERRORTYPE error_val = OMX_ErrorNone;

    gomx = (GOmxCore *) omx_base->gomx;
    _G_OMX_INIT_PARAM (&param);
    param.nPortIndex = omx_base->in_port->port_index;

    error_val = OMX_GetParameter (gomx->omx_handle,
                                  OMX_IndexParamVideoAvc,
                                  &param);
    g_assert (error_val == OMX_ErrorNone);

    switch(profile)
    {
        case AVC_BASE_PROFILE:
            param.eProfile = OMX_VIDEO_AVCProfileBaseline;
            break;
        case AVC_MAIN_PROFILE:
            param.eProfile = OMX_VIDEO_AVCProfileMain;
            break;
        case AVC_EXTENDED_PROFILE:
            param.eProfile = OMX_VIDEO_AVCProfileExtended;
            break;
        case AVC_HIGH_PROFILE:
            param.eProfile = OMX_VIDEO_AVCProfileHigh;
            break;
        case AVC_HIGH_10_PROFILE:
        case AVC_HIGH_422_PROFILE:
        case AVC_HIGH_444_PROFILE:
        default:
            /* Unsupported profiles by OMX.TI.DUCATI1.VIDEO.DECODER */
            GST_DEBUG_OBJECT (self, "profile code 0x%x %d not supported",
                              profile, profile);
    }

    switch(level)
    {
        case VIDEO_AVCLevel1b:
            param.eLevel = OMX_VIDEO_AVCLevel1b;
            break;
        case VIDEO_AVCLevel1:
            param.eLevel = OMX_VIDEO_AVCLevel1;
            break;
        case VIDEO_AVCLevel11:
            param.eLevel = OMX_VIDEO_AVCLevel11;
            break;
        case VIDEO_AVCLevel12:
            param.eLevel = OMX_VIDEO_AVCLevel12;
            break;
        case VIDEO_AVCLevel13:
            param.eLevel = OMX_VIDEO_AVCLevel13;
            break;
        case VIDEO_AVCLevel2:
            param.eLevel = OMX_VIDEO_AVCLevel2;
            break;
        case VIDEO_AVCLevel21:
            param.eLevel = OMX_VIDEO_AVCLevel21;
            break;
        case VIDEO_AVCLevel22:
            param.eLevel = OMX_VIDEO_AVCLevel22;
            break;
        case VIDEO_AVCLevel3:
            param.eLevel = OMX_VIDEO_AVCLevel3;
            break;
        case VIDEO_AVCLevel31:
            param.eLevel = OMX_VIDEO_AVCLevel31;
            break;
        case VIDEO_AVCLevel32:
            param.eLevel = OMX_VIDEO_AVCLevel32;
            break;
        case VIDEO_AVCLevel4:
            param.eLevel = OMX_VIDEO_AVCLevel4;
            break;
        case VIDEO_AVCLevel41:
            param.eLevel = OMX_VIDEO_AVCLevel41;
            break;
        case VIDEO_AVCLevel42:
            param.eLevel = OMX_VIDEO_AVCLevel42;
            break;
        case VIDEO_AVCLevel5:
            param.eLevel = OMX_VIDEO_AVCLevel5;
            break;
        case VIDEO_AVCLevel51:
            param.eLevel = OMX_VIDEO_AVCLevel51;
            break;
        default:
            /* Unsupported level value */
            GST_DEBUG_OBJECT (self, "level code 0x%x %d not supported",
                              level, level);
    }

    /* Setting stream AVC values*/
    error_val = OMX_SetParameter (gomx->omx_handle,
                                  OMX_IndexParamVideoAvc,
                                  &param);
    g_assert (error_val == OMX_ErrorNone);

    error_val = OMX_GetParameter (gomx->omx_handle,
                                  OMX_IndexParamVideoAvc,
                                  &param);
    g_assert (error_val == OMX_ErrorNone);
    GST_DEBUG_OBJECT (self, "H.264 component profile %d level %d",
                      param.eProfile, param.eLevel);
}

static GstFlowReturn
gst_h264_header_parse (GstOmxH264Dec *h264dec, GstBuffer *buf)
{
    GstFlowReturn res = GST_FLOW_OK;
    static GstNalHeaderType header_type = HEADER_UNKNOWN;
    guint8 *buffer;

    buffer = GST_BUFFER_DATA (buf);

    while (res == GST_FLOW_OK)
    {
        gint nal_offset = 0, nal_offset2 = 0;
        gint index = 0, header_offset = 0;
        gint profile = 0, level = 0;

        if (header_offset == 0)
        {
            if (GST_BUFFER_SIZE (buffer) < MIN_H264_TAG_SIZE)
            {
                GST_DEBUG_OBJECT (h264dec, "Buffer too small to find SPS");
                res = GST_FLOW_ERROR;
                break;
            }

            nal_offset =  (buffer[index + 2] << 8) & buffer[index + 3];
            if (nal_offset > GST_BUFFER_SIZE (buffer))
                nal_offset = 0;

            nal_offset2 = (buffer[index + 1] << 8) & buffer[index + 2];
            if (nal_offset2 > GST_BUFFER_SIZE (buffer))
                nal_offset2 = 0;

            /* Header */
            if (buffer[index] == 0 && buffer[index + 1] == 0 &&
                buffer[index + 2] == 0 &&
                buffer[index + 3] == 1)
            {
                header_type = BYTESTREAM_4BYTES;
                header_offset = 4;
            }
            else if (buffer[index]     == 0 &&
                     buffer[index + 1] == 0 &&
                     buffer[index + 2] == 1)
            {
                if ((nal_offset >= MIN_H264_TAG_SIZE) &&
                    (buffer [index + (nal_offset) + 4] == 0) &&
                    (buffer [index + (nal_offset) + 5] == 0))
                {
                    /* NAL size < 65535 */
                    header_type = NALU_4BYTES;
                    header_offset = 4;
                }
                else
                {
                    header_type = BYTESTREAM_3BYTES;
                    header_offset = 3;
                }
            }
            else if ((buffer[index] == 0) && (nal_offset2 >= MIN_H264_TAG_SIZE))
            {
                if ((buffer [index + (nal_offset2) + 3] == 0))
                {
                    header_type = NALU_3BYTES;
                    header_offset = 3;
                }
                else
                {
                    header_type = NALU_4BYTES;
                    header_offset = 4;
                }
            }
        }

        GST_DEBUG_OBJECT (h264dec, "Header size %d and type %d", header_offset,
                          header_type);

        index += header_offset;

        /* Figure out if this is a delta unit */
        {
            GstNalUnitType nal_type;
            gint nal_ref_idc;

            nal_type = (buffer[index] & 0x1f);
            nal_ref_idc = (buffer[index] & 0x60) >> 5;

            GST_DEBUG_OBJECT (h264dec, "NAL type: %d, ref_idc: %d", nal_type,
                              nal_ref_idc);

            /* Parse AVC info in case of right frame type */
            switch (nal_type)
            {
                case NAL_SPS:
                    GST_DEBUG_OBJECT (h264dec, "we have an SPS NAL");
                    index++;
                    /* profile ID */
                    profile = buffer[index++];
                    /* TODO: read compatible profiles */
                    index++;
                    /* level */
                    level = buffer[index++];
                    GST_DEBUG_OBJECT (h264dec, "H.264 ProfileID=%d, Level=%d",
                                      profile, level);

                    gst_h264_configure (h264dec, profile, level);
                    res = GST_FLOW_CUSTOM_SUCCESS;
                    /* gst_nal_decode_sps (h264dec, &bs); */
                    break;
                default:
                    GST_DEBUG_OBJECT (h264dec,"NAL type = %d encountered "
                                      "but not parsed", nal_type);
                    /* Calculate new offset or search for new header for
                       new tab */
                    res = GST_FLOW_NOT_NEGOTIATED;
            }
        }
    }
    return res;
}

static GstFlowReturn
pad_chain (GstPad *pad,
           GstBuffer *buf)
{
    GstOmxH264Dec *self;
    GstFlowReturn ret = GST_FLOW_OK;
    static GstFlowReturn avc_configured = GST_FLOW_OK;

    self = GST_OMX_H264DEC (GST_OBJECT_PARENT (pad));

    PRINT_BUFFER (self, buf);

    GST_LOG_OBJECT (self, "Begin: size=%u ", GST_BUFFER_SIZE (buf));

    if (avc_configured == GST_FLOW_OK)
    {
        avc_configured = gst_h264_header_parse (self, buf);
        if (avc_configured == GST_FLOW_CUSTOM_SUCCESS)
            GST_DEBUG_OBJECT (self, "AVC parameters updated");
        else
            GST_DEBUG_OBJECT (self, "AVC parameters not updated");
    }

    ret = GST_OMX_BASE_FILTER_CLASS (parent_class)->pad_chain (pad, buf);

    if (ret == GST_STATE_CHANGE_FAILURE)
        GST_LOG_OBJECT (self, "end");

    return ret;
}

static void
type_base_init (gpointer g_class)
{
    GstElementClass *element_class;

    element_class = GST_ELEMENT_CLASS (g_class);

    {
        GstElementDetails details;

        details.longname = "OpenMAX IL H.264/AVC video decoder";
        details.klass = "Codec/Decoder/Video";
        details.description = "Decodes video in H.264/AVC format with OpenMAX IL";
        details.author = "Felipe Contreras";

        gst_element_class_set_details (element_class, &details);
    }

    {
        GstPadTemplate *template;

        template = gst_pad_template_new ("sink", GST_PAD_SINK,
                                         GST_PAD_ALWAYS,
                                         generate_sink_template ());

        gst_element_class_add_pad_template (element_class, template);
    }
}

static void
type_class_init (gpointer g_class,
                 gpointer class_data)
{
    GObjectClass *gobject_class;
    GstOmxBaseFilterClass *bclass;

    gobject_class = G_OBJECT_CLASS (g_class);
    bclass = GST_OMX_BASE_FILTER_CLASS (g_class);

    bclass->pad_chain = pad_chain;

}

static void
type_instance_init (GTypeInstance *instance,
                    gpointer g_class)
{
    GstOmxBaseVideoDec *omx_base;

    omx_base = GST_OMX_BASE_VIDEODEC (instance);

    omx_base->compression_format = OMX_VIDEO_CodingAVC;
}
