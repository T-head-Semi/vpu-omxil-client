/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#define _GNU_SOURCE // for getline

/* OMX includes */
#include <OMX_Core.h>
#include <OMX_Types.h>

/* system includes */
#include <stdarg.h> /* va_list ... */
#include <stdio.h>  /* vprintf */
#include <string.h> /* strcmp */
#include <assert.h>
#include <errno.h>
#include <unistd.h> /* usleep */
#include <stdlib.h>

/* project includes */
#include "omxtestcommon.h"
#include "process_linker_types.h"


#define DEFAULT_BUFFER_SIZE_OUTPUT  1920 * 1088 * 4
#define EXTRA_BUFFERS               4
#define MIN_REF_SIZE  3655712       // miminum buffer size for 1080P streams

/* VP8 time resolution */
#define TIME_RESOLUTION     90000

/* Intermediate Video File Format */
#define IVF_HDR_BYTES       32
#define IVF_FRM_BYTES       12

/* Webp File Format */
#define RIFF_HEADER_SIZE 20
#define WEBP_METADATA_SIZE 12

#define Q16_FLOAT(a) ((float)(a) / 65536.0)

#define INIT_OMX_TYPE(f) \
    memset(&f, 0, sizeof(f)); \
  (f).nSize = sizeof(f);      \
  (f).nVersion.s.nVersionMajor = 1; \
  (f).nVersion.s.nVersionMinor = 1

typedef struct RCVSTATE
{
    int rcV1;
    int advanced;
    int filesize;
} RCVSTATE;

/* list function definitions */

OMX_U32 list_capacity(HEADERLIST * list)
{
    assert(list);
    return list->capacity - 1;
}

OMX_U32 list_available(HEADERLIST * list)
{
    assert(list);
    return (list->readpos < list->writepos)
        ? list->writepos - list->readpos
        : (list->capacity - list->readpos) + list->writepos;
}

void list_init(HEADERLIST * list, OMX_U32 capacity)
{
    assert(list);
    assert(capacity > 0);

    list->capacity = capacity + 1;
    list->readpos = 0;
    list->writepos = 0;

    list->hdrs =
        (OMX_BUFFERHEADERTYPE **) OSAL_Malloc(sizeof(OMX_BUFFERHEADERTYPE *) *
                                              list->capacity);
    assert(list->hdrs);

    memset(list->hdrs, 0, sizeof(OMX_BUFFERHEADERTYPE *) * list->capacity);
}

void list_clear(HEADERLIST * list)
{
    assert(list);
    assert(list->hdrs);
    assert(list->capacity > 0);

    memset(list->hdrs, 0, sizeof(OMX_BUFFERHEADERTYPE *) * list->capacity);

    list->readpos = 0;
    list->writepos = 0;
}

void list_destroy(HEADERLIST * list)
{
    memset(list->hdrs, 0, sizeof(OMX_BUFFERHEADERTYPE *) * list->capacity);
    OSAL_Free((OMX_PTR) list->hdrs);

    memset(list, 0, sizeof(HEADERLIST));
}

void list_copy(HEADERLIST * dst, HEADERLIST * src)
{
    OMX_U32 diff;

    assert(dst && src);
    assert(dst->capacity >= src->capacity);

    dst->capacity = src->capacity;
    dst->readpos = src->readpos;
    dst->writepos = src->writepos;

    assert(dst->hdrs);
    assert(src->hdrs);

    memcpy(dst->hdrs, src->hdrs,
           sizeof(OMX_BUFFERHEADERTYPE *) * dst->capacity);

    diff = dst->capacity - src->capacity;
    if(diff > 0)
    {
        memset(dst->hdrs + diff, 0, diff);
    }
}

OMX_BOOL list_push_header(HEADERLIST * list, OMX_BUFFERHEADERTYPE * header)
{
    assert(list);
    assert(list->writepos < list->capacity);

    if(((list->writepos + 1) % list->capacity) != list->readpos)
    {
        list->hdrs[list->writepos] = header;
        ++(list->writepos);
        list->writepos = (list->writepos) % list->capacity;
        return OMX_TRUE;
    }
    return OMX_FALSE;
}

void list_get_header(HEADERLIST * list, OMX_BUFFERHEADERTYPE ** header)
{
    assert(list);
    assert(header);

    if(list->readpos == list->writepos)
    {
        *header = NULL;
        return;
    }

    *header = list->hdrs[list->readpos++];
    list->readpos = list->readpos % list->capacity;
}

/* ---------------- TRACE-C -------------------- */

static OMX_HANDLETYPE gTraceMutex = 0;

OMX_U32 traceLevel =
    OMX_OSAL_TRACE_INFO | OMX_OSAL_TRACE_ERROR | OMX_OSAL_TRACE_DEBUG;

/**
 * error string table
 */
OMX_STRING OMX_OSAL_TraceErrorStr(OMX_IN OMX_ERRORTYPE omxError)
{
    switch (omxError)
    {
        CASE(OMX_ErrorNone);
        CASE(OMX_ErrorInsufficientResources);
        CASE(OMX_ErrorUndefined);
        CASE(OMX_ErrorInvalidComponentName);
        CASE(OMX_ErrorComponentNotFound);
        CASE(OMX_ErrorInvalidComponent);
        CASE(OMX_ErrorBadParameter);
        CASE(OMX_ErrorNotImplemented);
        CASE(OMX_ErrorUnderflow);
        CASE(OMX_ErrorOverflow);
        CASE(OMX_ErrorHardware);
        CASE(OMX_ErrorInvalidState);
        CASE(OMX_ErrorStreamCorrupt);
        CASE(OMX_ErrorPortsNotCompatible);
        CASE(OMX_ErrorResourcesLost);
        CASE(OMX_ErrorNoMore);
        CASE(OMX_ErrorVersionMismatch);
        CASE(OMX_ErrorNotReady);
        CASE(OMX_ErrorTimeout);
        CASE(OMX_ErrorSameState);
        CASE(OMX_ErrorResourcesPreempted);
        CASE(OMX_ErrorPortUnresponsiveDuringAllocation);
        CASE(OMX_ErrorPortUnresponsiveDuringDeallocation);
        CASE(OMX_ErrorPortUnresponsiveDuringStop);
        CASE(OMX_ErrorIncorrectStateTransition);
        CASE(OMX_ErrorIncorrectStateOperation);
        CASE(OMX_ErrorUnsupportedSetting);
        CASE(OMX_ErrorUnsupportedIndex);
        CASE(OMX_ErrorBadPortIndex);
        CASE(OMX_ErrorPortUnpopulated);
        CASE(OMX_ErrorComponentSuspended);
        CASE(OMX_ErrorDynamicResourcesUnavailable);
        CASE(OMX_ErrorMbErrorsInFrame);
        CASE(OMX_ErrorFormatNotDetected);
        CASE(OMX_ErrorContentPipeOpenFailed);
        CASE(OMX_ErrorContentPipeCreationFailed);
        CASE(OMX_ErrorSeperateTablesUsed);
        CASE(OMX_ErrorTunnelingUnsupported);
        CASE(OMX_ErrorKhronosExtensions);
        CASE(OMX_ErrorVendorStartUnused);
        CASE(OMX_ErrorMax);
        default: return "unknown error code";
    }

    return 0;
}

OMX_STRING OMX_OSAL_TraceCodingTypeStr(OMX_IN OMX_VIDEO_CODINGTYPE coding)
{
    switch ((OMX_U32)coding)
    {
        CASE(OMX_VIDEO_CodingUnused);
        CASE(OMX_VIDEO_CodingAutoDetect);
        CASE(OMX_VIDEO_CodingMPEG2);
        CASE(OMX_VIDEO_CodingH263);
        CASE(OMX_VIDEO_CodingMPEG4);
        CASE(OMX_VIDEO_CodingWMV);
        CASE(OMX_VIDEO_CodingRV);
        CASE(OMX_VIDEO_CodingAVC);
        CASE(OMX_VIDEO_CodingMJPEG);
        CASE(OMX_CSI_VIDEO_CodingVP6);
        CASE(OMX_CSI_VIDEO_CodingAVS);
        CASE(OMX_VIDEO_CodingVP8);
        CASE(OMX_CSI_VIDEO_CodingHEVC);
        CASE(OMX_CSI_VIDEO_CodingVP9);
        CASE(OMX_VIDEO_CodingKhronosExtensions);
        CASE(OMX_VIDEO_CodingVendorStartUnused);
        CASE(OMX_VIDEO_CodingMax);
        default: return "unknown video coding format value";
    }
    return 0;
}

OMX_STRING OMX_OSAL_TraceImageCodingTypeStr(OMX_IN OMX_IMAGE_CODINGTYPE coding)
{
    switch ((OMX_U32)coding)
    {
        CASE(OMX_IMAGE_CodingUnused);
        CASE(OMX_IMAGE_CodingAutoDetect);
        CASE(OMX_IMAGE_CodingJPEG);
        CASE(OMX_IMAGE_CodingWEBP);
        CASE(OMX_IMAGE_CodingMax);
        default: return "unknown image coding format value";
    }
    return 0;
}

