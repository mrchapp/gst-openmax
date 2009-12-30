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

#include <string.h>

#include "gstomx_util.h"
#include "gstomx_port.h"
#include "gstomx.h"

GST_DEBUG_CATEGORY_EXTERN (gstomx_util_debug);

#define CODEC_DATA_FLAG 0x00000080 /* special nFlags field to use to indicated codec-data */

static OMX_BUFFERHEADERTYPE * request_buffer (GOmxPort *port);
static void release_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer);

#define DEBUG(port, fmt, args...) \
    GST_DEBUG ("<%s:%s> "fmt, GST_OBJECT_NAME ((port)->core->object), (port)->name, ##args)
#define LOG(port, fmt, args...) \
    GST_LOG ("<%s:%s> "fmt, GST_OBJECT_NAME ((port)->core->object), (port)->name, ##args)
#define WARNING(port, fmt, args...) \
    GST_WARNING ("<%s:%s> "fmt, GST_OBJECT_NAME ((port)->core->object), (port)->name, ##args)

/*
 * Port
 */

GOmxPort *
g_omx_port_new (GOmxCore *core, const gchar *name, guint index)
{
    GOmxPort *port = g_new0 (GOmxPort, 1);

    port->core = core;
    port->name = g_strdup_printf ("%s:%d", name, index);
    port->port_index = index;
    port->num_buffers = 0;
    port->buffers = NULL;

    port->enabled = TRUE;
    port->queue = async_queue_new ();
    port->mutex = g_mutex_new ();

    return port;
}

void
g_omx_port_free (GOmxPort *port)
{
    DEBUG (port, "begin");

    g_mutex_free (port->mutex);
    async_queue_free (port->queue);

    g_free (port->name);

    g_free (port->buffers);
    g_free (port);

    GST_DEBUG ("end");
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
    port->port_index = omx_port->nPortIndex;

    DEBUG (port, "type=%d, num_buffers=%d, port_index=%d",
        port->type, port->num_buffers, port->port_index);

    /* I don't think it is valid for buffers to be allocated at this point..
     * if there is a case where it is, then call g_omx_port_free_buffers()
     * here instead:
     */
    g_return_if_fail (!port->buffers);
}

static GstBuffer *
buffer_alloc (GOmxPort *port, gint len)
{
    GstBuffer *buf = NULL;

    if (port->buffer_alloc)
        buf = port->buffer_alloc (port, len);

    if (!buf)
        buf = gst_buffer_new_and_alloc (len);

    return buf;
}


/**
 * Ensure that srcpad caps are set before beginning transition-to-idle or
 * transition-to-loaded.  This is a bit ugly, because it requires pad-alloc'ing
 * a buffer from the downstream element for no particular purpose other than
 * triggering upstream caps negotiation from the sink..
 */
void
g_omx_port_prepare (GOmxPort *port)
{
    OMX_PARAM_PORTDEFINITIONTYPE param;
    GstBuffer *buf;
    GstCaps *caps;
    guint size;

    DEBUG (port, "begin");

    G_OMX_PORT_GET_DEFINITION (port, &param);
    size = param.nBufferSize;

    buf = buffer_alloc (port, size);
    caps = GST_BUFFER_CAPS (buf);

    /* the buffer_alloc() could have triggered srccaps to be set, so don't
     * trust that the original port params are still valid:
     */
    G_OMX_PORT_GET_DEFINITION (port, &param);
    size = param.nBufferSize;

    if (caps)
    {
        GstStructure *s;
        gint cnt;

        g_warn_if_fail (gst_caps_is_fixed (caps));

        s = gst_caps_get_structure (caps, 0);

        if (gst_structure_get_int (s, "buffer-count-actual", &cnt))
        {
            port->num_buffers = param.nBufferCountActual = cnt;
            port->omx_allocate = FALSE;
            port->share_buffer = 2;
            DEBUG (port, "buffer allocator (sink) supports OMX compliant (non pBuffer swapping) buffer sharing mode");
            DEBUG (port, "nBufferCountActual: %d", param.nBufferCountActual);
            G_OMX_PORT_SET_DEFINITION (port, &param);
        }
    }

    if (GST_BUFFER_SIZE (buf) != size)
    {
        DEBUG (port, "buffer sized changed, %d->%d",
                size, GST_BUFFER_SIZE (buf));
        param.nBufferSize = GST_BUFFER_SIZE (buf);
        G_OMX_PORT_SET_DEFINITION (port, &param);
    }

    gst_buffer_unref (buf);

    DEBUG (port, "end");
}

void
g_omx_port_allocate_buffers (GOmxPort *port)
{
    OMX_PARAM_PORTDEFINITIONTYPE param;
    guint i;
    guint size;

    if (port->buffers)
        return;

    DEBUG (port, "begin");

    G_OMX_PORT_GET_DEFINITION (port, &param);
    size = param.nBufferSize;

    port->buffers = g_new0 (OMX_BUFFERHEADERTYPE *, port->num_buffers);

    for (i = 0; i < port->num_buffers; i++)
    {

        if (port->omx_allocate)
        {
            DEBUG (port, "%d: OMX_AllocateBuffer(), size=%d", i, size);
            OMX_AllocateBuffer (port->core->omx_handle,
                                &port->buffers[i],
                                port->port_index,
                                NULL,
                                size);

            g_return_if_fail (port->buffers[i]);
        }
        else
        {
            GstBuffer *buf = NULL;
            gpointer buffer_data;
            if (port->share_buffer)
            {
                buf = buffer_alloc (port, size);
                buffer_data = GST_BUFFER_DATA (buf);
            }
            else
            {
                buffer_data = g_malloc (size);
            }

            DEBUG (port, "%d: OMX_UseBuffer(), size=%d, share_buffer=%d", i, size, port->share_buffer);
            OMX_UseBuffer (port->core->omx_handle,
                           &port->buffers[i],
                           port->port_index,
                           NULL,
                           size,
                           buffer_data);

            g_return_if_fail (port->buffers[i]);

            if (port->share_buffer)
            {
                port->buffers[i]->pAppPrivate = buf;
                port->buffers[i]->pBuffer     = GST_BUFFER_DATA (buf);
                port->buffers[i]->nAllocLen   = GST_BUFFER_SIZE (buf);
                port->buffers[i]->nOffset     = 0;
            }
        }
    }

    DEBUG (port, "end");
}

void
g_omx_port_free_buffers (GOmxPort *port)
{
    guint i;

    if (!port->buffers)
        return;

    DEBUG (port, "begin");

    for (i = 0; i < port->num_buffers; i++)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer;

        /* pop the buffer, to be sure that it has been returned from the
         * OMX component, to avoid freeing a buffer that the component
         * is still accessing:
         */
        omx_buffer = async_queue_pop_full (port->queue, TRUE, TRUE);

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

            DEBUG (port, "OMX_FreeBuffer(%p)", omx_buffer);
            OMX_FreeBuffer (port->core->omx_handle, port->port_index, omx_buffer);
            port->buffers[i] = NULL;
        }
    }

    g_free (port->buffers);
    port->buffers = NULL;

    DEBUG (port, "end");
}

void
g_omx_port_start_buffers (GOmxPort *port)
{
    guint i;

    if (!port->enabled)
        return;

    g_return_if_fail (port->buffers);

    DEBUG (port, "begin");

    for (i = 0; i < port->num_buffers; i++)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer;

        omx_buffer = port->buffers[i];

        /* If it's an input port we will need to fill the buffer, so put it in
         * the queue, otherwise send to omx for processing (fill it up). */
        if (port->type == GOMX_PORT_INPUT)
        {
            g_omx_core_got_buffer (port->core, port, omx_buffer);
        }
        else
        {
            if ((port->share_buffer == 2) && (i >= port->num_buffers-3))
            {
                GstBuffer *buf = port->buffers[i]->pAppPrivate;
//                port->buffers[i]->pAppPrivate = NULL;
                gst_buffer_unref (buf);
            }
            else
            {
                release_buffer (port, omx_buffer);
            }
        }
    }

    DEBUG (port, "end");
}

