/* Original work Copyright 2023 joaquimorg
 * https://github.com/joaquimorg
 *
 * Modified work Copyright 2024 kamilsss655
 * https://github.com/kamilsss655
 *
 * Modified work Copyright 2025 dobrishinov
 * https://github.com/dobrishinov
 * Note: I hereby authorize the use of my modifications in this code within the premium firmware,
 * without any limitations on its application, including for closed-source or commercial purposes.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifdef ENABLE_MESSENGER

#include <string.h>
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/bk4819.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "settings.h"
#include "radio.h"
#include "app.h"
#include "audio.h"
#include "functions.h"
#include "frequencies.h"
#include "driver/system.h"
#include "app/messenger.h"
#include "ui/ui.h"
#ifdef ENABLE_ENCRYPTION
	#include "helper/crypto.h"
#endif
#ifdef ENABLE_MESSENGER_UART
    #include "driver/uart.h"
#endif

const uint8_t MSG_BUTTON_STATE_HELD = 1 << 1;

const uint8_t MSG_BUTTON_EVENT_SHORT =  0;
const uint8_t MSG_BUTTON_EVENT_LONG =  MSG_BUTTON_STATE_HELD;

const uint8_t MAX_MSG_LENGTH = PAYLOAD_LENGTH - 1;

uint16_t TONE2_FREQ;

#define NEXT_CHAR_DELAY 100 // 10ms tick

char T9TableLow[9][4] = { {',', '.', '?', '!'}, {'a', 'b', 'c', '\0'}, {'d', 'e', 'f', '\0'}, {'g', 'h', 'i', '\0'}, {'j', 'k', 'l', '\0'}, {'m', 'n', 'o', '\0'}, {'p', 'q', 'r', 's'}, {'t', 'u', 'v', '\0'}, {'w', 'x', 'y', 'z'} };
char T9TableUp[9][4] = { {',', '.', '?', '!'}, {'A', 'B', 'C', '\0'}, {'D', 'E', 'F', '\0'}, {'G', 'H', 'I', '\0'}, {'J', 'K', 'L', '\0'}, {'M', 'N', 'O', '\0'}, {'P', 'Q', 'R', 'S'}, {'T', 'U', 'V', '\0'}, {'W', 'X', 'Y', 'Z'} };
unsigned char numberOfLettersAssignedToKey[9] = { 4, 3, 3, 3, 3, 3, 4, 3, 4 };

char T9TableNum[9][4] = { {'1', '\0', '\0', '\0'}, {'2', '\0', '\0', '\0'}, {'3', '\0', '\0', '\0'}, {'4', '\0', '\0', '\0'}, {'5', '\0', '\0', '\0'}, {'6', '\0', '\0', '\0'}, {'7', '\0', '\0', '\0'}, {'8', '\0', '\0', '\0'}, {'9', '\0', '\0', '\0'} };
unsigned char numberOfNumsAssignedToKey[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };

char cMessage[PAYLOAD_LENGTH];
char lastcMessage[PAYLOAD_LENGTH];
char rxMessage[4][PAYLOAD_LENGTH + 2];
unsigned char cIndex = 0;
unsigned char prevKey = 0, prevLetter = 0;
KeyboardType keyboardType = UPPERCASE;

MsgStatus msgStatus = READY;

union DataPacket dataPacket;

uint16_t gErrorsDuringMSG;

uint8_t hasNewMessage = 0;

uint8_t keyTickCounter = 0;

// -----------------------------------------------------

void MSG_FSKSendData() {

	// turn off CTCSS/CDCSS during FFSK
	const uint16_t css_val = BK4819_ReadRegister(BK4819_REG_51);
	BK4819_WriteRegister(BK4819_REG_51, 0);

	// set the FM deviation level
	const uint16_t dev_val = BK4819_ReadRegister(BK4819_REG_40);

	{
		uint16_t deviation;
		switch (gEeprom.VfoInfo[gEeprom.TX_VFO].CHANNEL_BANDWIDTH)
		{
			case BK4819_FILTER_BW_WIDE:            deviation =  1300; break; // 20k // measurements by kamilsss655
			case BK4819_FILTER_BW_NARROW:          deviation =  1200; break; // 10k
			// case BK4819_FILTER_BW_NARROWAVIATION:  deviation =  850; break;  // 5k
			// case BK4819_FILTER_BW_NARROWER:        deviation =  850; break;  // 5k
			// case BK4819_FILTER_BW_NARROWEST:	      deviation =  850; break;  // 5k
			default:                               deviation =  850;  break;  // 5k
		}

		//BK4819_WriteRegister(0x40, (3u << 12) | (deviation & 0xfff));
		BK4819_WriteRegister(BK4819_REG_40, (dev_val & 0xf000) | (deviation & 0xfff));
	}

	// REG_2B   0
	//
	// <15> 1 Enable CTCSS/CDCSS DC cancellation after FM Demodulation   1 = enable 0 = disable
	// <14> 1 Enable AF DC cancellation after FM Demodulation            1 = enable 0 = disable
	// <10> 0 AF RX HPF 300Hz filter     0 = enable 1 = disable
	// <9>  0 AF RX LPF 3kHz filter      0 = enable 1 = disable
	// <8>  0 AF RX de-emphasis filter   0 = enable 1 = disable
	// <2>  0 AF TX HPF 300Hz filter     0 = enable 1 = disable
	// <1>  0 AF TX LPF filter           0 = enable 1 = disable
	// <0>  0 AF TX pre-emphasis filter  0 = enable 1 = disable
	//
	// disable the 300Hz HPF and FM pre-emphasis filter
	//
	const uint16_t filt_val = BK4819_ReadRegister(BK4819_REG_2B);
	BK4819_WriteRegister(BK4819_REG_2B, (1u << 2) | (1u << 0));
	
	MSG_ConfigureFSK(false);



	SYSTEM_DelayMs(100);

	{	// load the entire packet data into the TX FIFO buffer
		for (size_t i = 0, j = 0; i < sizeof(dataPacket.serializedArray); i += 2, j++) {
        	BK4819_WriteRegister(BK4819_REG_5F, (dataPacket.serializedArray[i + 1] << 8) | dataPacket.serializedArray[i]);
    	}
	}

	// enable FSK TX
	BK4819_FskEnableTx();

	{
		// allow up to 310ms for the TX to complete
		// if it takes any longer then somethings gone wrong, we shut the TX down
		unsigned int timeout = 1000 / 5;

		while (timeout-- > 0)
		{
			SYSTEM_DelayMs(5);
			if (BK4819_ReadRegister(BK4819_REG_0C) & (1u << 0))
			{	// we have interrupt flags
				BK4819_WriteRegister(BK4819_REG_02, 0);
				if (BK4819_ReadRegister(BK4819_REG_02) & BK4819_REG_02_FSK_TX_FINISHED)
					timeout = 0;       // TX is complete
			}
		}
	}
	//BK4819_WriteRegister(BK4819_REG_02, 0);

	SYSTEM_DelayMs(100);

	// disable TX
	MSG_ConfigureFSK(true);

	// restore FM deviation level
	BK4819_WriteRegister(BK4819_REG_40, dev_val);

	// restore TX/RX filtering
	BK4819_WriteRegister(BK4819_REG_2B, filt_val);

	// restore the CTCSS/CDCSS setting
	BK4819_WriteRegister(BK4819_REG_51, css_val);

}

void MSG_EnableRX(const bool enable) {

	if (enable) {
		MSG_ConfigureFSK(true);

		if(gEeprom.MESSENGER_CONFIG.data.receive)
			BK4819_FskEnableRx();
	} else {
		BK4819_WriteRegister(BK4819_REG_70, 0);
		BK4819_WriteRegister(BK4819_REG_58, 0);
	}
}


// -----------------------------------------------------

void moveUP(char (*rxMessages)[PAYLOAD_LENGTH + 2]) {
    // Shift existing lines up
    strcpy(rxMessages[0], rxMessages[1]);
	strcpy(rxMessages[1], rxMessages[2]);
	strcpy(rxMessages[2], rxMessages[3]);

    // Insert the new line at the last position
	memset(rxMessages[3], 0, sizeof(rxMessages[3]));
}

void MSG_SendPacket() {

	if ( msgStatus != READY ) return;

	RADIO_PrepareTX();

	if(RADIO_GetVfoState() != VFO_STATE_NORMAL){
		gRequestDisplayScreen = DISPLAY_MAIN;
		return;
	} 

	if ( strlen((char *)dataPacket.data.payload) > 0) {

		msgStatus = SENDING;

		RADIO_SetVfoState(VFO_STATE_NORMAL);
		BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
		BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);

		// display sent message (before encryption)
		if (dataPacket.data.header != ACK_PACKET) {
			moveUP(rxMessage);
			sprintf(rxMessage[3], "> %s", dataPacket.data.payload);
			#ifdef ENABLE_MESSENGER_UART
				UART_printf("SMS>%s\r\n", dataPacket.data.payload);
			#endif
			memset(lastcMessage, 0, sizeof(lastcMessage));
			memcpy(lastcMessage, dataPacket.data.payload, PAYLOAD_LENGTH);
			cIndex = 0;
			prevKey = 0;
			prevLetter = 0;
			memset(cMessage, 0, sizeof(cMessage));
		}

		#ifdef ENABLE_ENCRYPTION
			if(dataPacket.data.header == ENCRYPTED_MESSAGE_PACKET){

				CRYPTO_Random(dataPacket.data.nonce, NONCE_LENGTH);

				CRYPTO_Crypt(
					dataPacket.data.payload,
					PAYLOAD_LENGTH,
					dataPacket.data.payload,
					&dataPacket.data.nonce,
					gEncryptionKey,
					256
				);
			}
		#endif

		BK4819_DisableDTMF();

		// mute the mic during TX
		gMuteMic = true;

		SYSTEM_DelayMs(50);

		MSG_FSKSendData();

		SYSTEM_DelayMs(50);

		APP_EndTransmission(false);
		// this must be run after end of TX, otherwise radio will still TX transmit without even RED LED on
		FUNCTION_Select(FUNCTION_FOREGROUND);

		RADIO_SetVfoState(VFO_STATE_NORMAL);

		// disable mic mute after TX
		gMuteMic = false;

		BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);

		MSG_EnableRX(true);

		// clear packet buffer
		MSG_ClearPacketBuffer();

		msgStatus = READY;

	} else {
		AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
	}
}

uint8_t validate_char( uint8_t rchar ) {
	if ( (rchar == 0x1b) || (rchar >= 32 && rchar <= 127) ) {
		return rchar;
	}
	return 32;
}

void MSG_StorePacket(const uint16_t interrupt_bits) {

	//const uint16_t rx_sync_flags   = BK4819_ReadRegister(BK4819_REG_0B);

	const bool rx_sync             = (interrupt_bits & BK4819_REG_02_FSK_RX_SYNC) ? true : false;
	const bool rx_fifo_almost_full = (interrupt_bits & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) ? true : false;
	const bool rx_finished         = (interrupt_bits & BK4819_REG_02_FSK_RX_FINISHED) ? true : false;

	//UART_printf("\nMSG : S%i, F%i, E%i | %i", rx_sync, rx_fifo_almost_full, rx_finished, interrupt_bits);

	if (rx_sync) {
		#ifdef ENABLE_MESSENGER_FSK_MUTE
			// prevent listening to fsk data and squelch (kamilsss655)
			// CTCSS codes seem to false trigger the rx_sync
			if(gCurrentCodeType == CODE_TYPE_OFF)
				AUDIO_AudioPathOff();
		#endif
		gFSKWriteIndex = 0;
		MSG_ClearPacketBuffer();
		msgStatus = RECEIVING;
	}

	if (rx_fifo_almost_full && msgStatus == RECEIVING) {

		const uint16_t count = BK4819_ReadRegister(BK4819_REG_5E) & (7u << 0);  // almost full threshold
		for (uint16_t i = 0; i < count; i++) {
			const uint16_t word = BK4819_ReadRegister(BK4819_REG_5F);
			if (gFSKWriteIndex < sizeof(dataPacket.serializedArray))
				dataPacket.serializedArray[gFSKWriteIndex++] = (word >> 0) & 0xff;
			if (gFSKWriteIndex < sizeof(dataPacket.serializedArray))
				dataPacket.serializedArray[gFSKWriteIndex++] = (word >> 8) & 0xff;
		}

		SYSTEM_DelayMs(10);

	}

	if (rx_finished) {
		// turn off green LED
		BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, 0);
		BK4819_FskClearFifo();
		BK4819_FskEnableRx();
		msgStatus = READY;

		if (gFSKWriteIndex > 2) {
			MSG_HandleReceive();
		}
		gFSKWriteIndex = 0;
	}
}

void MSG_Init() {
	memset(rxMessage, 0, sizeof(rxMessage));
	memset(cMessage, 0, sizeof(cMessage));
	memset(lastcMessage, 0, sizeof(lastcMessage));
	hasNewMessage = 0;
	msgStatus = READY;
	prevKey = 0;
    prevLetter = 0;
	cIndex = 0;
	#ifdef ENABLE_ENCRYPTION
		gRecalculateEncKey = true;
	#endif
}

void MSG_SendAck() {
	// in the future we might reply with received payload and then the sending radio
	// could compare it and determine if the messegage was read correctly (kamilsss655)
	MSG_ClearPacketBuffer();
	dataPacket.data.header = ACK_PACKET;
	// sending only empty header seems to not work, so set few bytes of payload to increase reliability (kamilsss655)
	memset(dataPacket.data.payload, 255, 5);
	MSG_SendPacket();
}

void MSG_HandleReceive(){
	if (dataPacket.data.header == ACK_PACKET) {
	#ifdef ENABLE_MESSENGER_DELIVERY_NOTIFICATION
		#ifdef ENABLE_MESSENGER_UART
			UART_printf("SVC<RCPT\r\n");
		#endif
		rxMessage[3][0] = '+';
		gUpdateStatus = true;
		gUpdateDisplay = true;
	#endif
	} else {
		moveUP(rxMessage);
		if (dataPacket.data.header >= INVALID_PACKET) {
			snprintf(rxMessage[3], PAYLOAD_LENGTH + 2, "ERROR: INVALID PACKET.");
		}
		else
		{
			#ifdef ENABLE_ENCRYPTION
				if(dataPacket.data.header == ENCRYPTED_MESSAGE_PACKET)
				{
					CRYPTO_Crypt(dataPacket.data.payload,
						PAYLOAD_LENGTH,
						dataPacket.data.payload,
						&dataPacket.data.nonce,
						gEncryptionKey,
						256);
				}
				snprintf(rxMessage[3], PAYLOAD_LENGTH + 2, "< %s", dataPacket.data.payload);
			#else
				snprintf(rxMessage[3], PAYLOAD_LENGTH + 2, "< %s", dataPacket.data.payload);
			#endif
			#ifdef ENABLE_MESSENGER_UART
				UART_printf("SMS<%s\r\n", dataPacket.data.payload);
			#endif
		}

		if ( gScreenToDisplay != DISPLAY_MSG ) {
			hasNewMessage = 1;
			gUpdateStatus = true;
			gUpdateDisplay = true;
	#ifdef ENABLE_MESSENGER_NOTIFICATION
			gPlayMSGRing = true;
	#endif
		}
		else {
			gUpdateDisplay = true;
		}
	}

	// Transmit a message to the sender that we have received the message
	if (dataPacket.data.header == MESSAGE_PACKET ||
		dataPacket.data.header == ENCRYPTED_MESSAGE_PACKET)
	{
		// wait so the correspondent radio can properly receive it
		SYSTEM_DelayMs(700);

		if(gEeprom.MESSENGER_CONFIG.data.ack)
			MSG_SendAck();
	}
}

// ---------------------------------------------------------------------------------

void insertCharInMessage(uint8_t key) {
	if ( key == KEY_0 ) {
		if ( keyboardType == NUMERIC ) {
			cMessage[cIndex] = '0';
		} else {
			cMessage[cIndex] = ' ';
		}
		if ( cIndex < MAX_MSG_LENGTH ) {
			cIndex++;
		}
	} else if (prevKey == key)
	{
		cIndex = (cIndex > 0) ? cIndex - 1 : 0;
		if ( keyboardType == NUMERIC ) {
			cMessage[cIndex] = T9TableNum[key - 1][(++prevLetter) % numberOfNumsAssignedToKey[key - 1]];
		} else if ( keyboardType == LOWERCASE ) {
			cMessage[cIndex] = T9TableLow[key - 1][(++prevLetter) % numberOfLettersAssignedToKey[key - 1]];
		} else {
			cMessage[cIndex] = T9TableUp[key - 1][(++prevLetter) % numberOfLettersAssignedToKey[key - 1]];
		}
		if ( cIndex < MAX_MSG_LENGTH ) {
			cIndex++;
		}
	}
	else
	{
		prevLetter = 0;
		if ( cIndex >= MAX_MSG_LENGTH ) {
			cIndex = (cIndex > 0) ? cIndex - 1 : 0;
		}
		if ( keyboardType == NUMERIC ) {
			cMessage[cIndex] = T9TableNum[key - 1][prevLetter];
		} else if ( keyboardType == LOWERCASE ) {
			cMessage[cIndex] = T9TableLow[key - 1][prevLetter];
		} else {
			cMessage[cIndex] = T9TableUp[key - 1][prevLetter];
		}
		if ( cIndex < MAX_MSG_LENGTH ) {
			cIndex++;
		}

	}
	cMessage[cIndex] = '\0';
	if ( keyboardType == NUMERIC ) {
		prevKey = 0;
		prevLetter = 0;
	} else {
		prevKey = key;
	}
}

void processBackspace() {
	cIndex = (cIndex > 0) ? cIndex - 1 : 0;
	cMessage[cIndex] = '\0';
	prevKey = 0;
    prevLetter = 0;
}

void  MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld) {
	uint8_t state = bKeyPressed + 2 * bKeyHeld;

	if (state == MSG_BUTTON_EVENT_SHORT) {

		switch (Key)
		{
			case KEY_0:
			case KEY_1:
			case KEY_2:
			case KEY_3:
			case KEY_4:
			case KEY_5:
			case KEY_6:
			case KEY_7:
			case KEY_8:
			case KEY_9:
				if ( keyTickCounter > NEXT_CHAR_DELAY) {
					prevKey = 0;
    				prevLetter = 0;
				}
				insertCharInMessage(Key);
				keyTickCounter = 0;
				break;
			case KEY_STAR:
				keyboardType = (KeyboardType)((keyboardType + 1) % END_TYPE_KBRD);
				break;
			case KEY_F:
				processBackspace();
				break;
			case KEY_UP:
				memset(cMessage, 0, sizeof(cMessage));
				memcpy(cMessage, lastcMessage, PAYLOAD_LENGTH);
				cIndex = strlen(cMessage);
				break;
			/*case KEY_DOWN:
				break;*/
			case KEY_MENU:
				// Send message
				MSG_Send(cMessage);
				break;
			case KEY_EXIT:
				gRequestDisplayScreen = DISPLAY_MAIN;
				break;

			default:
				AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
				break;
		}

	} else if (state == MSG_BUTTON_EVENT_LONG) {

		switch (Key)
		{
			case KEY_F:
				MSG_Init();
				break;
			default:
				AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
				break;
		}
	}

}

