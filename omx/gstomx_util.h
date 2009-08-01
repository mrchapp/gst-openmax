/*
 * Copyright (C) 2006-2007 Texas Instruments, Incorporated
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

#ifndef GSTOMX_UTIL_H
#define GSTOMX_UTIL_H

#include <glib.h>
#include <OMX_Core.h>
#include <OMX_Component.h>

#include <async_queue.h>
#include <sem.h>

/* Typedefs. */

typedef struct GOmxCore GOmxCore;
typedef struct GOmxPort GOmxPort;
typedef struct GOmxImp GOmxImp;
typedef struct GOmxSymbolTable GOmxSymbolTable;
typedef enum GOmxPortType GOmxPortType;

typedef void (*GOmxCb) (GOmxCore *core);
typedef void (*GOmxPortCb) (GOmxPort *port);

/* Enums. */

enum GOmxPortType
{
    GOMX_PORT_INPUT,
    GOMX_PORT_OUTPUT
};

/* Structures. */

struct GOmxSymbolTable
{
    OMX_ERRORTYPE (*init) (void);
    OMX_ERRORTYPE (*deinit) (void);
    OMX_ERRORTYPE (*get_handle) (OMX_HANDLETYPE *handle,
                                 OMX_STRING name,
                                 OMX_PTR data,
                                 OMX_CALLBACKTYPE *callbacks);
    OMX_ERRORTYPE (*free_handle) (OMX_HANDLETYPE handle);
};

struct GOmxImp
{
    guint client_count;
    void *dl_handle;
    GOmxSymbolTable sym_table;
    GMutex *mutex;
};

struct GOmxCore
{
    gpointer object; /**< GStreamer element. */

    OMX_HANDLETYPE omx_handle;
    OMX_ERRORTYPE omx_error;

    OMX_STATETYPE omx_state;
    GCond *omx_state_condition;
    GMutex *omx_state_mutex;

    GPtrArray *ports;

    GSem *done_sem;
    GSem *flush_sem;
    GSem *port_sem;

    GOmxCb settings_changed_cb;
    GOmxImp *imp;

    gboolean done;
};

struct GOmxPort
{
    GOmxCore *core;
    GOmxPortType type;

    guint num_buffers;
    gulong buffer_size;
    guint port_index;
    OMX_BUFFERHEADERTYPE **buffers;

    GMutex *mutex;
    gboolean enabled;
    gboolean omx_allocate; /**< Setup with OMX_AllocateBuffer rather than OMX_UseBuffer */
    AsyncQueue *queue;
};

/* Functions. */

void g_omx_init (void);
void g_omx_deinit (void);

GOmxCore *g_omx_core_new (gpointer object);
void g_omx_core_free (GOmxCore *core);
void g_omx_core_init (GOmxCore *core);
void g_omx_core_deinit (GOmxCore *core);
void g_omx_core_prepare (GOmxCore *core);
void g_omx_core_start (GOmxCore *core);
void g_omx_core_pause (GOmxCore *core);
void g_omx_core_stop (GOmxCore *core);
void g_omx_core_unload (GOmxCore *core);
void g_omx_core_set_done (GOmxCore *core);
void g_omx_core_wait_for_done (GOmxCore *core);
void g_omx_core_flush_start (GOmxCore *core);
void g_omx_core_flush_stop (GOmxCore *core);
OMX_HANDLETYPE g_omx_core_get_handle (GOmxCore *core);
GOmxPort *g_omx_core_get_port (GOmxCore *core, guint index);


GOmxPort *g_omx_port_new (GOmxCore *core, guint index);
void g_omx_port_free (GOmxPort *port);
void g_omx_port_get_config (GOmxPort *port, OMX_PARAM_PORTDEFINITIONTYPE *param);
void g_omx_port_set_config (GOmxPort *port, OMX_PARAM_PORTDEFINITIONTYPE *param);
void g_omx_port_setup (GOmxPort *port, OMX_PARAM_PORTDEFINITIONTYPE *omx_port);
void g_omx_port_push_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer);
OMX_BUFFERHEADERTYPE *g_omx_port_request_buffer (GOmxPort *port);
void g_omx_port_release_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer);
void g_omx_port_resume (GOmxPort *port);
void g_omx_port_pause (GOmxPort *port);
void g_omx_port_flush (GOmxPort *port);
void g_omx_port_enable (GOmxPort *port);
void g_omx_port_disable (GOmxPort *port);
void g_omx_port_finish (GOmxPort *port);


/* note:  in case this is a build with TTIF logging, we can optimize slightly
 * and avoid the gst_caps_to_string() in case logging isn't enabled by using
 * the TTIF_TRACE_ARG_PROCESSOR feature of ttif_trace_fprintf():
 */
#ifdef GST_LOG_OVER_TTIF
#  define LOG_CAPS(obj, caps)    G_STMT_START {                 \
    if (caps) {                                                 \
      static TTIF_TRACE_ARG_PROCESSOR proc = {                  \
        .convert = (char (*)(void *))gst_caps_to_string,        \
        .free    = (void (*)(char *))g_free                     \
      };                                                        \
      GST_DEBUG_OBJECT (obj, "%s: %qs", #caps, &proc, (caps));  \
    } else {                                                    \
      GST_DEBUG_OBJECT (obj, "null");                           \
    }                                                           \
  } G_STMT_END
#else
#  define LOG_CAPS(obj, caps)    G_STMT_START {                 \
    if (caps) {                                                 \
      gchar *capstr = gst_caps_to_string (caps);                \
      GST_DEBUG_OBJECT (obj, "%s: %s", #caps, capstr);          \
      g_free (capstr);                                          \
    } else {                                                    \
      GST_DEBUG_OBJECT (obj, "null");                           \
    }                                                           \
  } G_STMT_END
#endif

#define PRINT_BUFFER(obj, buffer)    G_STMT_START {             \
    if (buffer) {                                               \
      GST_DEBUG_OBJECT (obj, #buffer "=0x%08x (time=%"GST_TIME_FORMAT", duration=%"GST_TIME_FORMAT", flags=%08x)", (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), GST_TIME_ARGS (GST_BUFFER_DURATION(buffer)), GST_BUFFER_FLAGS (buffer)); \
    } else {                                                    \
      GST_DEBUG_OBJECT (obj, #buffer "=null");                  \
    }                                                           \
  } G_STMT_END


#endif /* GSTOMX_UTIL_H */
