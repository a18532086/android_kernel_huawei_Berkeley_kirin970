/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2012-2015. All rights reserved.
 * foss@huawei.com
 *
 * If distributed as part of the Linux kernel, the following license terms
 * apply:
 *
 * * This program is free software; you can redistribute it and/or modify
 * * it under the terms of the GNU General Public License version 2 and
 * * only version 2 as published by the Free Software Foundation.
 * *
 * * This program is distributed in the hope that it will be useful,
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * * GNU General Public License for more details.
 * *
 * * You should have received a copy of the GNU General Public License
 * * along with this program; if not, write to the Free Software
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) Neither the name of Huawei nor the names of its contributors may
 * *    be used to endorse or promote products derived from this software
 * *    without specific prior written permission.
 *
 * * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */



#include "msp_errno.h"
#include "diag_common.h"
#include "diag_debug.h"
#include "msp_service.h"
#include "soc_socp_adapter.h"
#include "diag_acore_common.h"
#include  <linux/wakelock.h>


#define    THIS_FILE_ID        MSP_FILE_ID_DIAG_SERVICE_C


DIAG_DFR_INFO_STRU g_stDFRcnf;


typedef struct
{
    VOS_SEM                 ListSem;
    LIST_S                  ListHeader;
} DIAG_SRVC_MAIN_STRU;


#define     DIAG_PKT_TIMEOUT_LEN            (32768*3)     /* 3s */

typedef struct
{
    DIAG_SERVICE_HEAD_STRU  stHead;
    VOS_UINT32              ulSlice;        /* 创建节点的时间戳，用于检测超时 */
    LIST_S                  FrameList;
    DIAG_FRAME_INFO_STRU    *pFrame;
} DIAG_SRVC_FRAME_NODE_STRU;

DIAG_SERVICE_FUNC g_fnService = VOS_NULL_PTR;
DIAG_DFR_INFO_STRU g_stDFRreq;

DIAG_SRVC_MAIN_STRU  g_stDiagSrvc;



/*****************************************************************************
 Function Name   : diag_PktTimeoutClear
 Description     : 查看链表中是否有超时的节点，如果有则删除

 History         :
    1.c00326366         2015-08-27  Draft Enact

*****************************************************************************/
VOS_VOID diag_PktTimeoutClear(VOS_VOID)
{
    LIST_S* me = NULL;
    DIAG_SRVC_FRAME_NODE_STRU *pTempNode;
    VOS_UINT32 ulCurSlice = mdrv_timer_get_normal_timestamp();

    (VOS_VOID)VOS_SmP(g_stDiagSrvc.ListSem,0);

    blist_for_each(me, &g_stDiagSrvc.ListHeader)
    {
        pTempNode = blist_entry(me, DIAG_SRVC_FRAME_NODE_STRU, FrameList);

        if((ulCurSlice > pTempNode->ulSlice) && ((ulCurSlice - pTempNode->ulSlice) > DIAG_PKT_TIMEOUT_LEN))
        {
            /*删除节点*/
            if((VOS_NULL_PTR != pTempNode->FrameList.next) && (VOS_NULL_PTR != pTempNode->FrameList.prev))
            {
                blist_del(&pTempNode->FrameList);
            }

            printk(KERN_ERR"[debug] diag_PktTimeoutClear delete node.\n");

            if(VOS_NULL_PTR != pTempNode->pFrame)
            {
                VOS_MemFree(MSP_PID_DIAG_APP_AGENT, pTempNode->pFrame);
            }

            VOS_MemFree(MSP_PID_DIAG_APP_AGENT, pTempNode);
        }
    }
    (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);
}