void
g_omx_port_push_buffer (GOmxPort *port,
                        OMX_BUFFERHEADERTYPE *omx_buffer)
{
    async_queue_push (port->queue, omx_buffer);
}

static OMX_BUFFERHEADERTYPE *
request_buffer (GOmxPort *port)
{
    LOG (port, "request buffer");
    return async_queue_pop (port->queue);
}

static void
release_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer)
{
    switch (port->type)
    {
        case GOMX_PORT_INPUT:
            DEBUG (port, "ETB: omx_buffer=%p, pAppPrivate=%p, pBuffer=%p",
                    omx_buffer, omx_buffer ? omx_buffer->pAppPrivate : 0, omx_buffer ? omx_buffer->pBuffer : 0);
            OMX_EmptyThisBuffer (port->core->omx_handle, omx_buffer);
            break;
        case GOMX_PORT_OUTPUT:
            DEBUG (port, "FTB: omx_buffer=%p, pAppPrivate=%p, pBuffer=%p",
                    omx_buffer, omx_buffer ? omx_buffer->pAppPrivate : 0, omx_buffer ? omx_buffer->pBuffer : 0);
            OMX_FillThisBuffer (port->core->omx_handle, omx_buffer);
            break;
        default:
            break;
    }
}

/* NOTE ABOUT BUFFER SHARING:
 *
 * Buffer sharing is a sort of "extension" to OMX to allow zero copy buffer
 * passing between GST and OMX.
 *
 * There are only two cases:
 *
 * 1) shared_buffer is enabled, in which case we control nOffset, and use
 *    pAppPrivate to store the reference to the original GstBuffer that
 *    pBuffer ptr is copied from.  Note that in case of input buffers,
 *    the DSP/coprocessor should treat the buffer as read-only so cache-
 *    line alignment is not an issue.  For output buffers which are not
 *    pad_alloc()d, some care may need to be taken to ensure proper buffer
 *    alignment.
 * 2) shared_buffer is not enabled, in which case we respect the nOffset
 *    set by the component and pAppPrivate is NULL
 *
 */

