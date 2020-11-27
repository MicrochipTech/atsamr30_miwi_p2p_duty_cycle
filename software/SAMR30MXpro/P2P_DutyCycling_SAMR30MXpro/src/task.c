/**
* \file  task.c
*
* \brief Implementation of Tasks for Demo Application on MiWi P2P
*
* Copyright (c) 2018 - 2019 Microchip Technology Inc. and its subsidiaries.
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

/***********************Headers***************************************/
#include "task.h"
#include "miwi_api.h"
#include "p2p_demo.h"
#include "asf.h"

#ifdef DUTY_CYCLING
#include "dutyCycling.h"
#endif

#if defined(ENABLE_NETWORK_FREEZER)
#include "pdsDataServer.h"
#include "wlPdsTaskManager.h"
#endif

#if defined(ENABLE_SLEEP_FEATURE)
#include "sleep_mgr.h"
#endif
#include "phy.h"
/************************** VARIABLES ************************************/
#define LIGHT   0x01
#define SWITCH  0x02

#ifdef DUTY_CYCLING
/* Payload size for data sent out using Duty cycling */
#define PAYLOAD_SIZE 40

/* Payload for data sent out using Duty cycling */
#define PAYLOAD      "HelloWorldHelloWorldHelloWorldHelloWorld"

/* Interval in millisecond to send out data in Duty Cycling */
#define DUTY_CYCLED_DATA_SENDING_INTERVAL_MS  10000

/* Call back for App Data 1 confirmation */
static void dutyCyclingAppData1Confcb(uint8_t handle, miwi_status_t status, uint8_t* msgPointer);
/* Timer handler for sending App Data 1 */
static void dutyCyclingAppData1SendingTimerHandler(SYS_Timer_t *timer);

/* Call back for App Data 2 confirmation */
static void dutyCyclingAppData2Confcb(uint8_t handle, miwi_status_t status, uint8_t* msgPointer);
/* Timer handler for sending App Data 2 */
static void dutyCyclingAppData2SendingTimerHandler(SYS_Timer_t *timer);
/* Timer handler to handle if App Data 2 is supposed to be retried */
static void dutyCyclingAppData2RetryTimerHandler(SYS_Timer_t *timer);
#endif

/*************************************************************************/
// AdditionalNodeID variable array defines the additional
// information to identify a device on a PAN. This array
// will be transmitted when initiate the connection between
// the two devices. This  variable array will be stored in
// the Connection Entry structure of the partner device. The
// size of this array is ADDITIONAL_NODE_ID_SIZE, defined in
// miwi_config.h.
// In this demo, this variable array is set to be empty.
/*************************************************************************/
#if ADDITIONAL_NODE_ID_SIZE > 0
    uint8_t AdditionalNodeID[ADDITIONAL_NODE_ID_SIZE] = {LIGHT};
#endif

/* Connection Table Memory */
CONNECTION_ENTRY connectionTable[CONNECTION_SIZE];
#ifdef ENABLE_ACTIVE_SCAN
/* Active Scan Results Table Memory */
ACTIVE_SCAN_RESULT activeScanResults[ACTIVE_SCAN_RESULT_SIZE];
#endif

defaultParametersRomOrRam_t defaultParamsRomOrRam = {
    .ConnectionTable = &connectionTable[0],
#ifdef ENABLE_ACTIVE_SCAN
    .ActiveScanResults = &activeScanResults[0],
#endif
#if ADDITIONAL_NODE_ID_SIZE > 0
    .AdditionalNodeID = &AdditionalNodeID[0],
#endif
    .networkFreezerRestore = 0,
};

defaultParametersRamOnly_t defaultParamsRamOnly = {
    .dummy = 0,
};
extern API_UINT16_UNION  myPANID;

#ifdef DUTY_CYCLING
/* Periodic Timer for sending App Data 1 */
SYS_Timer_t dutyCyclingAppData1SendingTimer;
/* Delay Timer for sending App Data 2 and verify working of dutycycling demo */
SYS_Timer_t dutyCyclingAppData2DelayTimer;
#endif