OMX_STRING OMX_OSAL_TraceColorFormatStr(OMX_IN OMX_COLOR_FORMATTYPE color)
{
    switch (color)
    {
        CASE(OMX_COLOR_FormatUnused);
        CASE(OMX_COLOR_FormatMonochrome);
        CASE(OMX_COLOR_Format8bitRGB332);
        CASE(OMX_COLOR_Format12bitRGB444);
        CASE(OMX_COLOR_Format16bitARGB4444);
        CASE(OMX_COLOR_Format16bitARGB1555);
        CASE(OMX_COLOR_Format16bitRGB565);
        CASE(OMX_COLOR_Format16bitBGR565);
        CASE(OMX_COLOR_Format18bitRGB666);
        CASE(OMX_COLOR_Format18bitARGB1665);
        CASE(OMX_COLOR_Format19bitARGB1666);
        CASE(OMX_COLOR_Format24bitRGB888);
        CASE(OMX_COLOR_Format24bitBGR888);
        CASE(OMX_COLOR_Format24bitARGB1887);
        CASE(OMX_COLOR_Format25bitARGB1888);
        CASE(OMX_COLOR_Format32bitBGRA8888);
        CASE(OMX_COLOR_Format32bitARGB8888);
        CASE(OMX_COLOR_FormatYUV411Planar);
        CASE(OMX_COLOR_FormatYUV411PackedPlanar);
        CASE(OMX_COLOR_FormatYUV420Planar);
        CASE(OMX_COLOR_FormatYUV420PackedPlanar);
        CASE(OMX_COLOR_FormatYUV420SemiPlanar);
        CASE(OMX_COLOR_FormatYUV422Planar);
        CASE(OMX_COLOR_FormatYUV422PackedPlanar);
        CASE(OMX_COLOR_FormatYUV422SemiPlanar);
        CASE(OMX_COLOR_FormatYCbYCr);
        CASE(OMX_COLOR_FormatYCrYCb);
        CASE(OMX_COLOR_FormatCbYCrY);
        CASE(OMX_COLOR_FormatCrYCbY);
        CASE(OMX_COLOR_FormatYUV444Interleaved);
        CASE(OMX_COLOR_FormatRawBayer8bit);
        CASE(OMX_COLOR_FormatRawBayer10bit);
        CASE(OMX_COLOR_FormatRawBayer8bitcompressed);
        CASE(OMX_COLOR_FormatL2);
        CASE(OMX_COLOR_FormatL4);
        CASE(OMX_COLOR_FormatL8);
        CASE(OMX_COLOR_FormatL16);
        CASE(OMX_COLOR_FormatL24);
        CASE(OMX_COLOR_FormatL32);
        CASE(OMX_COLOR_FormatYUV420PackedSemiPlanar);
        CASE(OMX_COLOR_FormatYUV422PackedSemiPlanar);
        CASE(OMX_COLOR_Format18BitBGR666);
        CASE(OMX_COLOR_Format24BitARGB6666);
        CASE(OMX_COLOR_Format24BitABGR6666);
        CASE(OMX_CSI_COLOR_FormatYUV420SemiPlanarP010);
        CASE(OMX_COLOR_FormatKhronosExtensions);
        CASE(OMX_COLOR_FormatVendorStartUnused);
        CASE(OMX_COLOR_FormatMax);
        default: return "unknown color format value";
    }
    return 0;
}

#define CASE(x) case x: return #x

OMX_STRING HantroOmx_str_omx_state(OMX_STATETYPE s)
{
    switch (s)
    {
        CASE(OMX_StateLoaded);
        CASE(OMX_StateIdle);
        CASE(OMX_StateExecuting);
        CASE(OMX_StatePause);
        CASE(OMX_StateWaitForResources);
        CASE(OMX_StateInvalid);
        default: return "unknown state value";
    }
    return 0;
}

OMX_STRING HantroOmx_str_omx_event(OMX_EVENTTYPE e)
{
    switch (e)
    {
        CASE(OMX_EventCmdComplete);
        CASE(OMX_EventError);
        CASE(OMX_EventMark);
        CASE(OMX_EventPortSettingsChanged);
        CASE(OMX_EventBufferFlag);
        CASE(OMX_EventResourcesAcquired);
        CASE(OMX_EventComponentResumed);
        CASE(OMX_EventDynamicResourcesAvailable);
        default: return "unknown event value";
    }
    return 0;
}

OMX_STRING OMX_OSAL_TraceDirectionStr(OMX_IN OMX_DIRTYPE color)
{
    switch (color)
    {
    case OMX_DirInput:
        return "OMX_DirInput";
    case OMX_DirOutput:
        return "OMX_DirOutput";
    case OMX_DirMax:
        return "OMX_DirMax";
    }
    return 0;
}

void OMX_OSAL_TracePortSettings(OMX_IN OMX_U32 flags,
                                OMX_IN OMX_PARAM_PORTDEFINITIONTYPE * port)
{
    OMX_STRING str;

    switch (port->eDir)
    {
    case OMX_DirOutput:
        str = "Output";
        break;
    case OMX_DirInput:
        str = "Input";
        break;
    default:
        str = 0;
    }

    if (port->eDomain == OMX_PortDomainVideo)
    {
        OMX_OSAL_Trace(flags,
                   "%s port settings\n\n"
                   "           nBufferSize: %d\n"
                   "       nBufferCountMin: %d\n"
                   "    nBufferCountActual: %d\n"
                   "           nFrameWidth: %d\n"
                   "          nFrameHeight: %d\n"
                   "               nStride: %d\n"
                   "          nSliceHeight: %d\n\n"
                   "          eColorFormat: %s\n"
                   "    eCompressionFormat: %s\n"
                   "              nBitrate: %d\n"
                   "            xFramerate: %.2f\n"
                   " bFlagErrorConcealment: %s\n\n",
                   str,
                   (int) port->nBufferSize,
                   (int) port->nBufferCountMin,
                   (int) port->nBufferCountActual,
                   (int) port->format.video.nFrameWidth,
                   (int) port->format.video.nFrameHeight,
                   (int) port->format.video.nStride,
                   (int) port->format.video.nSliceHeight,
                   OMX_OSAL_TraceColorFormatStr(port->format.video.
                                                eColorFormat),
                   OMX_OSAL_TraceCodingTypeStr(port->format.video.
                                               eCompressionFormat),
                   port->format.video.nBitrate,
                   Q16_FLOAT(port->format.video.xFramerate),
                   port->format.video.
                   bFlagErrorConcealment ? "true" : "false");
    }
    else
    {
        OMX_OSAL_Trace(flags,
                   "%s port settings\n\n"
                   "           nBufferSize: %d\n"
                   "       nBufferCountMin: %d\n"
                   "    nBufferCountActual: %d\n"
                   "           nFrameWidth: %d\n"
                   "          nFrameHeight: %d\n"
                   "               nStride: %d\n"
                   "          nSliceHeight: %d\n\n"
                   "          eColorFormat: %s\n"
                   "    eCompressionFormat: %s\n",
                   str,
                   (int) port->nBufferSize,
                   (int) port->nBufferCountMin,
                   (int) port->nBufferCountActual,
                   (int) port->format.image.nFrameWidth,
                   (int) port->format.image.nFrameHeight,
                   (int) port->format.image.nStride,
                   (int) port->format.image.nSliceHeight,
                   OMX_OSAL_TraceColorFormatStr(port->format.image.
                                                eColorFormat),
                   OMX_OSAL_TraceImageCodingTypeStr(port->format.image.
                                               eCompressionFormat));
    }
}

/**
 *
 */
OMX_ERRORTYPE OMX_OSAL_Trace(OMX_IN OMX_U32 nTraceFlags,
                             OMX_IN OMX_STRING format, ...)
{
    va_list args;

    if(gTraceMutex)
        OSAL_MutexLock(gTraceMutex);

    if(nTraceFlags & traceLevel)
    {
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }

    if(gTraceMutex)
        OSAL_MutexUnlock(gTraceMutex);

    return OMX_ErrorNone;
}

/* ---------------- END TRACE -------------------- */

/* Callback declarations */
static OMX_ERRORTYPE omxclient_event_handler(OMX_IN OMX_HANDLETYPE hComponent,
                                             OMX_IN OMX_PTR pAppData,
                                             OMX_IN OMX_EVENTTYPE eEvent,
                                             OMX_IN OMX_U32 nData1,
                                             OMX_IN OMX_U32 nData2,
                                             OMX_IN OMX_PTR pEventData);

static OMX_ERRORTYPE omxclient_empty_buffer_done(OMX_IN OMX_HANDLETYPE
                                                 hComponent,
                                                 OMX_IN OMX_PTR pAppData,
                                                 OMX_IN OMX_BUFFERHEADERTYPE *
                                                 pBuffer);

static OMX_ERRORTYPE omxclient_buffer_fill_done(OMX_OUT OMX_HANDLETYPE
                                                hComponent,
                                                OMX_OUT OMX_PTR pAppData,
                                                OMX_OUT OMX_BUFFERHEADERTYPE *
                                                pBuffer);

/**
 *
 */
