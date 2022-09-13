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

#include <OMX_Component.h>
#include <OMX_Video.h>
#include <OMX_Image.h>

#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "omxencparameters.h"
#include "omxtestcommon.h"

#define FLOAT_Q16(a) ((OMX_U32) ((float)(a) * 65536.0))

/*
    Macro that is used specifically to check parameter values
    in parameter checking loop.
 */
#define OMXENCODER_CHECK_NEXT_VALUE(i, args, argc, msg) \
    if(++(i) == argc || (*((args)[i])) == '-')  { \
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, msg); \
        return OMX_ErrorBadParameter; \
    }

#define OMXENCODER_CHECK_NEXT_VALUE_SIGNED(i, args, argc, msg) \
    if(++(i) == argc)  { \
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, msg); \
        return OMX_ErrorBadParameter; \
    }

#define OMXENCODER_STRIDE(variable, alignment) (((variable) + (alignment) - 1) & (~((alignment) - 1)))

extern pthread_t encode_thread[];

int ParseDelim(char *optArg, char delim)
{
    OMX_S32 i;

    for (i = 0; i < (OMX_S32)strlen(optArg); i++)
        if (optArg[i] == delim)
        {
            optArg[i] = 0;
            return i;
        }

    return -1;
}

/*
    print_usage
 */
 
void print_usage(OMX_STRING swname)
{
    printf("usage: %s [options]\n"
           "\n"
           "  Available options:\n"
           "    -O, --outputFormat               Compression format; 'avc', 'hevc' or 'jpeg'\n"
           "    -l, --inputFormat                Color format for output\n"
           "                                     0  yuv420planar          1  yuv420semiplanar\n"
           "    -o, --output                     File name of the output\n"
           "    -i, --input                      File name of the input\n"
           "    -w, --lumWidthSrc                Width of source image\n"
           "    -h, --lumHeightSrc               Height of source image\n"
           "    -x, --height                     Height of output image\n"
           "    -y, --width                      Width of output image\n"
           "    -j, --inputRateNumer             Frame rate used for the input\n"
           "    -f, --outputRateNumer            Frame rate used for the output\n"
           "    -B, --bitsPerSecond              Bit-rate used for the output\n"
           "    -a, --firstVop                   First vop of input file [0]\n"
           "    -b, --lastVop                    Last vop of input file  [EOF]\n"
           "    -s, --buffer-size                Size of allocated buffers (default provided by component) \n"
           "    -c, --buffer-count               Count of buffers allocated for each port (30)\n"
           "    -A1, --roi1Area                  left:top:right:bottom macroblock coordinates\n"
           "    -A2, --roi2Area                  left:top:right:bottom macroblock coordinates\n"
           "    -Q1, --roi1DeltaQp               QP delta value for 1st Region-Of-Interest\n"
           "    -Q2, --roi2DeltaQp               QP delta value for 2nd Region-Of-Interest\n"
           "    --preset                         0...3 for HEVC. 0..1 for H264. Trade off performance and compression efficiency\n"
           "                                     Higher value means high quality but worse performance. User need explict claim preset when use this option\n"
           "    --inputAlignmentExp              0..12 set alignment value [7]\n"
           "                                     0 = Disable alignment \n"
           "                                     4..12 = addr of input frame buffer and each line aligned to 2^inputAlignmentExp \n"
           "    -u[n] --ctbRc                    0...3 CTB QP adjustment mode for Rate Control and Subjective Quality.[0]\n"
           "                                     0 = No CTB QP adjustment.\n"
           "                                     1 = CTB QP adjustment for Subjective Quality only.\n"
           "                                     2 = CTB QP adjustment for Rate Control only. (For HwCtbRcVersion >= 1 only).\n"
           "                                     3 = CTB QP adjustment for both Subjective Quality and Rate Control. (For HwCtbRcVersion >= 1 only).\n"
           "    --cpbSize                        HRD Coded Picture Buffer size in bits. [0]\n"
           "                                     Buffer size used by the HRD model, 0 means max CPB for the level.\n"
           "    -K[n] --enableCabac              0=OFF (CAVLC), 1=ON (CABAC). [1]\n"
           "    --roi1Qp                         0..51, absolute QP value for ROI 1 CTBs. [-1]. negative value is invalid. \n"
           "    --roi2Qp                         0..51, absolute QP value for ROI 2 CTBs. [-1]\n"
           "                                     roi1Qp/roi2Qp are only valid when absolute ROI QP supported. And please use either roiDeltaQp or roiQp.\n"
           "  -A[n] --intraQpDelta               51..51, Intra QP delta. [-5]\n"
           "                                     QP difference between target QP and intra frame QP.\n"
           "  -G[n] --fixedIntraQp               0..51, Fixed Intra QP, 0 = disabled. [0]\n"
           "                                     Use fixed QP value for every intra frame in stream.\n"
           "        --vbr                        0=OFF, 1=ON. Variable Bit Rate Control by qpMin. [0]\n"
           "        --enableVuiTimingInfo        Write VUI timing info in SPS. [1]\n"
           "                                     0=disable. \n"
           "                                     1=enable. \n"
           "        --rfcEnable                  0=disable reference frame compression, 1=enable reference frame compression. [0]\n"
           "\n"
           "    -r, --rotation                   Rotation value, angle in degrees\n"
           "    -di, --dma-input                 Use dmabuf as input\n"
           "\n", swname);

    print_avc_usage();
    print_hevc_usage();
    print_jpeg_usage();

    printf("  Return value:\n"
           "    0 = OK; failures indicated as OMX error codes\n" "\n");
}

/*
    process_parameters
 */