/*****************************************************************************
 Function Name   : diag_SrvcCreatePkt
 Description     : 收到第一个分包时创建缓存和节点

 History         :
    1.c64416         2015-03-18  Draft Enact

*****************************************************************************/
/*lint -save -e429*/
VOS_VOID diag_SrvcCreatePkt(DIAG_FRAME_INFO_STRU *pFrame)
{
    LIST_S* me = NULL;
    DIAG_SRVC_FRAME_NODE_STRU *pTempNode;

    /* 如果链表中已经有相同transid的节点则直接退出 */
    (VOS_VOID)VOS_SmP(g_stDiagSrvc.ListSem,0);

    blist_for_each(me, &g_stDiagSrvc.ListHeader)
    {
        pTempNode = blist_entry(me, DIAG_SRVC_FRAME_NODE_STRU, FrameList);

        pTempNode->stHead.index4b = pFrame->stService.index4b;
        pTempNode->stHead.eof1b   = pFrame->stService.eof1b;
        pTempNode->stHead.ff1b    = pFrame->stService.ff1b;
        if(VOS_MemCmp(&pTempNode->stHead, &pFrame->stService, sizeof(DIAG_SERVICE_HEAD_STRU)))
        {
            (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);

            diag_printf("%s : there is a uniform packet in list.\n", __FUNCTION__);
            return ;
        }
    }
    (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);

    /* 创建节点，申请内存 */
    pTempNode = (DIAG_SRVC_FRAME_NODE_STRU *)VOS_MemAlloc(MSP_PID_DIAG_APP_AGENT, DYNAMIC_MEM_PT,
                                                            sizeof(DIAG_SRVC_FRAME_NODE_STRU));
    if(VOS_NULL_PTR == pTempNode)
    {
        return ;
    }

    pTempNode->pFrame = (DIAG_FRAME_INFO_STRU*)VOS_MemAlloc(MSP_PID_DIAG_APP_AGENT, DYNAMIC_MEM_PT,
                                                            pFrame->ulMsgLen + sizeof(DIAG_FRAME_INFO_STRU));
    if(VOS_NULL_PTR == pTempNode->pFrame)
    {
        VOS_MemFree(MSP_PID_DIAG_APP_AGENT, pTempNode);
        return ;
    }

    (VOS_VOID)VOS_SmP(g_stDiagSrvc.ListSem,0);

    VOS_MemCpy_s(&pTempNode->stHead, (VOS_UINT32)sizeof(DIAG_SERVICE_HEAD_STRU), &pFrame->stService, sizeof(DIAG_SERVICE_HEAD_STRU));

    blist_add_tail(&pTempNode->FrameList, &g_stDiagSrvc.ListHeader);

    pTempNode->ulSlice = mdrv_timer_get_normal_timestamp();

    (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);

    return ; 

}
/*lint -restore*/

/*****************************************************************************
 Function Name   : diag_SrvcSavePkt
 Description     : 收到分包时把分包内容拷贝到缓存中

 History         :
    1.c64416         2015-03-18  Draft Enact

*****************************************************************************/
DIAG_FRAME_INFO_STRU * diag_SrvcSavePkt(DIAG_FRAME_INFO_STRU *pFrame)
{
    LIST_S* me = NULL;
    DIAG_SRVC_FRAME_NODE_STRU *pTempNode;
    VOS_UINT32 ulLen = 0;
    VOS_UINT32 uloffset = 0;

    (VOS_VOID)VOS_SmP(g_stDiagSrvc.ListSem,0);

    blist_for_each(me, &g_stDiagSrvc.ListHeader)
    {
        pTempNode = blist_entry(me, DIAG_SRVC_FRAME_NODE_STRU, FrameList);

        if(VOS_NULL_PTR == pTempNode)
        {
            /* coverity[dead_error_begin] */
            (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);
            return VOS_NULL_PTR;
        }

        pTempNode->stHead.index4b = pFrame->stService.index4b;
        pTempNode->stHead.eof1b   = pFrame->stService.eof1b;
        pTempNode->stHead.ff1b    = pFrame->stService.ff1b;
        if(0 == VOS_MemCmp(&pTempNode->stHead, &pFrame->stService, sizeof(DIAG_SERVICE_HEAD_STRU)))
        {
            if(0 == pFrame->stService.index4b)  /* 第0帧 */
            {
                /* 第0帧需要拷贝header, cmdid, meglen and data */
                (VOS_VOID)VOS_MemCpy_s(pTempNode->pFrame, DIAG_FRAME_MAX_LEN, pFrame, DIAG_FRAME_MAX_LEN);

                diag_SaveDFR(&g_stDFRreq, (VOS_UINT8*)pFrame, DIAG_FRAME_MAX_LEN);
            }
            else if(pFrame->stService.eof1b)  /* 最后1帧 */
            {
                /* 除最后一帧外，已存储的数据长度 */
                ulLen = (pFrame->stService.index4b * (DIAG_FRAME_MAX_LEN - sizeof(DIAG_SERVICE_HEAD_STRU)))
                        - sizeof(pFrame->ulCmdId) - sizeof(pFrame->ulMsgLen);

                if(     (VOS_NULL_PTR == pTempNode->pFrame)
                    ||  (pTempNode->pFrame->ulMsgLen > DIAG_FRAME_SUM_LEN)
                    ||  (pTempNode->pFrame->ulMsgLen < ulLen))
                {
                    (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);
                    return VOS_NULL_PTR;
                }

                /* 未缓存的数据长度 */
                ulLen = pTempNode->pFrame->ulMsgLen - ulLen;

                /* 当前缓存区的偏移 */
                uloffset = sizeof(DIAG_SERVICE_HEAD_STRU) + (pFrame->stService.index4b * (DIAG_FRAME_MAX_LEN - sizeof(DIAG_SERVICE_HEAD_STRU)));

                /* 最后一帧只需要拷贝剩余data */
                (VOS_VOID)VOS_MemCpy_s( ((VOS_UINT8*)pTempNode->pFrame) + uloffset,ulLen,
                            ((VOS_UINT8*)pFrame) + sizeof(DIAG_SERVICE_HEAD_STRU),
                            ulLen);

                diag_SaveDFR(&g_stDFRreq, (VOS_UINT8*)pFrame, (ulLen + sizeof(DIAG_SERVICE_HEAD_STRU)));
            }
            else
            {
                /* 当前缓存区的偏移 */
                uloffset = sizeof(DIAG_SERVICE_HEAD_STRU) + (pFrame->stService.index4b * (DIAG_FRAME_MAX_LEN - sizeof(DIAG_SERVICE_HEAD_STRU)));

                if(     (VOS_NULL_PTR == pTempNode->pFrame)
                    ||  (pTempNode->pFrame->ulMsgLen > DIAG_FRAME_SUM_LEN)
                    ||  (pTempNode->pFrame->ulMsgLen < (uloffset \
                                                        - sizeof(DIAG_SERVICE_HEAD_STRU) \
                                                        - sizeof(DIAG_MSG_INFO_STRU) \
                                                        + DIAG_FRAME_MAX_LEN - sizeof(DIAG_SERVICE_HEAD_STRU))))
                {
                    (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);
                    return VOS_NULL_PTR;
                }

                /* 中间的帧不拷贝cmdid和长度，只需要拷贝data */
                (VOS_VOID)VOS_MemCpy_s( ((VOS_UINT8*)pTempNode->pFrame) + uloffset,
                            (VOS_UINT32)(DIAG_FRAME_MAX_LEN - sizeof(DIAG_SERVICE_HEAD_STRU)),
                            ((VOS_UINT8*)pFrame) + sizeof(DIAG_SERVICE_HEAD_STRU),
                            DIAG_FRAME_MAX_LEN - sizeof(DIAG_SERVICE_HEAD_STRU));

                diag_SaveDFR(&g_stDFRreq, (VOS_UINT8*)pFrame, DIAG_FRAME_MAX_LEN);
            }

            (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);
            return pTempNode->pFrame;
        }
    }
    (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);

    return (DIAG_FRAME_INFO_STRU *)VOS_NULL_PTR;
}