static OMX_ERRORTYPE omxclient_event_handler(OMX_IN OMX_HANDLETYPE component,
                                             OMX_IN OMX_PTR pAppData,
                                             OMX_IN OMX_EVENTTYPE event,
                                             OMX_IN OMX_U32 nData1,
                                             OMX_IN OMX_U32 nData2,
                                             OMX_IN OMX_PTR pEventData)
{
    OMX_ERRORTYPE omxError;
    OMX_STATETYPE state;
    OMX_PARAM_PORTDEFINITIONTYPE port;
    OMX_STRING str;
    omxError = OMX_ErrorNone;

    OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO,
                   "Got component event: event:%s data1:%u data2:%u eventdata 0x%x\n",
                   HantroOmx_str_omx_event(event), (unsigned) nData1,
                   (unsigned) nData2, pEventData);

    switch (event)
    {
    case OMX_EventCmdComplete:
        {
            switch ((OMX_COMMANDTYPE) (nData1))
            {
            case OMX_CommandStateSet:
                OMX_GetState(component, &state);
                OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "State: %s\n",
                               HantroOmx_str_omx_state(state));
                OSAL_EventSet(OMXCLIENT_PTR(pAppData)->state_event);
                break;

            default:
                break;
            }
        }
        break;

    case OMX_EventBufferFlag:
        {
            OMXCLIENT_PTR(pAppData)->EOS = OMX_TRUE;
        }
        break;

    case OMX_EventError:
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Error: %s\n",
                           OMX_OSAL_TraceErrorStr((OMX_ERRORTYPE) nData1));
            omxError = (OMX_ERRORTYPE) nData1;
            OMXCLIENT_PTR(pAppData)->EOS = OMX_TRUE;
        }
        break;
    default:
        break;
    }

    return omxError;
}

static int omxclient_control_frame_rate(OMXCLIENT *client, OMX_U64 frame_count)
{
    int retval = 0;

    if (client->frame_rate_numer > 0 && client->frame_rate_denom > 0)
    {
        if (frame_count == 0)
        {
            int delay = 100000;
            char *env = getenv("VSTART_DELAY");
            if (env != NULL)
                delay = atoi(env);

            gettimeofday(&client->start, 0);
            client->start.tv_usec += delay;
            if (client->start.tv_usec >= 1000000)
            {
                client->start.tv_usec -= 1000000;
                client->start.tv_sec += 1;
            }
            fprintf(stderr, "Start Time:  %.3f\n", client->start.tv_sec * 1000 + client->start.tv_usec / 1000.0);
        }

        OMX_S64 timestamp_sec = (OMX_S64)(frame_count) * client->frame_rate_denom / client->frame_rate_numer;
        OMX_S64 timestamp_usec = ((OMX_S64)(frame_count) * client->frame_rate_denom % client->frame_rate_numer) * 1000000 / client->frame_rate_numer;

        struct timeval render_time;
        render_time.tv_sec = client->start.tv_sec + timestamp_sec;
        render_time.tv_usec = client->start.tv_usec + timestamp_usec;

        struct timeval now;
        gettimeofday(&now, 0);

        OMX_S64 sleep_usec = (render_time.tv_sec - now.tv_sec) * 1000000 + render_time.tv_usec - now.tv_usec;
        if (sleep_usec > 0)
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO, "Wait %lld microseconds\n", sleep_usec);
            usleep(sleep_usec);
        }
        else
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_WARNING, "Skip frame %lld, %lld\n", frame_count, sleep_usec);
            retval = 1;
        }
    }

    return retval;
}

/**
 *
 */
static OMX_ERRORTYPE omxclient_empty_buffer_done(OMX_IN OMX_HANDLETYPE
                                                 hComponent,
                                                 OMX_IN OMX_PTR pAppData,
                                                 OMX_IN OMX_BUFFERHEADERTYPE *
                                                 pBuffer)
{
    OMX_ERRORTYPE omxError = OMX_ErrorNone;
    OMXCLIENT *appdata = OMXCLIENT_PTR(pAppData);
    HEADERLIST *queue = &appdata->input_queue;

    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Empty buffer done\n");

    if (pBuffer->nInputPortIndex == 2)
        queue = &appdata->osd_queue;

    OSAL_MutexLock(appdata->queue_mutex);
    {
        if(queue->writepos >= queue->capacity)
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "No space in return queue\n");

            /* NOTE: correct return? */
            omxError = OMX_ErrorInsufficientResources;
        }
        else
        {
            list_push_header(queue, pBuffer);
        }
    }

    OSAL_MutexUnlock(appdata->queue_mutex);

    if (appdata->plinksink != NULL && pBuffer->nInputPortIndex == 0)
    {
        OMX_ERRORTYPE omxError = OMX_ErrorNone;
        OMX_CSI_BUFFER_MODE_CONFIGTYPE bufferMode;
        omxclient_struct_init(&bufferMode, OMX_CSI_BUFFER_MODE_CONFIGTYPE);
        bufferMode.nPortIndex = 0;
        OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                                    (hComponent, OMX_CSI_IndexParamBufferMode,
                                    &bufferMode), omxError);

        if (bufferMode.eMode == OMX_CSI_BUFFER_MODE_DMA)
            close((int)pBuffer->pBuffer);

        // return the buffer to source
        PlinkMsg msg;
        PlinkPacket sendpkt;
        msg.header.type = PLINK_TYPE_MESSAGE;
        msg.header.size = DATA_SIZE(PlinkMsg);
        msg.msg = 0;
        sendpkt.list[0] = &msg;
        sendpkt.num = 1;
        sendpkt.fd = PLINK_INVALID_FD;
        if (PLINK_send(appdata->plinksink, 0, &sendpkt) == PLINK_STATUS_ERROR)
            return OMX_ErrorBadParameter;
    }

    return omxError;
}

/**
 *
 */
static OMX_ERRORTYPE omxclient_buffer_fill_done(OMX_OUT OMX_HANDLETYPE
                                                component,
                                                OMX_OUT OMX_PTR pAppData,
                                                OMX_OUT OMX_BUFFERHEADERTYPE *
                                                buffer)
{
    OMXCLIENT *client = OMXCLIENT_PTR(pAppData);
    FILE *fLayer;
    char filename[100];

    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG,
                   "Fill Buffer Done: filledLen %u, nOffset %u\n",
                   (unsigned) buffer->nFilledLen, (unsigned) buffer->nOffset);

    /* if eos has already been processed */
    if(client->EOS == OMX_TRUE)
    {
        list_push_header(&(client->output_queue), buffer);
        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE omxError = OMX_ErrorNone;
    OMX_PARAM_PORTDEFINITIONTYPE port;
    omxclient_struct_init(&port, OMX_PARAM_PORTDEFINITIONTYPE);
    port.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                                (component, OMX_IndexParamPortDefinition,
                                &port), omxError);

    OMX_BOOL is_yuv =
        (port.eDomain == OMX_PortDomainVideo && port.format.video.eColorFormat != OMX_COLOR_FormatUnused) ||
        (port.eDomain == OMX_PortDomainImage && port.format.image.eColorFormat != OMX_COLOR_FormatUnused);
    OMX_BOOL is_bitstream =
        (port.eDomain == OMX_PortDomainVideo && port.format.video.eCompressionFormat != OMX_VIDEO_CodingUnused) ||
        (port.eDomain == OMX_PortDomainImage && port.format.image.eCompressionFormat != OMX_IMAGE_CodingUnused);

    if (buffer->nFilledLen > 0)
    {
        size_t ret = fwrite(buffer->pBuffer, 1, buffer->nFilledLen, client->output);
        fflush(client->output);
        OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "\twrote %u bytes to file\n", ret);
    }

    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Frame %lld\n", client->frame_count);

    if(buffer->nFilledLen > 0)
    {
        if(!(buffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG))
            client->frame_count++;
        client->output_size += buffer->nFilledLen;
    }
    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "File size %d\n", client->output_size);

    if(buffer->nFlags & OMX_BUFFERFLAG_EOS)
    {
        client->EOS = OMX_TRUE;
        list_push_header(&(client->output_queue), buffer);
        return OMX_ErrorNone;
    }

    buffer->nFilledLen = 0;
    buffer->nOffset = 0;
    buffer->nFlags = 0;
    return OMX_FillThisBuffer(component, buffer);
}

/**
 *
 */
OMX_ERRORTYPE omxclient_get_component_roles(OMX_STRING cComponentName,
                                            OMX_STRING cRole)
{
    OMX_ERRORTYPE omxError;
    OMX_U32 i, nRoles;
    OMX_STRING *sRoleArray;

    omxError = OMX_GetRolesOfComponent(cComponentName, &nRoles, (OMX_U8 **) 0);
    if(omxError == OMX_ErrorNone)
    {
        sRoleArray = (OMX_STRING *) OSAL_Malloc(nRoles * sizeof(OMX_STRING));

        /* check memory allocation */
        if(!sRoleArray)
        {
            return OMX_ErrorInsufficientResources;
        }

        for(i = 0; i < nRoles; ++i)
        {
            sRoleArray[i] =
                (OMX_STRING) OSAL_Malloc(sizeof(OMX_U8) *
                                         OMX_MAX_STRINGNAME_SIZE);
            if(!sRoleArray[i])
            {
                omxError = OMX_ErrorInsufficientResources;
            }
        }

        if(omxError == OMX_ErrorNone)
        {
            omxError =
                OMX_GetRolesOfComponent(cComponentName, &nRoles,
                                        (OMX_U8 **) sRoleArray);
            if(omxError == OMX_ErrorNone)
            {
                /*omxError = OMX_ErrorNotImplemented; */
                for(i = 0; i < nRoles; ++i)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG,
                                   "Role '%s' supported.\n", sRoleArray[i]);
                    if(strcmp(sRoleArray[i], cRole) == 0)
                    {
                        omxError = OMX_ErrorNone;
                    }
                }
            }
        }

        for(; i;)
        {
            OSAL_Free((OMX_PTR) sRoleArray[--i]);
        }
        OSAL_Free((OMX_PTR) sRoleArray);
    }

    return omxError;
}

/**
 *
 */