OMX_ERRORTYPE process_encoder_parameters(int argc, char **args,
                                         OMXENCODER_PARAMETERS * params)
{
    OMX_S32 i, j;
    OMX_STRING files[3] = { 0 };    // input, output, osd

    params->lastvop = 100;

    i = 1;
    while(i < argc)
    {
        if (params->id == 1)
        {
            if(strcmp(args[i], "-i2") == 0 || strcmp(args[i], "--input2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for input file missing.\n");
                files[0] = args[i];
            }
            /* input compression format */
            else if(strcmp(args[i], "-O2") == 0 ||
                    strcmp(args[i], "--output-compression-format2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for output format is missing.\n");
                if(strcasecmp(args[i], "avc") == 0)
                {
                    params->image_output = OMX_FALSE;
                    params->cRole = "video_encoder.avc";
                }
                else if(strcasecmp(args[i], "hevc") == 0)
                {
                    params->image_output = OMX_FALSE;
                    params->cRole = "video_encoder.hevc";
                }
                else if(strcasecmp(args[i], "jpeg") == 0)
                {
                    params->image_output = OMX_TRUE;
                    params->cRole = "image_encoder.jpeg";
                }
                else
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                "Unknown compression format.\n");
                    return OMX_ErrorBadParameter;
                }
            }
            else if(strcmp(args[i], "-o2") == 0 || strcmp(args[i], "--output2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for output file missing.\n");
                files[1] = args[i];
            }
            else if(strcmp(args[i], "-a2") == 0 ||
                    strcmp(args[i], "--firstVop2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for first vop is missing.\n");
                params->firstvop = atoi(args[i]);
            }
            else if(strcmp(args[i], "-b2") == 0 || strcmp(args[i], "--lastVop2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for last vop is missing.\n");
                params->lastvop = atoi(args[i]);
            }
            else
            {
                /* do nothing, paramter may be needed by subsequent parameter readers */
            }
        }
        else if(strcmp(args[i], "-i") == 0 || strcmp(args[i], "--input") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input file missing.\n");
            files[0] = args[i];
        }
        /* input compression format */
        else if(strcmp(args[i], "-O") == 0 ||
                strcmp(args[i], "--output-compression-format") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for output format is missing.\n");
            if(strcasecmp(args[i], "avc") == 0)
            {
                params->image_output = OMX_FALSE;
                params->cRole = "video_encoder.avc";
            }
            else if(strcasecmp(args[i], "hevc") == 0)
            {
                params->image_output = OMX_FALSE;
                params->cRole = "video_encoder.hevc";
            }
            else if(strcasecmp(args[i], "jpeg") == 0)
            {
                params->image_output = OMX_TRUE;
                params->cRole = "image_encoder.jpeg";
            }
            else
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Unknown compression format.\n");
                return OMX_ErrorBadParameter;
            }
        }
        else if(strcmp(args[i], "-o") == 0 || strcmp(args[i], "--output") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for output file missing.\n");
            files[1] = args[i];
        }
        else if(strcmp(args[i], "-a") == 0 ||
                strcmp(args[i], "--firstVop") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for first vop is missing.\n");
            params->firstvop = atoi(args[i]);
        }
        else if(strcmp(args[i], "-b") == 0 || strcmp(args[i], "--lastVop") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for last vop is missing.\n");
            params->lastvop = atoi(args[i]);
        }
        else if(strcmp(args[i], "-s") == 0 ||
                strcmp(args[i], "--buffer-size") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer allocation size is missing.\n");
            params->buffer_size = atoi(args[i]);
        }
        else if(strcmp(args[i], "-c") == 0 ||
                strcmp(args[i], "--buffer-count") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer count is missing.\n");
            params->buffer_count = atoi(args[i]);
        }
        else if(strcmp(args[i], "-r") == 0 ||
                strcmp(args[i], "--rotation") == 0)
        {
            if(++i == argc)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Parameter for rotation is missing.\n");
                return OMX_ErrorBadParameter;
            }
            params->rotation = atoi(args[i]);
        }
        else if(strcmp(args[i], "-cw") == 0 || strcmp(args[i], "--crop-width") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for output crop width missing.\n");
            params->cropping = OMX_TRUE;
            params->cwidth = atoi(args[i]);
        }
        else if(strcmp(args[i], "-ch") == 0 || strcmp(args[i], "--crop-height") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for output crop height missing.\n");
            params->cropping = OMX_TRUE;
            params->cheight = atoi(args[i]);
        }
        else if(strcmp(args[i], "-cx") == 0 ||
                strcmp(args[i], "--crop-left") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for horizontal crop offset is missing.\n");
            params->cropping = OMX_TRUE;
            params->cleft = atoi(args[i]);
        }
        else if(strcmp(args[i], "-cy") == 0 ||
                strcmp(args[i], "--crop-top") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for vertical crop offset is missing.\n");
            params->cropping = OMX_TRUE;
            params->ctop = atoi(args[i]);
        }
        else if(strcmp(args[i], "-A1") == 0 ||
                strcmp(args[i], "--roi1Area") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for ROI area 1 is missing.\n");
            if(atoi(args[i]) == 0)
            {
                params->roi1Area.enable = OMX_FALSE;
            }
            else
            {
                //printf("\n%s\n", args[i]);
                params->roi1Area.enable = OMX_TRUE;
                /* Argument must be "xx:yy:XX:YY".
                 * xx is left coordinate, replace first ':' with 0 */
                if ((j = ParseDelim(args[i], ':')) == -1) break;
                params->roi1Area.left = atoi(args[i]);
                /* yy is top coordinate */
                args[i] += j+1;
                if ((j = ParseDelim(args[i], ':')) == -1) break;
                params->roi1Area.top = atoi(args[i]);
                /* XX is right coordinate */
                args[i] += j+1;
                if ((j = ParseDelim(args[i], ':')) == -1) break;
                params->roi1Area.right = atoi(args[i]);
                /* YY is bottom coordinate */
                args[i] += j+1;
                params->roi1Area.bottom = atoi(args[i]);
                OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO, "Roi1Area enable %d, top %d left %d bottom %d right %d\n", params->roi1Area.enable,
                    params->roi1Area.top, params->roi1Area.left, params->roi1Area.bottom, params->roi1Area.right);
            }
        }
        else if(strcmp(args[i], "-A2") == 0 ||
                strcmp(args[i], "--roi2Area") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for ROI area 2 is missing.\n");
            if(atoi(args[i]) == 0)
            {
                params->roi2Area.enable = OMX_FALSE;
            }
            else
            {
                //printf("\n%s\n", args[i]);
                params->roi2Area.enable = OMX_TRUE;
                /* Argument must be "xx:yy:XX:YY".
                 * xx is left coordinate, replace first ':' with 0 */
                if ((j = ParseDelim(args[i], ':')) == -1) break;
                params->roi2Area.left = atoi(args[i]);
                /* yy is top coordinate */
                args[i] += j+1;
                if ((j = ParseDelim(args[i], ':')) == -1) break;
                params->roi2Area.top = atoi(args[i]);
                /* XX is right coordinate */
                args[i] += j+1;
                if ((j = ParseDelim(args[i], ':')) == -1) break;
                params->roi2Area.right = atoi(args[i]);
                /* YY is bottom coordinate */
                args[i] += j+1;
                params->roi2Area.bottom = atoi(args[i]);
                OMX_OSAL_Trace(OMX_OSAL_TRACE_INFO, "Roi2Area enable %d, top %d left %d bottom %d right %d\n", params->roi2Area.enable,
                    params->roi2Area.top, params->roi2Area.left, params->roi2Area.bottom, params->roi2Area.right);
            }
        }
        else if(strcmp(args[i], "-Q1") == 0 ||
                strcmp(args[i], "--roi1DeltaQp") == 0)
        {
            if(++i == argc)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Parameter for ROI delta QP is missing.\n");
                return OMX_ErrorBadParameter;
            }
            params->roi1DeltaQP = atoi(args[i]);
        }
        else if(strcmp(args[i], "-Q2") == 0 ||
                strcmp(args[i], "--roi2DeltaQp") == 0)
        {
            if(++i == argc)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Parameter for ROI 2 delta QP is missing.\n");
                return OMX_ErrorBadParameter;
            }
            params->roi2DeltaQP = atoi(args[i]);
        }
        else if(strcmp(args[i], "--roi1Qp") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for roi1Qp is missing.\n");
            params->roi1QP = atoi(args[i]);
        }
        else if(strcmp(args[i], "--roi2Qp") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for roi2Qp is missing.\n");
            params->roi2QP = atoi(args[i]);
        }
        else if(strcmp(args[i], "-CI") == 0 ||
                strcmp(args[i], "--compressedInput") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for lossless compressed input is missing.\n");
            params->compressedInput = atoi(args[i]);
        }
        else if(strcmp(args[i], "-di") == 0 ||
                strcmp(args[i], "--dma-input") == 0)
        {
            params->dma_input = OMX_TRUE;
        }
        else if(strcmp(args[i], "-do") == 0 ||
                strcmp(args[i], "--dma-output") == 0)
        {
            params->dma_output = OMX_TRUE;
        }
        else if(strcmp(args[i], "-oi") == 0 || strcmp(args[i], "--osd-input") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for output file missing.\n");
            files[2] = args[i];
        }
        else if(strcmp(args[i], "-ocw") == 0 || strcmp(args[i], "--osd-crop-width") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD crop width missing.\n");
            params->osdcropping = OMX_TRUE;
            params->ocwidth = atoi(args[i]);
        }
        else if(strcmp(args[i], "-och") == 0 || strcmp(args[i], "--osd-crop-height") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD crop height missing.\n");
            params->osdcropping = OMX_TRUE;
            params->ocheight = atoi(args[i]);
        }
        else if(strcmp(args[i], "-ocx") == 0 ||
                strcmp(args[i], "--osd-crop-left") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD horizontal crop offset is missing.\n");
            params->osdcropping = OMX_TRUE;
            params->ocleft = atoi(args[i]);
        }
        else if(strcmp(args[i], "-ocy") == 0 ||
                strcmp(args[i], "--osd-crop-top") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD vertical crop offset is missing.\n");
            params->osdcropping = OMX_TRUE;
            params->octop = atoi(args[i]);
        }
        else if(strcmp(args[i], "-ox") == 0 ||
                strcmp(args[i], "--osd-left") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD horizontal offset is missing.\n");
            params->oleft = atoi(args[i]);
        }
        else if(strcmp(args[i], "-oy") == 0 ||
                strcmp(args[i], "--osd-top") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD vertical offset is missing.\n");
            params->otop = atoi(args[i]);
        }
        else if(strcmp(args[i], "-oa") == 0 ||
                strcmp(args[i], "--osd-alpha") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD alpha is missing.\n");
            params->oalpha = atoi(args[i]);
        }
        else if(strcmp(args[i], "-oby") == 0 ||
                strcmp(args[i], "--osd-bitmap-y") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD bitmap Y is missing.\n");
            params->obitmap[0] = atoi(args[i]);
        }
        else if(strcmp(args[i], "-obu") == 0 ||
                strcmp(args[i], "--osd-bitmap-u") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD bitmap U is missing.\n");
            params->obitmap[0] = atoi(args[i]);
        }
        else if(strcmp(args[i], "-obv") == 0 ||
                strcmp(args[i], "--osd-bitmap-v") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for OSD bitmap V is missing.\n");
            params->obitmap[0] = atoi(args[i]);
        }
        else if(strcmp(args[i], "-cm") == 0 ||
                strcmp(args[i], "--cache-mode") == 0)
        {
            params->cache_mode = OMX_TRUE;
        }
        else if(strcmp(args[i], "--trace-level") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for trace level is missing.\n");
            extern OMX_U32 traceLevel;
            OMX_U32 level = atoi(args[i]);
            if (level > 4)
                level = 4;
            traceLevel = (1 << level) - 1;
        }
        else if(strcmp(args[i], "--frame-rate-numer") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for frame rate numerator is missing.\n");
            params->frame_rate_numer = atoi(args[i]);
        }
        else if(strcmp(args[i], "--frame-rate-denom") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for frame rate denominator is missing.\n");
            params->frame_rate_denom = atoi(args[i]);
        }
        else
        {
            /* do nothing, paramter may be needed by subsequent parameter readers */
        }
        ++i;
    }

    if(files[0] == 0)
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "No input file.\n");
        return OMX_ErrorBadParameter;
    }

    if(files[1] == 0)
    {
        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "No output file.\n");
        return OMX_ErrorBadParameter;
    }

    params->infile = files[0];
    params->outfile = files[1];
    params->osdfile = files[2];

    return OMX_ErrorNone;
}