/*****************************************************************************
 Function Name   : diag_SrvcDestroyPkt
 Description     : 删除缓存和节点

 History         :
    1.c64416         2015-03-18  Draft Enact

*****************************************************************************/
VOS_VOID diag_SrvcDestroyPkt(DIAG_FRAME_INFO_STRU *pFrame)
{
    LIST_S* me = NULL;
    DIAG_SRVC_FRAME_NODE_STRU *pTempNode;

    (VOS_VOID)VOS_SmP(g_stDiagSrvc.ListSem,0);

    blist_for_each(me, &g_stDiagSrvc.ListHeader)
    {
        pTempNode = blist_entry(me, DIAG_SRVC_FRAME_NODE_STRU, FrameList);

        if(VOS_NULL_PTR == pTempNode)
        {
            (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);
            return ;
        }

        pTempNode->stHead.index4b = pFrame->stService.index4b;
        pTempNode->stHead.eof1b   = pFrame->stService.eof1b;
        pTempNode->stHead.ff1b    = pFrame->stService.ff1b;
        if(0 == VOS_MemCmp(&pTempNode->stHead, &pFrame->stService, sizeof(DIAG_SERVICE_HEAD_STRU)))
        {
            /*删除节点*/
            if((VOS_NULL_PTR != pTempNode->FrameList.next) && (VOS_NULL_PTR != pTempNode->FrameList.prev))
            {
                blist_del(&pTempNode->FrameList);
            }

            if(VOS_NULL_PTR != pTempNode->pFrame)
            {
                VOS_MemFree(MSP_PID_DIAG_APP_AGENT, pTempNode->pFrame);
            }

            VOS_MemFree(MSP_PID_DIAG_APP_AGENT, pTempNode);

            break;
        }
    }

    (VOS_VOID)VOS_SmV(g_stDiagSrvc.ListSem);

    return ;
}