void MSG_ClearPacketBuffer()
{
	memset(dataPacket.serializedArray, 0, sizeof(dataPacket.serializedArray));
}

void MSG_Send(const char *cMessage){
	MSG_ClearPacketBuffer();
	#ifdef ENABLE_ENCRYPTION
		if(gEeprom.MESSENGER_CONFIG.data.encrypt)
		{
			dataPacket.data.header=ENCRYPTED_MESSAGE_PACKET;
		}
		else
		{
			dataPacket.data.header=MESSAGE_PACKET;
		}
	#else
		dataPacket.data.header=MESSAGE_PACKET;
	#endif
	memcpy(dataPacket.data.payload, cMessage, sizeof(dataPacket.data.payload));
	MSG_SendPacket();
}

void MSG_ConfigureFSK(bool rx)
{
	// REG_70
	//
	// <15>   0 Enable TONE1
	//        1 = Enable
	//        0 = Disable
	//
	// <14:8> 0 TONE1 tuning gain
	//        0 ~ 127
	//
	// <7>    0 Enable TONE2
	//        1 = Enable
	//        0 = Disable
	//
	// <6:0>  0 TONE2/FSK tuning gain
	//        0 ~ 127
	//
	BK4819_WriteRegister(BK4819_REG_70,
		( 0u << 15) |    // 0
		( 0u <<  8) |    // 0
		( 1u <<  7) |    // 1
		(96u <<  0));    // 96

	// Tone2 = FSK baudrate                       // kamilsss655 2024
	switch(gEeprom.MESSENGER_CONFIG.data.modulation)
	{
		case MOD_AFSK_1200:
			TONE2_FREQ = 12389u;
			break;
		case MOD_FSK_700:
			TONE2_FREQ = 7227u;
			break;
		case MOD_FSK_450:
			TONE2_FREQ = 4646u;
			break;
	}

	BK4819_WriteRegister(BK4819_REG_72, TONE2_FREQ);
	
	switch(gEeprom.MESSENGER_CONFIG.data.modulation)
	{
		case MOD_FSK_700:
		case MOD_FSK_450:
			BK4819_WriteRegister(BK4819_REG_58,
				(0u << 13) |		// 1 FSK TX mode selection
									//   0 = FSK 1.2K and FSK 2.4K TX .. no tones, direct FM
									//   1 = FFSK 1200 / 1800 TX
									//   2 = ???
									//   3 = FFSK 1200 / 2400 TX
									//   4 = ???
									//   5 = NOAA SAME TX
									//   6 = ???
									//   7 = ???
									//
				(0u << 10) |		// 0 FSK RX mode selection
									//   0 = FSK 1.2K, FSK 2.4K RX and NOAA SAME RX .. no tones, direct FM
									//   1 = ???
									//   2 = ???
									//   3 = ???
									//   4 = FFSK 1200 / 2400 RX
									//   5 = ???
									//   6 = ???
									//   7 = FFSK 1200 / 1800 RX
									//
				(3u << 8) |			// 0 FSK RX gain
									//   0 ~ 3
									//
				(0u << 6) |			// 0 ???
									//   0 ~ 3
									//
				(0u << 4) |			// 0 FSK preamble type selection
									//   0 = 0xAA or 0x55 due to the MSB of FSK sync byte 0
									//   1 = ???
									//   2 = 0x55
									//   3 = 0xAA
									//
				(0u << 1) |			// 1 FSK RX bandwidth setting
									//   0 = FSK 1.2K .. no tones, direct FM
									//   1 = FFSK 1200 / 1800
									//   2 = NOAA SAME RX
									//   3 = ???
									//   4 = FSK 2.4K and FFSK 1200 / 2400
									//   5 = ???
									//   6 = ???
									//   7 = ???
									//
				(1u << 0));			// 1 FSK enable
									//   0 = disable
									//   1 = enable
		break;
		case MOD_AFSK_1200:
			BK4819_WriteRegister(BK4819_REG_58,
				(1u << 13) |		// 1 FSK TX mode selection
									//   0 = FSK 1.2K and FSK 2.4K TX .. no tones, direct FM
									//   1 = FFSK 1200 / 1800 TX
									//   2 = ???
									//   3 = FFSK 1200 / 2400 TX
									//   4 = ???
									//   5 = NOAA SAME TX
									//   6 = ???
									//   7 = ???
									//
				(7u << 10) |		// 0 FSK RX mode selection
									//   0 = FSK 1.2K, FSK 2.4K RX and NOAA SAME RX .. no tones, direct FM
									//   1 = ???
									//   2 = ???
									//   3 = ???
									//   4 = FFSK 1200 / 2400 RX
									//   5 = ???
									//   6 = ???
									//   7 = FFSK 1200 / 1800 RX
									//
				(3u << 8) |			// 0 FSK RX gain
									//   0 ~ 3
									//
				(0u << 6) |			// 0 ???
									//   0 ~ 3
									//
				(0u << 4) |			// 0 FSK preamble type selection
									//   0 = 0xAA or 0x55 due to the MSB of FSK sync byte 0
									//   1 = ???
									//   2 = 0x55
									//   3 = 0xAA
									//
				(1u << 1) |			// 1 FSK RX bandwidth setting
									//   0 = FSK 1.2K .. no tones, direct FM
									//   1 = FFSK 1200 / 1800
									//   2 = NOAA SAME RX
									//   3 = ???
									//   4 = FSK 2.4K and FFSK 1200 / 2400
									//   5 = ???
									//   6 = ???
									//   7 = ???
									//
				(1u << 0));			// 1 FSK enable
									//   0 = disable
									//   1 = enable
		break;
	}

	// REG_5A .. bytes 0 & 1 sync pattern
	//
	// <15:8> sync byte 0
	// < 7:0> sync byte 1
	BK4819_WriteRegister(BK4819_REG_5A, 0x3072);

	// REG_5B .. bytes 2 & 3 sync pattern
	//
	// <15:8> sync byte 2
	// < 7:0> sync byte 3
	BK4819_WriteRegister(BK4819_REG_5B, 0x576C);

	// disable CRC
	BK4819_WriteRegister(BK4819_REG_5C, 0x5625);

	// set the almost full threshold
	if(rx)
		BK4819_WriteRegister(BK4819_REG_5E, (64u << 3) | (1u << 0));  // 0 ~ 127, 0 ~ 7

	// packet size .. sync + packet - size of a single packet

	uint16_t size = sizeof(dataPacket.serializedArray);
	// size -= (fsk_reg59 & (1u << 3)) ? 4 : 2;
	if(rx)
		size = (((size + 1) / 2) * 2) + 2;             // round up to even, else FSK RX doesn't work

	BK4819_WriteRegister(BK4819_REG_5D, (size << 8));
	// BK4819_WriteRegister(BK4819_REG_5D, ((sizeof(dataPacket.serializedArray)) << 8));

	// clear FIFO's
	BK4819_FskClearFifo();

	// configure main FSK params
	BK4819_WriteRegister(BK4819_REG_59,
				(0u        <<       15) |   // 0/1     1 = clear TX FIFO
				(0u        <<       14) |   // 0/1     1 = clear RX FIFO
				(0u        <<       13) |   // 0/1     1 = scramble
				(0u        <<       12) |   // 0/1     1 = enable RX
				(0u        <<       11) |   // 0/1     1 = enable TX
				(0u        <<       10) |   // 0/1     1 = invert data when RX
				(0u        <<        9) |   // 0/1     1 = invert data when TX
				(0u        <<        8) |   // 0/1     ???
				((rx ? 0u : 15u) <<  4) |   // 0 ~ 15  preamble length .. bit toggling
				(1u        <<        3) |   // 0/1     sync length
				(0u        <<        0)     // 0 ~ 7   ???
				
	);

	// clear interupts
	BK4819_WriteRegister(BK4819_REG_02, 0);
}

#endif
