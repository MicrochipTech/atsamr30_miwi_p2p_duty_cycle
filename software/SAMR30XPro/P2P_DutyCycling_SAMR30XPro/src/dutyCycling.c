/**
* \file  dutyCycling.c
*
* \brief Duty Cycling implementation for MiWi-P2P.
*
* Copyright (c) 2020 Microchip Technology Inc. and its subsidiaries.
*
* \asf_license_start
*
* \page License
*
* Subject to your compliance with these terms, you may use Microchip
* software and any derivatives exclusively with Microchip products.
* It is your responsibility to comply with third party license terms applicable
* to your use of third party software (including open source software) that
* may accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS".  NO WARRANTIES,
* WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE,
* INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY,
* AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT WILL MICROCHIP BE
* LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL OR CONSEQUENTIAL
* LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE
* SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF THE
* POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT
* ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN ANY WAY
* RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*
* \asf_license_stop
*
*/
/*
* Support and FAQ: visit <a href="https://www.microchip.com/support/">Microchip Support</a>
*/

#include "dutyCycling.h"
#include "sysTimer.h"

/* Refer	Table 14-20 : PPDU Timing of SAMR30 Datasheet  
			Table 37-2  : PPDU Timing of SAMR21 Datasheet 
			
   Note to change the corresponding configuration in PHY_Init() (in phy.c)
*/

/* PHY Synchronization header duration in us */
#define phySHRDurationMicroSec			2000

/* PHY header duration in us */
#define phyPHRDurationMicroSec			400

/* PHY time taken to transmit max PSDU in us */
#define phyMaxPSDUDurationMicroSec		(50.8 * MS)

/* PHY max PSDU length in bytes */
#define phyMaxPSDULength				127

/* Time taken to transmit one byte of PSDU in us */
#define phyPerPSDUTxDurationMicroSec	(phyMaxPSDUDurationMicroSec / phyMaxPSDULength)

/* Time taken to transmit 2 bytes of PHY footer (CRC) in us */
#define phyFooterTxDurationMicroSec		(2 * phyPerPSDUTxDurationMicroSec) /* 2-FCS */

/* Number of retries attempted when ACK is not received */
#define phyNumOfRetires					3

/* Number of bytes added by MAC for constructing P2P frame in bytes
   includes - FCF, seq.no., src & dst addresses... 
   refer 14.3.1.2 MAC Protocol Data Unit (MPDU) of SAMR30 datasheet
 */
#define p2pMacHeader					21

/* Duty cycling percentage - range : 1 to 99 */
#define dutyCyclePercentage				1

/* Macro to calculate the transmit duration of a P2P application frame based on payload length */
#define phyPacketTxDuration(packetLength) (phySHRDurationMicroSec + phyPHRDurationMicroSec + (phyPerPSDUTxDurationMicroSec * p2pMacHeader) + (packetLength * phyPerPSDUTxDurationMicroSec) + phyFooterTxDurationMicroSec)

/* Total size of Data request frame from sleeping device in bytes */
#define macDataRequestFrameSize			22

/* Duty cycling interval calculation include the transmission of data request in sleeping devices */
#define dataRequestDutyCyclingInterval	(((100 - dutyCyclePercentage) * phyPacketTxDuration(macDataRequestFrameSize)) / ONE_SECOND)

/* Call back to store and retrieve the application callback for each data */
DataConf_callback_t currentAppDataConfCb;

/* Stores the application payload length of each data for duty cycling calculation in confirmation callback */
uint8_t appDataPhyLen;

/* System timer ran to indicate and hold the active duty cycling period */
SYS_Timer_t dutyCyclingWaitTimer;

static void dutyCycledDataConfcb(uint8_t handle, miwi_status_t status, uint8_t* msgPointer);
static void dutyCyclingWaitTimerExpired(struct SYS_Timer_t *timer);
static uint32_t calculateDutyCyclingDurationMicroSec(uint8_t phyPayloadLen, miwi_status_t status);

/*********************************************************************
* Function: MiApp_DutyCyclingInit
*
* Overview: Initializes the duty cycling timer and assigns the handler
*
* Return:  None
********************************************************************/
void MiApp_DutyCyclingInit(void)
{
	/* configuring duty cycling timer */
	dutyCyclingWaitTimer.handler = dutyCyclingWaitTimerExpired;
	dutyCyclingWaitTimer.mode = SYS_TIMER_INTERVAL_MODE;
}