/*************************************************************************/
// The variable myChannel defines the channel that the device
// is operate on. This variable will be only effective if energy scan
// (ENABLE_ED_SCAN) is not turned on. Once the energy scan is turned
// on, the operating channel will be one of the channels available with
// least amount of energy (or noise).
/*************************************************************************/
#if defined(PHY_AT86RF233)
uint8_t myChannel = 26;
/* Range: 11 to 26 */
#elif defined(PHY_AT86RF212B)
uint8_t myChannel = 8;
/* Range for default configuration: 1 to 10
 Note: TX Power and PHY Mode Setting needs to be modified as per the
 recommendation from Data Sheet for European band (ie.,Channel 0)*/
#endif

/*********************************************************************
* Function: bool freezer_feature(void)
*
* Overview: Allows user to select network freezer restore
*
* Return:  true if network freezer to be used for restoring
********************************************************************/
bool freezer_feature(void)
{
    MIWI_TICK tick1, tick2;
    uint8_t switch_val;
    tick1.Val = MiWi_TickGet();
    while(1)
    {
        tick2.Val = MiWi_TickGet();
        if(MiWi_TickGetDiff(tick2, tick1) > (ONE_SECOND * 4))
            break;
        switch_val = ButtonPressed ();
        if(switch_val == 1)
        {
#if defined (ENABLE_LCD)
            LCDDisplay((char *)"Restoring Network !!", 0, false);
            delay_ms(1000);
#endif
            return true;
        }
        else
        {
            return false;
        }

    }
    return false;
}

/*********************************************************************
* Function: static void longAddressValidationAndUpdation(void)
*
* Overview: validates the long address and assigns new address
            by random allocation if invalid address found
********************************************************************/
static void longAddressValidationAndUpdation(void)
{
    bool invalidIEEEAddrFlag = false;
    uint64_t invalidIEEEAddr;

    srand(PHY_RandomReq());

    /* Check if a valid IEEE address is available.
    0x0000000000000000 and 0xFFFFFFFFFFFFFFFF is persumed to be invalid */
    /* Check if IEEE address is 0x0000000000000000 */
    memset((uint8_t *)&invalidIEEEAddr, 0x00, LONG_ADDR_LEN);
    if (0 == memcmp((uint8_t *)&invalidIEEEAddr, (uint8_t *)&myLongAddress, LONG_ADDR_LEN))
    {
        invalidIEEEAddrFlag = true;
    }

    /* Check if IEEE address is 0xFFFFFFFFFFFFFFFF */
    memset((uint8_t *)&invalidIEEEAddr, 0xFF, LONG_ADDR_LEN);
    if (0 == memcmp((uint8_t *)&invalidIEEEAddr, (uint8_t *)&myLongAddress, LONG_ADDR_LEN))
    {
        invalidIEEEAddrFlag = true;
    }

    if (invalidIEEEAddrFlag)
    {
         /* In case no valid IEEE address is available, a random
          * IEEE address will be generated to be able to run the
          * applications for demonstration purposes.
          * In production code this can be omitted.
         */
        uint8_t* peui64 = (uint8_t *)&myLongAddress;
        for(uint8_t i = 0; i < MY_ADDRESS_LENGTH; i++)
        {
            *peui64++ = (uint8_t)rand();
        }
    }
    /* Set the address in transceiver */
    PHY_SetIEEEAddr((uint8_t *)&myLongAddress);
}

