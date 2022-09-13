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

#ifndef OMXTESTCOMMON_
#define OMXTESTCOMMON_

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "OMX_Component.h"
#include "OMX_CsiExt.h"
#include "OSAL.h"

/**
 *
 */
typedef struct HEADERLIST
{
    OMX_BUFFERHEADERTYPE **hdrs;

    OMX_U32 readpos;
    OMX_U32 writepos;
    OMX_U32 capacity;

} HEADERLIST;

typedef struct OMXCLIENT
{
    int id;
    OMX_HANDLETYPE component;
    OMX_HANDLETYPE queue_mutex;
    OMX_HANDLETYPE state_event;

    HEADERLIST input_queue;
    HEADERLIST output_queue;
    HEADERLIST osd_queue;

    OMX_BOOL EOS;

    OMX_STRING output_name;

    FILE *input;
    FILE *output;
    FILE *osd;

    void *file_buffer;

    void *plinksink;
    int channel;

    OMX_U64 frame_count;
    OMX_U32 output_size;
    OMX_PORTDOMAINTYPE domain;
    OMX_VIDEO_CODINGTYPE coding_type;

    OMX_U32 frame_rate_numer;
    OMX_U32 frame_rate_denom;
    struct timeval start;

    OMX_U32 ports;
    OMX_BOOL cache_mode; // only load the first nBufferCountMin frames from file, and reuse for remaining encoding. For perf test
} OMXCLIENT;

typedef int (*read_func)(FILE*, char*, int, OMX_BOOL*);

#define OMXCLIENT_PTR(P) ((OMXCLIENT*)P)

/* define event timeout if not defined */
#ifndef OMXCLIENT_EVENT_TIMEOUT
#define OMXCLIENT_EVENT_TIMEOUT 10000    /* ms */
#endif

/**
 * Common struct initialization
 */
#define omxclient_struct_init(_p_, _name_) \
	do { \
	memset((_p_), 0, sizeof(_name_)); \
	(_p_)->nSize = sizeof(_name_); \
	(_p_)->nVersion.s.nVersionMajor = OMX_VERSION_MINOR; \
	(_p_)->nVersion.s.nVersionMinor = OMX_VERSION_MAJOR; \
	(_p_)->nVersion.s.nRevision = OMX_VERSION_REVISION; \
	(_p_)->nVersion.s.nStep  = OMX_VERSION_STEP; \
	} while(0)

#define OMXCLIENT_RETURN_ON_ERROR(f, e) \
do { \
	(e) = (f); \
	if((e) != OMX_ErrorNone) \
    	return (e); \
} while(0)

#ifdef __CPLUSPLUS
extern "C"
{
#endif                       /* __CPLUSPLUS */

/* function prototypes */
    OMX_ERRORTYPE omxclient_wait_state(OMXCLIENT * client, OMX_STATETYPE state);

    OMX_ERRORTYPE omxclient_change_state_and_wait(OMXCLIENT * client,
                                                  OMX_STATETYPE state);

    OMX_ERRORTYPE omxclient_component_create(OMXCLIENT * ppComp,
                                             OMX_STRING cComponentName,
                                             OMX_STRING cRole,
                                             OMX_U32 buffer_count);

    OMX_ERRORTYPE omxclient_component_destroy(OMXCLIENT * appdata);

    OMX_ERRORTYPE omxclient_component_initialize(OMXCLIENT * appdata,
                                                 OMX_ERRORTYPE
                                                 (*port_configurator_fn)
                                                 (OMXCLIENT * appdata,
                                                  OMX_PARAM_PORTDEFINITIONTYPE *
                                                  port));

    OMX_ERRORTYPE omxclient_component_initialize_image(OMXCLIENT * appdata,
                                                      OMX_ERRORTYPE
                                                      (*port_configurator_fn)
                                                      (OMXCLIENT * appdata,
                                                       OMX_PARAM_PORTDEFINITIONTYPE
                                                       * port));

    OMX_ERRORTYPE omxclient_initialize_buffers(OMXCLIENT * client);

    OMX_ERRORTYPE omxclient_component_free_buffers(OMXCLIENT * client);

    OMX_ERRORTYPE omxclient_check_component_version(OMX_IN OMX_HANDLETYPE
                                                    hComponent);

    OMX_ERRORTYPE omxclient_execute_yuv_range(OMXCLIENT * appdata,
                                              OMX_STRING input_filename,
                                              OMX_STRING output_filename,
                                              OMX_STRING osd_filename,
                                              OMX_U32 firstVop,
                                              OMX_U32 lastVop);

    OMX_ERRORTYPE omxclient_execute_yuv_sliced(OMXCLIENT * appdata,
                                               OMX_STRING input_filename,
                                               OMX_STRING output_filename,
                                               OMX_U32 firstVop,
                                               OMX_U32 lastVop);

/* ---------------- TRACE-H -------------------- */

#define OMX_OSAL_TRACE_ERROR    (1 << 0)
#define OMX_OSAL_TRACE_WARNING  (1 << 1)
#define OMX_OSAL_TRACE_INFO     (1 << 2)
#define OMX_OSAL_TRACE_DEBUG    (1 << 3)
#define OMX_OSAL_TRACE_BUFFER   (1 << 4)

    OMX_ERRORTYPE OMX_OSAL_Trace(OMX_IN OMX_U32 nTraceFlags,
                                 OMX_IN char *format, ...);

    OMX_STRING OMX_OSAL_TraceErrorStr(OMX_IN OMX_ERRORTYPE omxError);

/* ---------------- TRACE - END -------------------- */

#ifdef __CPLUSPLUS
}
#endif                       /* __CPLUSPLUS */

#endif                       /*OMXTESTCOMMON_ */
