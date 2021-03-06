/* Copyright(C) 2013, OpenOSEK by Fan Wang(parai). All rights reserved.
 *
 * This file is part of OpenOSEK.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Email: parai@foxmail.com
 * Sourrce Open At: https://github.com/parai/OpenOSEK/
 */
// TODO: No matter what, There is Serious Porblem in CanTp.
// So, I think I should better to learn more from the CanTp of arcore.


/* ================================ INCLUDEs  =============================== */
#include "Com.h"
/* ================================ MACROs    =============================== */
#define cfgCanTpMainTaskPeriod 10
#define msToCanTpTick(__ms) ((__ms+cfgCanTpMainTaskPeriod-1)/cfgCanTpMainTaskPeriod)
// Configuration Time(in ms)
// This CanTp is just for UDS only, and just has one Channel
#define N_As  1
#define N_Bs  200 // important
#define N_Cs  xx   // same with the period of CanTpMainTask
#define N_Ar  200
#define N_Br  200
#define N_Cr  200  // important
#define N_STmin 10
#define N_BS    8

//see ISO 15765-2 2004
#define N_PCI_MASK  0x30
#define N_PCI_SF    0x00
#define N_PCI_FF    0x10
#define N_PCI_CF    0x20
#define N_PCI_FC    0x30
#define N_PCI_SF_DL 0x07
//Flow Control Status Mask
#define N_PCI_FS    0x0F
//Flow Control Status
#define N_PCI_CTS   0x00
#define N_PCI_WT    0x01	// TODO: not supported for receiving
#define N_PCI_OVFLW 0x02

#define N_PCI_SN   0x0F

#define N_SF_MAX_LENGTH   7

#define tpSetAlarm(__tp,__tick)			\
do{										\
	cantpRte[__tp].timer = __tick + 1;	\
}while(0)
#define tpSignalAlarm(__tp)				\
do{										\
	if(cantpRte[__tp].timer > 1)		\
	{									\
		cantpRte[__tp].timer --;		\
	}									\
}while(0)
#define tpCancelAlarm(__tp)	{ cantpRte[__tp].timer = 0;}
#define tpIsAlarmTimeout(__tp) ( 1u == cantpRte[__tp].timer )
#define tpIsAlarmStarted(__tp) ( 0u != cantpRte[__tp].timer )
//
/* ================================ TYPEs     =============================== */
typedef struct
{
	uint8 data[8];
	uint8 length;
	PduIdType handle;
}CanTp_QItemType;

typedef struct
{
	CanTp_QItemType queue[N_BS];
	uint8 counter; // must < N_BS
}CanTp_QueueType;

typedef enum
{
	CanTp_stIdle = 0,
	/* ========== Receive ========== */
	CanTp_stWaitCF,
	CanTp_stWaitFC,
	/* ========== BUSY ============= */
	CanTp_stBusy,          // To say the rx buffer of handle is in used by UDS,locked
	/* ========== Send ============= */
	CanTp_stStartToSend,
	CanTp_stWaitToSendCF,
	CanTp_stWaitToSendFC,
}CanTp_StateType;
typedef struct
{
	CanTp_QueueType Q; 		// For Rx only
	PduLengthType index;  	// For Rx and Tx
	PduLengthType length; 	// For Rx and Tx
	TickType timer;
	uint8 BS;				//Block Size
	uint8 SN;				//Sequence Number
	uint8 STmin;
	volatile CanTp_StateType state;
}cantpRteType; // RTE

/* ================================ DATAs     =============================== */
IMPORT const Com_IPDUConfigType ComRxIPDUConfig[];
IMPORT const Com_IPDUConfigType ComTxIPDUConfig[];
LOCAL cantpRteType cantpRte[cfgCOM_TPIPDU_NUM];
/* ================================ FUNCTIONs =============================== */
LOCAL void canTpReceiveSF(PduIdType RxPduId,uint8* Data);
LOCAL void canTpReceiveFF(PduIdType RxPduId,uint8* Data);
LOCAL void canTpReceiveCF(PduIdType RxPduId,uint8* Data);
LOCAL void canTpReceiveFC(PduIdType RxPduId,uint8* Data);
LOCAL void CanTp_ReceivingMain(PduIdType RxPduId);
LOCAL void canTpSendSF(PduIdType RxPduId);
LOCAL void canTpSendFF(PduIdType TxPduId);
LOCAL void canTpSendFC(PduIdType RxPduId);
LOCAL void canTpSendCF(PduIdType TxPduId);
LOCAL void CanTp_StartToSendMain(PduIdType TxPduId);