typedef void (*SendPrep) (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer, gpointer obj);

static void
send_prep_codec_data (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer, GstBuffer *buf)
{
    omx_buffer->nFlags |= CODEC_DATA_FLAG;
    omx_buffer->nFilledLen = GST_BUFFER_SIZE (buf);

    if (port->share_buffer)
    {
        omx_buffer->nOffset = 0;
        omx_buffer->pBuffer = malloc (omx_buffer->nFilledLen);
    }

    memcpy (omx_buffer->pBuffer + omx_buffer->nOffset,
            GST_BUFFER_DATA (buf), omx_buffer->nFilledLen);
}

static void
send_prep_buffer_data (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer, GstBuffer *buf)
{
    if (port->share_buffer)
    {
        omx_buffer->nOffset     = 0;
        omx_buffer->pBuffer     = GST_BUFFER_DATA (buf);
        omx_buffer->nFilledLen  = GST_BUFFER_SIZE (buf);
        omx_buffer->nAllocLen   = GST_BUFFER_SIZE (buf);
        omx_buffer->pAppPrivate = gst_buffer_ref (buf);
    }
    else
    {
        omx_buffer->nFilledLen = MIN (GST_BUFFER_SIZE (buf),
                omx_buffer->nAllocLen - omx_buffer->nOffset);
        DEBUG (port, "begin evil memcpy of %d bytes", omx_buffer->nFilledLen);
        memcpy (omx_buffer->pBuffer + omx_buffer->nOffset,
                GST_BUFFER_DATA (buf), omx_buffer->nFilledLen);
        DEBUG (port, "done memcpy");
    }

    if (port->core->use_timestamps)
    {
        omx_buffer->nTimeStamp = gst_util_uint64_scale_int (
                GST_BUFFER_TIMESTAMP (buf),
                OMX_TICKS_PER_SECOND, GST_SECOND);
    }

    DEBUG (port, "omx_buffer: size=%lu, len=%lu, flags=%lu, offset=%lu, timestamp=%lld",
            omx_buffer->nAllocLen, omx_buffer->nFilledLen, omx_buffer->nFlags,
            omx_buffer->nOffset, omx_buffer->nTimeStamp);
}

static void
send_prep_eos_event (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer, GstEvent *evt)
{
    omx_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
    omx_buffer->nFilledLen = 0;
    omx_buffer->nAllocLen  = 0;
    /* OMX should not try to read from the buffer, since it is empty.. but yet
     * it complains if pBuffer is NULL.  This will get us past that check, and
     * ensure that OMX segfaults in a debuggible way if they do something
     * stupid like read from the empty buffer:
     */
    omx_buffer->pBuffer    = (OMX_U8 *)1;
}

/**
 * Send a buffer/event to the OMX component.  This handles conversion of
 * GST buffer, codec-data, and EOS events to the equivalent OMX buffer.
 *
 * This method does not take ownership of the ref to @obj
 *
 * Returns number of bytes sent, or negative if error
 */