OMX_ERRORTYPE  omxclient_component_create(OMXCLIENT * appdata,
                                         OMX_STRING cComponentName,
                                         OMX_STRING cRole, OMX_U32 buffer_count)
{
    OMX_ERRORTYPE omxError;
    OMX_CALLBACKTYPE oCallbacks;

    oCallbacks.EmptyBufferDone = omxclient_empty_buffer_done;
    oCallbacks.EventHandler = omxclient_event_handler;
    oCallbacks.FillBufferDone = omxclient_buffer_fill_done;

    /* get roles */
    omxError = omxclient_get_component_roles(cComponentName, cRole);
    if(omxError == OMX_ErrorNone)
    {
        /* create */
        omxError =
            OMX_GetHandle(&(appdata->component), cComponentName, appdata,
                          &oCallbacks);
        if(omxError == OMX_ErrorNone)
        {
            list_init(&(appdata->input_queue), buffer_count);
            list_init(&(appdata->output_queue), buffer_count);
            list_init(&(appdata->osd_queue), buffer_count);

            OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Component '%s' created.\n",
                           cComponentName);

            omxError = OSAL_MutexCreate(&(appdata->queue_mutex));
            if(omxError == OMX_ErrorNone)
            {
                omxError = OSAL_EventCreate(&(appdata->state_event));
                if(omxError != OMX_ErrorNone)
                {
                    OSAL_MutexDestroy(appdata->queue_mutex);
                    list_destroy(&(appdata->input_queue));
                    list_destroy(&(appdata->output_queue));
                    list_destroy(&(appdata->osd_queue));
                }
                else
                {
                    OMX_PARAM_COMPONENTROLETYPE sRoleDef;
                    omxclient_struct_init(&(sRoleDef), OMX_PARAM_COMPONENTROLETYPE);
                    strcpy((char *)sRoleDef.cRole, cRole);
                    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter(appdata->component,
                                                               OMX_IndexParamStandardComponentRole,
                                                               &sRoleDef),omxError);
                }
            }
            else
            {
                list_destroy(&(appdata->input_queue));
                list_destroy(&(appdata->output_queue));
                list_destroy(&(appdata->osd_queue));
            }
        }
        /* no else, error propagated onwards */
    }
    /* no else */

    return omxError;
}

/**
 *
 */
OMX_ERRORTYPE omxclient_component_destroy(OMXCLIENT * appdata)
{
    OMX_ERRORTYPE omxError;
    OMX_STATETYPE state;
    OMX_BOOL timeout;
    timeout = OMX_FALSE;
    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "omxclient_component_destroy\n");

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetState(appdata->component, &state),
                              omxError);
    while(state != OMX_StateLoaded)
    {
        switch (state)
        {
        case OMX_StateWaitForResources:
            return OMX_ErrorInvalidState;

        case OMX_StateLoaded:
            /* Buffers should be deallocated during state transition from idle to loaded, however,
             * if buffer allocation has been failed, component has not been able to move into idle */
            {
                OMX_COMPONENTTYPE *pComp = (OMX_COMPONENTTYPE *)appdata->component;
                pComp->ComponentDeInit(appdata->component);
            }
            break;

        case OMX_StateExecuting:
        case OMX_StatePause:

            OMXCLIENT_RETURN_ON_ERROR(omxclient_change_state_and_wait
                                      (appdata, OMX_StateIdle), omxError);

            state = OMX_StateIdle;
            break;

        case OMX_StateIdle:

            OSAL_EventReset(appdata->state_event);

            OMXCLIENT_RETURN_ON_ERROR(OMX_SendCommand
                                      (appdata->component, OMX_CommandStateSet,
                                       OMX_StateLoaded, NULL), omxError);

            omxclient_component_free_buffers(appdata);

            /* wait for LOADED */
            OMXCLIENT_RETURN_ON_ERROR(omxclient_wait_state
                                      (appdata, OMX_StateLoaded), omxError);

            state = OMX_StateLoaded;
            break;

        case OMX_StateInvalid:
            return OMX_FreeHandle(appdata->component);
            break;

        default:
            return OMX_ErrorInvalidState;
        }
    }

    list_destroy(&(appdata->input_queue));
    list_destroy(&(appdata->output_queue));
    list_destroy(&(appdata->osd_queue));

    OSAL_MutexDestroy(appdata->queue_mutex);
    appdata->queue_mutex = 0;

    OSAL_EventDestroy(appdata->state_event);
    appdata->state_event = 0;

    if (appdata->plinksink != NULL)
    {
        PlinkPacket pkt;
        PlinkMsg msg;
        msg.header.type = PLINK_TYPE_MESSAGE;
        msg.header.size = DATA_SIZE(PlinkMsg);
        msg.msg = PLINK_EXIT_CODE;
        pkt.list[0] = &msg;
        pkt.num = 1;
        pkt.fd = PLINK_INVALID_FD;
        PLINK_send(appdata->plinksink, appdata->channel, &pkt);

        sleep(1); // Sleep one second to make sure client is ready for exit

        PLINK_close(appdata->plinksink, 0);
        appdata->plinksink = NULL;
    }

    return OMX_FreeHandle(appdata->component);
}

OMX_ERRORTYPE omxclient_component_free_buffers(OMXCLIENT * appdata)
{
    OMX_ERRORTYPE error = OMX_ErrorNone;
    OMX_U32 i;
    OMX_BUFFERHEADERTYPE *hdr;

    for(i = list_available(&(appdata->input_queue));
        i && error == OMX_ErrorNone; --i)
    {
        list_get_header(&(appdata->input_queue), &hdr);
        error = OMX_FreeBuffer(appdata->component, 0, hdr);
    }

    for(i = list_available(&(appdata->output_queue));
        i && error == OMX_ErrorNone; --i)
    {
        list_get_header(&(appdata->output_queue), &hdr);
        error = OMX_FreeBuffer(appdata->component, 1, hdr);
    }

    for(i = list_available(&(appdata->osd_queue));
        i && error == OMX_ErrorNone; --i)
    {
        list_get_header(&(appdata->osd_queue), &hdr);
        if (hdr != NULL)
            error = OMX_FreeBuffer(appdata->component, 2, hdr);
    }

    return error;
}

OMX_ERRORTYPE omxclient_wait_state(OMXCLIENT * client, OMX_STATETYPE state)
{
    OSAL_BOOL event_timeout = OMX_FALSE;
    OMX_ERRORTYPE error;
    OMX_STATETYPE current_state;

    /* change state to EXECUTING */
    error = OMX_GetState(client->component, &current_state);
    if(current_state != state)
    {
        OMXCLIENT_RETURN_ON_ERROR(OSAL_EventWait
                                  (client->state_event, OMXCLIENT_EVENT_TIMEOUT,
                                   &event_timeout), error);

        if(event_timeout)
            return OMX_ErrorTimeout;

        error = OMX_GetState(client->component, &current_state);
        if(error != OMX_ErrorNone)
            return error;

        if(current_state != state)
            return OMX_ErrorInvalidState;
    }

    return error;
}

OMX_ERRORTYPE omxclient_change_state_and_wait(OMXCLIENT * client,
                                              OMX_STATETYPE state)
{
    OSAL_BOOL event_timeout = OMX_FALSE;
    OMX_ERRORTYPE error;
    OMX_STATETYPE current_state;

    /* change state to EXECUTING */
    error = OMX_GetState(client->component, &current_state);
    if(current_state != state)
    {
        OSAL_EventReset(client->state_event);

        OMXCLIENT_RETURN_ON_ERROR(OMX_SendCommand
                                  (client->component, OMX_CommandStateSet,
                                   state, NULL), error);

        OMXCLIENT_RETURN_ON_ERROR(OSAL_EventWait
                                  (client->state_event, OMXCLIENT_EVENT_TIMEOUT,
                                   &event_timeout), error);

        if(event_timeout)
            return OMX_ErrorTimeout;

        error = OMX_GetState(client->component, &current_state);
        if(error != OMX_ErrorNone)
            return error;

        if(current_state != state)
            return OMX_ErrorInvalidState;
    }

    return error;
}

/*------------------------------------------------------------------------------

    NextVop

    Function calculates next input vop depending input and output frame
    rates.

    Input   inputRateNumer  (input.yuv) frame rate numerator.
            inputRateDenom  (input.yuv) frame rate denominator
            outputRateNumer (stream.mpeg4) frame rate numerator.
            outputRateDenom (stream.mpeg4) frame rate denominator.
            frameCnt        Frame counter.
            firstVop        The first vop of input.yuv sequence.

    Return  next    The next vop of input.yuv sequence.

------------------------------------------------------------------------------*/
OMX_U32 omxclient_next_vop(OMX_U32 inputRateNumer, OMX_U32 inputRateDenom,
                           OMX_U32 outputRateNumer, OMX_U32 outputRateDenom,
                           OMX_U32 frameCnt, OMX_U32 firstVop)
{
    OMX_U32 sift;
    OMX_U32 skip;
    OMX_U32 numer;
    OMX_U32 denom;
    OMX_U32 next;

    numer = inputRateNumer * outputRateDenom;
    denom = inputRateDenom * outputRateNumer;

    if(numer >= denom)
    {
        sift = 9;
        do
        {
            sift--;
        }
        while(((numer << sift) >> sift) != numer);
    }
    else
    {
        sift = 17;
        do
        {
            sift--;
        }
        while(((numer << sift) >> sift) != numer);
    }
    skip = (numer << sift) / denom;
    next = ((frameCnt * skip) >> sift) + firstVop;

    return next;
}