/*****************************************************************************
 Function Name   : diag_ServiceProc
 Description     : DIAG service 处理函数

 History         :
    1.c64416         2014-11-18  Draft Enact

*****************************************************************************/
VOS_UINT32 diag_ServiceProc(MSP_SERVICE_HEAD_STRU *pData)
{
    VOS_UINT32 ulRet = VOS_ERR;
    DIAG_FRAME_INFO_STRU *pHeader;
    DIAG_FRAME_INFO_STRU *pProcHead = VOS_NULL_PTR;

    if(VOS_NULL_PTR == pData)
    {
        return VOS_ERR;
    }

    pHeader = (DIAG_FRAME_INFO_STRU*)pData;

    mdrv_diag_PTR(EN_DIAG_PTR_SERVICE_IN, 1, pHeader->ulCmdId, 0);

    /* 只处理DIAG服务 */
    if(MSP_SID_DIAG_SERVICE == pData->sid8b)
    {
        mdrv_diag_PTR(EN_DIAG_PTR_SERVICE_1, 1, pHeader->ulCmdId, 0 );

        /* 开始处理，不允许睡眠 */
        wake_lock(&diag_wakelock);
        if(pHeader->stService.ff1b)
        {
            /* 每次有分包时检测是否有超时的节点 */
            diag_PktTimeoutClear();

            if(0 == pHeader->stService.index4b)
            {
                diag_SrvcCreatePkt(pHeader);
                (VOS_VOID)diag_SrvcSavePkt(pHeader);
                wake_unlock(&diag_wakelock);
                return VOS_OK;
            }
            else if(pHeader->stService.eof1b)
            {
                pProcHead = diag_SrvcSavePkt(pHeader);
            }
            else
            {
                (VOS_VOID)diag_SrvcSavePkt(pHeader);
                wake_unlock(&diag_wakelock);
                return VOS_OK;
            }
        }
        else
        {
            pProcHead = pHeader;

            diag_SaveDFR(&g_stDFRreq, (VOS_UINT8*)pHeader, (sizeof(DIAG_FRAME_INFO_STRU) + pHeader->ulMsgLen));
        }

        if(g_fnService && pProcHead)
        {
            /* 记录最近的N条cmdid */
            diag_LNR(EN_DIAG_LNR_CMDID, pHeader->ulCmdId, VOS_GetTick());
            diag_DumpDFInfo(pProcHead);
            ulRet = g_fnService(pProcHead);
        }
        else
        {
            ulRet = ERR_MSP_NO_INITILIZATION;
            mdrv_diag_PTR(EN_DIAG_PTR_DIAG_SERVICE_ERR1, 1, pHeader->ulCmdId, 0);
        }

        if(pHeader->stService.ff1b)
        {
            diag_SrvcDestroyPkt(pHeader);
        }

        /* 处理结束，允许睡眠 */
        wake_unlock(&diag_wakelock);
    }
    else
    {
        mdrv_diag_PTR(EN_DIAG_PTR_DIAG_SERVICE_ERR2, 1, pHeader->ulCmdId, 0);
        ulRet = ERR_MSP_INVALID_PARAMETER;
    }

    return ulRet;
}


/*****************************************************************************
 Function Name   : diag_ServiceProcReg
 Description     : DIAG service服务注册接口

 History         :
    1.c64416         2014-11-18  Draft Enact

*****************************************************************************/
VOS_VOID diag_ServiceProcReg(DIAG_SERVICE_FUNC pServiceFn)
{
    g_fnService = pServiceFn;
}

VOS_VOID diag_ServiceInit(VOS_VOID)
{
    VOS_UINT32 ret;

    /* 创建节点保护信号量*/
    ret = VOS_SmBCreate("SRVC", 1, VOS_SEMA4_FIFO, &g_stDiagSrvc.ListSem);
    if(VOS_OK != ret)
    {
        diag_printf("diag_ServiceInit VOS_SmBCreate failed.\n");
    }

    /* 初始化请求链表 */
    blist_head_init(&g_stDiagSrvc.ListHeader);

    msp_ServiceProcReg(MSP_SID_DIAG_SERVICE, diag_ServiceProc);

    /* coverity[alloc_arg] */
    ret = diag_CreateDFR("DFRReqA", DIAG_DFR_BUFFER_MAX, &g_stDFRreq);
    if(ret)
    {
        diag_printf("%s : diag_CreateDFR DFR_Req failed.\n", __FUNCTION__);
    }

    /* coverity[alloc_arg] */
    ret = diag_CreateDFR("DFRCnfA", DIAG_DFR_BUFFER_MAX, &g_stDFRcnf);

    if(ret)
    {
        diag_printf("%s : diag_CreateDFR DFR_Cnf failed.\n", __FUNCTION__);
    }
}


