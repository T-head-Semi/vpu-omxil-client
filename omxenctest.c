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

#include <OMX_Core.h>
#include <OMX_Types.h>

/* std includes */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

/* test client */
#include "omxtestcommon.h"
#include "omxencparameters.h"

#define VIDEO_COMPONENT_NAME "OMX.hantro.H2.video.encoder"
#define IMAGE_COMPONENT_NAME "OMX.hantro.H2.image.encoder"

#define OMXENCODER_MAX_THREADS 2
pthread_t encode_thread[OMXENCODER_MAX_THREADS];

OMXENCODER_PARAMETERS parameters[2];


static int arg_count;

static char **arguments;

/* forward declarations */

OMX_U32 omxclient_next_vop(OMX_U32 inputRateNumer, OMX_U32 inputRateDenom,
                           OMX_U32 outputRateNumer, OMX_U32 outputRateDenom,
                           OMX_U32 frameCnt, OMX_U32 firstVop);

/*
    Macro that is used specifically to check parameter values
    in parameter checking loop.
 */
#define OMXENCODER_CHECK_NEXT_VALUE(i, args, argc, msg) \
    if(++(i) == argc || (*((args)[i])) == '-')  { \
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, msg); \
        return OMX_ErrorBadParameter; \
    }

/*
    omx_encoder_port_initialize
*/
OMX_ERRORTYPE omx_encoder_port_initialize(OMXCLIENT * appdata,
                                          OMX_PARAM_PORTDEFINITIONTYPE * p)
{
    OMX_ERRORTYPE error = OMX_ErrorNone;
    int id = appdata->id;

    switch (p->eDir)
    {
        /* CASE IN: initialize input port */
    case OMX_DirInput:
        OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO,
                       "Using port at index %i as input port\n", p->nPortIndex);

        if (p->nPortIndex == 2)
            error = process_encoder_osd_parameters(arg_count, arguments, p);
        else
            error = process_encoder_input_parameters(arg_count, arguments, p);
        break;

        /* CASE OUT: initialize output port */
    case OMX_DirOutput:

        OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO,
                       "Using port at index %i as output port\n",
                       p->nPortIndex);

        error = process_encoder_output_parameters(arg_count, arguments, p);
        appdata->domain = OMX_PortDomainVideo;

        if(error != OMX_ErrorNone)
            break;

        parameters[id].output_compression = p->format.video.eCompressionFormat;
        switch ((OMX_U32)parameters[id].output_compression)
        {
        case OMX_VIDEO_CodingAVC:
        {
            OMX_VIDEO_PARAM_AVCTYPE avc_parameters;
            omxclient_struct_init(&avc_parameters, OMX_VIDEO_PARAM_AVCTYPE);
            avc_parameters.nPortIndex = 1;

            error =
                process_avc_parameters(arg_count, arguments, &avc_parameters);
            break;
        }
        case OMX_CSI_VIDEO_CodingHEVC:
        {
            OMX_CSI_VIDEO_PARAM_HEVCTYPE hevc_parameters;
            omxclient_struct_init(&hevc_parameters, OMX_CSI_VIDEO_PARAM_HEVCTYPE);
            hevc_parameters.nPortIndex = 1;

            error =
                process_hevc_parameters(arg_count, arguments, &hevc_parameters);
            break;
        }
        default:
            return OMX_ErrorBadParameter;
        }

        break;

    case OMX_DirMax:
    default:
        break;
    }

    return error;
}

OMX_ERRORTYPE omx_encoder_image_port_initialize(OMXCLIENT * appdata,
                                               OMX_PARAM_PORTDEFINITIONTYPE * p)
{
    OMX_ERRORTYPE error = OMX_ErrorNone;
    int id = appdata->id;

    switch (p->eDir)
    {
        /* CASE IN: initialize input port */
    case OMX_DirInput:
        OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO,
                       "Using port at index %i as input port\n", p->nPortIndex);

        p->nBufferCountActual = parameters[id].buffer_count;
        error = process_encoder_image_input_parameters(arg_count, arguments, p);
        break;

        /* CASE OUT: initialize output port */
    case OMX_DirOutput:

        OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO,
                       "Using port at index %i as output port\n",
                       p->nPortIndex);

        p->nBufferCountActual = parameters[id].buffer_count;
        error = process_encoder_image_output_parameters(arg_count, arguments, p);
        appdata->coding_type = p->format.image.eCompressionFormat;
        appdata->domain = OMX_PortDomainImage;
        break;

    case OMX_DirMax:
    default:
        break;
    }
    return error;
}