EXPORT void CanTp_Init(void)
{
	memset(&cantpRte,0,sizeof(cantpRte));
}

EXPORT void CanTp_ReleaseRxBuffer(PduIdType RxPduId)
{
	if(CanTp_stBusy == cantpRte[RxPduId].state)
	{
		cantpRte[RxPduId].state = CanTp_stIdle;
	}
	else
	{
		devTrace(tlError,"Error In CanTp_ReleaseRxBuffer state[%d] = %d.\n",RxPduId,cantpRte[RxPduId].state);
	}
}

EXPORT void CanTp_TxConformation(PduIdType TxPduId)
{
	// OK, No need of it.
	// Treat each transmit is OK.
}
EXPORT void CanTp_RxIndication( PduIdType RxPduId, const PduInfoType *CanTpRxPduPtr )
{
	uint8 index = cantpRte[RxPduId].Q.counter;

	if( index < N_BS)
	{
		memcpy(cantpRte[RxPduId].Q.queue[index].data,CanTpRxPduPtr->SduDataPtr,CanTpRxPduPtr->SduLength);
		cantpRte[RxPduId].Q.queue[index].length = CanTpRxPduPtr->SduLength;
		cantpRte[RxPduId].Q.counter = index + 1;
	}
}

EXPORT Std_ReturnType CanTp_Transmit( PduIdType TxSduId, PduLengthType Length)
{
	Std_ReturnType ercd = E_OK;
	if(CanTp_stBusy == cantpRte[TxSduId].state)
	{
		cantpRte[TxSduId].state = CanTp_stStartToSend;
		cantpRte[TxSduId].length = Length;
	}
	else
	{
		ercd = E_NOT_OK;
	}
	return ercd;
}

LOCAL void canTpReceiveSF(PduIdType RxPduId,uint8* Data)
{
	if(cantpRte[RxPduId].state != CanTp_stIdle)
	{
		devTrace(tlError,"Error: CanTp[%d] Received SF when in state %d.\n",RxPduId,cantpRte[RxPduId].state);
	}
	else
	{
		uint8 length = Data[0]&N_PCI_SF_DL;
		memcpy(ComRxIPDUConfig[RxPduId].pdu.SduDataPtr,&(Data[1]),length);
		cantpRte[RxPduId].state = CanTp_stBusy;
		Uds_RxIndication(RxPduId,length);
	}
}

LOCAL void canTpSendFC(PduIdType RxPduId)
{
	Can_ReturnType ercd;
	Can_PduType pdu;
	uint8 data[8];
	pdu.id  = ComTxIPDUConfig[RxPduId].id;
	if(cantpRte[RxPduId].length < ComTxIPDUConfig[RxPduId].pdu.SduLength)
	{
		data[0] = N_PCI_FC|N_PCI_CTS;
	}
	data[1] = N_BS;
	data[2] = N_STmin;
	pdu.sdu = data;
	pdu.length = 3;
	pdu.swPduHandle = RxPduId;

	// Note, Can_Write will push the data to Transmit CAN message box, and return.
	// It always do a Can_TxConformation after the return from Can_Write,
	// otherwise, Your CAN controller speed is too fast.
	ercd = Can_Write(ComTxIPDUConfig[RxPduId].controller,&pdu);
	if(CAN_OK == ercd)
	{
		cantpRte[RxPduId].state = CanTp_stWaitCF;
		tpSetAlarm(RxPduId,msToCanTpTick(N_Ar));
		cantpRte[RxPduId].BS = N_BS;
		devTrace(tlCanTp,"CanTp[%d] FC sent.\n",RxPduId);
	}
	else
	{
		cantpRte[RxPduId].state = CanTp_stWaitToSendFC; // Re-Do it later
	}
}