bool startNetwork = false;
/*********************************************************************
* Function: static void Connection_Confirm(miwi_status_t status)
*
* Overview: callback function called upon MiAPP_StarConnection
*           or MiApp_EstablishConnection procedure completes
* Parameter: status of the completed operation
********************************************************************/
static void Connection_Confirm(miwi_status_t status)
{
    /* If success or already exists status, update the LED,CONSOLE,LCD
       and show the demo instruction for the user to proceed */
    if ((SUCCESS == status) || (ALREADY_EXISTS == status))
    {

#if !defined(ENABLE_SLEEP_FEATURE)
        /* Turn on LED 1 to indicate connection established */
        LED_On(LED0);
#endif

        if (!startNetwork)
        {
            DemoOutput_Channel(myChannel, 1);
        }
        else
        {
            printf("\r\nStarted Wireless Communication on Channel ");
            printf("%u",currentChannel);
            printf("\r\n");
        }
        DemoOutput_Instruction();
#if defined(ENABLE_CONSOLE)
        DumpConnection(0xFF);
#endif
#ifdef DUTY_CYCLING
		/* Once the device connects to an existing node in the network, dutyCyclingAppData1SendingTimer is 
		   started in periodic mode to send dutycycled data to the connected peer */
        if (!startNetwork)
        {
			/* Configure and start the APP data1 timer */
			dutyCyclingAppData1SendingTimer.handler = dutyCyclingAppData1SendingTimerHandler;
			dutyCyclingAppData1SendingTimer.interval = DUTY_CYCLED_DATA_SENDING_INTERVAL_MS;
			dutyCyclingAppData1SendingTimer.mode = SYS_TIMER_PERIODIC_MODE;
			SYS_TimerStart(&dutyCyclingAppData1SendingTimer);

			/* First data initiated as the periodic data will be handled by above timer */
 			printf("\r\n--------------------\r\nApp Data 1 - ");
			uint32_t timeToSend = MiApp_SendDutyCycledData(LONG_ADDR_LEN, connectionTable[0].Address, PAYLOAD_SIZE, PAYLOAD, 1, true, dutyCyclingAppData1Confcb);
			if (timeToSend)
			{
				printf("should be sent after %ldms", timeToSend);
			}
			else
			{
				printf("%d bytes sent after Duty Cycling",PAYLOAD_SIZE);
			}
        }
#endif
	}
    else
    {
        /* Upon EstablishConnection failure, initiate the startConnection to form a network */
        startNetwork = true;
        MiApp_StartConnection(START_CONN_DIRECT, 10, (1L << myChannel), Connection_Confirm);
    }
}

/*********************************************************************
* Function: bool Initialize_Demo(bool freezer_enable)
*
* Overview: Initializes the demo by initializing protocol, required
            components and initiates connection
********************************************************************/
bool Initialize_Demo(bool freezer_enable)
{
    uint16_t broadcastAddr = 0xFFFF;
    /* Subscribe for data indication */
    MiApp_SubscribeDataIndicationCallback(ReceivedDataIndication);

#ifdef ENABLE_SLEEP_FEATURE
    /* Sleep manager initialization */
    sleepMgr_init();
#endif

    /* Update NetworkFreezerRestore parameter whether to restore from network freezer or not */
    defaultParamsRomOrRam.networkFreezerRestore = freezer_enable;

    /* Initialize the P2P and Star Protocol */
    if (MiApp_ProtocolInit(&defaultParamsRomOrRam, &defaultParamsRamOnly) == RECONNECTED)
    {
        printf("\r\nPANID:");
        printf("%x",myPANID.v[1]);
        printf("%x",myPANID.v[0]);
        printf(" Channel:");
        printf("%d",currentChannel);
#if defined(PROTOCOL_P2P)
        DemoOutput_Instruction();
#else
        STAR_DEMO_OPTIONS_MESSAGE (role);
#endif
        return true;
    }
    /* Unable to boot from the Network Freezer parameters, so initiate connection */
    /* Check Valid address is found , else update with random */
    longAddressValidationAndUpdation();

    /* Enable all kinds of connection */
    MiApp_ConnectionMode(ENABLE_ALL_CONN);

    // Set default channel
    if( MiApp_Set(CHANNEL, &myChannel) == false )
    {
        DemoOutput_ChannelError(myChannel);
        return false;
    }

    DemoOutput_Channel(myChannel, 0);

    startNetwork =  false;

    /* Try to establish a new connection with peer device by broadcast Connection Request */
    return MiApp_EstablishConnection(myChannel, 2, (uint8_t*)&broadcastAddr, 0, Connection_Confirm);
}