/*
 */
static OMX_COLOR_FORMATTYPE inputPixelFormats[] = 
{
    OMX_COLOR_FormatYUV420Planar
    , OMX_COLOR_FormatYUV420SemiPlanar
};

OMX_ERRORTYPE process_encoder_input_parameters(int argc, char **args,
                                               OMX_PARAM_PORTDEFINITIONTYPE *
                                               params)
{
    OMX_S32 i;
    pthread_t tid = pthread_self();

    params->format.video.nSliceHeight = 0;
    params->format.video.nFrameWidth = 0;
    params->format.video.nStride = 0;
    params->nBufferAlignment = 128;

    i = 1;
    while(i < argc)
    {
        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-l2") == 0 || strcmp(args[i], "--inputFormat2") == 0)
            {
                if(++i == argc)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                "Parameter for input color format missing.\n");
                    return OMX_ErrorBadParameter;
                }

                {
                    int fmtTabSize = sizeof(inputPixelFormats) / sizeof(OMX_COLOR_FORMATTYPE);
                    int fmtIdx = atoi(args[i]);

                    params->format.video.eColorFormat = OMX_COLOR_FormatUnused;
                    if(fmtIdx >= 0 && fmtIdx < fmtTabSize)
                    {
                        params->format.video.eColorFormat = inputPixelFormats[fmtIdx];
                    }

                    if(params->format.video.eColorFormat == OMX_COLOR_FormatUnused)
                    {
                        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "Unknown color format.\n");
                        return OMX_ErrorBadParameter;
                    }
                }
            }
            else if(strcmp(args[i], "-h2") == 0 ||
                    strcmp(args[i], "--lumHeightSrc2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for input height missing.\n");
                params->format.video.nFrameHeight = atoi(args[i]);
            }
            else if(strcmp(args[i], "-w2") == 0 ||
                    strcmp(args[i], "--lumWidthSrc2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for input width missing.\n");
                params->format.video.nFrameWidth = atoi(args[i]);
            }
        }
        /* input color format */
        else if(strcmp(args[i], "-l") == 0 || strcmp(args[i], "--inputFormat") == 0)
        {
            if(++i == argc)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Parameter for input color format missing.\n");
                return OMX_ErrorBadParameter;
            }

            {
                int fmtTabSize = sizeof(inputPixelFormats) / sizeof(OMX_COLOR_FORMATTYPE);
                int fmtIdx = atoi(args[i]);

                params->format.video.eColorFormat = OMX_COLOR_FormatUnused;
                if(fmtIdx >= 0 && fmtIdx < fmtTabSize)
                {
                    params->format.video.eColorFormat = inputPixelFormats[fmtIdx];
                }

                if(params->format.video.eColorFormat == OMX_COLOR_FormatUnused)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "Unknown color format.\n");
                    return OMX_ErrorBadParameter;
                }
            }
        }
        else if(strcmp(args[i], "-h") == 0 ||
                strcmp(args[i], "--lumHeightSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input height missing.\n");
            params->format.video.nFrameHeight = atoi(args[i]);
        }
        else if(strcmp(args[i], "-w") == 0 ||
                strcmp(args[i], "--lumWidthSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input width missing.\n");
            params->format.video.nFrameWidth = atoi(args[i]);
        }
        else if(strcmp(args[i], "-j") == 0 ||
                strcmp(args[i], "--inputRateNumer") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input frame rate is missing.\n");
            params->format.video.xFramerate = FLOAT_Q16(strtod(args[i], 0));
        }
        else if(strcmp(args[i], "-s") == 0 ||
                strcmp(args[i], "--buffer-size") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer allocation size is missing.\n");
            params->nBufferSize = atoi(args[i]);
        }
        else if(strcmp(args[i], "-c") == 0 ||
                strcmp(args[i], "--buffer-count") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer count is missing.\n");
            params->nBufferCountActual = atoi(args[i]);
        }
        else if(strcmp(args[i], "--inputAlignmentExp") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for Set alignment value is missing.\n");
            params->nBufferAlignment = (1 << atoi(args[i]));
        }
        else if(strcmp(args[i], "--inputStride") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for Set stride value is missing.\n");
            params->format.video.nStride = atoi(args[i]);
        }
        else
        {
            /* do nothing, paramter may be needed by subsequent parameter readers */
        }

        ++i;
    }

    if (params->format.video.nStride < params->format.video.nFrameWidth)
        params->format.video.nStride = params->format.video.nFrameWidth;

    params->format.video.nStride = 
        OMXENCODER_STRIDE(params->format.video.nStride, params->nBufferAlignment);

    return OMX_ErrorNone;
}