LOCAL void canTpReceiveFF(PduIdType RxPduId,uint8* Data)
{
	PduLengthType length = Data[0]&0x0F;
	length += (length << 8) + Data[1];
	if(length < ComRxIPDUConfig[RxPduId].pdu.SduLength)
	{
		devTrace(tlCanTp,"CanTp[%d] FF Received.\n",RxPduId);
		cantpRte[RxPduId].length = length;
		memcpy(ComRxIPDUConfig[RxPduId].pdu.SduDataPtr,&(Data[2]),6);
		cantpRte[RxPduId].index = 6; // 6 bytes already received by FF
		canTpSendFC(RxPduId);
		cantpRte[RxPduId].SN = 1;
	}
	else
	{
		// Length out of range
		cantpRte[RxPduId].state = CanTp_stIdle;
		devTrace(tlError,"CanTp request out of range!\n");
	}
}

LOCAL void canTpReceiveCF(PduIdType RxPduId,uint8* Data)
{
	if(cantpRte[RxPduId].SN == (Data[0]&N_PCI_SN))
	{
		uint8 size;
		cantpRte[RxPduId].SN ++ ;
		if(cantpRte[RxPduId].SN > 15) { cantpRte[RxPduId].SN = 0; }
		size = cantpRte[RxPduId].length - cantpRte[RxPduId].index;
		if( size > 7 ) { size = 7; }
		memcpy(ComRxIPDUConfig[RxPduId].pdu.SduDataPtr + cantpRte[RxPduId].index,
				&(Data[1]),size);
		cantpRte[RxPduId].index += size;
		if(cantpRte[RxPduId].index >= cantpRte[RxPduId].length)
		{
			cantpRte[RxPduId].state = CanTp_stBusy;
			Uds_RxIndication(RxPduId,cantpRte[RxPduId].length);
		}
		else
		{
			cantpRte[RxPduId].BS --;
			if(0 == cantpRte[RxPduId].BS)
			{
				canTpSendFC(RxPduId);
			}
		}
	}
	else
	{	// Sequence Number Wrong
		cantpRte[RxPduId].state = CanTp_stIdle;
		devTrace(tlError,"ERROR: CanTp[%d] Sequence Number Wrong,Abort Current Receiving.",RxPduId);
	}
}

