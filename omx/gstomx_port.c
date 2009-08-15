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

#include "gstomx_util.h"
#include "gstomx.h"

GST_DEBUG_CATEGORY_EXTERN (gstomx_util_debug);


/*
 * Port
 */

GOmxPort *
g_omx_port_new (GOmxCore *core, guint index)
{
    GOmxPort *port;
    port = g_new0 (GOmxPort, 1);

    port->core = core;
    port->port_index = index;
    port->num_buffers = 0;
    port->buffer_size = 0;
    port->buffers = NULL;

    port->enabled = TRUE;
    port->queue = async_queue_new ();
    port->mutex = g_mutex_new ();

    return port;
}

void
g_omx_port_free (GOmxPort *port)
{
    g_mutex_free (port->mutex);
    async_queue_free (port->queue);

    g_free (port->buffers);
    g_free (port);
}

void
g_omx_port_setup (GOmxPort *port,
                  OMX_PARAM_PORTDEFINITIONTYPE *omx_port)
{
    GOmxPortType type = -1;

    switch (omx_port->eDir)
    {
        case OMX_DirInput:
            type = GOMX_PORT_INPUT;
            break;
        case OMX_DirOutput:
            type = GOMX_PORT_OUTPUT;
            break;
        default:
            break;
    }

    port->type = type;
    /** @todo should it be nBufferCountMin? */
    port->num_buffers = omx_port->nBufferCountActual;
    port->buffer_size = omx_port->nBufferSize;
    port->port_index = omx_port->nPortIndex;

    GST_DEBUG_OBJECT (port->core->object,
        "type=%d, num_buffers=%d, buffer_size=%d, port_index=%d",
        port->type, port->num_buffers, port->buffer_size, port->port_index);

    g_free (port->buffers);
    port->buffers = g_new0 (OMX_BUFFERHEADERTYPE *, port->num_buffers);
}

void
g_omx_port_allocate_buffers (GOmxPort *port)
{
    guint i;
    guint size;

    size = port->buffer_size;

    for (i = 0; i < port->num_buffers; i++)
    {

        if (port->omx_allocate)
        {
            GST_DEBUG_OBJECT (port->core->object, "%d: OMX_AllocateBuffer(), size=%d", i, size);
            OMX_AllocateBuffer (port->core->omx_handle,
                                &port->buffers[i],
                                port->port_index,
                                NULL,
                                size);
        }
        else
        {
            gpointer buffer_data;
            buffer_data = g_malloc (size);
            GST_DEBUG_OBJECT (port->core->object, "%d: OMX_UseBuffer(), size=%d", i, size);
            OMX_UseBuffer (port->core->omx_handle,
                           &port->buffers[i],
                           port->port_index,
                           NULL,
                           size,
                           buffer_data);
        }
    }
}

void
g_omx_port_free_buffers (GOmxPort *port)
{
    guint i;

    for (i = 0; i < port->num_buffers; i++)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer;

        omx_buffer = port->buffers[i];

        if (omx_buffer)
        {
#if 0
            /** @todo how shall we free that buffer? */
            if (!port->omx_allocate)
            {
                g_free (omx_buffer->pBuffer);
                omx_buffer->pBuffer = NULL;
            }
#endif

            OMX_FreeBuffer (port->core->omx_handle, port->port_index, omx_buffer);
            port->buffers[i] = NULL;
        }
    }
}

void
g_omx_port_start_buffers (GOmxPort *port)
{
    guint i;

    for (i = 0; i < port->num_buffers; i++)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer;

        omx_buffer = port->buffers[i];

        /* If it's an input port we will need to fill the buffer, so put it in
         * the queue, otherwise send to omx for processing (fill it up). */
        if (port->type == GOMX_PORT_INPUT)
            g_omx_core_got_buffer (port->core, port, omx_buffer);
        else
            g_omx_port_release_buffer (port, omx_buffer);
    }
}

void
g_omx_port_push_buffer (GOmxPort *port,
                        OMX_BUFFERHEADERTYPE *omx_buffer)
{
    async_queue_push (port->queue, omx_buffer);
}

OMX_BUFFERHEADERTYPE *
g_omx_port_request_buffer (GOmxPort *port)
{
    return async_queue_pop (port->queue);
}

void
g_omx_port_release_buffer (GOmxPort *port,
                           OMX_BUFFERHEADERTYPE *omx_buffer)
{
    switch (port->type)
    {
        case GOMX_PORT_INPUT:
            OMX_EmptyThisBuffer (port->core->omx_handle, omx_buffer);
            break;
        case GOMX_PORT_OUTPUT:
            OMX_FillThisBuffer (port->core->omx_handle, omx_buffer);
            break;
        default:
            break;
    }
}

void
g_omx_port_resume (GOmxPort *port)
{
    async_queue_enable (port->queue);
}

void
g_omx_port_pause (GOmxPort *port)
{
    async_queue_disable (port->queue);
}

void
g_omx_port_flush (GOmxPort *port)
{
    if (port->type == GOMX_PORT_OUTPUT)
    {
        /* This will get rid of any buffers that we have received, but not
         * yet processed in the output_loop.
         */
        OMX_BUFFERHEADERTYPE *omx_buffer;
        while ((omx_buffer = async_queue_pop_forced (port->queue)))
        {
            omx_buffer->nFilledLen = 0;
            g_omx_port_release_buffer (port, omx_buffer);
        }
    }

    OMX_SendCommand (port->core->omx_handle, OMX_CommandFlush, port->port_index, NULL);
    g_sem_down (port->core->flush_sem);
}

void
g_omx_port_enable (GOmxPort *port)
{
    GOmxCore *core;

    core = port->core;

    OMX_SendCommand (core->omx_handle, OMX_CommandPortEnable, port->port_index, NULL);
    g_omx_port_allocate_buffers (port);
    if (core->omx_state != OMX_StateLoaded)
        g_omx_port_start_buffers (port);
    g_omx_port_resume (port);

    g_sem_down (core->port_sem);
}

void
g_omx_port_disable (GOmxPort *port)
{
    GOmxCore *core;

    core = port->core;

    OMX_SendCommand (core->omx_handle, OMX_CommandPortDisable, port->port_index, NULL);
    g_omx_port_pause (port);
    g_omx_port_flush (port);
    g_omx_port_free_buffers (port);

    g_sem_down (core->port_sem);
}

void
g_omx_port_finish (GOmxPort *port)
{
    port->enabled = FALSE;
    async_queue_disable (port->queue);
}
