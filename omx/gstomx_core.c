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
 * Forward declarations
 */

static inline void
change_state (GOmxCore *core,
              OMX_STATETYPE state);

static inline void
wait_for_state (GOmxCore *core,
                OMX_STATETYPE state);

static inline void
in_port_cb (GOmxPort *port,
            OMX_BUFFERHEADERTYPE *omx_buffer);

static inline void
out_port_cb (GOmxPort *port,
             OMX_BUFFERHEADERTYPE *omx_buffer);

static OMX_ERRORTYPE
EventHandler (OMX_HANDLETYPE omx_handle,
              OMX_PTR app_data,
              OMX_EVENTTYPE event,
              OMX_U32 data_1,
              OMX_U32 data_2,
              OMX_PTR event_data);

static OMX_ERRORTYPE
EmptyBufferDone (OMX_HANDLETYPE omx_handle,
                 OMX_PTR app_data,
                 OMX_BUFFERHEADERTYPE *omx_buffer);

static OMX_ERRORTYPE
FillBufferDone (OMX_HANDLETYPE omx_handle,
                OMX_PTR app_data,
                OMX_BUFFERHEADERTYPE *omx_buffer);

static inline GOmxPort *get_port (GOmxCore *core, guint index);


static OMX_CALLBACKTYPE callbacks = { EventHandler, EmptyBufferDone, FillBufferDone };


/*
 * Util
 */

static void
g_ptr_array_clear (GPtrArray *array)
{
    guint index;
    for (index = 0; index < array->len; index++)
        array->pdata[index] = NULL;
}

static void
g_ptr_array_insert (GPtrArray *array,
                    guint index,
                    gpointer data)
{
    if (index + 1 > array->len)
    {
        g_ptr_array_set_size (array, index + 1);
    }

    array->pdata[index] = data;
}

typedef void (*GOmxPortFunc) (GOmxPort *port);

static void inline
core_for_each_port (GOmxCore *core,
                    GOmxPortFunc func)
{
    guint index;

    for (index = 0; index < core->ports->len; index++)
    {
        GOmxPort *port;

        port = get_port (core, index);

        if (port)
            func (port);
    }
}


/*
 * Core
 */

/**
 * Construct new core
 *
 * @object: the GstOmx object (ie. GstOmxBaseFilter, GstOmxBaseSrc, or
 *    GstOmxBaseSink).  The GstOmx object should have "component-role",
 *    "component-name", and "library-name" properties.
 */
GOmxCore *
g_omx_core_new (gpointer object, gpointer klass)
{
    GOmxCore *core;

    core = g_new0 (GOmxCore, 1);

    core->object = object;

    core->ports = g_ptr_array_new ();

    core->omx_state_condition = g_cond_new ();
    core->omx_state_mutex = g_mutex_new ();

    core->done_sem = g_sem_new ();
    core->flush_sem = g_sem_new ();
    core->port_sem = g_sem_new ();

    core->omx_state = OMX_StateInvalid;

    core->use_timestamps = TRUE;

    {
        gchar *library_name, *component_name, *component_role;

        library_name = g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
                g_quark_from_static_string ("library-name"));

        component_name = g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
                g_quark_from_static_string ("component-name"));

        component_role = g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
                g_quark_from_static_string ("component-role"));

        g_object_set (core->object,
            "component-role", component_role,
            "component-name", component_name,
            "library-name", library_name,
            NULL);
    }

    return core;
}

void
g_omx_core_free (GOmxCore *core)
{
    g_omx_core_deinit (core);     /* just in case we didn't have a READY->NULL.. mainly for gst-inspect */

    g_sem_free (core->port_sem);
    g_sem_free (core->flush_sem);
    g_sem_free (core->done_sem);

    g_mutex_free (core->omx_state_mutex);
    g_cond_free (core->omx_state_condition);

    g_ptr_array_free (core->ports, TRUE);

    g_free (core);
}