/*********************************************************************
* Function:  MiApp_SendDutyCycledData
*
* Overview: Initializes the duty cycling timer and assigns the handler
*
* Return:  uint32_t - time to wait for the duty cycling to complete 
					  before transmitting current data.
					  0 indicates successful transmission.
********************************************************************/
uint32_t MiApp_SendDutyCycledData(uint8_t addr_len, uint8_t *addr, uint8_t msglen, uint8_t *msgpointer,
	uint8_t msghandle, bool ackReq, DataConf_callback_t ConfCallback)
{
	/* If the duty cycling timer is running, it means that the device is in duty cycling mode */
	uint32_t remainingDutyCycleDuration = SYS_TimerRemainingTimeout(&dutyCyclingWaitTimer);
	if (remainingDutyCycleDuration)
	{
		/* return the remaining duty cycling duration to wait before sending next app data */
		return remainingDutyCycleDuration;
	}
	
#ifdef ENABLE_SLEEP_FEATURE
	uint16_t currDataReqInterval = MiApp_CurrentDataRequestIntervalSec();
	/* In sleeping device - when we detect that stack is about to send data request in less than or equal 1 sec,
	   we pro actively avoid sending the data for duty cycling duration calculated for data request frame 
	   Note: currDataReqInterval <= 2 is intentional as dataRequestInterval is decremented and compared to 0.
	   So to detect less than or equal 1 sec is to check if the value is less than 2. */
	if ((dataRequestDutyCyclingInterval > currDataReqInterval) || (currDataReqInterval <= 2))
	{
		return dataRequestDutyCyclingInterval;
	}
#endif
	/* if the data is allowed to be sent after duty cycling checks, the confirm pointer is store to reuse later */
	currentAppDataConfCb = ConfCallback;
	/* Send data through existing MiApp API and configure the callback to calculate duty cycling interval based on status */
	MiApp_SendData(addr_len, addr, msglen, msgpointer, msghandle, ackReq, dutyCycledDataConfcb);
	/* Length of app payload for immediate duty cycling calculation on the confirmation call back */
	appDataPhyLen = msglen;
	return 0;
}

/*********************************************************************
* Function:  dutyCycledDataConfcb
*
* Overview: Initializes the duty cycling timer and assigns the handler
*
* Return:  uint32_t - time to wait for the duty cycling to complete 
					  before transmitting current data.
					  0 indicates successful transmission.
********************************************************************/
static void dutyCycledDataConfcb(uint8_t handle, miwi_status_t status, uint8_t* msgPointer)
{
	/* Get the duty cycling duration using the payload length and status */
	uint32_t dutyCycleDurationMilliSec = calculateDutyCyclingDurationMicroSec(appDataPhyLen, status);
	/* Convert the microsecond to milliseconds as the timer runs on ms resolution 
	   If the value is less than 1000 microseconds, then we should perform duty cycling 
	   for minimum timer value i.e. 1 ms 
	   Note: In order to avoid frequent interrupts to timer services MiWi SysTimer's 
	   minimum resolution is 10ms (even if interval between 1-10), However if code is 
	   moved to OS based / 1ms Timer, logic is expected to work without any changes.
	 */
	dutyCycleDurationMilliSec = (dutyCycleDurationMilliSec / 1000) + 1;

	/* Configure the timer interval and start the duty cycling timer */
	dutyCyclingWaitTimer.interval = dutyCycleDurationMilliSec;
	SYS_TimerStart(&dutyCyclingWaitTimer);
	printf("\r\nSend next data after %ld ms", dutyCycleDurationMilliSec);
	
	/* Call the app data callback if its not null*/
	if (NULL != currentAppDataConfCb)
	{
		currentAppDataConfCb(handle, status, msgPointer);
	}
}

/*********************************************************************
* Function:  calculateDutyCyclingDurationMicroSec
*
* Overview: Calculates the duty cycling duration to avoid sending the next data
*           based on payload length and status of transmission.
*
* Return:  uint32_t - time in us to perform duty cycling to send next data.
********************************************************************/
static uint32_t calculateDutyCyclingDurationMicroSec(uint8_t phyPayloadLen, miwi_status_t status)
{
	uint32_t phyTxDurationMicroSec;
	uint32_t dutyCyclingDurationMicroSec;
	uint8_t txCount;
	/* Calculate the duration taken to transmit the payload length for once */
	phyTxDurationMicroSec = phyPacketTxDuration(phyPayloadLen);

	if (NO_ACK == status)
	{
		/* If the packet was not acknowledged, then the hardware would retry for phyNumOfRetires */
		txCount = phyNumOfRetires + 1;
	}
	else if (SUCCESS == status)
	{
		/* If the packet was acknowledged and success, frame was sent only once */
		txCount = 1;
	}
	else
	{
		/* If status is CHANNEL_ACCESS_FAILURE, FAILURE etc..., then there is zero transmission */
		txCount = 0;
	}
	/* Duty cycling calculation based on the duration and number of transmissions */
	dutyCyclingDurationMicroSec = ((100 - dutyCyclePercentage) * (phyTxDurationMicroSec * txCount));
	return dutyCyclingDurationMicroSec;
}


/*********************************************************************
* Function:  dutyCyclingWaitTimerExpired
*
* Overview: Callback when the timer ran for duty cycling expires.
*
* Return:  None
********************************************************************/
static void dutyCyclingWaitTimerExpired(struct SYS_Timer_t *timer)
{
	/* This can be used to indicate out of duty cycling mode, like switch on LED. */
}