/*------------------------------------------------------------------------------

    ReadVop

    Read raw YUV image data from file
    Image is divided into slices, each slice consists of equal amount of
    image rows except for the bottom slice which may be smaller than the
    others. sliceNum is the number of the slice to be read
    and sliceRows is the amount of rows in each slice (or 0 for all rows).

------------------------------------------------------------------------------*/
OMX_S32 omxclient_read_vop_sliced(OMX_U8 * image, OMX_U32 width, OMX_U32 height,
                                  OMX_U32 stride, OMX_U32 alignment,
                                  OMX_U32 sliceNum, OMX_U32 sliceRows,
                                  OMX_U32 frameNum, FILE * file,
                                  OMX_COLOR_FORMATTYPE inputMode)
{
    OMX_U32 byteCount = 0;
    OMX_U32 frameSize;
    OMX_U32 frameOffset;
    OMX_U32 sliceLumOffset = 0;
    OMX_U32 sliceCbOffset = 0;
    OMX_U32 sliceCrOffset = 0;
    OMX_U32 sliceLumSize;    /* The size of one slice in bytes */
    OMX_U32 sliceCbSize;
    OMX_U32 sliceCrSize;
    OMX_U32 sliceLumSizeRead;   /* The size of the slice to be read */
    OMX_U32 sliceCbSizeRead;
    OMX_U32 sliceCrSizeRead;
    OMX_U8 * buf;
    OMX_U32 strideChr;
    OMX_U32 widthChr;
    OMX_U32 i;

    if(sliceRows == 0)
    {
        sliceRows = height;
    }

    if(inputMode == OMX_COLOR_FormatYUV420Planar)
    {
        /* YUV 4:2:0 planar */
        frameSize = width * height + (width / 2 * height / 2) * 2;
        sliceLumSizeRead = sliceLumSize = width * sliceRows;
        sliceCbSizeRead = sliceCbSize = width / 2 * sliceRows / 2;
        sliceCrSizeRead = sliceCrSize = width / 2 * sliceRows / 2;
        strideChr = (stride / 2 + alignment - 1) & ~(alignment - 1);
        widthChr = width / 2;
    }
    else if(inputMode == OMX_COLOR_FormatYUV420SemiPlanar)
    {
        /* YUV 4:2:0 semiplanar */
        frameSize = width * height + (width / 2 * height / 2) * 2;
        sliceLumSizeRead = sliceLumSize = width * sliceRows;
        sliceCbSizeRead = sliceCbSize = width * sliceRows / 2;
        sliceCrSizeRead = sliceCrSize = 0;
        strideChr = stride;
        widthChr = width;
    }
    else
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "Input color format is not supported\n");
        return -1;
    }

    /* The bottom slice may be smaller than the others */
    if(sliceRows * (sliceNum + 1) > height)
    {
        sliceRows = height - sliceRows * sliceNum;

        if(inputMode == OMX_COLOR_FormatYUV420Planar)
        {
            sliceLumSizeRead = width * sliceRows;
            sliceCbSizeRead = width / 2 * sliceRows / 2;
            sliceCrSizeRead = width / 2 * sliceRows / 2;
        }
        else if(inputMode == OMX_COLOR_FormatYUV420SemiPlanar)
        {
            sliceLumSizeRead = width * sliceRows;
            sliceCbSizeRead = width * sliceRows / 2;
        }
    }

    /* Offset for frame start from start of file */
    frameOffset = frameSize * frameNum;
    /* Offset for slice luma start from start of frame */
    sliceLumOffset = sliceLumSize * sliceNum;
    /* Offset for slice cb start from start of frame */
    if(inputMode == OMX_COLOR_FormatYUV420Planar ||
       inputMode == OMX_COLOR_FormatYUV420SemiPlanar)
        sliceCbOffset = width * height + sliceCbSize * sliceNum;
    /* Offset for slice cr start from start of frame */
    if(inputMode == OMX_COLOR_FormatYUV420Planar)
        sliceCrOffset = width * height +
            width / 2 * height / 2 + sliceCrSize * sliceNum;

    /* Read input from file frame by frame */
    if(file == NULL)
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "Input file not provided\n");
        return -1;
    }

    fseek(file, frameOffset + sliceLumOffset, SEEK_SET);
    buf = image;
    for (i = 0; i < sliceRows; i++)
    {
        fread(buf, 1, width, file);
        byteCount += stride;
        buf += stride;
    }
    if(sliceCbSizeRead)
    {
        for (i = 0; i < sliceRows/2; i++)
        {
            fread(buf, 1, widthChr, file);
            byteCount += strideChr;
            buf += strideChr;
        }
    }
    if(sliceCrSizeRead)
    {
        for (i = 0; i < sliceRows/2; i++)
        {
            fread(buf, 1, widthChr, file);
            byteCount += strideChr;
            buf += strideChr;
        }
    }

    /* Stop if last VOP of the file */
    if(feof(file))
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "Can't read VOP no: %d\n",
                       frameNum);
        return -1;
    }
    return byteCount;
}

/**
 *
 */