OMX_ERRORTYPE initialize_avc_output(OMXCLIENT * client, int argc, char **args)
{
    OMX_ERRORTYPE omxError;
    OMX_VIDEO_PARAM_BITRATETYPE bitrate;
    OMX_PARAM_DEBLOCKINGTYPE deblocking;
    OMX_VIDEO_PARAM_AVCTYPE parameters;
    OMX_VIDEO_PARAM_QUANTIZATIONTYPE quantization;
    OMX_CSI_VIDEO_PARAM_AVCTYPEEXT extensions;

    /* set common AVC parameters */
    omxclient_struct_init(&parameters, OMX_VIDEO_PARAM_AVCTYPE);
    client->coding_type = OMX_VIDEO_CodingAVC;
    parameters.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component, OMX_IndexParamVideoAvc,
                               &parameters), omxError);
    OMXCLIENT_RETURN_ON_ERROR(process_avc_parameters(argc, args, &parameters),
                              omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component, OMX_IndexParamVideoAvc,
                               &parameters), omxError);

    /* set bitrate */
    omxclient_struct_init(&bitrate, OMX_VIDEO_PARAM_BITRATETYPE);
    bitrate.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component, OMX_IndexParamVideoBitrate,
                               &bitrate), omxError);
    OMXCLIENT_RETURN_ON_ERROR(process_parameters_bitrate(argc, args, &bitrate),
                              omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component, OMX_IndexParamVideoBitrate,
                               &bitrate), omxError);

    /* set deblocking */
    omxclient_struct_init(&deblocking, OMX_PARAM_DEBLOCKINGTYPE);
    deblocking.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component,
                               OMX_IndexParamCommonDeblocking, &deblocking),
                              omxError);
    OMXCLIENT_RETURN_ON_ERROR(process_avc_parameters_deblocking
                              (argc, args, &deblocking), omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component,
                               OMX_IndexParamCommonDeblocking, &deblocking),
                              omxError);

    /* set quantization */
    omxclient_struct_init(&quantization, OMX_VIDEO_PARAM_QUANTIZATIONTYPE);
    quantization.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component,
                               OMX_IndexParamVideoQuantization, &quantization),
                              omxError);

    OMXCLIENT_RETURN_ON_ERROR(process_parameters_quantization
                              (argc, args, &quantization), omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component,
                               OMX_IndexParamVideoQuantization, &quantization),
                              omxError);


    /* set extension encoding parameters */
    omxclient_struct_init(&extensions, OMX_CSI_VIDEO_PARAM_AVCTYPEEXT);
    extensions.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component,
                               OMX_CSI_IndexParamVideoAvcExt, &extensions),
                              omxError);

    OMXCLIENT_RETURN_ON_ERROR(process_parameters_avc_extension
                              (argc, args, &extensions), omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component,
                               OMX_CSI_IndexParamVideoAvcExt, &extensions),
                              omxError);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE initialize_hevc_output(OMXCLIENT * client, int argc, char **args)
{
    OMX_ERRORTYPE omxError;
    OMX_CSI_VIDEO_PARAM_HEVCTYPE hevc_parameters;
    OMX_VIDEO_PARAM_BITRATETYPE bitrate;
    OMX_PARAM_DEBLOCKINGTYPE deblocking;
    OMX_VIDEO_PARAM_QUANTIZATIONTYPE quantization;

    /* set parameters */
    omxclient_struct_init(&hevc_parameters, OMX_CSI_VIDEO_PARAM_HEVCTYPE);
    client->coding_type = OMX_CSI_VIDEO_CodingHEVC;
    hevc_parameters.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component, OMX_CSI_IndexParamVideoHevc,
                               &hevc_parameters), omxError);
    OMXCLIENT_RETURN_ON_ERROR(process_hevc_parameters(argc, args, &hevc_parameters),
                              omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component, OMX_CSI_IndexParamVideoHevc,
                               &hevc_parameters), omxError);

    /* set bitrate */
    omxclient_struct_init(&bitrate, OMX_VIDEO_PARAM_BITRATETYPE);
    bitrate.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component, OMX_IndexParamVideoBitrate,
                               &bitrate), omxError);
    OMXCLIENT_RETURN_ON_ERROR(process_parameters_bitrate(argc, args, &bitrate),
                              omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component, OMX_IndexParamVideoBitrate,
                               &bitrate), omxError);

    /* set deblocking */
    omxclient_struct_init(&deblocking, OMX_PARAM_DEBLOCKINGTYPE);
    deblocking.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component,
                               OMX_IndexParamCommonDeblocking, &deblocking),
                              omxError);
    OMXCLIENT_RETURN_ON_ERROR(process_avc_parameters_deblocking
                              (argc, args, &deblocking), omxError);

    deblocking.nPortIndex = 1;
    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component,
                               OMX_IndexParamCommonDeblocking, &deblocking),
                              omxError);

    /* set quantization */
    omxclient_struct_init(&quantization, OMX_VIDEO_PARAM_QUANTIZATIONTYPE);
    quantization.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component,
                               OMX_IndexParamVideoQuantization, &quantization),
                              omxError);

    OMXCLIENT_RETURN_ON_ERROR(process_parameters_quantization
                              (argc, args, &quantization), omxError);

    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component,
                               OMX_IndexParamVideoQuantization, &quantization),
                              omxError);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE initialize_image_output(OMXCLIENT * client, int argc, char **args)
{
    OMX_ERRORTYPE omxError;

    OMX_IMAGE_PARAM_QFACTORTYPE qfactor;

    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "%s\n", __FUNCTION__);

    /* set parameters */
    omxclient_struct_init(&qfactor, OMX_IMAGE_PARAM_QFACTORTYPE);
    qfactor.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_GetParameter
                              (client->component, OMX_IndexParamQFactor,
                               &qfactor), omxError);
    OMXCLIENT_RETURN_ON_ERROR(process_parameters_image_qfactor
                              (argc, args, &qfactor), omxError);

    OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, "QValue: %d\n", (int) qfactor.nQFactor);
    qfactor.nPortIndex = 1;

    OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter
                              (client->component, OMX_IndexParamQFactor,
                               &qfactor), omxError);

    return OMX_ErrorNone;
}