/*****************************************************************************
 Function Name   : diag_SrvcPackFirst
 Description     : 不分包时的封装，或分包时，第一包的封装

 History         :
    1.c64416         2015-03-12  Draft Enact

*****************************************************************************/
VOS_UINT32 diag_SrvcPackFirst(DIAG_MSG_REPORT_HEAD_STRU *pData, VOS_UINT8 *puctime)
{
    DIAG_FRAME_INFO_STRU stFrame;
    VOS_UINT32 ulTmpLen =0;
    VOS_UINT32 ret = ERR_MSP_FAILURE;
    VOS_UINT32 ulSplit = 0;
    VOS_ULONG  ulLockLevel;
    SCM_CODER_SRC_PACKET_HEADER_STRU* pstCoderSrc;
    SOCP_BUFFER_RW_STRU stSocpBuf = {VOS_NULL, VOS_NULL, 0, 0};
    SCM_CODER_SRC_MEMCPY_STRU stCpy;

    ulTmpLen = (sizeof(DIAG_FRAME_INFO_STRU) + pData->ulHeaderSize + pData->ulDataSize);

    if(ulTmpLen > DIAG_FRAME_MAX_LEN)
    {
        ulTmpLen = DIAG_FRAME_MAX_LEN;
        ulSplit = 1;
    }
    else
    {
        ulSplit = 0;
    }

    stFrame.stService.sid8b       = MSP_SID_DIAG_SERVICE;
    stFrame.stService.ssid4b      = pData->ulSsid;
    stFrame.stService.mdmid3b     = pData->ulModemId;
    stFrame.stService.rsv1b       = 0;
    stFrame.stService.sessionid8b = MSP_SERVICE_SESSION_ID;

    stFrame.stService.ff1b        = ulSplit;
    stFrame.stService.eof1b       = 0;
    stFrame.stService.index4b     = 0;

    stFrame.stService.mt2b        = pData->ulDirection;
    stFrame.stService.ulMsgTransId= pData->ulMsgTransId;
    stFrame.ulCmdId               = pData->u.ulID;
    stFrame.ulMsgLen              = pData->ulDataSize + pData->ulHeaderSize;

    (VOS_VOID)VOS_MemCpy_s(stFrame.stService.aucTimeStamp, (VOS_UINT32)sizeof(stFrame.stService.aucTimeStamp), 
                            puctime, sizeof(stFrame.stService.aucTimeStamp));
    if(SCM_CODER_SRC_LOM_IND == pData->ulChanId)
    {
        VOS_SpinLockIntLock(&g_stScmIndSrcBuffSpinLock, ulLockLevel);
        ret = mdrv_scm_get_ind_src_buff(ulTmpLen, &pstCoderSrc, &stSocpBuf);
        if(ret != ERR_MSP_SUCCESS)
        {
            VOS_SpinUnlockIntUnlock(&g_stScmIndSrcBuffSpinLock, ulLockLevel);
            diag_debug_ind_src_lost(EN_DIAG_SRC_LOST_ALLOC, ulTmpLen);
            return ERR_MSP_FAILURE;
        }
        stCpy.pHeader   = pstCoderSrc;
        stCpy.pSrc      = &stFrame;
        stCpy.uloffset  = SCM_HISI_HEADER_LENGTH;
        stCpy.ulLen     = sizeof(stFrame);
        mdrv_scm_ind_src_buff_mempy(&stCpy, &stSocpBuf);

        if(pData->ulHeaderSize)
        {
            stCpy.pHeader   = pstCoderSrc;
            stCpy.pSrc      = pData->pHeaderData;
            stCpy.uloffset  = SCM_HISI_HEADER_LENGTH + sizeof(stFrame);
            stCpy.ulLen     = pData->ulHeaderSize;
            mdrv_scm_ind_src_buff_mempy(&stCpy, &stSocpBuf);
        }

        stCpy.pHeader   = pstCoderSrc;
        stCpy.pSrc      = pData->pData;
        stCpy.uloffset  = SCM_HISI_HEADER_LENGTH + sizeof(stFrame) + pData->ulHeaderSize;
        stCpy.ulLen     = (ulTmpLen - sizeof(DIAG_FRAME_INFO_STRU) - pData->ulHeaderSize);
        mdrv_scm_ind_src_buff_mempy(&stCpy, &stSocpBuf);

        ret = mdrv_scm_send_ind_src_data((VOS_UINT8*)pstCoderSrc, ulTmpLen);

        VOS_SpinUnlockIntUnlock(&g_stScmIndSrcBuffSpinLock, ulLockLevel);
        if(ret)
        {
            diag_debug_ind_src_lost(EN_DIAG_SRC_LOST_ALLOC, ulTmpLen);

            DIAG_DEBUG_SDM_FUN(EN_DIAG_CBT_API_PACKET_ERR_REQ, ret, __LINE__, 2);
        }
    }
    else if(SCM_CODER_SRC_LOM_CNF == pData->ulChanId)
    {
        VOS_SpinLockIntLock(&g_stScmCnfSrcBuffSpinLock, ulLockLevel);
        ret = mdrv_scm_get_cnf_src_buff(ulTmpLen, &pstCoderSrc, &stSocpBuf);
        if(ret != ERR_MSP_SUCCESS)
        {
            VOS_SpinUnlockIntUnlock(&g_stScmCnfSrcBuffSpinLock, ulLockLevel);
            mdrv_diag_PTR(EN_DIAG_PTR_DIAG_SERVICE_ERR3, 1, stFrame.ulCmdId, 0);
            return ERR_MSP_FAILURE;
        }
        stCpy.pHeader   = pstCoderSrc;
        stCpy.pSrc      = &stFrame;
        stCpy.uloffset  = SCM_HISI_HEADER_LENGTH;
        stCpy.ulLen     = sizeof(stFrame);
        mdrv_scm_cnf_src_buff_mempy(&stCpy, &stSocpBuf);

        if(pData->ulHeaderSize)
        {
            stCpy.pHeader   = pstCoderSrc;
            stCpy.pSrc      = pData->pHeaderData;
            stCpy.uloffset  = SCM_HISI_HEADER_LENGTH + sizeof(stFrame);
            stCpy.ulLen     = pData->ulHeaderSize;
            mdrv_scm_cnf_src_buff_mempy(&stCpy, &stSocpBuf);
        }

        stCpy.pHeader   = pstCoderSrc;
        stCpy.pSrc      = pData->pData;
        stCpy.uloffset  = SCM_HISI_HEADER_LENGTH + sizeof(stFrame) + pData->ulHeaderSize;
        stCpy.ulLen     = (ulTmpLen - sizeof(DIAG_FRAME_INFO_STRU) - pData->ulHeaderSize);
        mdrv_scm_cnf_src_buff_mempy(&stCpy, &stSocpBuf);

        ret = mdrv_scm_send_cnf_src_data((VOS_UINT8*)pstCoderSrc,ulTmpLen);

        VOS_SpinUnlockIntUnlock(&g_stScmCnfSrcBuffSpinLock, ulLockLevel);
        if(ret)
        {
            DIAG_DEBUG_SDM_FUN(EN_DIAG_CBT_API_PACKET_ERR_REQ, ret, __LINE__, 4);
        }
    }
    else
    {
        diag_printf("channeld id is error,  channel id = 0x%x\n", pData->ulChanId);
    }

    return ERR_MSP_SUCCESS;
}