LOCAL void canTpReceiveFC(PduIdType RxPduId,uint8* Data)
{
	if(CanTp_stWaitFC == cantpRte[RxPduId].state) // Special process
	{
		if((Data[0]&N_PCI_FS) == N_PCI_CTS)
		{
			cantpRte[RxPduId].state = CanTp_stWaitToSendCF;
			if(0u != Data[1])
			{
				cantpRte[RxPduId].BS = Data[1] + 1;
			}
			else
			{
				cantpRte[RxPduId].BS = 0; //Send all the left without FC
			}
			cantpRte[RxPduId].STmin = Data[2];
			tpSetAlarm(RxPduId,msToCanTpTick(cantpRte[RxPduId].STmin));
		}
		else if((Data[0]&N_PCI_FS) == N_PCI_WT)
		{
			tpSetAlarm(RxPduId,msToCanTpTick(N_Br));
		}
		else if((Data[0]&N_PCI_FS) == N_PCI_OVFLW)
		{
			cantpRte[RxPduId].state =CanTp_stIdle; // Abort as Error.
			devTrace(tlError,"Error: The Client buffer overflow CanTp[%d].\n",RxPduId);
		}
	}

}
LOCAL void CanTp_ReceivingMain(PduIdType RxPduId)
{
	uint8 i;
	for(i=0;i<cantpRte[RxPduId].Q.counter;i++)
	{
#if(tlCanTp > cfgDEV_TRACE_LEVEL)
		{
			int j;
			printf("Received Q[%d] = [",i);
			for(j=0;j<cantpRte[RxPduId].Q.queue[i].length;j++)
			{
				printf("0x%-2X,",cantpRte[RxPduId].Q.queue[i].data[j]);
			}
			printf("]\n");
		}
#endif
		if( N_PCI_SF == (cantpRte[RxPduId].Q.queue[i].data[0]&N_PCI_MASK))
		{
			canTpReceiveSF(RxPduId,cantpRte[RxPduId].Q.queue[i].data);
		}
		else if(N_PCI_FF == (cantpRte[RxPduId].Q.queue[i].data[0]&N_PCI_MASK))
		{
			canTpReceiveFF(RxPduId,cantpRte[RxPduId].Q.queue[i].data);
		}
		else if(N_PCI_CF == (cantpRte[RxPduId].Q.queue[i].data[0]&N_PCI_MASK))
		{
			canTpReceiveCF(RxPduId,cantpRte[RxPduId].Q.queue[i].data);
		}
		else if(N_PCI_FC == (cantpRte[RxPduId].Q.queue[i].data[0]&N_PCI_MASK))
		{
			canTpReceiveFC(RxPduId,cantpRte[RxPduId].Q.queue[i].data);
		}
		else
		{
		}
	}

	cantpRte[RxPduId].Q.counter = 0;
}
LOCAL void canTpSendSF(PduIdType TxPduId)
{
	Can_ReturnType ercd;
	Can_PduType pdu;
	uint8 data[8];
	uint8 i;
	pdu.id  = ComTxIPDUConfig[TxPduId].id;
	data[0] = N_PCI_SF|cantpRte[TxPduId].length;
	for(i=0;i<cantpRte[TxPduId].length;i++)
	{
		data[1+i] = ComTxIPDUConfig[TxPduId].pdu.SduDataPtr[i];
	}
	pdu.sdu = data;
	pdu.length = cantpRte[TxPduId].length+1;
	pdu.swPduHandle = TxPduId;

	// Note, Can_Write will push the data to Transmit CAN message box, and return.
	// It always do a Can_TxConformation after the return from Can_Write,
	// otherwise, Your CAN controller speed is too fast.
	ercd = Can_Write(ComTxIPDUConfig[TxPduId].controller,&pdu);
	if(CAN_OK == ercd)
	{
		cantpRte[TxPduId].state = CanTp_stIdle;
		Uds_TxConformation(TxPduId,E_OK);
	}
	else
	{
		// Failed, Redo
	}
}
LOCAL void canTpSendFF(PduIdType TxPduId)
{
	Can_ReturnType ercd;
	Can_PduType pdu;
	uint8 data[8];
	uint8 i;
	pdu.id  = ComTxIPDUConfig[TxPduId].id;
	data[0] = N_PCI_FF|( (cantpRte[TxPduId].length>>8)&0x0F );
	data[1] = cantpRte[TxPduId].length&0xFF;
	for(i=0;i<6;i++)
	{
		data[2+i] = ComTxIPDUConfig[TxPduId].pdu.SduDataPtr[i];
	}
	pdu.sdu = data;
	pdu.length = 8;
	pdu.swPduHandle = TxPduId;

	// Note, Can_Write will push the data to Transmit CAN message box, and return.
	// It always do a Can_TxConformation after the return from Can_Write,
	// otherwise, Your CAN controller speed is too fast.
	ercd = Can_Write(ComTxIPDUConfig[TxPduId].controller,&pdu);
	if(CAN_OK == ercd)
	{
		cantpRte[TxPduId].index = 6;
		cantpRte[TxPduId].SN = 1;
		cantpRte[TxPduId].state = CanTp_stWaitFC;
		tpSetAlarm(TxPduId,msToCanTpTick(N_Br));
	}
	else
	{
		// Failed, Redo
	}
}
LOCAL void CanTp_StartToSendMain(PduIdType TxPduId)
{
	if(cantpRte[TxPduId].length <= N_SF_MAX_LENGTH)
	{
		canTpSendSF(TxPduId);
	}
	else
	{
		canTpSendFF(TxPduId);
	}
}