#ifdef DUTY_CYCLING
/*********************************************************************
* Function:  dutyCyclingAppData1SendingTimerHandler
*
* Overview: Handler for Sys timer used to send periodic data
*           in duty cycling mode - App Data 1
*
* Return:  None.
********************************************************************/
static void dutyCyclingAppData1SendingTimerHandler(SYS_Timer_t *timer)
{
	/* This function is called periodically as configured in DUTY_CYCLED_DATA_SENDING_INTERVAL_MS to send App Data 1 */
	printf("\r\n--------------------\r\nApp Data 1 - ");
	uint32_t timeToSend = MiApp_SendDutyCycledData(LONG_ADDR_LEN, connectionTable[0].Address, PAYLOAD_SIZE, PAYLOAD, 1, true, dutyCyclingAppData1Confcb);
	if (timeToSend)
	{
		printf("should be sent after %ldms", timeToSend);
	}
	else
	{
		printf("%d bytes sent after Duty Cycling",PAYLOAD_SIZE);
	}
}

/*********************************************************************
* Function:  dutyCyclingAppData1Confcb
*
* Overview: Call back function configured for sending App Data 1
*
* Return:  None.
********************************************************************/
static void dutyCyclingAppData1Confcb(uint8_t handle, miwi_status_t status, uint8_t* msgPointer)
{
	printf("\r\nApp Data 1 - sent successfully");
	/* After transmitting App Data 1, start a timer with random interval and try to send App Data 2.
	
	   Case-1: If the random delay is less than the duty cycling timer then App data 2 will not be 
               sent and the pending wait interval will be return by MiApp_SendDutyCycledData for App Data 2
			   
       Case-2: If the random delay is greater than the duty cycling timer then App Data 2 will be sent successfully.   
	*/
	dutyCyclingAppData2DelayTimer.handler = dutyCyclingAppData2SendingTimerHandler;
	dutyCyclingAppData2DelayTimer.interval = (rand() & 0x3F) * 100;
	dutyCyclingAppData2DelayTimer.mode = SYS_TIMER_INTERVAL_MODE;
	SYS_TimerStart(&dutyCyclingAppData2DelayTimer);
	printf("\r\nCalculated Delay for next App Data - %ldms", dutyCyclingAppData2DelayTimer.interval);
}

/*********************************************************************
* Function:  dutyCyclingAppData2SendingTimerHandler
*
* Overview: Handler for Sys timer used to send App Data 2 after random delay
*           in duty cycling mode
*
* Return:  None.
********************************************************************/
static void dutyCyclingAppData2SendingTimerHandler(SYS_Timer_t *timer)
{
	printf("\r\nApp Data 2 - ");
	/* After the random timer, send the App Data 2 to verify the working of duty cycling. */
	uint32_t timeToSend = MiApp_SendDutyCycledData(LONG_ADDR_LEN, connectionTable[0].Address, PAYLOAD_SIZE, PAYLOAD, 1, true, dutyCyclingAppData2Confcb);
	if (timeToSend)
	{
		printf("should be sent after %ldms", timeToSend);
		/* If the App data 2 was not blocked due to duty cycling, restart the timer with returned time */
		dutyCyclingAppData2DelayTimer.handler = dutyCyclingAppData2RetryTimerHandler;
		dutyCyclingAppData2DelayTimer.interval = timeToSend;
		dutyCyclingAppData2DelayTimer.mode = SYS_TIMER_INTERVAL_MODE;
		SYS_TimerStart(&dutyCyclingAppData2DelayTimer);
		printf("\r\nTimer for %ldms started to retry App Data 2", timeToSend);
	}
	else
	{
		printf("%d bytes sent after Duty Cycling",PAYLOAD_SIZE);
	}
}

/*********************************************************************
* Function:  dutyCyclingAppData2SendingTimerHandler
*
* Overview: Handler for Sys timer used to retry App Data 2 if the 
*           previous try to transmit was halted due to duty cycling.
*
* Return:  None.
********************************************************************/
static void dutyCyclingAppData2RetryTimerHandler(SYS_Timer_t *timer)
{
	printf("\r\nApp Data 2 - ");
	uint32_t timeToSend = MiApp_SendDutyCycledData(LONG_ADDR_LEN, connectionTable[0].Address, PAYLOAD_SIZE, PAYLOAD, 1, true, dutyCyclingAppData2Confcb);
	if (timeToSend)
	{
		printf("should be sent after %ldms", timeToSend);
	}
	else
	{
		printf("%d bytes sent after Duty Cycling",PAYLOAD_SIZE);
	}
}