/*****************************************************************************
 Function Name   : diag_SrvcPackOther
 Description     : 分包时，除第一包外其他包的封装

 History         :
    1.c64416         2015-03-12  Draft Enact

*****************************************************************************/
VOS_UINT32 diag_SrvcPackOther(DIAG_PACKET_INFO_STRU *pPacket, DIAG_MSG_REPORT_HEAD_STRU *pstMsg)
{
    DIAG_FRAME_INFO_STRU stFrame;
    VOS_UINT32 ulTmpLen =0;
    VOS_UINT32 ret = ERR_MSP_FAILURE;
    VOS_ULONG  ulLockLevel;
    SCM_CODER_SRC_PACKET_HEADER_STRU * pstCoderSrc;
    SOCP_BUFFER_RW_STRU stSocpBuf = {VOS_NULL, VOS_NULL, 0, 0};
    SCM_CODER_SRC_MEMCPY_STRU stCpy;

    ulTmpLen = pPacket->ulLen + sizeof(DIAG_SERVICE_HEAD_STRU);

    if(ulTmpLen > DIAG_FRAME_MAX_LEN)
    {
        return ERR_MSP_FAILURE;
    }

    stFrame.stService.sid8b       = MSP_SID_DIAG_SERVICE;
    stFrame.stService.ssid4b      = pstMsg->ulSsid;
    stFrame.stService.mdmid3b     = pstMsg->ulModemId;
    stFrame.stService.rsv1b       = 0;
    stFrame.stService.sessionid8b = MSP_SERVICE_SESSION_ID;

    stFrame.stService.ff1b        = 1;
    stFrame.stService.eof1b       = pPacket->ulEnd;
    stFrame.stService.index4b     = pPacket->ulIndex;

    stFrame.stService.mt2b        = pstMsg->ulDirection;
    stFrame.stService.ulMsgTransId= pstMsg->ulMsgTransId;

    (VOS_VOID)VOS_MemCpy_s(stFrame.stService.aucTimeStamp, (VOS_UINT32)sizeof(stFrame.stService.aucTimeStamp), 
                           pPacket->auctime, sizeof(stFrame.stService.aucTimeStamp));

    if(SCM_CODER_SRC_LOM_IND == pstMsg->ulChanId)
    {
        VOS_SpinLockIntLock(&g_stScmIndSrcBuffSpinLock, ulLockLevel);
        ret = mdrv_scm_get_ind_src_buff(ulTmpLen, &pstCoderSrc, &stSocpBuf);
        if(ERR_MSP_SUCCESS != ret)
        {
            VOS_SpinUnlockIntUnlock(&g_stScmIndSrcBuffSpinLock, ulLockLevel);
            diag_debug_ind_src_lost(EN_DIAG_SRC_LOST_ALLOC, ulTmpLen);

            return ERR_MSP_FAILURE;
        }

        /* 只拷贝service头 */
        stCpy.pHeader   = pstCoderSrc;
        stCpy.pSrc      = &stFrame.stService;
        stCpy.uloffset  = SCM_HISI_HEADER_LENGTH;
        stCpy.ulLen     = sizeof(stFrame.stService);
        mdrv_scm_ind_src_buff_mempy(&stCpy, &stSocpBuf);

        /* 非第一个分包，从service头往后都是数据，不再有cmdid */
        stCpy.pSrc      = pPacket->pData;
        stCpy.uloffset  = SCM_HISI_HEADER_LENGTH + sizeof(stFrame.stService);
        stCpy.ulLen     = pPacket->ulLen;
        mdrv_scm_ind_src_buff_mempy(&stCpy, &stSocpBuf);

        ret = mdrv_scm_send_ind_src_data((VOS_UINT8*)pstCoderSrc,ulTmpLen);

        VOS_SpinUnlockIntUnlock(&g_stScmIndSrcBuffSpinLock, ulLockLevel);
        if(ret)
        {
            diag_debug_ind_src_lost(EN_DIAG_SRC_LOST_SEND, ulTmpLen);

            DIAG_DEBUG_SDM_FUN(EN_DIAG_CBT_API_PACKET_ERR_REQ, ret, __LINE__, 2);
        }
    }
    else if(SCM_CODER_SRC_LOM_CNF == pstMsg->ulChanId)
    {
        VOS_SpinLockIntLock(&g_stScmCnfSrcBuffSpinLock, ulLockLevel);
        ret = mdrv_scm_get_cnf_src_buff(ulTmpLen, &pstCoderSrc, &stSocpBuf);
        if(ERR_MSP_SUCCESS != ret)
        {
            VOS_SpinUnlockIntUnlock(&g_stScmCnfSrcBuffSpinLock, ulLockLevel);
            mdrv_diag_PTR(EN_DIAG_PTR_DIAG_SERVICE_ERR3, 1, stFrame.ulCmdId, 0);
            return ERR_MSP_FAILURE;
        }

        /* 只拷贝service头 */
        stCpy.pHeader   = pstCoderSrc;
        stCpy.pSrc      = &stFrame.stService;
        stCpy.uloffset  = SCM_HISI_HEADER_LENGTH;
        stCpy.ulLen     = sizeof(stFrame.stService);
        mdrv_scm_cnf_src_buff_mempy(&stCpy, &stSocpBuf);

        /* 非第一个分包，从service头往后都是数据，不再有cmdid */
        stCpy.pSrc      = pPacket->pData;
        stCpy.uloffset  = SCM_HISI_HEADER_LENGTH + sizeof(stFrame.stService);
        stCpy.ulLen     = pPacket->ulLen;
        mdrv_scm_cnf_src_buff_mempy(&stCpy, &stSocpBuf);

        ret = mdrv_scm_send_cnf_src_data((VOS_UINT8*)pstCoderSrc,ulTmpLen);

        VOS_SpinUnlockIntUnlock(&g_stScmCnfSrcBuffSpinLock, ulLockLevel);
        if(ret)
        {
            DIAG_DEBUG_SDM_FUN(EN_DIAG_CBT_API_PACKET_ERR_REQ, ret, __LINE__, 4);
        }

        diag_SaveDFR(&g_stDFRcnf, (VOS_UINT8*)&stFrame.stService, sizeof(stFrame.stService));
    }
    else
    {
        diag_printf("channeld id is error,  channel id = 0x%x\n", pstMsg->ulChanId);
    }
    return ERR_MSP_SUCCESS;
}