LOCAL void canTpSendCF(PduIdType TxPduId)
{
	Can_ReturnType ercd;
	Can_PduType pdu;
	uint8 data[8];
	uint8 i;
	uint8 size;
	size = cantpRte[TxPduId].length - cantpRte[TxPduId].index;
	if(size > 7) { size = 7;}
	pdu.id  = ComTxIPDUConfig[TxPduId].id;
	data[0] = N_PCI_CF|cantpRte[TxPduId].SN;
	for(i=0;i<size;i++)
	{
		data[1+i] = ComTxIPDUConfig[TxPduId].pdu.SduDataPtr[i+cantpRte[TxPduId].index];
	}
	pdu.sdu = data;
	pdu.length = size + 1;
	pdu.swPduHandle = TxPduId;

	// Note, Can_Write will push the data to Transmit CAN message box, and return.
	// It always do a Can_TxConformation after the return from Can_Write,
	// otherwise, Your CAN controller speed is too fast.
	ercd = Can_Write(ComTxIPDUConfig[TxPduId].controller,&pdu);
	if(CAN_OK == ercd)
	{
		cantpRte[TxPduId].index += size;
		cantpRte[TxPduId].SN ++;
		if(cantpRte[TxPduId].SN > 15) {cantpRte[TxPduId].SN = 0;}
		if(cantpRte[TxPduId].index >= cantpRte[TxPduId].length)
		{
			cantpRte[TxPduId].state = CanTp_stIdle;
			Uds_TxConformation(TxPduId,E_OK);
			devTrace(tlCanTp,"CanTp[%d] Segmented Message Transmission Done!\n",TxPduId);
		}
		else if(cantpRte[TxPduId].BS > 1)
		{
			cantpRte[TxPduId].BS --;
			if(1u == cantpRte[TxPduId].BS)
			{
				cantpRte[TxPduId].state = CanTp_stWaitFC;
				tpCancelAlarm(TxPduId);
			}
			else
			{
				tpSetAlarm(TxPduId,msToCanTpTick(cantpRte[TxPduId].STmin)); // Reset the Alarm for CF
			}
		}
		else // BS = 0
		{
			tpSetAlarm(TxPduId,msToCanTpTick(cantpRte[TxPduId].STmin)); // Reset the Alarm for CF
		}
	}
	else
	{
		// Failed, Redo
		devTrace(tlCanTp,"Re-Transmit CF as Error.\n");
	}
}

void CanTp_TaskMain(void)
{
	uint8 i;
	SuspendAllInterrupts();
	for(i=0;i<cfgCOM_TPIPDU_NUM;i++)
	{
		if(0u != cantpRte[i].Q.counter)
		{
			CanTp_ReceivingMain(i);
		}
		switch(cantpRte[i].state)
		{
			case CanTp_stWaitToSendFC:
			{
				canTpSendFC(i);
				break;
			}
			case CanTp_stStartToSend:
			{
				CanTp_StartToSendMain(i);
				break;
			}
			case CanTp_stWaitToSendCF:
			{
				if(tpIsAlarmStarted(i))
				{
					tpSignalAlarm(i);
					if(tpIsAlarmTimeout(i))
					{
						canTpSendCF(i);
					}
				}
				break;
			}
			case CanTp_stWaitCF:
			case CanTp_stWaitFC:
			{
				if(tpIsAlarmStarted(i))
				{
					tpSignalAlarm(i);
					if(tpIsAlarmTimeout(i))
					{
						devTrace(tlError,"Error: CanTp[%d] Timeout in the state %d.\n",(int)i,cantpRte[i].state);
						cantpRte[i].state = CanTp_stIdle;
						memset(&cantpRte[i],0,sizeof(cantpRteType));
						Uds_TxConformation(i,E_NOT_OK);
					}
				}
				break;
			}
			default:
				break;
		}
	}
	ResumeAllInterrupts();
}
TASK(TaskCanTpMain)
{
    CanTp_TaskMain();
	TerminateTask();
}


// -------------- For Debug Only
#if( tlCanTp > cfgDEV_TRACE_LEVEL )
void CanTp_Print(void)
{
	int i;
	for(i=0;i<cfgCOM_TPIPDU_NUM;i++)
	{
		printf("CanTp[%d] State is %d,",i,(int)cantpRte[i].state);
		printf("Q.couner = %d.\n",cantpRte[i].Q.counter);
	}
}
#endif