void
g_omx_core_init (GOmxCore *core)
{
    gchar *library_name=NULL, *component_name=NULL, *component_role=NULL;

    if (core->omx_handle)
      return;

    g_object_get (core->object,
        "component-role", &component_role,
        "component-name", &component_name,
        "library-name", &library_name,
        NULL);

    GST_DEBUG_OBJECT (core->object, "loading: %s %s (%s)", component_name,
            component_role ? component_role : "", library_name);

    g_return_if_fail (component_name);
    g_return_if_fail (library_name);

    core->imp = g_omx_request_imp (library_name);

    if (!core->imp)
        return;

    core->omx_error = core->imp->sym_table.get_handle (&core->omx_handle,
                                                       (char *) component_name,
                                                       core,
                                                       &callbacks);

    GST_DEBUG_OBJECT (core->object, "OMX_GetHandle(&%p) -> %s",
        core->omx_handle, g_omx_error_to_str (core->omx_error));

    if (component_role)
    {
        OMX_PARAM_COMPONENTROLETYPE param;
        memset(&param, 0, sizeof(param));
        param.nSize = sizeof (OMX_PARAM_COMPONENTROLETYPE);
        param.nVersion.s.nVersionMajor = 1;
        param.nVersion.s.nVersionMinor = 1;

        GST_DEBUG_OBJECT (core->object, "setting component role: %s",
                component_role);

        OMX_GetParameter (core->omx_handle,
                OMX_IndexParamStandardComponentRole, &param);

        strcpy((char*)param.cRole, "audio_decode.dsp.mp3");

        OMX_SetParameter ( core->omx_handle,
                OMX_IndexParamStandardComponentRole, &param);

        g_free (component_role);
    }

    g_free (component_name);
    g_free (library_name);

    g_return_if_fail (core->omx_handle);

    if (!core->omx_error)
        core->omx_state = OMX_StateLoaded;
}

void
g_omx_core_deinit (GOmxCore *core)
{
    if (!core->imp)
        return;

    core_for_each_port (core, g_omx_port_free);
    g_ptr_array_clear (core->ports);

    if (core->omx_state == OMX_StateLoaded ||
        core->omx_state == OMX_StateInvalid)
    {
        if (core->omx_handle)
        {
            core->omx_error = core->imp->sym_table.free_handle (core->omx_handle);
            GST_DEBUG_OBJECT (core->object, "OMX_FreeHandle(%p) -> %s",
                core->omx_handle, g_omx_error_to_str (core->omx_error));
            core->omx_handle = NULL;
        }
    }

    g_omx_release_imp (core->imp);
    core->imp = NULL;
}

void
g_omx_core_prepare (GOmxCore *core)
{
    change_state (core, OMX_StateIdle);

    /* Allocate buffers. */
    core_for_each_port (core, g_omx_port_allocate_buffers);

    wait_for_state (core, OMX_StateIdle);
}

void
g_omx_core_start (GOmxCore *core)
{
    change_state (core, OMX_StateExecuting);
    wait_for_state (core, OMX_StateExecuting);

    if (core->omx_state == OMX_StateExecuting)
        core_for_each_port (core, g_omx_port_start_buffers);
}

void
g_omx_core_stop (GOmxCore *core)
{
    if (core->omx_state == OMX_StateExecuting ||
        core->omx_state == OMX_StatePause)
    {
        change_state (core, OMX_StateIdle);
        wait_for_state (core, OMX_StateIdle);
    }
}

void
g_omx_core_pause (GOmxCore *core)
{
    change_state (core, OMX_StatePause);
    wait_for_state (core, OMX_StatePause);
}

void
g_omx_core_unload (GOmxCore *core)
{
    if (core->omx_state == OMX_StateIdle ||
        core->omx_state == OMX_StateWaitForResources ||
        core->omx_state == OMX_StateInvalid)
    {
        if (core->omx_state != OMX_StateInvalid)
            change_state (core, OMX_StateLoaded);

        core_for_each_port (core, g_omx_port_free_buffers);

        if (core->omx_state != OMX_StateInvalid)
            wait_for_state (core, OMX_StateLoaded);
    }
}