/*****************************************************************************
 Function Name   : diag_ServicePackData
 Description     : DIAG service层封包上报数据接口

 History         :
    1.c64416         2014-11-18  Draft Enact
    2.c64416         2015-03-14  新增分包组包处理
                    受帧结构限制，分包组包有如下约束:
                    A. 第一包有ulCmdId和ulMsgLen，其余包直接跟数据
                    B. 除最后一包外，其他每包都必须保证按最大长度填充
                    C. transid和timestamp作为区分一组分包的标志

*****************************************************************************/
VOS_UINT32 diag_ServicePackData(DIAG_MSG_REPORT_HEAD_STRU *pData)
{
    VOS_UINT32 ret = ERR_MSP_FAILURE;
    VOS_INT32  lDataLen =0;         /* 总长度 */
    VOS_UINT32 ulCurlen = 0;        /* 当前已封包的数据长度 */
    VOS_UINT32 ulEnd = 0;           /* 分包结束标记 */
    DIAG_PACKET_INFO_STRU stPacket = {0};

    if(SCM_CODER_SRC_LOM_CNF == pData->ulChanId)
    {
        mdrv_diag_PTR(EN_DIAG_PTR_SERVICE_PACKETDATA, 1, pData->u.ulID, 0);
    }

    /* 所要发送数据的总长度 */
    lDataLen = (VOS_INT32)(pData->ulHeaderSize + pData->ulDataSize + sizeof(DIAG_FRAME_INFO_STRU));

    if(lDataLen > (VOS_INT32)(DIAG_FRAME_SUM_LEN - 15*sizeof(DIAG_FRAME_INFO_STRU)))
    {
        diag_printf("[error] : diag report length is %d.\n", lDataLen);
        return ERR_MSP_FAILURE;
    }

    /* 统计数据通道的吞吐率 */
    if(SCM_CODER_SRC_LOM_IND == pData->ulChanId)
    {
        diag_ThroughputSave(EN_DIAG_THRPUT_DATA_CHN_ENC, \
            (sizeof(DIAG_FRAME_INFO_STRU) + pData->ulHeaderSize + pData->ulDataSize));
    }

    /* 分包时，各分包需要用同一个时间戳 */
    (VOS_VOID)mdrv_timer_get_accuracy_timestamp((VOS_UINT32*)&(stPacket.auctime[4]), (VOS_UINT32*)&(stPacket.auctime[0]));

    /* 由于分包时第一包中有cmdid需要填充，其他包没有，所以第一包单独处理 */
    ret = diag_SrvcPackFirst(pData, stPacket.auctime);
    if(ret)
    {
        return ERR_MSP_FAILURE;
    }

    /* 需要分包 */
    if(lDataLen > (VOS_INT32)DIAG_FRAME_MAX_LEN)
    {
        /* 剩余的没有发送的数据的长度 */
        lDataLen = lDataLen - (VOS_INT32)DIAG_FRAME_MAX_LEN;

        while(lDataLen > 0)
        {
            if((lDataLen + sizeof(DIAG_SERVICE_HEAD_STRU)) > DIAG_FRAME_MAX_LEN)
            {
                ulCurlen = DIAG_FRAME_MAX_LEN - sizeof(DIAG_SERVICE_HEAD_STRU);
            }
            else
            {
                ulCurlen = (VOS_UINT32)lDataLen;
                ulEnd = 1;    /* 记录分包结束标记 */
            }

            stPacket.ulIndex++;
            stPacket.ulEnd = ulEnd;
            stPacket.ulLen = ulCurlen;
            stPacket.pData = (VOS_UINT8*)(pData->pData) + (pData->ulDataSize - lDataLen);

            ret = diag_SrvcPackOther(&stPacket, pData);
            if(ret)
            {
                return ERR_MSP_FAILURE;
            }

            lDataLen -= (VOS_INT32)ulCurlen;
        }
    }

    return ERR_MSP_SUCCESS;
}



VOS_VOID DIAG_DebugDFR(VOS_VOID)
{
    diag_printf("%d, %s, %d.\n", g_stDFRreq.ulMagicNum, g_stDFRreq.name, g_stDFRreq.ulLen);
    diag_GetDFR(&g_stDFRreq);
    diag_printf("%d, %s, %d.\n", g_stDFRcnf.ulMagicNum, g_stDFRcnf.name, g_stDFRcnf.ulLen);

    diag_GetDFR(&g_stDFRcnf);
}