/*
 */
static OMX_COLOR_FORMATTYPE osdPixelFormats[] = 
{
    OMX_COLOR_FormatYUV420SemiPlanar
    , OMX_COLOR_Format32bitARGB8888
    , OMX_COLOR_FormatMonochrome
};

OMX_ERRORTYPE process_encoder_osd_parameters(int argc, char **args,
                                             OMX_PARAM_PORTDEFINITIONTYPE *
                                             params)
{
    OMX_S32 i;
    pthread_t tid = pthread_self();

    params->format.video.nSliceHeight = 0;
    params->format.video.nFrameWidth = 0;
    params->format.video.nStride = 0;

    i = 1;
    while(i < argc)
    {
        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-ol2") == 0 || strcmp(args[i], "--osdFormat2") == 0)
            {
                if(++i == argc)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                "Parameter for input color format missing.\n");
                    return OMX_ErrorBadParameter;
                }

                {
                    int fmtTabSize = sizeof(osdPixelFormats) / sizeof(OMX_COLOR_FORMATTYPE);
                    int fmtIdx = atoi(args[i]);

                    params->format.video.eColorFormat = OMX_COLOR_FormatUnused;
                    if(fmtIdx >= 0 && fmtIdx < fmtTabSize)
                    {
                        params->format.video.eColorFormat = osdPixelFormats[fmtIdx];
                    }

                    if(params->format.video.eColorFormat == OMX_COLOR_FormatUnused)
                    {
                        OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "Unknown color format.\n");
                        return OMX_ErrorBadParameter;
                    }
                }
            }
            else if(strcmp(args[i], "-oh2") == 0 ||
                    strcmp(args[i], "--osdHeightSrc2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for input height missing.\n");
                params->format.video.nFrameHeight = atoi(args[i]);
            }
            else if(strcmp(args[i], "-ow2") == 0 ||
                    strcmp(args[i], "--osdWidthSrc2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for input width missing.\n");
                params->format.video.nFrameWidth = atoi(args[i]);
            }
        }
        /* input color format */
        else if(strcmp(args[i], "-ol") == 0 || strcmp(args[i], "--osdFormat") == 0)
        {
            if(++i == argc)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Parameter for input color format missing.\n");
                return OMX_ErrorBadParameter;
            }

            {
                int fmtTabSize = sizeof(osdPixelFormats) / sizeof(OMX_COLOR_FORMATTYPE);
                int fmtIdx = atoi(args[i]);

                params->format.video.eColorFormat = OMX_COLOR_FormatUnused;
                if(fmtIdx >= 0 && fmtIdx < fmtTabSize)
                {
                    params->format.video.eColorFormat = osdPixelFormats[fmtIdx];
                }

                if(params->format.video.eColorFormat == OMX_COLOR_FormatUnused)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "Unknown color format.\n");
                    return OMX_ErrorBadParameter;
                }
            }
        }
        else if(strcmp(args[i], "-oh") == 0 ||
                strcmp(args[i], "--lumHeightSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input height missing.\n");
            params->format.video.nFrameHeight = atoi(args[i]);
        }
        else if(strcmp(args[i], "-ow") == 0 ||
                strcmp(args[i], "--lumWidthSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input width missing.\n");
            params->format.video.nFrameWidth = atoi(args[i]);
        }
        else if(strcmp(args[i], "-oc") == 0 ||
                strcmp(args[i], "--osd-buffer-count") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer count is missing.\n");
            params->nBufferCountActual = atoi(args[i]);
        }
        else if(strcmp(args[i], "--osdInputAlignmentExp") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for Set alignment value is missing.\n");
            params->nBufferAlignment = (1 << atoi(args[i]));
        }
        else if(strcmp(args[i], "--osdInputStride") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for Set stride value is missing.\n");
            params->format.video.nStride = atoi(args[i]);
        }
        else
        {
            /* do nothing, paramter may be needed by subsequent parameter readers */
        }

        ++i;
    }

    if (params->format.video.nStride < params->format.video.nFrameWidth)
        params->format.video.nStride = params->format.video.nFrameWidth;

    return OMX_ErrorNone;
}

/*
 */
