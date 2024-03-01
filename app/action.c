/* Original work Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Modified work Copyright 2024 kamilsss655
 * https://github.com/kamilsss655
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

#include <string.h>

#include "app/action.h"
#include "app/app.h"
#include "app/chFrScanner.h"
#include "app/common.h"
#include "app/dtmf.h"
#ifdef ENABLE_FMRADIO
	#include "app/fm.h"
#endif
#include "app/scanner.h"
#include "audio.h"
#include "bsp/dp32g030/gpio.h"
#ifdef ENABLE_FMRADIO
	#include "driver/bk1080.h"
#endif
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/backlight.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"
#include "spectrum.h"

static void ACTION_FlashLight(void)
{
	switch (gFlashLightState)
	{
		case 0:
			gFlashLightState++;
			GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
			break;
		case 1:
		case 2:
			gFlashLightState++;
			break;
		default:
			gFlashLightState = 0;
			GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
	}
}

void ACTION_Power(void)
{
	if (++gTxVfo->OUTPUT_POWER > OUTPUT_POWER_HIGH)
		gTxVfo->OUTPUT_POWER = OUTPUT_POWER_LOW;

	gRequestSaveChannel = 1;

#ifdef ENABLE_VOICE
	gAnotherVoiceID   = VOICE_ID_POWER;
#endif

	gRequestDisplayScreen = gScreenToDisplay;
}

void ACTION_Monitor(void)
{
	if (gCurrentFunction != FUNCTION_MONITOR) { // enable the monitor
		RADIO_SelectVfos();
		RADIO_SetupRegisters(true);
		APP_StartListening(FUNCTION_MONITOR);
		return;
	}

	gMonitor = false;

	if (gScanStateDir != SCAN_OFF) {
		gScanPauseDelayIn_10ms = scan_pause_delay_in_1_10ms;
		gScheduleScanListen    = false;
		gScanPauseMode         = true;
	}

	RADIO_SetupRegisters(true);

	gRequestDisplayScreen = gScreenToDisplay;
}

void ACTION_Scan(bool bRestart)
{
	(void)bRestart;

	if (!SCANNER_IsScanning())
	{	// not scanning

		gMonitor = false;

#ifdef ENABLE_DTMF_CALLING
		DTMF_clear_RX();
#endif
		gDTMF_RX_live_timeout = 0;
		memset(gDTMF_RX_live, 0, sizeof(gDTMF_RX_live));

		RADIO_SelectVfos();

		GUI_SelectNextDisplay(DISPLAY_MAIN);

		if (gScanStateDir != SCAN_OFF)
		{	// already scanning

			if (IS_MR_CHANNEL(gNextMrChannel))
			{	// channel mode

				// keep scanning but toggle between scan lists
				gEeprom.SCAN_LIST_DEFAULT = (gEeprom.SCAN_LIST_DEFAULT + 1) % 3;

				// jump to the next channel
				CHFRSCANNER_Start(false, gScanStateDir);
				gScanPauseDelayIn_10ms = 1;
				gScheduleScanListen    = false;

				gUpdateStatus = true;
			}
			else
			{	// stop scanning

				CHFRSCANNER_Stop();

				#ifdef ENABLE_VOICE
					gAnotherVoiceID = VOICE_ID_SCANNING_STOP;
				#endif
			}
		}
		else
		{	// start scanning

			CHFRSCANNER_Start(true, SCAN_FWD);

			#ifdef ENABLE_VOICE
				AUDIO_SetVoiceID(0, VOICE_ID_SCANNING_BEGIN);
				AUDIO_PlaySingleVoice(true);
			#endif

			// clear the other vfo's rssi level (to hide the antenna symbol)
			gVFO_RSSI_bar_level[(gEeprom.RX_VFO + 1) & 1u] = 0;

			// let the user see DW is not active
			gDualWatchActive = false;
			gUpdateStatus    = true;
		}
	}
}

#ifdef ENABLE_VOX
	void ACTION_Vox(void)
	{
		gEeprom.VOX_SWITCH   = !gEeprom.VOX_SWITCH;
		gRequestSaveSettings = true;
		gFlagReconfigureVfos = true;
		#ifdef ENABLE_VOICE
			gAnotherVoiceID  = VOICE_ID_VOX;
		#endif
		gUpdateStatus        = true;
	}
#endif

#if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
	static void ACTION_AlarmOr1750(const bool b1750)
	{
		(void)b1750;
		gInputBoxIndex = 0;

		#if defined(ENABLE_ALARM) && defined(ENABLE_TX1750)
			gAlarmState = b1750 ? ALARM_STATE_TX1750 : ALARM_STATE_TXALARM;
			gAlarmRunningCounter = 0;
		#elif defined(ENABLE_ALARM)
			gAlarmState          = ALARM_STATE_TXALARM;
			gAlarmRunningCounter = 0;
		#else
			gAlarmState = ALARM_STATE_TX1750;
		#endif

		gFlagPrepareTX = true;

		if (gScreenToDisplay != DISPLAY_MENU)     // 1of11 .. don't close the menu
			gRequestDisplayScreen = DISPLAY_MAIN;
	}
#endif


#ifdef ENABLE_FMRADIO
	void ACTION_FM(void)
	{
		if (gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_MONITOR)
		{
			if (gFmRadioMode)
			{
				FM_TurnOff();

				gInputBoxIndex        = 0;
				#ifdef ENABLE_VOX
					gVoxResumeCountdown = 80;
				#endif
				gFlagReconfigureVfos  = true;

				gRequestDisplayScreen = DISPLAY_MAIN;
				return;
			}

			gMonitor = false;

			RADIO_SelectVfos();
			RADIO_SetupRegisters(true);

			FM_Start();

			gInputBoxIndex = 0;

			gRequestDisplayScreen = DISPLAY_FM;
		}
	}
#endif

void ACTION_RunSpectrum(void)
{
	#ifdef ENABLE_SPECTRUM_CHANNEL_SCAN

		if(gScanRangeStart){
			// if scanRangeStart then we enter in scan range mode
			APP_RunSpectrum(SCAN_RANGE_MODE);
		}
		else{
			// if we are in channel mode then enter spectrum in channel mode
			// otherwise enter spectrum in frequency mode
			APP_RunSpectrum(IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE));
		}
	#elif
		APP_RunSpectrum();
	#endif
}

void ACTION_SwitchDemodul(void)
{
	gTxVfo->Modulation++;
	if(gTxVfo->Modulation == MODULATION_UKNOWN)
		gTxVfo->Modulation = MODULATION_FM;

	gRequestSaveChannel = 1;
}

BK4819_FilterBandwidth_t ACTION_NextBandwidth(BK4819_FilterBandwidth_t currentBandwidth, const bool dynamic)
{
  BK4819_FilterBandwidth_t nextBandwidth;
  if (currentBandwidth == BK4819_FILTER_BW_NARROWEST)
  {
	nextBandwidth = BK4819_FILTER_BW_WIDE;
  }
  else
  {
	nextBandwidth = currentBandwidth + 1;
  }

  BK4819_SetFilterBandwidth(nextBandwidth, dynamic);
  gRequestSaveChannel = 1;
  return nextBandwidth;
}

#ifdef ENABLE_BLMIN_TMP_OFF
void ACTION_BlminTmpOff(void)
{
	if(++gEeprom.BACKLIGHT_MIN_STAT == BLMIN_STAT_UNKNOWN)
	{
		gEeprom.BACKLIGHT_MIN_STAT = BLMIN_STAT_ON;
		BACKLIGHT_SetBrightness(gEeprom.BACKLIGHT_MIN);
	} else
	{
		BACKLIGHT_SetBrightness(0);
	}
}
#endif

void ACTION_Handle(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	if (gScreenToDisplay == DISPLAY_MAIN && gDTMF_InputMode) // entering DTMF code
	{
		if (Key == KEY_SIDE1 && !bKeyHeld && bKeyPressed) // side1 btn pressed
		{
			gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

			if (gDTMF_InputBox_Index > 0) // DTMF codes are in the input box
			{
				gDTMF_InputBox[--gDTMF_InputBox_Index] = '-'; // delete one code
				if (gDTMF_InputBox_Index > 0)
				{
					gPttWasReleased       = true;
					gRequestDisplayScreen = DISPLAY_MAIN;
					return;
				}
			}

			#ifdef ENABLE_VOICE
				gAnotherVoiceID   = VOICE_ID_CANCEL;
			#endif

			gRequestDisplayScreen = DISPLAY_MAIN;
			gDTMF_InputMode       = false; // turn off DTMF input box if no codes left
		}

		gPttWasReleased = true;
		return;
	}

	uint8_t funcShort = ACTION_OPT_NONE;
	uint8_t funcLong  = ACTION_OPT_NONE;
	switch(Key) {
		case KEY_SIDE1:
			funcShort = gEeprom.KEY_1_SHORT_PRESS_ACTION;
			funcLong  = gEeprom.KEY_1_LONG_PRESS_ACTION;
			break;
		case KEY_SIDE2:
			funcShort = gEeprom.KEY_2_SHORT_PRESS_ACTION;
			funcLong  = gEeprom.KEY_2_LONG_PRESS_ACTION;
			break;
		case KEY_MENU:
			funcLong  = gEeprom.KEY_M_LONG_PRESS_ACTION;
			break;
		default:
			break;
	}

	if (!bKeyHeld && bKeyPressed) // button pushed
	{
		return;
	}

	// held or released beyond this point

	if(!(bKeyHeld && !bKeyPressed)) // don't beep on released after hold
		gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

	if (bKeyHeld || bKeyPressed) // held
	{
		funcShort = funcLong;

		if (!bKeyPressed) //ignore release if held
			return;
	}

	// held or released after short press beyond this point

	switch (funcShort)
	{
		default:
		case ACTION_OPT_NONE:
			break;
		case ACTION_OPT_FLASHLIGHT:
			ACTION_FlashLight();
			break;
		case ACTION_OPT_POWER:
			ACTION_Power();
			break;
		case ACTION_OPT_MONITOR:
			ACTION_Monitor();
			break;
		case ACTION_OPT_SCAN:
			ACTION_Scan(true);
			break;
		case ACTION_OPT_VOX:
#ifdef ENABLE_VOX
			ACTION_Vox();
#endif
			break;
		case ACTION_OPT_ALARM:
#ifdef ENABLE_ALARM
			ACTION_AlarmOr1750(false);
#endif
			break;
#ifdef ENABLE_FMRADIO
		case ACTION_OPT_FM:
			ACTION_FM();
			break;
#endif
		case ACTION_OPT_1750:
			#ifdef ENABLE_TX1750
				ACTION_AlarmOr1750(true);
			#endif
			break;
		case ACTION_OPT_KEYLOCK:
			COMMON_KeypadLockToggle();
			break;
		case ACTION_OPT_A_B:
			COMMON_SwitchVFOs();
			break;
		case ACTION_OPT_VFO_MR:
			COMMON_SwitchVFOMode();
			break;
		case ACTION_OPT_SWITCH_DEMODUL:
			ACTION_SwitchDemodul();
			break;
		case ACTION_OPT_BANDWIDTH:
			gTxVfo->CHANNEL_BANDWIDTH =
				ACTION_NextBandwidth(gTxVfo->CHANNEL_BANDWIDTH, gTxVfo->Modulation != MODULATION_AM);
			break;
		case ACTION_OPT_SPECTRUM:
			ACTION_RunSpectrum();
			break;
#ifdef ENABLE_BLMIN_TMP_OFF
		case ACTION_OPT_BLMIN_TMP_OFF:
			ACTION_BlminTmpOff();
			break;
#endif
	}
}