int encode_main(void *arg)
{
    OMXCLIENT client;
    OMX_ERRORTYPE omxError;
    int id = *((int *)arg);

    memset(&parameters[id], 0, sizeof(OMXENCODER_PARAMETERS));
    memset(&client, 0, sizeof(OMXCLIENT));

    client.id = id;
    parameters[id].id = id;
    parameters[id].buffer_size = 0;
    parameters[id].buffer_count = 9;
    parameters[id].roi1QP = -1;
    parameters[id].roi2QP = -1;

    omxError = process_encoder_parameters(arg_count, arguments, &parameters[id]);
    if(omxError != OMX_ErrorNone)
    {
        if (id == 0)
        {
            print_usage(arguments[0]);
            return omxError;
        }
        else
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_DEBUG, 
                "Parameters for encode thread %d are not valid. Exit thread\n", id);
            return OMX_ErrorNone;
        }
    }

    if(omxError == OMX_ErrorNone)
    {
        if(parameters[id].image_output)
        {
            parameters[id].buffer_count = 1;
            omxError =
                omxclient_component_create(&client,
                                           IMAGE_COMPONENT_NAME,
                                           parameters[id].cRole /*"image_encoder.jpeg"*/,
                                           parameters[id].buffer_count);
        }
        else
        {
            omxError =
                omxclient_component_create(&client,
                                           VIDEO_COMPONENT_NAME,
                                           parameters[id].cRole /*"video_encoder.avc"*/,
                                           parameters[id].buffer_count);
        }

        client.output_name = parameters[id].outfile;
        client.cache_mode = parameters[id].cache_mode;
        client.frame_rate_numer = parameters[id].frame_rate_numer;
        client.frame_rate_denom = parameters[id].frame_rate_denom;

        if(omxError == OMX_ErrorNone)
        {
            omxError = omxclient_check_component_version(client.component);
        }
        else
        {
            OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                           "Component creation failed: '%s'\n",
                           OMX_OSAL_TraceErrorStr(omxError));
        }

        if(omxError == OMX_ErrorNone)
        {
            if(parameters[id].image_output == OMX_FALSE)
            {
                omxError =
                    omxclient_component_initialize(&client,
                                                   &omx_encoder_port_initialize);

                switch ((OMX_U32)parameters[id].output_compression)
                {

                case OMX_VIDEO_CodingAVC:
                    omxError = initialize_avc_output(&client, arg_count, arguments);
                    break;

                case OMX_CSI_VIDEO_CodingHEVC:
                    omxError = initialize_hevc_output(&client, arg_count, arguments);
                    break;

                default:
                    {
                        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                       "Format is unsupported\n");
                        return OMX_ErrorNone;
                    }
                }
            }
            else
            {
                omxError =
                    omxclient_component_initialize_image(&client,
                                                        &omx_encoder_image_port_initialize);
                if(omxError == OMX_ErrorNone)
                {
                    initialize_image_output(&client, arg_count, arguments);
                }
            }

            if (!parameters[id].osdfile && client.ports >= 3)
            {
                /* disable OSD port */
                OMXCLIENT_RETURN_ON_ERROR(OMX_SendCommand
                                        (client.component, OMX_CommandPortDisable,
                                        2, NULL), omxError);
            }

            /* set rotation */
            if(omxError == OMX_ErrorNone && parameters[id].rotation != 0)
            {

                OMX_CONFIG_ROTATIONTYPE rotation;

                omxclient_struct_init(&rotation, OMX_CONFIG_ROTATIONTYPE);

                rotation.nPortIndex = 0;
                rotation.nRotation = parameters[id].rotation;

                if((omxError =
                    OMX_SetConfig(client.component, OMX_IndexConfigCommonRotate,
                                  &rotation)) != OMX_ErrorNone)
                {

                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                   "Rotation could not be set: %s\n",
                                   OMX_OSAL_TraceErrorStr(omxError));
                    return omxError;
                }
            }

            /* set cropping */
            if(omxError == OMX_ErrorNone && parameters[id].cropping)
            {

                OMX_CONFIG_RECTTYPE rect;

                omxclient_struct_init(&rect, OMX_CONFIG_RECTTYPE);

                rect.nPortIndex = 0;
                rect.nLeft      = parameters[id].cleft;
                rect.nTop       = parameters[id].ctop;
                rect.nHeight    = parameters[id].cheight;
                rect.nWidth     = parameters[id].cwidth;

                if((omxError =
                    OMX_SetConfig(client.component,
                                  OMX_IndexConfigCommonInputCrop,
                                  &rect)) != OMX_ErrorNone)
                {

                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                   "Cropping could not be enabled: %s\n",
                                   OMX_OSAL_TraceErrorStr(omxError));

                    return omxError;
                }
            }

            /* set intra area */
            if(omxError == OMX_ErrorNone && parameters[id].intraArea.enable)
            {

                OMX_CSI_VIDEO_CONFIG_INTRAAREATYPE area;

                omxclient_struct_init(&area, OMX_CSI_VIDEO_CONFIG_INTRAAREATYPE);

                area.nPortIndex = 1;
                area.bEnable    = parameters[id].intraArea.enable;
                area.nLeft      = parameters[id].intraArea.left;
                area.nTop       = parameters[id].intraArea.top;
                area.nBottom    = parameters[id].intraArea.bottom;
                area.nRight     = parameters[id].intraArea.right;

                if((omxError =
                    OMX_SetConfig(client.component,
                                  OMX_CSI_IndexConfigVideoIntraArea,
                                  &area)) != OMX_ErrorNone)
                {

                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                   "Intra area could not be enabled: %s\n",
                                   OMX_OSAL_TraceErrorStr(omxError));

                    return omxError;
                }
            }

            /* set ROI 1 area */
            if(omxError == OMX_ErrorNone && parameters[id].roi1Area.enable)
            {

                OMX_CSI_VIDEO_CONFIG_ROIAREATYPE roi;
                omxclient_struct_init(&roi, OMX_CSI_VIDEO_CONFIG_ROIAREATYPE);

                roi.nPortIndex = 1;
                roi.nArea      = 1;
                roi.bEnable    = parameters[id].roi1Area.enable;
                roi.nLeft      = parameters[id].roi1Area.left;
                roi.nTop       = parameters[id].roi1Area.top;
                roi.nBottom    = parameters[id].roi1Area.bottom;
                roi.nRight     = parameters[id].roi1Area.right;

                if((omxError =
                    OMX_SetConfig(client.component,
                                  OMX_CSI_IndexConfigVideoRoiArea,
                                  &roi)) != OMX_ErrorNone)
                {

                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                   "ROI area 1 could not be enabled: %s\n",
                                   OMX_OSAL_TraceErrorStr(omxError));

                    return omxError;
                }

                if (parameters[id].roi1QP >= 0)
                {
                    OMX_CSI_VIDEO_CONFIG_ROIQPTYPE Qp;
                    omxclient_struct_init(&Qp, OMX_CSI_VIDEO_CONFIG_ROIQPTYPE);

                    Qp.nPortIndex = 1;
                    Qp.nArea      = 1;
                    Qp.nQP        = parameters[id].roi1QP;

                    if((omxError =
                        OMX_SetConfig(client.component,
                                    OMX_CSI_IndexConfigVideoRoiQp,
                                    &Qp)) != OMX_ErrorNone)
                    {

                        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                    "ROI 1 QP could not be enabled: %s\n",
                                    OMX_OSAL_TraceErrorStr(omxError));

                        return omxError;
                    }
                }
                else
                {
                    OMX_CSI_VIDEO_CONFIG_ROIDELTAQPTYPE deltaQp;
                    omxclient_struct_init(&deltaQp, OMX_CSI_VIDEO_CONFIG_ROIDELTAQPTYPE);

                    deltaQp.nPortIndex = 1;
                    deltaQp.nArea      = 1;
                    deltaQp.nDeltaQP   = parameters[id].roi1DeltaQP;

                    if((omxError =
                        OMX_SetConfig(client.component,
                                    OMX_CSI_IndexConfigVideoRoiDeltaQp,
                                    &deltaQp)) != OMX_ErrorNone)
                    {

                        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                    "ROI 1 delta QP could not be enabled: %s\n",
                                    OMX_OSAL_TraceErrorStr(omxError));

                        return omxError;
                    }
                }
            }

           /* set ROI 2 area */
            if(omxError == OMX_ErrorNone && parameters[id].roi2Area.enable)
            {

                OMX_CSI_VIDEO_CONFIG_ROIAREATYPE roi;
                omxclient_struct_init(&roi, OMX_CSI_VIDEO_CONFIG_ROIAREATYPE);

                roi.nPortIndex = 1;
                roi.nArea      = 2;
                roi.bEnable    = parameters[id].roi2Area.enable;
                roi.nLeft      = parameters[id].roi2Area.left;
                roi.nTop       = parameters[id].roi2Area.top;
                roi.nBottom    = parameters[id].roi2Area.bottom;
                roi.nRight     = parameters[id].roi2Area.right;

                if((omxError =
                    OMX_SetConfig(client.component,
                                  OMX_CSI_IndexConfigVideoRoiArea,
                                  &roi)) != OMX_ErrorNone)
                {

                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                   "ROI area 2 could not be enabled: %s\n",
                                   OMX_OSAL_TraceErrorStr(omxError));

                    return omxError;
                }

                if (parameters[id].roi2QP >= 0)
                {
                    OMX_CSI_VIDEO_CONFIG_ROIQPTYPE Qp;
                    omxclient_struct_init(&Qp, OMX_CSI_VIDEO_CONFIG_ROIQPTYPE);

                    Qp.nPortIndex = 1;
                    Qp.nArea      = 2;
                    Qp.nQP        = parameters[id].roi2QP;

                    if((omxError =
                        OMX_SetConfig(client.component,
                                    OMX_CSI_IndexConfigVideoRoiQp,
                                    &Qp)) != OMX_ErrorNone)
                    {

                        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                    "ROI 2 QP could not be enabled: %s\n",
                                    OMX_OSAL_TraceErrorStr(omxError));

                        return omxError;
                    }
                }
                else
                {
                    OMX_CSI_VIDEO_CONFIG_ROIDELTAQPTYPE deltaQp;
                    omxclient_struct_init(&deltaQp, OMX_CSI_VIDEO_CONFIG_ROIDELTAQPTYPE);

                    deltaQp.nPortIndex = 1;
                    deltaQp.nArea      = 2;
                    deltaQp.nDeltaQP   = parameters[id].roi2DeltaQP;

                    if((omxError =
                        OMX_SetConfig(client.component,
                                    OMX_CSI_IndexConfigVideoRoiDeltaQp,
                                    &deltaQp)) != OMX_ErrorNone)
                    {

                        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                    "ROI 2 delta QP could not be enabled: %s\n",
                                    OMX_OSAL_TraceErrorStr(omxError));

                        return omxError;
                    }
                }
            }

            /* set lossless compressed input */
            if(omxError == OMX_ErrorNone && parameters[id].compressedInput)
            {
                OMX_CSI_COMPRESSION_MODE_CONFIGTYPE compressionMode;
                omxclient_struct_init(&compressionMode, OMX_CSI_COMPRESSION_MODE_CONFIGTYPE);

                compressionMode.nPortIndex  = 0;
                compressionMode.eMode       = OMX_CSI_COMPRESSION_MODE_LOSSLESS;

                if((omxError =
                    OMX_SetParameter(client.component,
                                  OMX_CSI_IndexParamCompressionMode,
                                  &compressionMode)) != OMX_ErrorNone)
                {

                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                   "Compressed input could not be used: %s\n",
                                   OMX_OSAL_TraceErrorStr(omxError));

                    return omxError;
                }
            }

            /* set dmabuf input */
            if(omxError == OMX_ErrorNone && parameters[id].dma_input)
            {

                OMX_CSI_BUFFER_MODE_CONFIGTYPE bufferMode;
                omxclient_struct_init(&bufferMode, OMX_CSI_BUFFER_MODE_CONFIGTYPE);

                bufferMode.nPortIndex = 0;
                bufferMode.eMode = OMX_CSI_BUFFER_MODE_DMA;
                OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter(client.component,
                                                        OMX_CSI_IndexParamBufferMode,
                                                        &bufferMode),
                                                            omxError);
            }

            /* set OSD cropping */
            if(omxError == OMX_ErrorNone && parameters[id].osdfile && parameters[id].osdcropping)
            {

                OMX_CONFIG_RECTTYPE rect;

                omxclient_struct_init(&rect, OMX_CONFIG_RECTTYPE);

                rect.nPortIndex = 2;
                rect.nLeft      = parameters[id].ocleft;
                rect.nTop       = parameters[id].octop;
                rect.nHeight    = parameters[id].ocheight;
                rect.nWidth     = parameters[id].ocwidth;

                if((omxError =
                    OMX_SetConfig(client.component,
                                  OMX_IndexConfigCommonInputCrop,
                                  &rect)) != OMX_ErrorNone)
                {

                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                   "OSD cropping could not be enabled: %s\n",
                                   OMX_OSAL_TraceErrorStr(omxError));

                    return omxError;
                }
            }

            /* set OSD cropping */
            if(omxError == OMX_ErrorNone && parameters[id].osdfile)
            {

                OMX_CSI_VIDEO_CONFIG_OSDTYPE osd;

                omxclient_struct_init(&osd, OMX_CSI_VIDEO_CONFIG_OSDTYPE);

                osd.nPortIndex = 2;
                osd.nAlpha      = parameters[id].oalpha;
                osd.nOffsetX    = parameters[id].oleft;
                osd.nOffsetY    = parameters[id].otop;
                osd.nBitmapY    = parameters[id].obitmap[0];
                osd.nBitmapU    = parameters[id].obitmap[1];
                osd.nBitmapV    = parameters[id].obitmap[2];

                if((omxError =
                    OMX_SetConfig(client.component,
                                  OMX_CSI_IndexConfigVideoOsd,
                                  &osd)) != OMX_ErrorNone)
                {

                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                   "OSD area could not be enabled: %s\n",
                                   OMX_OSAL_TraceErrorStr(omxError));

                    return omxError;
                }
            }

            /* set dmabuf output */
            if(omxError == OMX_ErrorNone && parameters[id].dma_output)
            {

                OMX_CSI_BUFFER_MODE_CONFIGTYPE bufferMode;
                omxclient_struct_init(&bufferMode, OMX_CSI_BUFFER_MODE_CONFIGTYPE);

                bufferMode.nPortIndex = 1;
                bufferMode.eMode = OMX_CSI_BUFFER_MODE_DMA;
                OMXCLIENT_RETURN_ON_ERROR(OMX_SetParameter(client.component,
                                                        OMX_CSI_IndexParamBufferMode,
                                                        &bufferMode),
                                                            omxError);
            }

            if(omxError == OMX_ErrorNone)
            {
                omxError = omxclient_initialize_buffers(&client);

                if(omxError != OMX_ErrorNone)
                {
                    /* raw free, because state was not changed to idle */
                    omxclient_component_free_buffers(&client);
                }

            }

            if(omxError == OMX_ErrorNone)
            {
                if(parameters[id].image_output)
                {
                    /* execute conversion as sliced */
                    omxError =
                        omxclient_execute_yuv_sliced(&client,
                                                        parameters[id].infile,
                                                        parameters[id].outfile,
                                                        parameters[id].firstvop,
                                                        parameters[id].lastvop);
                }
                else
                {
                    omxError =
                        omxclient_execute_yuv_range(&client,
                                                    parameters[id].infile,
                                                    parameters[id].outfile,
                                                    parameters[id].osdfile,
                                                    parameters[id].firstvop,
                                                    parameters[id].lastvop);
                }

                if(omxError != OMX_ErrorNone)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                   "Video processing failed: '%s'\n",
                                   OMX_OSAL_TraceErrorStr(omxError));
                }
            }
            else
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Component video initialization failed: '%s'\n",
                               OMX_OSAL_TraceErrorStr(omxError));
            }

            /* destroy the component since it was succesfully created */
            omxError = omxclient_component_destroy(&client);
            if(omxError != OMX_ErrorNone)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Component destroy failed: '%s'\n",
                               OMX_OSAL_TraceErrorStr(omxError));
            }
        }
    }
    else
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                       "OMX initialization failed: '%s'\n",
                       OMX_OSAL_TraceErrorStr(omxError));

        /* FAIL: OMX initialization failed, reason */
    }

    return omxError;
}

/*
    main
 */
int id[2] = {0, 1};
int main(int argc, char **args)
{
    OMX_ERRORTYPE omxError = OMX_ErrorNone;

    arg_count = argc;
    arguments = args;

    omxError = OMX_Init();

    if(omxError == OMX_ErrorNone)
    {
        pthread_create(&encode_thread[0], NULL, encode_main, &id[0]);
        pthread_create(&encode_thread[1], NULL, encode_main, &id[1]);

        for (int i = 0; i < 2; i++)
        {
            pthread_join(encode_thread[i], NULL);
        }

        OMX_Deinit();
    }

    return omxError;
}