/*********************************************************************
* Function:  dutyCyclingAppData2Confcb
*
* Overview: Call back function configured for sending App Data 2
*
* Return:  None.
********************************************************************/
static void dutyCyclingAppData2Confcb(uint8_t handle, miwi_status_t status, uint8_t* msgPointer)
{
	printf("\r\nApp Data 2 - sent successfully");
}
#endif
/*********************************************************************
* Function: void Run_Demo(void)
*
* Overview: runs the demo based on user input
********************************************************************/
void Run_Demo(void)
{
    P2PTasks();
#if defined(ENABLE_NETWORK_FREEZER)
#if PDS_ENABLE_WEAR_LEVELING
    PDS_TaskHandler();
#endif
#endif
    run_p2p_demo();
}

#ifdef ENABLE_CONSOLE
#ifdef ENABLE_DUMP
/*********************************************************************
    * void DumpConnection(uint8_t index)
    *
    * Overview:        This function prints out the content of the connection 
    *                  with the input index of the P2P Connection Entry
    *
    * PreCondition:    
    *
    * Input:  
    *          index   - The index of the P2P Connection Entry to be printed out
    *                  
    * Output:  None
    *
    * Side Effects:    The content of the connection pointed by the index 
    *                  of the P2P Connection Entry will be printed out
    *
    ********************************************************************/
void DumpConnection(INPUT uint8_t index)
{
    uint8_t i, j;
        
    if( index > CONNECTION_SIZE )
    {
        printf("\r\n\r\nMy Address: 0x");
        for(i = 0; i < MY_ADDRESS_LENGTH; i++)
        {
            printf("%02x",myLongAddress[MY_ADDRESS_LENGTH-1-i]);
        }
        #if defined(IEEE_802_15_4)
            printf("  PANID: 0x");
            printf("%x",myPANID.v[1]);
            printf("%x",myPANID.v[0]);
        #endif
        printf("  Channel: ");
        printf("%d",currentChannel);
    }
            
    if( index < CONNECTION_SIZE )
    {
        printf("\r\nConnection \tPeerLongAddress \tPeerInfo\r\n");  
        if( connectionTable[index].status.bits.isValid )
        {
            printf("%02x",index);
            printf("\t\t\t");
            for(i = 0; i < 8; i++)
            {
                if(i < MY_ADDRESS_LENGTH)
                {
                    printf("%02x", connectionTable[index].Address[MY_ADDRESS_LENGTH-1-i] );
                }
                else
                {
                    printf("\t");
                }
            }
            printf("/t");
            #if ADDITIONAL_NODE_ID_SIZE > 0
                for(i = 0; i < ADDITIONAL_NODE_ID_SIZE; i++)
                {
                    printf("%02x", connectionTable[index].PeerInfo[i] );
                }
            #endif
            printf("\r\n");
        }
    }
    else
    {
        printf("\r\n\r\nConnection     PeerLongAddress     PeerInfo\r\n");  
        for(i = 0; i < CONNECTION_SIZE; i++)
        {
                
            if( connectionTable[i].status.bits.isValid )
            {
                printf("%02x",i);
                printf("             ");
                for(j = 0; j < 8; j++)
                {
                    if( j < MY_ADDRESS_LENGTH )
                    {
                        printf("%02x", connectionTable[i].Address[MY_ADDRESS_LENGTH-1-j] );
                    }
                    else
                    {
                        printf("  ");
                    }
                }
                printf("    ");
#if ADDITIONAL_NODE_ID_SIZE > 0
                    for(j = 0; j < ADDITIONAL_NODE_ID_SIZE; j++)
                    {
                        printf("%02x", connectionTable[i].PeerInfo[j] );
                    }
#endif
                printf("\r\n");
            }  
        }
    }
}
#endif
#endif