gint
g_omx_port_send (GOmxPort *port, gpointer obj)
{
    SendPrep send_prep = NULL;

    g_return_val_if_fail (port->type == GOMX_PORT_INPUT, -1);

    if (GST_IS_BUFFER (obj))
    {
        if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (obj, GST_BUFFER_FLAG_IN_CAPS)))
            send_prep = (SendPrep)send_prep_codec_data;
        else
            send_prep = (SendPrep)send_prep_buffer_data;
    }
    else if (GST_IS_EVENT (obj))
    {
        if (G_LIKELY (GST_EVENT_TYPE (obj) == GST_EVENT_EOS))
            send_prep = (SendPrep)send_prep_eos_event;
    }

    if (G_LIKELY (send_prep))
    {
        gint ret;
        OMX_BUFFERHEADERTYPE *omx_buffer = request_buffer (port);

        if (!omx_buffer)
        {
            DEBUG (port, "null buffer");
            return -1;
        }

        /* if buffer sharing is enabled, pAppPrivate might hold the ref to
         * a buffer that is no longer required and should be unref'd.  We
         * do this check here, rather than in send_prep_buffer_data() so
         * we don't keep the reference live in case, for example, this time
         * the buffer is used for an EOS event.
         */
        if (omx_buffer->pAppPrivate)
        {
            GstBuffer *old_buf = omx_buffer->pAppPrivate;
            gst_buffer_unref (old_buf);
            omx_buffer->pAppPrivate = NULL;
            omx_buffer->pBuffer = NULL;     /* just to ease debugging */
        }

        send_prep (port, omx_buffer, obj);

        ret = omx_buffer->nFilledLen;

        release_buffer (port, omx_buffer);

        return ret;
    }

    WARNING (port, "unknown obj type");
    return -1;
}

/**
 * Receive a buffer/event from OMX component.  This handles the conversion
 * of OMX buffer to GST buffer, codec-data, or EOS event.
 *
 * Returns <code>NULL</code> if buffer could not be received.
 */
gpointer
g_omx_port_recv (GOmxPort *port)
{
    gpointer ret = NULL;

    g_return_val_if_fail (port->type == GOMX_PORT_OUTPUT, NULL);

    while (!ret && port->enabled)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer = request_buffer (port);

        if (G_UNLIKELY (!omx_buffer))
        {
            return NULL;
        }

        DEBUG (port, "omx_buffer: size=%lu, len=%lu, flags=%lu, offset=%lu, timestamp=%lld",
                omx_buffer->nAllocLen, omx_buffer->nFilledLen, omx_buffer->nFlags,
                omx_buffer->nOffset, omx_buffer->nTimeStamp);

        if (G_UNLIKELY (omx_buffer->nFlags & OMX_BUFFERFLAG_EOS))
        {
            DEBUG (port, "got eos");
            ret = gst_event_new_eos ();
        }
        else if (G_LIKELY (omx_buffer->nFilledLen > 0))
        {
            GstBuffer *buf = omx_buffer->pAppPrivate;

            /* I'm not really sure if it was intentional to block zero-copy of
             * the codec-data buffer.. this is how the original code worked,
             * so I kept the behavior
             */
            if (!buf || (omx_buffer->nFlags & CODEC_DATA_FLAG))
            {
                if (buf)
                    gst_buffer_unref (buf);

                /* this probably won't work for share_buffers==2 case..
                 * but this is only for encoders anyways..
                 */
                buf = buffer_alloc (port, omx_buffer->nFilledLen);
                DEBUG (port, "begin evil memcpy of %d bytes", omx_buffer->nFilledLen);
                memcpy (GST_BUFFER_DATA (buf),
                        omx_buffer->pBuffer + omx_buffer->nOffset,
                        omx_buffer->nFilledLen);
                DEBUG (port, "done memcpy");
            }
            else if (buf)
            {
                /* don't rely on OMX having told us the correct buffer size
                 * when we allocated the buffer.
                 */
                GST_BUFFER_SIZE (buf) = omx_buffer->nFilledLen;
            }

            if (port->core->use_timestamps)
            {
                GST_BUFFER_TIMESTAMP (buf) = gst_util_uint64_scale_int (
                        omx_buffer->nTimeStamp,
                        GST_SECOND, OMX_TICKS_PER_SECOND);
            }

            if (G_UNLIKELY (omx_buffer->nFlags & CODEC_DATA_FLAG))
            {
                GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
            }

            ret = buf;
        }
        else
        {
            GstBuffer *buf = omx_buffer->pAppPrivate;

            if (buf)
                gst_buffer_unref (buf);

            DEBUG (port, "empty buffer"); /* keep looping */
        }

        if (port->share_buffer)
        {
            GstBuffer *new_buf = buffer_alloc (port, omx_buffer->nAllocLen);

            if (port->share_buffer == 2)
            {
                gpointer buffer_data = GST_BUFFER_DATA (new_buf);
                gint i;
                omx_buffer = NULL;
                for (i=0; i<port->num_buffers; i++)
                {
                    if (port->buffers[i]->pBuffer == buffer_data)
                    {
                        DEBUG (port, "found buffer %d", i);
                        omx_buffer = port->buffers[i];
                        break;
                    }
                }
                if (!omx_buffer)
                {
                    WARNING (port, "could not find buffer!!  goodbye cruel world!");
                    g_return_val_if_fail (omx_buffer, NULL);
                }
            }


            omx_buffer->pAppPrivate = new_buf;
            omx_buffer->pBuffer     = GST_BUFFER_DATA (new_buf);
            omx_buffer->nAllocLen   = GST_BUFFER_SIZE (new_buf);
            omx_buffer->nOffset     = 0;
        }
        else
        {
            g_assert (omx_buffer->pBuffer && !omx_buffer->pAppPrivate);
        }

        release_buffer (port, omx_buffer);
    }


    return ret;
}

