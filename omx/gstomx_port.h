/*
 * Copyright (C) 2006-2009 Texas Instruments, Incorporated
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

#ifndef GSTOMX_PORT_H
#define GSTOMX_PORT_H

#include <string.h> /* for memset, memcpy */
#include <gst/gst.h>

#include "gstomx_util.h"

/* Typedefs. */

typedef enum GOmxPortType GOmxPortType;

/* Enums. */

enum GOmxPortType
{
    GOMX_PORT_INPUT,
    GOMX_PORT_OUTPUT
};

struct GOmxPort
{
    GOmxCore *core;
    GOmxPortType type;

    guint num_buffers;
    guint port_index;
    OMX_BUFFERHEADERTYPE **buffers;

    GMutex *mutex;
    gboolean enabled;
    gboolean omx_allocate; /**< Setup with OMX_AllocateBuffer rather than OMX_UseBuffer */
    AsyncQueue *queue;

    GstBuffer * (*buffer_alloc)(GOmxPort *port, gint len); /**< allows elements to override shared buffer allocation for output ports */

    /** @todo this is a hack.. OpenMAX IL spec should be revised. */
    gboolean share_buffer;
};

/* Functions. */

GOmxPort *g_omx_port_new (GOmxCore *core, guint index);
void g_omx_port_free (GOmxPort *port);

#define G_OMX_PORT_GET_PARAM(port, idx, param) G_STMT_START {  \
		_G_OMX_INIT_PARAM (param);                         \
        (param)->nPortIndex = (port)->port_index;          \
        OMX_GetParameter (g_omx_core_get_handle ((port)->core), idx, (param)); \
    } G_STMT_END

#define G_OMX_PORT_SET_PARAM(port, idx, param)                      \
        OMX_SetParameter (                                          \
            g_omx_core_get_handle ((port)->core), idx, (param))

/* I think we can remove these two:
 */
#define g_omx_port_get_config(port, param) \
        G_OMX_PORT_GET_PARAM (port, OMX_IndexParamPortDefinition, param)

#define g_omx_port_set_config(port, param) \
        G_OMX_PORT_SET_PARAM (port, OMX_IndexParamPortDefinition, param)



void g_omx_port_setup (GOmxPort *port, OMX_PARAM_PORTDEFINITIONTYPE *omx_port);
void g_omx_port_allocate_buffers (GOmxPort *port);
void g_omx_port_free_buffers (GOmxPort *port);
void g_omx_port_start_buffers (GOmxPort *port);
void g_omx_port_resume (GOmxPort *port);
void g_omx_port_pause (GOmxPort *port);
void g_omx_port_flush (GOmxPort *port);
void g_omx_port_enable (GOmxPort *port);
void g_omx_port_disable (GOmxPort *port);
void g_omx_port_finish (GOmxPort *port);
void g_omx_port_push_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer);
gint g_omx_port_send (GOmxPort *port, gpointer obj);
gpointer g_omx_port_recv (GOmxPort *port);

/*
 * Some domain specific port related utility functions:
 */

#define GSTOMX_ALL_FORMATS  "{ I420, YUY2, UYVY }"

GstCaps * g_omx_port_set_video_formats (GOmxPort *port, GstCaps *caps);


#endif /* GSTOMX_PORT_H */