OMX_ERRORTYPE process_encoder_image_input_parameters(int argc, char **args,
                                                    OMX_PARAM_PORTDEFINITIONTYPE
                                                    * params)
{
    OMX_S32 i;
    pthread_t tid = pthread_self();

    params->format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
    params->format.image.nSliceHeight = 0;
    params->format.image.nFrameWidth = 0;
    params->format.image.nStride = 0;
    params->nBufferAlignment = 128;

    i = 1;
    while(i < argc)
    {
        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-l2") == 0 || strcmp(args[i], "--inputFormat2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for input color format missing.\n");
                switch (atoi(args[i]))
                {
                case 0:
                    params->format.image.eColorFormat = OMX_COLOR_FormatYUV420Planar;
                    break;
                case 1:
                    params->format.image.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
                    break;
                default:
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "Unknown color format.\n");
                    return OMX_ErrorBadParameter;
                }
            }
            else if(strcmp(args[i], "-h2") == 0 ||
                    strcmp(args[i], "--lumHeightSrc2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for input height missing.\n");
                params->format.image.nFrameHeight = atoi(args[i]);
            }
            else if(strcmp(args[i], "-w2") == 0 ||
                    strcmp(args[i], "--lumWidthSrc2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for input width missing.\n");
                params->format.image.nFrameWidth = atoi(args[i]);
            }
        }
        /* input color format */
        else if(strcmp(args[i], "-l") == 0 || strcmp(args[i], "--inputFormat") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input color format missing.\n");
            switch (atoi(args[i]))
            {
            case 0:
                params->format.image.eColorFormat = OMX_COLOR_FormatYUV420Planar;
                break;
            case 1:
                params->format.image.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
                break;
            default:
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR, "Unknown color format.\n");
                return OMX_ErrorBadParameter;
            }
        }
        else if(strcmp(args[i], "-h") == 0 ||
                strcmp(args[i], "--lumHeightSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input height missing.\n");
            params->format.image.nFrameHeight = atoi(args[i]);
        }
        else if(strcmp(args[i], "-w") == 0 ||
                strcmp(args[i], "--lumWidthSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input width missing.\n");
            params->format.image.nFrameWidth = atoi(args[i]);
        }
        else if(strcmp(args[i], "-s") == 0 ||
                strcmp(args[i], "--buffer-size") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer allocation size is missing.\n");
            params->nBufferSize = atoi(args[i]);
        }
        else if(strcmp(args[i], "-c") == 0 ||
                strcmp(args[i], "--buffer-count") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer count is missing.\n");
            params->nBufferCountActual = atoi(args[i]);
        }
        else if(strcmp(args[i], "--inputAlignmentExp") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for Set alignment value is missing.\n");
            params->nBufferAlignment = (1 << atoi(args[i]));
        }
        else
        {
            /* do nothing, paramter may be needed by subsequent parameter readers */
        }

        ++i;
    }

    if (params->format.image.nStride < params->format.image.nFrameWidth)
        params->format.image.nStride = params->format.image.nFrameWidth;

    params->format.image.nStride = 
        OMXENCODER_STRIDE(params->format.image.nStride, params->nBufferAlignment);

    return OMX_ErrorNone;
}

/*
 */
OMX_ERRORTYPE process_encoder_output_parameters(int argc, char **args,
                                                OMX_PARAM_PORTDEFINITIONTYPE *
                                                params)
{
    OMX_S32 i;
    pthread_t tid = pthread_self();

    params->format.video.nSliceHeight = 0;
    params->format.video.nStride = 0;

    i = 1;
    while(i < argc)
    {
        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-O2") == 0 ||
            strcmp(args[i], "--output-compression-format2") == 0)
            {
                if(++i == argc)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                "Parameter for output compression format missing.\n");
                    return OMX_ErrorBadParameter;
                }

                if(strcasecmp(args[i], "avc") == 0)
                    params->format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
                else if(strcasecmp(args[i], "hevc") == 0)
                    params->format.video.eCompressionFormat = OMX_CSI_VIDEO_CodingHEVC;
                else
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                "Unknown compression format.\n");
                    return OMX_ErrorBadParameter;
                }
            }
            else if(strcmp(args[i], "-B2") == 0 ||
                    strcmp(args[i], "--bitsPerSecond2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for bit-rate missing.\n");
                params->format.video.nBitrate = atoi(args[i]);
            }
        }
        /* output compression format */
        else if(strcmp(args[i], "-O") == 0 ||
           strcmp(args[i], "--output-compression-format") == 0)
        {
            if(++i == argc)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Parameter for output compression format missing.\n");
                return OMX_ErrorBadParameter;
            }

            if(strcasecmp(args[i], "avc") == 0)
                params->format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
            else if(strcasecmp(args[i], "hevc") == 0)
                params->format.video.eCompressionFormat = OMX_CSI_VIDEO_CodingHEVC;
            else
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Unknown compression format.\n");
                return OMX_ErrorBadParameter;
            }
        }
        else if(strcmp(args[i], "-B") == 0 ||
                strcmp(args[i], "--bitsPerSecond") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for bit-rate missing.\n");
            params->format.video.nBitrate = atoi(args[i]);
        }
        else if(strcmp(args[i], "-h") == 0 ||
                strcmp(args[i], "--lumHeightSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input height missing.\n");
            params->format.video.nFrameHeight = atoi(args[i]);
        }
        else if(strcmp(args[i], "-w") == 0 ||
                strcmp(args[i], "--lumWidthSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input width missing.\n");
            params->format.video.nFrameWidth = atoi(args[i]);
        }
        else if(strcmp(args[i], "-f") == 0 ||
                strcmp(args[i], "--outputRateNumer") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for output frame rate is missing.\n");
            params->format.video.xFramerate = FLOAT_Q16(strtod(args[i], 0));
        }
        else if(strcmp(args[i], "-s") == 0 ||
                strcmp(args[i], "--buffer-size") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer allocation size is missing.\n");
            params->nBufferSize = atoi(args[i]);
        }
        else if(strcmp(args[i], "-c") == 0 ||
                strcmp(args[i], "--buffer-count") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer count is missing.\n");
            params->nBufferCountActual = atoi(args[i]);
        }
        else
        {
            /* do nothing, paramter may be needed by subsequent parameter readers */
        }
        ++i;
    }

    return OMX_ErrorNone;
}

/*
 */
OMX_ERRORTYPE process_encoder_image_output_parameters(int argc, char **args,
                                                     OMX_PARAM_PORTDEFINITIONTYPE
                                                     * params)
{
    OMX_S32 i;
    pthread_t tid = pthread_self();

    params->format.image.nSliceHeight = 0;
    params->format.image.nStride = 0;
    params->format.image.eColorFormat = OMX_COLOR_FormatUnused;

    i = 1;
    while(i < argc)
    {
        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-O2") == 0 ||
            strcmp(args[i], "--output-compression-format2") == 0)
            {
                if(++i == argc)
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                "Parameter for output compression format missing.\n");
                    return OMX_ErrorBadParameter;
                }

                if(strcmp(args[i], "jpeg") == 0)
                    params->format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
                else
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                "Unknown compression format.\n");
                    return OMX_ErrorBadParameter;
                }
            }
        }
        /* input compression format */
        else if(strcmp(args[i], "-O") == 0 ||
           strcmp(args[i], "--output-compression-format") == 0)
        {
            if(++i == argc)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Parameter for output compression format missing.\n");
                return OMX_ErrorBadParameter;
            }

            if(strcmp(args[i], "jpeg") == 0)
                params->format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
            else
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Unknown compression format.\n");
                return OMX_ErrorBadParameter;
            }
        }
        else if(strcmp(args[i], "-h") == 0 || strcmp(args[i], "--lumHeightSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input height missing.\n");
            params->format.image.nFrameHeight = atoi(args[i]);
        }
        else if(strcmp(args[i], "-w") == 0 ||
                strcmp(args[i], "--lumWidthSrc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for input width missing.\n");
            params->format.image.nFrameWidth = atoi(args[i]);
        }
        else if(strcmp(args[i], "-s") == 0 ||
                strcmp(args[i], "--buffer-size") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer allocation size is missing.\n");
            params->nBufferSize = atoi(args[i]);
        }
        else if(strcmp(args[i], "-c") == 0 ||
                strcmp(args[i], "--buffer-count") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for buffer count is missing.\n");
            params->nBufferCountActual = atoi(args[i]);
        }
        else
        {
            /* do nothing, paramter may be needed by subsequent parameter readers */
        }
        ++i;
    }

    return OMX_ErrorNone;
}