void
g_omx_port_resume (GOmxPort *port)
{
    DEBUG (port, "resume");
    async_queue_enable (port->queue);
}

void
g_omx_port_pause (GOmxPort *port)
{
    DEBUG (port, "pause");
    async_queue_disable (port->queue);
}

void
g_omx_port_flush (GOmxPort *port)
{
    DEBUG (port, "begin");

    if (port->type == GOMX_PORT_OUTPUT)
    {
        /* This will get rid of any buffers that we have received, but not
         * yet processed in the output_loop.
         */
        OMX_BUFFERHEADERTYPE *omx_buffer;
        while ((omx_buffer = async_queue_pop_full (port->queue, FALSE, TRUE)))
        {
            omx_buffer->nFilledLen = 0;
            release_buffer (port, omx_buffer);
        }
    }

    OMX_SendCommand (port->core->omx_handle, OMX_CommandFlush, port->port_index, NULL);
    g_sem_down (port->core->flush_sem);
    DEBUG (port, "end");
}

void
g_omx_port_enable (GOmxPort *port)
{
    if (port->enabled)
    {
        DEBUG (port, "already enabled");
        return;
    }

    DEBUG (port, "begin");

    g_omx_port_prepare (port);

    OMX_SendCommand (g_omx_core_get_handle (port->core),
            OMX_CommandPortEnable, port->port_index, NULL);

    g_omx_port_allocate_buffers (port);

    g_sem_down (port->core->port_sem);

    port->enabled = TRUE;

    if (port->core->omx_state == OMX_StateExecuting)
        g_omx_port_start_buffers (port);

    DEBUG (port, "end");
}

void
g_omx_port_disable (GOmxPort *port)
{
    if (!port->enabled)
    {
        DEBUG (port, "already disabled");
        return;
    }

    DEBUG (port, "begin");

    port->enabled = FALSE;

    OMX_SendCommand (g_omx_core_get_handle (port->core),
            OMX_CommandPortDisable, port->port_index, NULL);

    g_omx_port_free_buffers (port);

    g_sem_down (port->core->port_sem);

    DEBUG (port, "end");
}

void
g_omx_port_finish (GOmxPort *port)
{
    DEBUG (port, "finish");
    port->enabled = FALSE;
    async_queue_disable (port->queue);
}


/*
 * Some domain specific port related utility functions:
 */

/* keep this list in sync GSTOMX_ALL_FORMATS */
static gint32 all_fourcc[] = {
        GST_MAKE_FOURCC ('I','4','2','0'),
        GST_MAKE_FOURCC ('Y','U','Y','2'),
        GST_MAKE_FOURCC ('U','Y','V','Y'),
        GST_MAKE_FOURCC ('N','V','1','2')
};

#ifndef DIM  /* XXX is there a better alternative available? */
#  define DIM(x) (sizeof(x)/sizeof((x)[0]))
#endif

/**
 * A utility function to query the port for supported color formats, and
 * add the appropriate list of formats to @caps.  The @port can either
 * be an input port for a video encoder, or an output port for a decoder
 */
