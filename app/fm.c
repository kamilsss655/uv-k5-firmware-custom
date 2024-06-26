/* Original work Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Modified work Copyright 2024 kamilsss655
 * https://github.com/kamilsss655
 *
 * Copyright 2024 kamilsss655
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

#ifdef ENABLE_FMRADIO

// #include <string.h>

#include "app/action.h"
#include "app/fm.h"
#include "app/generic.h"
#include "audio.h"
#include "driver/bk1080.h"
#include "misc.h"
#include "settings.h"
#include "ui/ui.h"

const uint16_t FM_RADIO_MAX_FREQ = 1080; // 108.0  Mhz
const uint16_t FM_RADIO_MIN_FREQ = 760;  // 76.0 Mhz

bool              gFmRadioMode;
uint8_t           gFmRadioCountdown_500ms;
volatile uint16_t gFmPlayCountdown_10ms;
volatile int8_t   gFM_ScanState;
uint16_t          gFM_RestoreCountdown_10ms;
uint16_t st;
uint16_t fr;

void FM_TurnOff(void)
{
	gFmRadioMode              = false;
	gFM_ScanState             = FM_SCAN_OFF;
	gFM_RestoreCountdown_10ms = 0;

	AUDIO_AudioPathOff();

	gEnableSpeaker = false;

	BK1080_Init(0, false);

	gUpdateStatus  = true;
}

static void Key_EXIT()
{
	ACTION_FM();
	return;
}

static void Key_UP_DOWN(bool direction)
{
	fr=gEeprom.FM_FrequencyPlaying;
if(direction){
	st=0+1;
}
else{
	st=0-1;
}
fr=fr+st;
if(fr>FM_RADIO_MAX_FREQ){
	fr=FM_RADIO_MIN_FREQ;
}
if(fr<FM_RADIO_MIN_FREQ){
	fr=FM_RADIO_MAX_FREQ;
}
gEeprom.FM_FrequencyPlaying=fr;
gRequestSaveSettings = true;
BK1080_SetFrequency(fr);

//	BK1080_TuneNext(direction);
//	gEeprom.FM_FrequencyPlaying = BK1080_GetFrequency();
//	// save
//	gRequestSaveSettings = true;
}



void FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{	
	uint8_t state = bKeyPressed + 2 * bKeyHeld;
	//uint8_t state = 0;

	if (state > 0) { //NOT == 0
		switch (Key)
		{	
			case KEY_UP:
				Key_UP_DOWN(true);
				break;
			case KEY_DOWN:
				Key_UP_DOWN(false);
				break;
			case KEY_EXIT:
				Key_EXIT();
				break;
			default:
				gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
				break;
		}
	}
}

void FM_Start(void)
{
	gFmRadioMode              = true;
	gFM_ScanState             = FM_SCAN_OFF;
	gFM_RestoreCountdown_10ms = 0;

	gFmPlayCountdown_10ms = 0;
	gScheduleFM           = false;
	gAskToSave            = false;

	BK1080_Init(gEeprom.FM_FrequencyPlaying, true);

	AUDIO_AudioPathOn();

	GUI_SelectNextDisplay(DISPLAY_FM);

	// let the user see DW is not active
	gDualWatchActive     = false;

	gEnableSpeaker       = true;
	gUpdateStatus        = true;
}

#endif