/*
*/
void print_avc_usage()
{
    printf("  AVC parameters:\n"
           "    -p, --profile                    Sum of following: \n"
           "                                      0x01    baseline (def)  0x10    high10\n"
           "                                      0x02    main            0x20    high422\n"
           "                                      0x04    extended        0x40    high444\n"
           "                                      0x08    high\n"
           "\n"
           "    -L, --level                      Sum of following: \n"
           "                                      0x0001   level1         0x0100    level3\n"
           "                                      0x0002   level1b        0x0200    level31\n"
           "                                      0x0004   level11        0x0400    level32\n"
           "                                      0x0008   level12        0x0800    level4 (def)\n"
           "                                      0x0010   level13        0x1000    level41\n"
           "                                      0x0020   level2         0x2000    level42\n"
           "                                      0x0040   level21        0x4000    level5\n"
           "                                      0x0080   level22        0x8000    level51\n"
           "\n"
           "    -C, --control-rate               disable, variable, constant, variable-skipframes, constant-skipframes\n"
           "                                     constant-skipframes\n"
           "    -n, --npframes                   Number of P frames between each I frame\n"
           "                                     Set to 65535 for infinit P frames (only the 1st frame is I frame)\n"
           "    -d, --deblocking                 Set deblocking\n"
           "    -q, --qpp                        QP value to use for P frames\n"
           "\n");

}

/*
*/
void print_hevc_usage()
{
    printf("  HEVC parameters:\n"
           "    -p, --profile                    1    main (def)\n"
           "\n"
           "    -L, --level                      30    level1\n"
           "                                     60    level2\n"
           "                                     63    level2.1\n"
           "                                     90    level3\n"
           "                                     93    level3.1\n"
           "                                     120   level4\n"
           "                                     123   level4.1\n"
           "                                     150   level5\n"
           "                                     153   level5.1\n"
           "\n"
           "    -C, --control-rate               disable, variable, constant, variable-skipframes, constant-skipframes\n"
           "                                     constant-skipframes\n"
           "    -n, --npframes                   Number of P frames between each I frame\n"
           "                                     Set to 65535 for infinit P frames (only the 1st frame is I frame)\n"
           "    -d, --deblocking                 Set deblocking\n"
           "    -q, --qpp                        QP value to use for P frames\n\n"

           "        --nTcOffset                  Deblocking filter tc offset\n"
           "        --nBetaOffset                Deblocking filter beta offset\n"
           "        --bEnableDeblockOverride     Deblocking override enable flag\n"
           "        --bDeblockOverride           Deblocking override flag\n"
           "        --bEnableSAO                 Disable or Enable SAO Filter\n"
           "\n");

}

/*
*/
void print_jpeg_usage()
{
    printf("  JPEG parameters:\n"
           "    -q, --qLevel                     JPEG Q factor value in the range of 0-10\n");
}

/*
*/
OMX_ERRORTYPE process_avc_parameters(int argc, char **args,
                                     OMX_VIDEO_PARAM_AVCTYPE * parameters)
{
    int i = 0;
    pthread_t tid = pthread_self();

    parameters->eProfile = OMX_VIDEO_AVCProfileHigh;
    parameters->eLevel = OMX_VIDEO_AVCLevel51;

    while(++i < argc)
    {
        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-p2") == 0 || strcmp(args[i], "--profile2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for profile is missing.\n");
                parameters->eProfile = strtol(args[i], 0, 16);
            }
        }
        else if(strcmp(args[i], "-p") == 0 || strcmp(args[i], "--profile") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for profile is missing.\n");
            parameters->eProfile = strtol(args[i], 0, 16);
        }
        else if(strcmp(args[i], "-L") == 0 || strcmp(args[i], "--level") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for level is missing.\n");

            switch (atoi(args[i]))
            {
            case 10:
                parameters->eLevel = OMX_VIDEO_AVCLevel1;
                break;

            case 99:
                parameters->eLevel = OMX_VIDEO_AVCLevel1b;
                break;

            case 11:
                parameters->eLevel = OMX_VIDEO_AVCLevel11;
                break;

            case 12:
                parameters->eLevel = OMX_VIDEO_AVCLevel12;
                break;

            case 13:
                parameters->eLevel = OMX_VIDEO_AVCLevel13;
                break;

            case 20:
                parameters->eLevel = OMX_VIDEO_AVCLevel2;
                break;

            case 21:
                parameters->eLevel = OMX_VIDEO_AVCLevel21;
                break;

            case 22:
                parameters->eLevel = OMX_VIDEO_AVCLevel22;
                break;

            case 30:
                parameters->eLevel = OMX_VIDEO_AVCLevel3;
                break;

            case 31:
                parameters->eLevel = OMX_VIDEO_AVCLevel31;
                break;

            case 32:
                parameters->eLevel = OMX_VIDEO_AVCLevel32;
                break;
            case 40:
                parameters->eLevel = OMX_VIDEO_AVCLevel4;
                break;
            case 41:
                parameters->eLevel = OMX_VIDEO_AVCLevel41;
                break;
            case 42:
                parameters->eLevel = OMX_VIDEO_AVCLevel42;
                break;
            case 50:
                parameters->eLevel = OMX_VIDEO_AVCLevel5;
                break;
            case 51:
                parameters->eLevel = OMX_VIDEO_AVCLevel51;
                break;
            case 52:
                parameters->eLevel = OMX_CSI_VIDEO_AVCLevel52;
                break;
            case 60:
                parameters->eLevel = OMX_CSI_VIDEO_AVCLevel60;
                break;
            case 61:
                parameters->eLevel = OMX_CSI_VIDEO_AVCLevel61;
                break;
            case 62:
                parameters->eLevel = OMX_CSI_VIDEO_AVCLevel62;
                break;
            default:
                parameters->eLevel = 0;
                break;
            }
        }
        else if(strcmp(args[i], "-n") == 0 ||
                strcmp(args[i], "--npframes") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for number of P frames is missing.\n");
            parameters->nPFrames = atoi(args[i]);
        }
        else if(strcmp(args[i], "-F") == 0 ||
                strcmp(args[i], "--nrefframes") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for number of reference frames is missing.\n");
            parameters->nRefFrames = atoi(args[i]);
        }
        else if(strcmp(args[i], "-K") == 0 ||
                strcmp(args[i], "--enableCabac") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for CABAC enable is missing.\n");
            parameters->bEntropyCodingCABAC = atoi(args[i]);
        }
        else
        {
            /* Do nothing, purpose is to traverse all options and to find known ones */
        }
    }

    return OMX_ErrorNone;
}