OMX_ERRORTYPE omxclient_execute_yuv_range(OMXCLIENT * appdata,
                                          OMX_STRING input_filename,
                                          OMX_STRING output_filename,
                                          OMX_STRING osd_filename,
                                          OMX_U32 firstVop, OMX_U32 lastVop)
{
    OMX_ERRORTYPE omxError = OMX_ErrorNone;
    //OMX_STATETYPE state = OMX_StateLoaded;
    OMX_U32 last_pos, i;
    OMX_U32 src_img_size, vop;
    OMX_U64 vop_count = 0;
    OMX_U32 src_lum_size, src_chr_size;
    OMX_U32 osd_img_size;
    OMX_U32 byte_count = 0;
    FILE *fLayer;
    char filename[100];

    OMX_PARAM_PORTDEFINITIONTYPE input_port;
    OMX_PARAM_PORTDEFINITIONTYPE output_port;
    OMX_PARAM_PORTDEFINITIONTYPE osd_port;
    OMX_VIDEO_VP8REFERENCEFRAMETYPE vp8Ref;
    OMX_CONFIG_INTRAREFRESHVOPTYPE intraRefresh;
    PlinkPacket recvpkt;

    omxclient_struct_init(&vp8Ref, OMX_VIDEO_VP8REFERENCEFRAMETYPE);
    omxclient_struct_init(&intraRefresh, OMX_CONFIG_INTRAREFRESHVOPTYPE);

    /* get port definitions */
    omxclient_struct_init(&input_port, OMX_PARAM_PORTDEFINITIONTYPE);
    omxclient_struct_init(&output_port, OMX_PARAM_PORTDEFINITIONTYPE);
    omxclient_struct_init(&osd_port, OMX_PARAM_PORTDEFINITIONTYPE);

    input_port.nPortIndex = 0;
    output_port.nPortIndex = 1;
    osd_port.nPortIndex = 2;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (appdata->component, OMX_IndexParamPortDefinition,
                               &input_port), omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (appdata->component, OMX_IndexParamPortDefinition,
                               &output_port), omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (appdata->component, OMX_IndexParamPortDefinition,
                               &osd_port), omxError);

    /* open input and output files */
    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Using input '%s'\n", input_filename);
    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Using output '%s'\n",
                   output_filename);

    appdata->input = NULL;
    appdata->output = NULL;
    appdata->output_size = 0;
    appdata->plinksink = NULL;
    appdata->osd = NULL;

    char error_string[256];

    memset(error_string, 0, sizeof(error_string));

    OMX_CSI_BUFFER_MODE_CONFIGTYPE bufferMode;
    omxclient_struct_init(&bufferMode, OMX_CSI_BUFFER_MODE_CONFIGTYPE);
    bufferMode.nPortIndex = 0;
    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                                (appdata->component, OMX_CSI_IndexParamBufferMode,
                                &bufferMode), omxError);

    /* Open input file */
    if(strncmp(input_filename, "plink:", strlen("plink:")) == 0 &&
        bufferMode.eMode == OMX_CSI_BUFFER_MODE_DMA)
    {
        OMX_STRING inname = input_filename + strlen("plink:");
        if (PLINK_create(&appdata->plinksink, inname, PLINK_MODE_CLIENT) != PLINK_STATUS_OK)
        {
            strerror_r(errno, error_string, sizeof(error_string));
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

            return OMX_ErrorStreamCorrupt;
        }

        if (PLINK_connect(appdata->plinksink, 0) != PLINK_STATUS_OK)
        {
            strerror_r(errno, error_string, sizeof(error_string));
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

            return OMX_ErrorStreamCorrupt;
        }
    }
    else if (bufferMode.eMode == OMX_CSI_BUFFER_MODE_NORMAL)
    {
        appdata->input = fopen(input_filename, "rb");
        if(appdata->input == NULL)
        {
            strerror_r(errno, error_string, sizeof(error_string));
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

            return OMX_ErrorStreamCorrupt;
        }
    }
    else
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "ERROR: Buffer mode doesn't match input file.");
        return OMX_ErrorBadParameter;
    }

    if (osd_filename)
    {
        appdata->osd = fopen(osd_filename, "rb");
        if(appdata->osd == NULL)
        {
            strerror_r(errno, error_string, sizeof(error_string));
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

            return OMX_ErrorStreamCorrupt;
        }
    }

    bufferMode.nPortIndex = 1;
    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                                (appdata->component, OMX_CSI_IndexParamBufferMode,
                                &bufferMode), omxError);
    if (bufferMode.eMode == OMX_CSI_BUFFER_MODE_NORMAL)
    {
        appdata->output = fopen(output_filename, "wb");
        if(appdata->output == NULL)
        {
            strerror_r(errno, error_string, sizeof(error_string));
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

            return OMX_ErrorStreamCorrupt;
        }
    }
    else
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "ERROR: Buffer mode doesn't match input file.");
        return OMX_ErrorBadParameter;
    }

    /* change component state */

    /* -> waiting for IDLE */
    OMXCLIENT_RETURN_ON_ERROR(omxclient_wait_state(appdata, OMX_StateIdle),
                              omxError);

    /* -> transition to EXECUTING */
    OMXCLIENT_RETURN_ON_ERROR(omxclient_change_state_and_wait
                              (appdata, OMX_StateExecuting), omxError);

    /* Tell component to ... fill these buffers */
    OMX_BUFFERHEADERTYPE *output_buffer = NULL;

    do
    {
        list_get_header(&(appdata->output_queue), &output_buffer);
        if(output_buffer)
        {
            output_buffer->nOutputPortIndex = 1;
            omxError = OMX_FillThisBuffer(appdata->component, output_buffer);

            if(omxError != OMX_ErrorNone)
            {
                return omxError;
            }
        }
    }
    while(output_buffer);

    /* run the decoder/encoder job */

    /* calculate input frame size */
    switch ((int)input_port.format.video.eColorFormat)
    {

    case OMX_COLOR_FormatYUV420Planar:
    case OMX_COLOR_FormatYUV420SemiPlanar:

        src_lum_size =
            input_port.format.video.nFrameWidth *
            input_port.format.video.nFrameHeight;
        src_chr_size = src_lum_size >> 1;
        src_img_size = src_lum_size + src_chr_size;
        break;

    default:
        return OMX_ErrorBadParameter;
    }

    last_pos = (lastVop + 1) * src_img_size;
    vop = omxclient_next_vop(Q16_FLOAT(input_port.format.video.xFramerate), 1,
                             Q16_FLOAT(output_port.format.video.xFramerate), 1,
                             0, firstVop);

    /* set input file to correct position */
    if(appdata->input && fseek(appdata->input, src_img_size * vop, SEEK_SET) != 0)
    {
        strerror_r(errno, error_string, sizeof(error_string));
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

        return OMX_ErrorStreamCorrupt;
    }

    /* calculate osd frame size */
    switch ((int)osd_port.format.video.eColorFormat)
    {

    case OMX_COLOR_FormatYUV420SemiPlanar:

        osd_img_size =
            osd_port.format.video.nFrameWidth *
            osd_port.format.video.nFrameHeight * 3 / 2;
        break;

    case OMX_COLOR_Format32bitARGB8888:

        osd_img_size =
            osd_port.format.video.nFrameWidth *
            osd_port.format.video.nFrameHeight * 4;
        break;

    case OMX_COLOR_FormatMonochrome:

        osd_img_size =
            osd_port.format.video.nFrameWidth *
            osd_port.format.video.nFrameHeight / 8;
        break;

    default:
        return OMX_ErrorBadParameter;
    }

    appdata->EOS = OMX_FALSE;

    OMX_BOOL eof = OMX_FALSE;
    OMX_U64 frame_count = 0;

    while(eof == OMX_FALSE  && !appdata->EOS)
    {
        OMX_BUFFERHEADERTYPE *input_buffer = NULL;

        /* Get input (synch) >> */
        OSAL_MutexLock(appdata->queue_mutex);
        {
            list_get_header(&appdata->input_queue, &input_buffer);
        }
        OSAL_MutexUnlock(appdata->queue_mutex);
        /* << Get input (synch) */

        if(input_buffer == NULL)
        {
            //OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "No input buffer available... wait\n");
            usleep(1000);
            continue;
        }

        OMX_BUFFERHEADERTYPE *osd_buffer = NULL;
        if (appdata->osd)
        {
            /* Get osd (synch) >> */
            OSAL_MutexLock(appdata->queue_mutex);
            {
                list_get_header(&appdata->osd_queue, &osd_buffer);
            }
            OSAL_MutexUnlock(appdata->queue_mutex);
            /* << Get osd (synch) */

            if(osd_buffer == NULL)
            {
                //OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "No input buffer available... wait\n");
                usleep(1000);
                continue;
            }
        }

        if(!appdata->input && !appdata->plinksink)
        {
            return OMX_ErrorInsufficientResources;
        }

        input_buffer->nInputPortIndex = 0;

        size_t ret = 0;

        if (osd_buffer)
        {
            size_t rbytes = fread(osd_buffer->pBuffer, 1, osd_img_size, appdata->osd);

            osd_buffer->nInputPortIndex = 2;
            osd_buffer->nOffset = 0;
            osd_buffer->nFilledLen = osd_buffer->nAllocLen;

            omxError = OMX_EmptyThisBuffer(appdata->component, osd_buffer);
            if(omxError != OMX_ErrorNone)
            {
                return omxError;
            }

            OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "\twrote %lu bytes to component for OSD\n", rbytes);
            usleep(0);
        }

        if (appdata->input != NULL)
        {
            OMX_U32 i = 0;
            OMX_U8* pBuffer = input_buffer->pBuffer;
            /* check last vop */
            if (!appdata->cache_mode || vop_count <= list_capacity(&appdata->input_queue))
            {
                switch ((int)input_port.format.video.eColorFormat)
                {

                case OMX_COLOR_FormatYUV420Planar:
                {
                    OMX_U32 alignment = input_port.nBufferAlignment;
                    OMX_U32 stride = input_port.format.video.nStride;
                    OMX_U32 stride_chroma = (stride / 2 + alignment - 1) & ~(alignment - 1);

                    for (i = 0; i < input_port.format.video.nFrameHeight; i++)
                    {
                        ret += fread(pBuffer, 1, input_port.format.video.nFrameWidth,
                                    appdata->input);
                        pBuffer += stride;
                    }

                    for (i = 0; i < input_port.format.video.nFrameHeight; i++)
                    {
                        ret += fread(pBuffer, 1, input_port.format.video.nFrameWidth / 2,
                                    appdata->input);
                        pBuffer += stride_chroma;
                    }
                    break;
                }

                case OMX_COLOR_FormatYUV420SemiPlanar:

                    for (i = 0; i < input_port.format.video.nFrameHeight*3/2; i++)
                    {
                        ret += fread(pBuffer, 1, input_port.format.video.nFrameWidth,
                                    appdata->input);
                        pBuffer += input_port.format.video.nStride;
                    }
                    break;

                default:
                    return OMX_ErrorBadParameter;
                }
            }
            else
            {
                ret = src_img_size;
            }

            /* feof does not indicate EOF if we don't read one byte more */
            /* if remaining data is less than one frame, send EOS. */
            if(ret == last_pos || ret < src_img_size || 
                (byte_count + ret + src_img_size > last_pos))
            {
                input_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
            }

            byte_count += ret;

            input_buffer->nOffset = 0;
            input_buffer->nFilledLen = input_buffer->nAllocLen;

            if(input_buffer->nFlags & OMX_BUFFERFLAG_EOS ||
            (eof = feof(appdata->input)) != 0)
            {
                eof = OMX_TRUE;
            }

            if(eof)
            {
                input_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
                OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "\tinput EOF reached\n");
            }

            int skip = omxclient_control_frame_rate(appdata, vop_count);
            if (!skip || (input_buffer->nFlags | OMX_BUFFERFLAG_EOS))
            {
                omxError = OMX_EmptyThisBuffer(appdata->component, input_buffer);
                if(omxError != OMX_ErrorNone)
                {
                    return omxError;
                }

                OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "\twrote %lu bytes to component\n", ret);
                usleep(0);
            }
            else
            {
                list_push_header(&appdata->input_queue, input_buffer);
            }
        }
        else
        {
            if (PLINK_recv(appdata->plinksink, 0, &recvpkt) == PLINK_STATUS_ERROR)
                return OMX_ErrorBadParameter;
            if (recvpkt.num != 1) // we assume the server send a single frame in one packet.
                return OMX_ErrorBadParameter;

            PlinkDescHdr *hdr = (PlinkDescHdr *)(recvpkt.list[i]);
            if (hdr->type == PLINK_TYPE_MESSAGE &&
                ((PlinkMsg *)recvpkt.list[i])->msg == PLINK_EXIT_CODE)
            {
                eof = OMX_TRUE;
                input_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
                input_buffer->nOffset = 0;
                input_buffer->nFilledLen = 0;
            }
            else if (hdr->type == PLINK_TYPE_2D_YUV)
            {
                PlinkYuvInfo *pic = (PlinkYuvInfo *)(recvpkt.list[i]);
                OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Received frame %d 0x%010llx: %dx%d, stride = luma %d, chroma %d\n", 
                        pic->header.id, pic->bus_address_y, 
                        pic->pic_width, pic->pic_height,
                        pic->stride_y, pic->stride_u);

                if (pic->stride_y != input_port.format.video.nStride ||
                    pic->pic_width != input_port.format.video.nFrameWidth ||
                    pic->pic_height != input_port.format.video.nFrameHeight)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "ERROR: Expect %ldx%ld with stride %ld while received %dx%d with stride %d\n",
                        input_port.format.video.nFrameWidth,
                        input_port.format.video.nFrameHeight,
                        input_port.format.video.nStride,
                        pic->pic_width,
                        pic->pic_height,
                        pic->stride_y);
                    return OMX_ErrorBadParameter;
                }

                if (recvpkt.fd == PLINK_INVALID_FD)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "ERROR: Received invalid dma-buf fd.\n");
                    return OMX_ErrorBadParameter;
                }
                else
                {
                    input_buffer->nOffset = 0;
                    input_buffer->nFilledLen = pic->stride_y * pic->pic_height * 3 / 2;
                    input_buffer->pBuffer = (OMX_U8 *)recvpkt.fd;
                }
            }
            else
                continue;

            if(eof)
            {
                input_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
                OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "\tinput EOF reached\n");
            }

            int skip = omxclient_control_frame_rate(appdata, vop_count);
            if (!skip || (input_buffer->nFlags | OMX_BUFFERFLAG_EOS))
            {
                omxError = OMX_EmptyThisBuffer(appdata->component, input_buffer);
                if(omxError != OMX_ErrorNone)
                {
                    return omxError;
                }
            }
            else
            {
                list_push_header(&appdata->input_queue, input_buffer);
            }
        }

        vop_count++;
    }

    /* get stream end event */
    while(appdata->EOS == OMX_FALSE)
    {
        usleep(1000);
    }

    return omxError;
}