GstCaps *
g_omx_port_set_video_formats (GOmxPort *port, GstCaps *caps)
{
    OMX_VIDEO_PARAM_PORTFORMATTYPE param;
    int i,j;

    G_OMX_PORT_GET_PARAM (port, OMX_IndexParamVideoPortFormat, &param);

    caps = gst_caps_make_writable (caps);

    for (i=0; i<gst_caps_get_size (caps); i++)
    {
        GstStructure *struc = gst_caps_get_structure (caps, i);
        GValue formats = {0};

        g_value_init (&formats, GST_TYPE_LIST);

        for (j=0; j<DIM(all_fourcc); j++)
        {
            OMX_ERRORTYPE err;
            GValue fourccval = {0};

            g_value_init (&fourccval, GST_TYPE_FOURCC);

            /* check and see if OMX supports the format:
             */
            param.eColorFormat = g_omx_fourcc_to_colorformat (all_fourcc[j]);
            err = G_OMX_PORT_SET_PARAM (port, OMX_IndexParamVideoPortFormat, &param);

            if( err == OMX_ErrorIncorrectStateOperation )
            {
                DEBUG (port, "already executing?");

                /* if we are already executing, such as might be the case if
                 * we get a OMX_EventPortSettingsChanged event, just take the
                 * current format and bail:
                 */
                G_OMX_PORT_GET_PARAM (port, OMX_IndexParamVideoPortFormat, &param);
                gst_value_set_fourcc (&fourccval,
                        g_omx_colorformat_to_fourcc (param.eColorFormat));
                gst_value_list_append_value (&formats, &fourccval);
                break;
            }
            else if( err == OMX_ErrorNone )
            {
                gst_value_set_fourcc (&fourccval, all_fourcc[j]);
                gst_value_list_append_value (&formats, &fourccval);
            }
        }

        gst_structure_set_value (struc, "format", &formats);
    }

    return caps;
}

    /*For avoid repeated code needs to do only one function in order to configure
    video and images caps strure, and also maybe adding RGB color format*/

static gint32 jpeg_fourcc[] = {
        GST_MAKE_FOURCC ('U','Y','V','Y'),
        GST_MAKE_FOURCC ('N','V','1','2')
};

/**
 * A utility function to query the port for supported color formats, and
 * add the appropriate list of formats to @caps.  The @port can either
 * be an input port for a image encoder, or an output port for a decoder
 */
GstCaps *
g_omx_port_set_image_formats (GOmxPort *port, GstCaps *caps)
{
    //OMX_IMAGE_PARAM_PORTFORMATTYPE param;
    int i,j;

    //G_OMX_PORT_GET_PARAM (port, OMX_IndexParamImagePortFormat, &param);

    caps = gst_caps_make_writable (caps);

    for (i=0; i<gst_caps_get_size (caps); i++)
    {
        GstStructure *struc = gst_caps_get_structure (caps, i);
        GValue formats = {0};

        g_value_init (&formats, GST_TYPE_LIST);

        for (j=0; j<DIM(jpeg_fourcc); j++)
        {
            //OMX_ERRORTYPE err;
            GValue fourccval = {0};

            g_value_init (&fourccval, GST_TYPE_FOURCC);

        /* Got error from omx jpeg component , avoiding these lines by the moment till they support it*/
#if 0
            /* check and see if OMX supports the format:
             */
            param.eColorFormat = g_omx_fourcc_to_colorformat (all_fourcc[j]);
            err = G_OMX_PORT_SET_PARAM (port, OMX_IndexParamImagePortFormat, &param);

            if( err == OMX_ErrorIncorrectStateOperation )
            {
                DEBUG (port, "already executing?");

                /* if we are already executing, such as might be the case if
                 * we get a OMX_EventPortSettingsChanged event, just take the
                 * current format and bail:
                 */
                G_OMX_PORT_GET_PARAM (port, OMX_IndexParamImagePortFormat, &param);
                gst_value_set_fourcc (&fourccval,
                        g_omx_colorformat_to_fourcc (param.eColorFormat));
                gst_value_list_append_value (&formats, &fourccval);
                break;
            }
            else if( err == OMX_ErrorNone )
            {
                gst_value_set_fourcc (&fourccval, all_fourcc[j]);
                gst_value_list_append_value (&formats, &fourccval);
            }
#else
            gst_value_set_fourcc (&fourccval, jpeg_fourcc[j]);
            gst_value_list_append_value (&formats, &fourccval);
#endif
        }

        gst_structure_set_value (struc, "format", &formats);
    }

    return caps;
}