/*
*/
OMX_ERRORTYPE process_parameters_bitrate(int argc, char **args,
                                         OMX_VIDEO_PARAM_BITRATETYPE * bitrate)
{
    int i = 0;
    pthread_t tid = pthread_self();

    bitrate->eControlRate = OMX_Video_ControlRateDisable;

    while(++i < argc)
    {
        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-C2") == 0 || strcmp(args[i], "--control-rate2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for control rate is missing.\n");

                if(strcasecmp(args[i], "disable") == 0)
                    bitrate->eControlRate = OMX_Video_ControlRateDisable;
                else if(strcasecmp(args[i], "variable") == 0)
                    bitrate->eControlRate = OMX_Video_ControlRateVariable;
                else if(strcasecmp(args[i], "constant") == 0)
                    bitrate->eControlRate = OMX_Video_ControlRateConstant;
                else if(strcasecmp(args[i], "variable-skipframes") == 0)
                    bitrate->eControlRate = OMX_Video_ControlRateVariableSkipFrames;
                else if(strcasecmp(args[i], "constant-skipframes") == 0)
                    bitrate->eControlRate = OMX_Video_ControlRateConstantSkipFrames;
                else
                {
                    OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                                "Invalid control rate type\n");
                    return OMX_ErrorBadParameter;
                }
            }
            /* duplicate for common parameters */
            if(strcmp(args[i], "-B2") == 0 ||
            strcmp(args[i], "--bitsPerSecond2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for bit rate is missing.\n");
                bitrate->nTargetBitrate = atoi(args[i]);
            }
        }
        else if(strcmp(args[i], "-C") == 0 || strcmp(args[i], "--control-rate") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for control rate is missing.\n");

            if(strcasecmp(args[i], "disable") == 0)
                bitrate->eControlRate = OMX_Video_ControlRateDisable;
            else if(strcasecmp(args[i], "variable") == 0)
                bitrate->eControlRate = OMX_Video_ControlRateVariable;
            else if(strcasecmp(args[i], "constant") == 0)
                bitrate->eControlRate = OMX_Video_ControlRateConstant;
            else if(strcasecmp(args[i], "variable-skipframes") == 0)
                bitrate->eControlRate = OMX_Video_ControlRateVariableSkipFrames;
            else if(strcasecmp(args[i], "constant-skipframes") == 0)
                bitrate->eControlRate = OMX_Video_ControlRateConstantSkipFrames;
            else
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                               "Invalid control rate type\n");
                return OMX_ErrorBadParameter;
            }
        }
        /* duplicate for common parameters */
        if(strcmp(args[i], "-B") == 0 ||
           strcmp(args[i], "--bitsPerSecond") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for bit rate is missing.\n");
            bitrate->nTargetBitrate = atoi(args[i]);
        }
        else
        {
            /* Do nothing, purpose is to traverse all options and to find known ones */
        }
    }
    return OMX_ErrorNone;
}

/*
*/
OMX_ERRORTYPE process_avc_parameters_deblocking(int argc, char **args,
                                                OMX_PARAM_DEBLOCKINGTYPE *
                                                deblocking)
{
    int i = 0;
    deblocking->bDeblocking = OMX_TRUE; //OMX_FALSE;

    while(++i < argc)
    {

        if(strcmp(args[i], "-d") == 0 || strcmp(args[i], "--deblocking") == 0)
        {
            deblocking->bDeblocking = OMX_TRUE;
            break;
        }
        else
        {
            /* Do nothing, purpose is to traverse all options and to find known ones */
        }
    }

    return OMX_ErrorNone;
}

/*
*/
OMX_ERRORTYPE process_parameters_quantization(int argc, char **args,
                                              OMX_VIDEO_PARAM_QUANTIZATIONTYPE *
                                              quantization)
{
    int i = 0;
    pthread_t tid = pthread_self();

    while(++i < argc)
    {
        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-q2") == 0 || strcmp(args[i], "--qpi2") == 0 ||
            strcmp(args[i], "-qLevel2") == 0 )
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for QPI is missing.\n");
                quantization->nQpI = atoi(args[i]);
                break;
            }
        }
        else if(strcmp(args[i], "-q") == 0 || strcmp(args[i], "--qpi") == 0 ||
           strcmp(args[i], "-qLevel") == 0 )
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for QPI is missing.\n");
            quantization->nQpI = atoi(args[i]);
            break;
        }
        else
        {
            /* Do nothing, purpose is to traverse all options and to find known ones */
        }
    }

    return OMX_ErrorNone;
}

/*
*/
OMX_ERRORTYPE process_parameters_avc_extension(int argc, char **args,
                                              OMX_CSI_VIDEO_PARAM_AVCTYPEEXT *
                                              extensions)
{
    int i = 0;
    pthread_t tid = pthread_self();

    while(++i < argc)
    {
        if (tid == encode_thread[1])
        {
            // the 2nd thread just uses default AVC extension parameters
        }
        else if(strcmp(args[i], "--cpbSize") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for hrd cpbSize is missing.\n");
            extensions->nHrdCpbSize = atoi(args[i]);
        }
        else if(strcmp(args[i], "--preset") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for preset is missing.\n");
            extensions->nPreset = atoi(args[i]);
        }
        else if(strcmp(args[i], "--rfcEnable") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for preset is missing.\n");
            extensions->bEnableMBS = atoi(args[i]);
        }
        else if(strcmp(args[i], "-A") == 0 ||
                strcmp(args[i], "--intraQpDelta") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for intraQpDelta is missing.\n");
            extensions->nIntraQpDelta = atoi(args[i]);
        }
        else if(strcmp(args[i], "-u") == 0 ||
                strcmp(args[i], "--ctbRc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for CTB QP adjustment mode for Rate Control and Subjective Quality is missing.\n");
            extensions->nCTBRC = atoi(args[i]);
        }
        else if(strcmp(args[i], "--vbr") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for vbr is missing.\n");
            extensions->bEnableConstrainedVBR = atoi(args[i]);
        }
        else if(strcmp(args[i], "--qpMinI") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for qpMinI is missing.\n");
            extensions->nQpMinI = atoi(args[i]);
        }
        else if(strcmp(args[i], "--qpMaxI") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for qpMaxI is missing.\n");
            extensions->nQpMaxI = atoi(args[i]);
        }
        else if(strcmp(args[i], "--qpMinPB") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for qpMinPB is missing.\n");
            extensions->nQpMinPB = atoi(args[i]);
        }
        else if(strcmp(args[i], "--qpMaxPB") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for qpMaxPB is missing.\n");
            extensions->nQpMaxPB = atoi(args[i]);
        }
        else
        {
            /* Do nothing, purpose is to traverse all options and to find known ones */
        }
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE process_parameters_image_qfactor(int argc, char **args,
                                               OMX_IMAGE_PARAM_QFACTORTYPE *
                                               quantization)
{
    int i = 0;
    pthread_t tid = pthread_self();

    while(++i < argc)
    {

        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-q2") == 0 || strcmp(args[i], "--qfactor2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for QFactor is missing.\n");
                quantization->nQFactor = atoi(args[i]);

                break;
            }
        }
        else if(strcmp(args[i], "-q") == 0 || strcmp(args[i], "--qfactor") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for QFactor is missing.\n");
            quantization->nQFactor = atoi(args[i]);

            break;
        }
        else
        {
            /* Do nothing, purpose is to traverse all options and to find known ones */
        }
    }

    return OMX_ErrorNone;
}

