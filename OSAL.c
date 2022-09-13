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

#include "OSAL.h"


#define _XOPEN_SOURCE 600

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <assert.h>

#define UNUSED_PARAMETER(p) (void)(p)

#define DBGT_PTRACE(...)
#define DBGT_PROLOG(...)
#define DBGT_EPILOG(...)
#define DBGT_PDEBUG(...)

#define LOGE(fmt, args...) printf(fmt "\n", ##args)
#define DBGT_CRITICAL(fmt, args...)                             \
    LOGE( "OSAL ! %s " fmt " %s:%d",                  \
          __FUNCTION__, ## args, __FILE__, __LINE__)

#ifndef LOG_ALWAYS_FATAL_IF
#define LOG_ALWAYS_FATAL_IF(condition, ...)\
do {                                     \
    if (condition) {                       \
        LOGE(__VA_ARGS__);                   \
        assert(!condition);                  \
    }                                      \
} while (0)
#endif

#define DBGT_ASSERT(condition, args...)                            \
do {                                                             \
    if (!(condition)) {                                            \
        LOGE("OSAL ! "                        \
            "assertion !(" #condition ") ""failed at %s, %s:%d",    \
            __FUNCTION__,  __FILE__, __LINE__); \
        LOG_ALWAYS_FATAL_IF(!(condition), ## args);                  \
    }                                                              \
} while (0)

/*------------------------------------------------------------------------------
    Definitions
------------------------------------------------------------------------------*/
int getpagesize(void);
#define MEMORY_SENTINEL 0xACDCACDC;

typedef struct OSAL_THREADDATATYPE {
    pthread_t oPosixThread;
    pthread_attr_t oThreadAttr;
    OSAL_U32 (*pFunc)(OSAL_PTR pParam);
    OSAL_PTR pParam;
    OSAL_U32 uReturn;
} OSAL_THREADDATATYPE;

typedef struct {
    OSAL_BOOL       bSignaled;
    pthread_mutex_t mutex;
    int             fd[2];
} OSAL_THREAD_EVENT;

/*------------------------------------------------------------------------------
    OSAL_Malloc
------------------------------------------------------------------------------*/
OSAL_PTR OSAL_Malloc(OSAL_U32 size)
{
    DBGT_PROLOG("");

    OSAL_U32 extra = sizeof(OSAL_U32) * 2;
    OSAL_U8*  data = (OSAL_U8*)malloc(size + extra);
    OSAL_U32 sentinel = MEMORY_SENTINEL;

    if (!data) {
        DBGT_CRITICAL("No more memory (size=%d)", (int)(size + extra));
        return(0);
    }

    memcpy(data, &size, sizeof(size));
    memcpy(&data[size + sizeof(size)], &sentinel, sizeof(sentinel));

    DBGT_EPILOG("");
    return data + sizeof(size);
}

/*------------------------------------------------------------------------------
    OSAL_Free
------------------------------------------------------------------------------*/
void OSAL_Free(OSAL_PTR pData)
{
    DBGT_PROLOG("");

    OSAL_U8* block    = ((OSAL_U8*)pData) - sizeof(OSAL_U32);

    OSAL_U32 sentinel = MEMORY_SENTINEL;
    OSAL_U32 size     = *((OSAL_U32*)block);

    DBGT_ASSERT(memcmp(&block[size+sizeof(size)], &sentinel, sizeof(sentinel))==0 &&
            "mem corruption detected");

    free(block);
    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    OSAL_AllocatorInit
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_AllocatorInit(OSAL_ALLOCATOR* alloc)
{
    DBGT_PROLOG("");
    UNUSED_PARAMETER(alloc);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_AllocatorDestroy
------------------------------------------------------------------------------*/
void OSAL_AllocatorDestroy(OSAL_ALLOCATOR* alloc)
{
    DBGT_PROLOG("");
    UNUSED_PARAMETER(alloc);
    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    OSAL_AllocatorAllocMem
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_AllocatorAllocMem(OSAL_ALLOCATOR* alloc, OSAL_U32* size,
        OSAL_U8** bus_data, OSAL_BUS_WIDTH* bus_address, OSAL_BUS_WIDTH* unmap_bus_address)
{
    DBGT_PROLOG("");

    UNUSED_PARAMETER(alloc);
    OSAL_U32 extra = sizeof(OSAL_U32);
    OSAL_U8* data  = (OSAL_U8*)malloc(*size + extra);
    if (data == NULL)
    {
        DBGT_CRITICAL("malloc failed (size=%d) - OSAL_ERROR_INSUFFICIENT_RESOURCES", (int)(*size + extra));
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    OSAL_U32 sentinel = MEMORY_SENTINEL;
    // copy sentinel at the end of mem block
    memcpy(&data[*size], &sentinel, sizeof(OSAL_U32));

    *bus_data    = data;
    *bus_address = (OSAL_BUS_WIDTH)data;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_AllocatorFreeMem
------------------------------------------------------------------------------*/
void OSAL_AllocatorFreeMem(OSAL_ALLOCATOR* alloc, OSAL_U32 size,
        OSAL_U8* bus_data, OSAL_BUS_WIDTH bus_address, OSAL_BUS_WIDTH unmap_bus_address)
{
    DBGT_PROLOG("");

    DBGT_ASSERT(((OSAL_BUS_WIDTH)bus_data) == bus_address);
    OSAL_U32 sentinel = MEMORY_SENTINEL;
    DBGT_ASSERT(memcmp(&bus_data[size], &sentinel, sizeof(OSAL_U32)) == 0 &&
            "memory corruption detected");

    UNUSED_PARAMETER(alloc);
    /*UNUSED_PARAMETER(size);
    UNUSED_PARAMETER(bus_address);*/
    free(bus_data);

    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    OSAL_ExportMem
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_ExportMem(OSAL_ALLOCATOR* alloc, OSAL_BUS_WIDTH unmap_bus_address, OSAL_I32 *fd)
{
    DBGT_PROLOG("");
    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    OSAL_ImportMem
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_ImportMem(OSAL_ALLOCATOR* alloc, OSAL_I32 fd, 
                    OSAL_U32 *size, OSAL_U8** bus_data, 
                    OSAL_BUS_WIDTH *bus_address, OSAL_BUS_WIDTH *unmap_bus_address)
{
    DBGT_PROLOG("");
    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    OSAL_ReleaseMem
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_ReleaseMem(OSAL_ALLOCATOR* alloc, OSAL_I32 fd, 
                    OSAL_U32 size, OSAL_U8* bus_data, 
                    OSAL_BUS_WIDTH bus_address, OSAL_BUS_WIDTH unmap_bus_address)
{
    DBGT_PROLOG("");
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}


/*------------------------------------------------------------------------------
    OSAL_AllocatorIsReady
------------------------------------------------------------------------------*/
OSAL_BOOL OSAL_AllocatorIsReady(const OSAL_ALLOCATOR* alloc)
{
    DBGT_PROLOG("");
    UNUSED_PARAMETER(alloc);
    DBGT_EPILOG("");
    return 1;
}


/*------------------------------------------------------------------------------
    OSAL_Memset
------------------------------------------------------------------------------*/
OSAL_PTR OSAL_Memset(OSAL_PTR pDest, OSAL_U32 cChar, OSAL_U32 nCount)
{
    return memset(pDest, cChar, nCount);
}

/*------------------------------------------------------------------------------
    OSAL_Memcpy
------------------------------------------------------------------------------*/
OSAL_PTR OSAL_Memcpy(OSAL_PTR pDest, OSAL_PTR pSrc, OSAL_U32 nCount)
{
    return memcpy(pDest, pSrc, nCount);
}

/*------------------------------------------------------------------------------
    BlockSIGIO      Linux EWL uses SIGIO to signal interrupt
------------------------------------------------------------------------------*/
static void BlockSIGIO()
{
    DBGT_PROLOG("");
    sigset_t set, oldset;

    /* Block SIGIO from the main thread to make sure that it will be handled
     * in the encoding thread */

    sigemptyset(&set);
    sigemptyset(&oldset);
    sigaddset(&set, SIGIO);
    pthread_sigmask(SIG_BLOCK, &set, &oldset);
    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    threadFunc
------------------------------------------------------------------------------*/
static void *threadFunc(void *pParameter)
{
    DBGT_PROLOG("");

    OSAL_THREADDATATYPE *pThreadData;
    pThreadData = (OSAL_THREADDATATYPE *)pParameter;
    pThreadData->uReturn = pThreadData->pFunc(pThreadData->pParam);
    DBGT_EPILOG("");
    return pThreadData;
}

/*------------------------------------------------------------------------------
    OSAL_ThreadCreate
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_ThreadCreate(OSAL_U32 (*pFunc)(OSAL_PTR pParam),
        OSAL_PTR pParam, OSAL_U32 nPriority, OSAL_PTR *phThread)
{
    DBGT_PROLOG("");

    OSAL_THREADDATATYPE *pThreadData;
    struct sched_param sched;

    pThreadData = (OSAL_THREADDATATYPE*)OSAL_Malloc(sizeof(OSAL_THREADDATATYPE));
    if (pThreadData == NULL)
    {
        DBGT_CRITICAL("OSAL_Malloc failed - OSAL_ERROR_INSUFFICIENT_RESOURCES");
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    pThreadData->pFunc = pFunc;
    pThreadData->pParam = pParam;
    pThreadData->uReturn = 0;

    pthread_attr_init(&pThreadData->oThreadAttr);

    pthread_attr_getschedparam(&pThreadData->oThreadAttr, &sched);
    sched.sched_priority += nPriority;
    pthread_attr_setschedparam(&pThreadData->oThreadAttr, &sched);

    if (pthread_create(&pThreadData->oPosixThread,
                       &pThreadData->oThreadAttr,
                       threadFunc,
                       pThreadData)) {
        DBGT_CRITICAL("pthread_create failed - OSAL_ERROR_INSUFFICIENT_RESOURCES");
        OSAL_Free(pThreadData);
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    BlockSIGIO();

    *phThread = (OSAL_PTR)pThreadData;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_ThreadDestroy
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_ThreadDestroy(OSAL_PTR hThread)
{
    DBGT_PROLOG("");

    OSAL_THREADDATATYPE *pThreadData = (OSAL_THREADDATATYPE *)hThread;
    void *retVal = &pThreadData->uReturn;

    if (pThreadData == NULL) {
        DBGT_CRITICAL("(pThreadData == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    //pthread_cancel(pThreadData->oPosixThread);

    if (pthread_join(pThreadData->oPosixThread, &retVal)) {
        DBGT_CRITICAL("pthread_join failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    OSAL_Free(pThreadData);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_ThreadSleep
------------------------------------------------------------------------------*/
void OSAL_ThreadSleep(OSAL_U32 ms)
{
    DBGT_PROLOG("");
    usleep(ms*1000);
    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    OSAL_MutexCreate
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_MutexCreate(OSAL_PTR *phMutex)
{
    DBGT_PROLOG("");

    pthread_mutex_t *pMutex = (pthread_mutex_t *)
                                OSAL_Malloc(sizeof(pthread_mutex_t));
    static pthread_mutexattr_t oAttr;
    static pthread_mutexattr_t *pAttr = NULL;

    if (pAttr == NULL &&
        !pthread_mutexattr_init(&oAttr) &&
        !pthread_mutexattr_settype(&oAttr, PTHREAD_MUTEX_RECURSIVE))
    {
        pAttr = &oAttr;
    }

    if (pMutex == NULL)
    {
        DBGT_CRITICAL("OSAL_Malloc failed - OSAL_ERROR_INSUFFICIENT_RESOURCES");
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    if (pthread_mutex_init(pMutex, pAttr)) {
        DBGT_CRITICAL("pthread_mutex_init failed - OSAL_ERROR_INSUFFICIENT_RESOURCES");
        OSAL_Free(pMutex);
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    *phMutex = (void *)pMutex;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}


/*------------------------------------------------------------------------------
    OSAL_MutexDestroy
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_MutexDestroy(OSAL_PTR hMutex)
{
    DBGT_PROLOG("");
    pthread_mutex_t *pMutex = (pthread_mutex_t *)hMutex;

    if (pMutex == NULL) {
        DBGT_CRITICAL("(pMutex == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pthread_mutex_destroy(pMutex)) {
        DBGT_CRITICAL("pthread_mutex_destroy failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    OSAL_Free(pMutex);
    pMutex = NULL;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_MutexLock
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_MutexLock(OSAL_PTR hMutex)
{
    DBGT_PROLOG("");

    pthread_mutex_t *pMutex = (pthread_mutex_t *)hMutex;
    int err;

    if (pMutex == NULL) {
        DBGT_CRITICAL("(pMutex == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    err = pthread_mutex_lock(pMutex);
    switch (err) {
    case 0:
        DBGT_EPILOG("");
        return OSAL_ERRORNONE;
    case EINVAL:
        DBGT_CRITICAL("pthread_mutex_lock EINVAL");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    case EDEADLK:
        DBGT_CRITICAL("pthread_mutex_lock EDEADLK");
        DBGT_EPILOG("");
        return OSAL_ERROR_NOT_READY;
    default:
        DBGT_CRITICAL("pthread_mutex_lock undefined err");
        DBGT_EPILOG("");
        return OSAL_ERROR_UNDEFINED;
    }

    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_MutexUnlock
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_MutexUnlock(OSAL_PTR hMutex)
{
    DBGT_PROLOG("");

    pthread_mutex_t *pMutex = (pthread_mutex_t *)hMutex;
    int err;

    if (pMutex == NULL) {
        DBGT_CRITICAL("(pMutex == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    err = pthread_mutex_unlock(pMutex);
    switch (err) {
    case 0:
        DBGT_EPILOG("");
        return OSAL_ERRORNONE;
    case EINVAL:
        DBGT_CRITICAL("pthread_mutex_unlock EINVAL");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    case EPERM:
        DBGT_CRITICAL("pthread_mutex_unlock EPERM");
        DBGT_EPILOG("");
        return OSAL_ERROR_NOT_READY;
    default:
        DBGT_CRITICAL("pthread_mutex_unlock undefined err");
        DBGT_EPILOG("");
        return OSAL_ERROR_UNDEFINED;
    }

    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_EventCreate
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventCreate(OSAL_PTR *phEvent)
{
    DBGT_PROLOG("");

    OSAL_THREAD_EVENT *pEvent = OSAL_Malloc(sizeof(OSAL_THREAD_EVENT));

    if (pEvent == NULL) {
        DBGT_CRITICAL("OSAL_Malloc failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    pEvent->bSignaled = 0;

    if (pipe(pEvent->fd) == -1)
    {
        DBGT_CRITICAL("pipe(pEvent->fd) failed");
        OSAL_Free(pEvent);
        pEvent = NULL;
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    if (pthread_mutex_init(&pEvent->mutex, NULL))
    {
        DBGT_CRITICAL("pthread_mutex_init failed");
        close(pEvent->fd[0]);
        close(pEvent->fd[1]);
        OSAL_Free(pEvent);
        pEvent = NULL;
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    *phEvent = (OSAL_PTR)pEvent;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_EventDestroy
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventDestroy(OSAL_PTR hEvent)
{
    DBGT_PROLOG("");

    OSAL_THREAD_EVENT *pEvent = (OSAL_THREAD_EVENT *)hEvent;
    if (pEvent == NULL) {
        DBGT_CRITICAL("(pEvent == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pthread_mutex_lock(&pEvent->mutex)) {
        DBGT_CRITICAL("pthread_mutex_lock failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    int err = 0;
    err = close(pEvent->fd[0]); DBGT_ASSERT(err == 0);
    err = close(pEvent->fd[1]); DBGT_ASSERT(err == 0);

    pthread_mutex_unlock(&pEvent->mutex);
    pthread_mutex_destroy(&pEvent->mutex);

    OSAL_Free(pEvent);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_EventReset
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventReset(OSAL_PTR hEvent)
{
    DBGT_PROLOG("");

    OSAL_THREAD_EVENT *pEvent = (OSAL_THREAD_EVENT *)hEvent;
    if (pEvent == NULL) {
        DBGT_CRITICAL("(pEvent == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pthread_mutex_lock(&pEvent->mutex)) {
        DBGT_CRITICAL("pthread_mutex_lock failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pEvent->bSignaled)
    {
        // empty the pipe
        char c = 1;
        int ret = read(pEvent->fd[0], &c, 1);
        if (ret == -1) {
            DBGT_CRITICAL("read(pEvent->fd[0], &c, 1) failed");
            DBGT_EPILOG("");
            return OSAL_ERROR_UNDEFINED;
        }
        pEvent->bSignaled = 0;
    }

    pthread_mutex_unlock(&pEvent->mutex);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_GetTime
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventSet(OSAL_PTR hEvent)
{
    DBGT_PROLOG("");

    OSAL_THREAD_EVENT *pEvent = (OSAL_THREAD_EVENT *)hEvent;
    if (pEvent == NULL) {
        DBGT_CRITICAL("(pEvent == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pthread_mutex_lock(&pEvent->mutex)) {
        DBGT_CRITICAL("pthread_mutex_lock failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (!pEvent->bSignaled)
    {
        char c = 1;
        int ret = write(pEvent->fd[1], &c, 1);
        if (ret == -1) {
            DBGT_CRITICAL("write(pEvent->fd[1], &c, 1) failed");
            DBGT_EPILOG("");
            return OSAL_ERROR_UNDEFINED;
        }
        pEvent->bSignaled = 1;
    }

    pthread_mutex_unlock(&pEvent->mutex);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_EventWait
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventWait(OSAL_PTR hEvent, OSAL_U32 uMsec,
        OSAL_BOOL* pbTimedOut)
{
    OSAL_BOOL signaled = 0;
    return OSAL_EventWaitMultiple(&hEvent, &signaled, 1, uMsec, pbTimedOut);
}

/*------------------------------------------------------------------------------
    OSAL_EventWaitMultiple
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventWaitMultiple(OSAL_PTR* hEvents,
        OSAL_BOOL* bSignaled, OSAL_U32 nCount, OSAL_U32 mSecs,
        OSAL_BOOL* pbTimedOut)
{
    DBGT_PROLOG("");

    DBGT_ASSERT(hEvents);
    DBGT_ASSERT(bSignaled);

    fd_set read;
    FD_ZERO(&read);

    int max = 0;
    unsigned i = 0;
    for (i=0; i<nCount; ++i)
    {
        OSAL_THREAD_EVENT* pEvent = (OSAL_THREAD_EVENT*)(hEvents[i]);

        if (pEvent == NULL) {
            DBGT_CRITICAL("(pEvent == NULL)");
            DBGT_EPILOG("");
            return OSAL_ERROR_BAD_PARAMETER;
        }

        int fd = pEvent->fd[0];
        if (fd > max)
            max = fd;

        FD_SET(fd, &read);
    }

    if (mSecs == INFINITE_WAIT)
    {
        int ret = select(max+1, &read, NULL, NULL, NULL);
        if (ret == -1) {
            //DBGT_CRITICAL("select(max+1, &read, NULL, NULL, NULL) failed");
            DBGT_EPILOG("");
            return OSAL_ERROR_UNDEFINED;
        }
    }
    else
    {
        struct timeval tv;
        memset(&tv, 0, sizeof(struct timeval));
        tv.tv_sec = mSecs / 1000;
        tv.tv_usec = (mSecs % 1000) * 1000;
        int ret = select(max+1, &read, NULL, NULL, &tv);
        if (ret == -1) {
            //DBGT_CRITICAL("select(max+1, &read, NULL, NULL, &tv) failed");
            DBGT_EPILOG("");
            return OSAL_ERROR_UNDEFINED;
        }
        if (ret == 0)
        {
            *pbTimedOut =  1;
        }
    }

    for (i=0; i<nCount; ++i)
    {
        OSAL_THREAD_EVENT* pEvent = (OSAL_THREAD_EVENT*)hEvents[i];

        if (pEvent == NULL) {
            DBGT_CRITICAL("(pEvent == NULL)");
            DBGT_EPILOG("");
            return OSAL_ERROR_BAD_PARAMETER;
        }

        int fd = pEvent->fd[0];
        if (FD_ISSET(fd, &read))
            bSignaled[i] = 1;
        else
            bSignaled[i] = 0;
    }
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}


/*------------------------------------------------------------------------------
    OSAL_GetTime
------------------------------------------------------------------------------*/
OSAL_U32 OSAL_GetTime()
{
    DBGT_PROLOG("");

    struct timeval now;
    gettimeofday(&now, NULL);
    DBGT_EPILOG("");
    return ((OSAL_U32)now.tv_sec) * 1000 + ((OSAL_U32)now.tv_usec) / 1000;
}