static inline GOmxPort *
get_port (GOmxCore *core, guint index)
{
    if (G_LIKELY (index < core->ports->len))
    {
        return g_ptr_array_index (core->ports, index);
    }

    return NULL;
}

GOmxPort *
g_omx_core_get_port (GOmxCore *core, guint index)
{
    GOmxPort *port = get_port (core, index);

    if (!port)
    {
        port = g_omx_port_new (core, index);
        g_ptr_array_insert (core->ports, index, port);
    }

    return port;
}

void
g_omx_core_set_done (GOmxCore *core)
{
    g_sem_up (core->done_sem);
}

void
g_omx_core_wait_for_done (GOmxCore *core)
{
    g_sem_down (core->done_sem);
}

void
g_omx_core_flush_start (GOmxCore *core)
{
    core_for_each_port (core, g_omx_port_pause);
}

void
g_omx_core_flush_stop (GOmxCore *core)
{
    core_for_each_port (core, g_omx_port_flush);
    core_for_each_port (core, g_omx_port_resume);
}

/**
 * Accessor for OMX component handle.  If the OMX component is not constructed
 * yet, this will trigger it to be constructed (OMX_GetHandle()).  This should
 * at least be used in places where g_omx_core_init() might not have been
 * called yet (such as setting/getting properties)
 */
OMX_HANDLETYPE
g_omx_core_get_handle (GOmxCore *core)
{
  if (!core->omx_handle) g_omx_core_init (core);
  g_return_val_if_fail (core->omx_handle, NULL);
  return core->omx_handle;
}


/*
 * Helper functions.
 */

static inline void
change_state (GOmxCore *core,
              OMX_STATETYPE state)
{
    GST_DEBUG_OBJECT (core->object, "state=%d", state);
    OMX_SendCommand (core->omx_handle, OMX_CommandStateSet, state, NULL);
}

static inline void
complete_change_state (GOmxCore *core,
                       OMX_STATETYPE state)
{
    g_mutex_lock (core->omx_state_mutex);

    core->omx_state = state;
    g_cond_signal (core->omx_state_condition);
    GST_DEBUG_OBJECT (core->object, "state=%d", state);

    g_mutex_unlock (core->omx_state_mutex);
}

static inline void
wait_for_state (GOmxCore *core,
                OMX_STATETYPE state)
{
    GTimeVal tv;
    gboolean signaled;

    g_mutex_lock (core->omx_state_mutex);

    if (core->omx_error != OMX_ErrorNone)
        goto leave;

    g_get_current_time (&tv);
    g_time_val_add (&tv, 100000000);

    /* try once */
    if (core->omx_state != state)
    {
        signaled = g_cond_timed_wait (core->omx_state_condition, core->omx_state_mutex, &tv);

        if (!signaled)
        {
            GST_ERROR_OBJECT (core->object, "timed out");
        }
    }

    if (core->omx_error != OMX_ErrorNone)
        goto leave;

    if (core->omx_state != state)
    {
        GST_ERROR_OBJECT (core->object, "wrong state received: state=%d, expected=%d",
                          core->omx_state, state);
    }

leave:
    g_mutex_unlock (core->omx_state_mutex);
}

/*
 * Callbacks
 */

static inline void
in_port_cb (GOmxPort *port,
            OMX_BUFFERHEADERTYPE *omx_buffer)
{
    /** @todo remove this */

    if (!port->enabled)
        return;
}

static inline void
out_port_cb (GOmxPort *port,
             OMX_BUFFERHEADERTYPE *omx_buffer)
{
    /** @todo remove this */

    if (!port->enabled)
        return;

#if 0
    if (omx_buffer->nFlags & OMX_BUFFERFLAG_EOS)
    {
        g_omx_port_set_done (port);
        return;
    }
#endif
}