#define DEFAULT -255
/*
*/
OMX_ERRORTYPE process_hevc_parameters(int argc, char **args,
                                     OMX_CSI_VIDEO_PARAM_HEVCTYPE * parameters)
{
    int i;
    char *endp;
    pthread_t tid = pthread_self();

    i = 0;
    while(++i < argc)
    {
        if (tid == encode_thread[1])
        {
            if(strcmp(args[i], "-p2") == 0 || strcmp(args[i], "--profile2") == 0)
            {
                OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                            "Parameter for profile is missing.\n");
                parameters->eProfile = strtol(args[i], 0, 16);
            }
        }
        else if(strcmp(args[i], "-p") == 0 || strcmp(args[i], "--profile") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for profile is missing.\n");
            parameters->eProfile = strtol(args[i], 0, 16);
        }
        else if(strcmp(args[i], "-L") == 0 || strcmp(args[i], "--level") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for level is missing.\n");

            switch (atoi(args[i]))
            {
            case 30:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel1;
                break;

            case 60:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel2;
                break;

            case 63:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel21;
                break;

            case 90:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel3;
                break;

            case 93:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel31;
                break;

            case 120:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel4;
                break;

            case 123:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel41;
                break;

            case 150:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel5;
                break;

            case 153:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel51;
                break;

            case 156:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel52;
                break;
            case 180:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel6;
                break;
            case 183:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel61;
                break;
            case 186:
                parameters->eLevel = OMX_CSI_VIDEO_HEVCLevel62;
                break;
            default:
                parameters->eLevel = 0;
                break;
            }
        }
        else if(strcmp(args[i], "-n") == 0 ||
                strcmp(args[i], "--npframes") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for number of P frames is missing.\n");
            parameters->nPFrames = atoi(args[i]);
        }
        else if(strcmp(args[i], "-F") == 0 ||
                strcmp(args[i], "--nrefframes") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for number of reference frames is missing.\n");
            parameters->nRefFrames = atoi(args[i]);
        }
        else if(strcmp(args[i], "--nTcOffset") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE_SIGNED(i, args, argc,
                                        "Parameter for nTcOffset is missing.\n");
            parameters->nTcOffset = strtol(args[i], &endp, 10);

            if (*endp)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                    "Parameter for nTcOffset is missing.\n");
                return OMX_ErrorBadParameter;
            }
        }
        else if(strcmp(args[i], "--nBetaOffset") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE_SIGNED(i, args, argc,
                                        "Parameter for nBetaOffset is missing.\n");
            parameters->nBetaOffset = strtol(args[i], &endp, 10);

            if (*endp)
            {
                OMX_OSAL_Trace(OMX_OSAL_TRACE_ERROR,
                    "Parameter for nBetaOffset is missing.\n");
                return OMX_ErrorBadParameter;
            }
        }
        else if(strcmp(args[i], "--bEnableDeblockOverride") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for bEnableDeblockOverride is missing.\n");
            parameters->bEnableDeblockOverride = atoi(args[i]);
        }
        else if(strcmp(args[i], "--bDeblockOverride") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for bDeblockOverride is missing.\n");
            parameters->bDeblockOverride = atoi(args[i]);
        }
        else if(strcmp(args[i], "--bEnableSAO") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for bEnableSAO is missing.\n");
            parameters->bEnableSAO = atoi(args[i]);
        }
        else if(strcmp(args[i], "-u") == 0 ||
                strcmp(args[i], "--ctbRc") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for CTB QP adjustment mode for Rate Control and Subjective Quality is missing.\n");
            parameters->nCTBRC = atoi(args[i]);
        }
        else if(strcmp(args[i], "--ipcmFilterDisable") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for ipcmFilterDisable is missing.\n");
            parameters->bDisablePcmLF = atoi(args[i]);
        }
        else if(strcmp(args[i], "--cpbSize") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for hrd-cpbSize is missing.\n");
            parameters->nHrdCpbSize = atoi(args[i]);
        }
        else if(strcmp(args[i], "-A") == 0 ||
                strcmp(args[i], "--intraQpDelta") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for intraQpDelta is missing.\n");
            parameters->nIntraQpDelta = atoi(args[i]);
        }
        else if(strcmp(args[i], "-G") == 0 ||
                strcmp(args[i], "--fixedIntraQp") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for fixedIntraQp is missing.\n");
            parameters->nFixedIntraQp = atoi(args[i]);
        }
        else if(strcmp(args[i], "--vbr") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for vbr is missing.\n");
            parameters->bEnableConstrainedVBR = atoi(args[i]);
        }
        else if(strcmp(args[i], "--qpMinI") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for qpMinI is missing.\n");
            parameters->nQpMinI = atoi(args[i]);
        }
        else if(strcmp(args[i], "--qpMaxI") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for qpMaxI is missing.\n");
            parameters->nQpMaxI = atoi(args[i]);
        }
        else if(strcmp(args[i], "--qpMinPB") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for qpMinPB is missing.\n");
            parameters->nQpMinPB = atoi(args[i]);
        }
        else if(strcmp(args[i], "--qpMaxPB") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for qpMaxPB is missing.\n");
            parameters->nQpMaxPB = atoi(args[i]);
        }
        else if(strcmp(args[i], "--enableVuiTimingInfo") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for enableVuiTimingInfo is missing.\n");
            parameters->bEnableVuiTimingInfo = atoi(args[i]);
        }
        else if(strcmp(args[i], "--preset") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for preset is missing.\n");
            parameters->nPreset = atoi(args[i]);
        }
        else if(strcmp(args[i], "--rfcEnable") == 0)
        {
            OMXENCODER_CHECK_NEXT_VALUE(i, args, argc,
                                        "Parameter for preset is missing.\n");
            parameters->bEnableMBS = atoi(args[i]);
        }
        else
        {
            /* Do nothing, purpose is to traverse all options and to find known ones */
        }
    }

    return OMX_ErrorNone;
}