/**
 *
 */
OMX_ERRORTYPE omxclient_execute_yuv_sliced(OMXCLIENT * appdata,
                                           OMX_STRING input_filename,
                                           OMX_STRING output_filename,
                                           OMX_U32 firstVop, OMX_U32 lastVop)
{
    OMX_ERRORTYPE omxError = OMX_ErrorNone;
    //OMX_STATETYPE state = OMX_StateLoaded;
    OMX_U32 vop, i;
    OMX_PARAM_PORTDEFINITIONTYPE input_port;
    PlinkPacket recvpkt;

    /* get port definitions */
    omxclient_struct_init(&input_port, OMX_PARAM_PORTDEFINITIONTYPE);
    input_port.nPortIndex = 0;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (appdata->component, OMX_IndexParamPortDefinition,
                               &input_port), omxError);

    /* open input and output files */
    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Using input '%s'\n", input_filename);
    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Using output '%s'\n",
                   output_filename);

    appdata->input = NULL;
    appdata->output = NULL;
    appdata->plinksink = NULL;

    char error_string[256];

    memset(error_string, 0, sizeof(error_string));

    OMX_CSI_BUFFER_MODE_CONFIGTYPE bufferMode;
    omxclient_struct_init(&bufferMode, OMX_CSI_BUFFER_MODE_CONFIGTYPE);
    bufferMode.nPortIndex = 0;
    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                                (appdata->component, OMX_CSI_IndexParamBufferMode,
                                &bufferMode), omxError);

    /* Open input file */
    if(strncmp(input_filename, "plink:", strlen("plink:")) == 0 &&
        bufferMode.eMode == OMX_CSI_BUFFER_MODE_DMA)
    {
        OMX_STRING inname = input_filename + strlen("plink:");
        if (PLINK_create(&appdata->plinksink, inname, PLINK_MODE_CLIENT) != PLINK_STATUS_OK)
        {
            strerror_r(errno, error_string, sizeof(error_string));
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

            return OMX_ErrorStreamCorrupt;
        }

        if (PLINK_connect(appdata->plinksink, 0) != PLINK_STATUS_OK)
        {
            strerror_r(errno, error_string, sizeof(error_string));
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

            return OMX_ErrorStreamCorrupt;
        }
    }
    else if (bufferMode.eMode == OMX_CSI_BUFFER_MODE_NORMAL)
    {
        appdata->input = fopen(input_filename, "rb");
        if(appdata->input == NULL)
        {
            strerror_r(errno, error_string, sizeof(error_string));
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

            return OMX_ErrorStreamCorrupt;
        }
    }
    else
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "ERROR: Buffer mode doesn't match input file.");
        return OMX_ErrorBadParameter;
    }

    bufferMode.nPortIndex = 1;
    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                                (appdata->component, OMX_CSI_IndexParamBufferMode,
                                &bufferMode), omxError);

    /* Open output file */
    if (bufferMode.eMode == OMX_CSI_BUFFER_MODE_NORMAL)
    {
        appdata->output = fopen(output_filename, "wb");
        if(appdata->output == NULL)
        {
            strerror_r(errno, error_string, sizeof(error_string));
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "'%s'\n", error_string);

            return OMX_ErrorStreamCorrupt;
        }
    }
    else
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "ERROR: Buffer mode doesn't match input file.");
        return OMX_ErrorBadParameter;
    }

    vop = firstVop;

    /* change component state */

    /* -> waiting for IDLE */
    OMXCLIENT_RETURN_ON_ERROR(omxclient_wait_state(appdata, OMX_StateIdle),
                              omxError);

    /* -> transition to EXECUTING */
    OMXCLIENT_RETURN_ON_ERROR(omxclient_change_state_and_wait
                              (appdata, OMX_StateExecuting), omxError);

    /* Tell component to ... fill these buffers */
    OMX_BUFFERHEADERTYPE *output_buffer = NULL;

    do
    {
        list_get_header(&(appdata->output_queue), &output_buffer);
        if(output_buffer)
        {
            output_buffer->nOutputPortIndex = 1;
            omxError = OMX_FillThisBuffer(appdata->component, output_buffer);

            if(omxError != OMX_ErrorNone)
            {
                return omxError;
            }
        }
    }
    while(output_buffer);

    /* run the decoder/encoder job */

    appdata->EOS = OMX_FALSE;
    OMX_BOOL eof = OMX_FALSE;

    OMX_U32 count = 0;

    OMX_U32 slice = 0;

    while(eof == OMX_FALSE  && !appdata->EOS)
    {
        OMX_BUFFERHEADERTYPE *input_buffer = NULL;

        /* Get input (synch) >> */
        OSAL_MutexLock(appdata->queue_mutex);
        {
            list_get_header(&appdata->input_queue, &input_buffer);
        }
        OSAL_MutexUnlock(appdata->queue_mutex);
        /* << Get input (synch) */

        if(input_buffer == NULL)
        {
            /* NOTE: output buffer not available, wait -> event */
            usleep(10 * 1000);
            continue;
        }

        if(!appdata->input && !appdata->plinksink)
        {
            return OMX_ErrorInsufficientResources;
        }

        input_buffer->nInputPortIndex = 0;

        if(appdata->input)
        {
            OMX_U32 read_count = (input_port.format.image.nSliceHeight == 0)
                ? input_port.format.image.nFrameHeight
                : input_port.format.image.nSliceHeight;

            OMX_S32 ret = omxclient_read_vop_sliced(input_buffer->pBuffer,
                                                    input_port.format.image.nFrameWidth,
                                                    input_port.format.image.nFrameHeight,
                                                    input_port.format.image.nStride,
                                                    input_port.nBufferAlignment,
                                                    slice,
                                                    read_count,
                                                    vop,
                                                    appdata->input,
                                                    input_port.format.image.eColorFormat);

            if(ret == -1)
            {
                eof = OMX_TRUE;
                input_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
                ret = 0;

                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "\tinput EOF reached\n");
            }
            else
            {
                count += read_count;
                if(count < input_port.format.image.nFrameHeight)
                {
                    ++slice;
                }
                else
                {
                    slice = 0;
                    count = 0;
                    ++vop;
                }
            }

            if(vop == lastVop && count == 0)
            {
                eof = OMX_TRUE;
                input_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
            }

            input_buffer->nFilledLen = ret;
            input_buffer->nOffset = 0;

            omxError = OMX_EmptyThisBuffer(appdata->component, input_buffer);
            if(omxError != OMX_ErrorNone)
            {
                return omxError;
            }

            OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "\twrote %u bytes to component\n",
                        ret);
        }
        else
        {
            if (PLINK_recv(appdata->plinksink, 0, &recvpkt) == PLINK_STATUS_ERROR)
                return OMX_ErrorBadParameter;
            if (recvpkt.num != 1) // we assume the server send a single frame in one packet.
                return OMX_ErrorBadParameter;

            PlinkDescHdr *hdr = (PlinkDescHdr *)(recvpkt.list[i]);
            if (hdr->type == PLINK_TYPE_MESSAGE &&
                ((PlinkMsg *)recvpkt.list[i])->msg == PLINK_EXIT_CODE)
            {
                eof = OMX_TRUE;
                input_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
                input_buffer->nOffset = 0;
                input_buffer->nFilledLen = 0;
            }
            else if (hdr->type == PLINK_TYPE_2D_YUV)
            {
                PlinkYuvInfo *pic = (PlinkYuvInfo *)(recvpkt.list[i]);
                OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "Received frame %d 0x%010llx: %dx%d, stride = luma %d, chroma %d\n", 
                        pic->header.id, pic->bus_address_y, 
                        pic->pic_width, pic->pic_height,
                        pic->stride_y, pic->stride_u);

                if (pic->stride_y != input_port.format.video.nStride ||
                    pic->pic_width != input_port.format.video.nFrameWidth ||
                    pic->pic_height != input_port.format.video.nFrameHeight)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "ERROR: Expect %ldx%ld with stride %ld while received %dx%d with stride %d\n",
                        input_port.format.video.nFrameWidth,
                        input_port.format.video.nFrameHeight,
                        input_port.format.video.nStride,
                        pic->pic_width,
                        pic->pic_height,
                        pic->stride_y);
                    return OMX_ErrorBadParameter;
                }

                if (recvpkt.fd == PLINK_INVALID_FD)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "ERROR: Received invalid dma-buf fd.\n");
                    return OMX_ErrorBadParameter;
                }
                else
                {
                    input_buffer->nOffset = 0;
                    input_buffer->nFilledLen = pic->stride_y * pic->pic_height * 3 / 2;
                    input_buffer->pBuffer = (OMX_U8 *)recvpkt.fd;
                    ++vop;
                }
            }
            else
                continue;

            if (vop == lastVop)
                eof = OMX_TRUE;
            if(eof)
            {
                input_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
                OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "\tinput EOF reached\n");
            }

            omxError = OMX_EmptyThisBuffer(appdata->component, input_buffer);
            if(omxError != OMX_ErrorNone)
            {
                return omxError;
            }
        }

        usleep(0);
    }

    /* get stream end event */
    while(appdata->EOS == OMX_FALSE)
    {
        usleep(1000);
    }

    return omxError;
}