void
g_omx_core_got_buffer (GOmxCore *core,
            GOmxPort *port,
            OMX_BUFFERHEADERTYPE *omx_buffer)
{
    if (G_UNLIKELY (!omx_buffer))
    {
        return;
    }

    if (G_LIKELY (port))
    {
        g_omx_port_push_buffer (port, omx_buffer);

        switch (port->type)
        {
            case GOMX_PORT_INPUT:
                in_port_cb (port, omx_buffer);
                break;
            case GOMX_PORT_OUTPUT:
                out_port_cb (port, omx_buffer);
                break;
            default:
                break;
        }
    }
}

/*
 * OpenMAX IL callbacks.
 */

static OMX_ERRORTYPE
EventHandler (OMX_HANDLETYPE omx_handle,
              OMX_PTR app_data,
              OMX_EVENTTYPE event,
              OMX_U32 data_1,
              OMX_U32 data_2,
              OMX_PTR event_data)
{
    GOmxCore *core;

    core = (GOmxCore *) app_data;

    switch (event)
    {
        case OMX_EventCmdComplete:
            {
                OMX_COMMANDTYPE cmd;

                cmd = (OMX_COMMANDTYPE) data_1;

                GST_DEBUG_OBJECT (core->object, "OMX_EventCmdComplete: %d", cmd);

                switch (cmd)
                {
                    case OMX_CommandStateSet:
                        complete_change_state (core, data_2);
                        break;
                    case OMX_CommandFlush:
                        g_sem_up (core->flush_sem);
                        break;
                    case OMX_CommandPortDisable:
                    case OMX_CommandPortEnable:
                        g_sem_up (core->port_sem);
                    default:
                        break;
                }
                break;
            }
        case OMX_EventBufferFlag:
            {
                GST_DEBUG_OBJECT (core->object, "OMX_EventBufferFlag");
                if (data_2 & OMX_BUFFERFLAG_EOS)
                {
                    g_omx_core_set_done (core);
                }
                break;
            }
        case OMX_EventPortSettingsChanged:
            {
                GST_DEBUG_OBJECT (core->object, "OMX_EventPortSettingsChanged");
                /** @todo only on the relevant port. */
                if (core->settings_changed_cb)
                {
                    core->settings_changed_cb (core);
                }
                break;
            }
        case OMX_EventError:
            {
                core->omx_error = data_1;
                GST_ERROR_OBJECT (core->object, "unrecoverable error: %s (0x%lx)",
                                  g_omx_error_to_str (data_1), data_1);
                /* component might leave us waiting for buffers, unblock */
                g_omx_core_flush_start (core);
                /* unlock wait_for_state */
                g_mutex_lock (core->omx_state_mutex);
                g_cond_signal (core->omx_state_condition);
                g_mutex_unlock (core->omx_state_mutex);
                break;
            }
        default:
            break;
    }

    return OMX_ErrorNone;
}

static OMX_ERRORTYPE
EmptyBufferDone (OMX_HANDLETYPE omx_handle,
                 OMX_PTR app_data,
                 OMX_BUFFERHEADERTYPE *omx_buffer)
{
    GOmxCore *core;
    GOmxPort *port;

    core = (GOmxCore*) app_data;
    port = get_port (core, omx_buffer->nInputPortIndex);

    GST_DEBUG_OBJECT (core->object, "EBD: omx_buffer=%p, pAppPrivate=%p", omx_buffer, omx_buffer ? omx_buffer->pAppPrivate : 0);
    g_omx_core_got_buffer (core, port, omx_buffer);

    return OMX_ErrorNone;
}

static OMX_ERRORTYPE
FillBufferDone (OMX_HANDLETYPE omx_handle,
                OMX_PTR app_data,
                OMX_BUFFERHEADERTYPE *omx_buffer)
{
    GOmxCore *core;
    GOmxPort *port;

    core = (GOmxCore *) app_data;
    port = get_port (core, omx_buffer->nOutputPortIndex);

    GST_DEBUG_OBJECT (core->object, "FBD: omx_buffer=%p, pAppPrivate=%p", omx_buffer, omx_buffer ? omx_buffer->pAppPrivate : 0);
    g_omx_core_got_buffer (core, port, omx_buffer);

    return OMX_ErrorNone;
}