OMX_ERRORTYPE omxclient_initialize_buffers(OMXCLIENT * client)
{
    OMX_U32 i, j;
    OMX_ERRORTYPE omxError;
    OMX_PARAM_PORTDEFINITIONTYPE port;
    OSAL_EventReset(client->state_event);

    // create the buffers
    OMXCLIENT_RETURN_ON_ERROR(OMX_SendCommand
                              (client->component, OMX_CommandStateSet,
                               OMX_StateIdle, NULL), omxError);

    for(j = 0; j < client->ports; ++j)
    {
        omxclient_struct_init(&port, OMX_PARAM_PORTDEFINITIONTYPE);
        port.nPortIndex = j;

        OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter(client->component,
                                                   OMX_IndexParamPortDefinition,
                                                   (OMX_PTR) &port), omxError);

        /* allocate buffers */
        for(i = 0; i < port.nBufferCountActual; ++i)
        {
            OMX_BUFFERHEADERTYPE *header = 0;

            OMXCLIENT_RETURN_ON_ERROR(OMX_AllocateBuffer(client->component,
                                                         &header,
                                                         port.nPortIndex, 0,
                                                         port.nBufferSize), omxError);

            switch (port.eDir)
            {
            case OMX_DirInput:
                if (port.nPortIndex == 2) // OSD
                    list_push_header(&(client->osd_queue), header);
                else
                    list_push_header(&(client->input_queue), header);
                break;

            case OMX_DirOutput:
                list_push_header(&(client->output_queue), header);
                break;

            case OMX_DirMax:
            default:
                assert(!"Port direction may not be 'OMX_DirMax'.");
                break;
            }
        }
    }

    return omxError;
}

OMX_ERRORTYPE omxclient_component_initialize(OMXCLIENT * appdata,
                                             OMX_ERRORTYPE
                                             (*port_configurator_fn) (OMXCLIENT
                                                                      * appdata,
                                                                      OMX_PARAM_PORTDEFINITIONTYPE
                                                                      * port))
{
    OMX_ERRORTYPE omxError;
    OMX_PORT_PARAM_TYPE oPortParam;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef[3];
    OMX_U32 i;

    omxclient_struct_init(&oPortParam, OMX_PORT_PARAM_TYPE);

    /* detect all video ports of component */
    omxError =
        OMX_GetParameter(appdata->component, OMX_IndexParamVideoInit,
                         &oPortParam);
    if(omxError == OMX_ErrorNone)
    {
        if(oPortParam.nPorts > 0x00)
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG,
                           "Detected %i video ports starting at %i\n",
                           oPortParam.nPorts, oPortParam.nStartPortNumber);

            appdata->ports = oPortParam.nPorts;

            /* NOTE: should the ports be known beforehand? */

            for(i = 0; i < oPortParam.nPorts; ++i)
            {
                omxclient_struct_init(&(sPortDef[i]), OMX_PARAM_PORTDEFINITIONTYPE);
                sPortDef[i].nPortIndex = oPortParam.nStartPortNumber + i;

                OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter(appdata->component,
                                                           OMX_IndexParamPortDefinition,
                                                           (OMX_PTR) & (sPortDef[i])),
                                                            omxError);

                OMXCLIENT_RETURN_ON_ERROR(port_configurator_fn
                                          (appdata, &(sPortDef[i])), omxError);

                OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter(appdata->component,
                                                           OMX_IndexParamPortDefinition,
                                                           &(sPortDef[i])),omxError);

                OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter(appdata->component,
                                                           OMX_IndexParamPortDefinition,
                                                           (OMX_PTR) & (sPortDef[i])),
                                                            omxError);

                OMX_OSAL_TracePortSettings(OMX_OSAL_TRACE_INFO, &(sPortDef[i]));
            }

        }
        else
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                           "Component has no video ports.\n", oPortParam.nPorts,
                           oPortParam.nStartPortNumber);
        }
    }
    else
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                       "Initializing video port parameters failed: '%s'\n",
                       OMX_OSAL_TraceErrorStr(omxError));

        /* error propagated onwards */
    }

    return omxError;
}

OMX_ERRORTYPE omxclient_component_initialize_image(OMXCLIENT * appdata,
                                                  OMX_ERRORTYPE
                                                  (*port_configurator_fn)
                                                  (OMXCLIENT * appdata,
                                                   OMX_PARAM_PORTDEFINITIONTYPE
                                                   * port))
{
    OMX_ERRORTYPE omxError;
    OMX_PORT_PARAM_TYPE oPortParam;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef[3];
    OMX_U32 i;

    omxclient_struct_init(&oPortParam, OMX_PORT_PARAM_TYPE);

    /* detect all image ports of component */
    omxError =
        OMX_GetParameter(appdata->component, OMX_IndexParamImageInit,
                         &oPortParam);
    if(omxError == OMX_ErrorNone)
    {
        if(oPortParam.nPorts > 0x00)
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG,
                           "Detected %i image ports starting at %i\n",
                           oPortParam.nPorts, oPortParam.nStartPortNumber);

            appdata->ports = oPortParam.nPorts;

            /* NOTE: should the ports be known beforehand? */

            for(i = 0; i < oPortParam.nPorts; ++i)
            {
                omxclient_struct_init(&(sPortDef[i]), OMX_PARAM_PORTDEFINITIONTYPE);
                sPortDef[i].nPortIndex = oPortParam.nStartPortNumber + i;

                OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter(appdata->component,
                                                           OMX_IndexParamPortDefinition,
                                                           (OMX_PTR) &
                                                           (sPortDef[i])),
                                                            omxError);

                port_configurator_fn(appdata, &(sPortDef[i]));

                OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter(appdata->component,
                                                           OMX_IndexParamPortDefinition,
                                                           &(sPortDef[i])),
                                                            omxError);

                OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter(appdata->component,
                                                           OMX_IndexParamPortDefinition,
                                                           (OMX_PTR) &
                                                           (sPortDef[i])),
                                                            omxError);

                OMX_OSAL_TracePortSettings(OMX_OSAL_TRACE_INFO, &(sPortDef[i]));
            }

        }
        else
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                           "Component has no image ports.\n", oPortParam.nPorts,
                           oPortParam.nStartPortNumber);
        }
    }
    else
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                       "Initializing image port parameters failed: '%s'\n",
                       OMX_OSAL_TraceErrorStr(omxError));

        /* error propagated onwards */
    }

    return omxError;
}

/**
 *
 */
OMX_ERRORTYPE omxclient_check_component_version(OMX_IN OMX_HANDLETYPE
                                                hComponent)
{
    OMX_ERRORTYPE omxError;
    OMX_STRING componentName;
    OMX_VERSIONTYPE componentVersion;
    OMX_VERSIONTYPE specVersion;
    OMX_UUIDTYPE uuid;

    componentName = (OMX_STRING) OSAL_Malloc(OMX_MAX_STRINGNAME_SIZE);

    if(componentName)
    {
        omxError = OMX_GetComponentVersion(hComponent, componentName,
                                           &componentVersion, &specVersion,
                                           &uuid);

        if(omxError == OMX_ErrorNone)
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO,
                           "Tester IL-Specification version: %d.%d.%d.%d\n",
                           OMX_VERSION_MAJOR, OMX_VERSION_MINOR,
                           OMX_VERSION_REVISION, OMX_VERSION_STEP);

            OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO,
                           "Component IL-Specification version: %d.%d.%d.%d\n",
                           specVersion.s.nVersionMajor,
                           specVersion.s.nVersionMinor, specVersion.s.nRevision,
                           specVersion.s.nStep);

            OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO,
                           "Component version: %d.%d.%d.%d\n",
                           componentVersion.s.nVersionMajor,
                           componentVersion.s.nVersionMinor,
                           componentVersion.s.nRevision,
                           componentVersion.s.nStep);

            /* OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO, "Component uuid: %s\n", uuid); */

            if(specVersion.s.nVersionMajor != OMX_VERSION_MAJOR ||
               specVersion.s.nVersionMinor != OMX_VERSION_MINOR ||
               specVersion.s.nRevision != OMX_VERSION_REVISION ||
               specVersion.s.nStep != OMX_VERSION_STEP)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Used OMX IL-specification versions differ\n");
                omxError = OMX_ErrorVersionMismatch;
            }
        }

        OSAL_Free((OMX_PTR) componentName);
    }

    return omxError;
}
