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
#include <stdlib.h>  // abs()

#include "app/dtmf.h"
#include "app/menu.h"
#include "bitmaps.h"
#include "board.h"
#include "dcs.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"   // EEPROM_ReadBuffer()
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "frequencies.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/menu.h"
#include "ui/ui.h"
#ifdef ENABLE_ENCRYPTION
	#include "helper/crypto.h"
#endif

const t_menu_item MenuList[] =
{
//   text,     voice ID,                               menu ID
	{"Step",   VOICE_ID_FREQUENCY_STEP,                MENU_STEP          },
	{"Bandw",  VOICE_ID_CHANNEL_BANDWIDTH,             MENU_W_N           },
	{"TxPwr",  VOICE_ID_POWER,                         MENU_TXP           }, // was "TXP"
	{"RxDCS",  VOICE_ID_DCS,                           MENU_R_DCS         }, // was "R_DCS"
	{"RxCTCS", VOICE_ID_CTCSS,                         MENU_R_CTCS        }, // was "R_CTCS"
	{"TxDCS",  VOICE_ID_DCS,                           MENU_T_DCS         }, // was "T_DCS"
	{"TxCTCS", VOICE_ID_CTCSS,                         MENU_T_CTCS        }, // was "T_CTCS"
	{"TxODir", VOICE_ID_TX_OFFSET_FREQUENCY_DIRECTION, MENU_SFT_D         }, // was "SFT_D"
	{"TxOffs", VOICE_ID_TX_OFFSET_FREQUENCY,           MENU_OFFSET        }, // was "OFFSET"
	{"RxOffs", VOICE_ID_INVALID,                       MENU_RX_OFFSET     },
	{"Scramb", VOICE_ID_SCRAMBLER_ON,                  MENU_SCR           }, // was "SCR"
	{"BusyCL", VOICE_ID_BUSY_LOCKOUT,                  MENU_BCL           }, // was "BCL"
	{"Compnd", VOICE_ID_INVALID,                       MENU_COMPAND       },
	{"Demodu", VOICE_ID_INVALID,                       MENU_AM            }, // was "AM"
	{"RxAGC",  VOICE_ID_INVALID,                       MENU_RX_AGC        }, // RX Auto Gain Control
	{"ScAdd1", VOICE_ID_INVALID,                       MENU_S_ADD1        },
	{"ScAdd2", VOICE_ID_INVALID,                       MENU_S_ADD2        },
	{"ChSave", VOICE_ID_MEMORY_CHANNEL,                MENU_MEM_CH        }, // was "MEM-CH"
	{"ChDele", VOICE_ID_DELETE_CHANNEL,                MENU_DEL_CH        }, // was "DEL-CH"
	{"ChName", VOICE_ID_INVALID,                       MENU_MEM_NAME      },	
	{"ScnRev", VOICE_ID_INVALID,                       MENU_SC_REV        },
#ifdef ENABLE_NOAA
	{"NOAA-S", VOICE_ID_INVALID,                       MENU_NOAA_S        },
#endif
	{"F1Shrt",    VOICE_ID_INVALID,                    MENU_F1SHRT        },
	{"F1Long",    VOICE_ID_INVALID,                    MENU_F1LONG        },
	{"F2Shrt",    VOICE_ID_INVALID,                    MENU_F2SHRT        },
	{"F2Long",    VOICE_ID_INVALID,                    MENU_F2LONG        },
	{"M Long",    VOICE_ID_INVALID,                    MENU_MLONG         },
	{"KeyLck", VOICE_ID_INVALID,                       MENU_AUTOLK        }, // was "AUTOLk"
	{"TxTOut", VOICE_ID_TRANSMIT_OVER_TIME,            MENU_TOT           }, // was "TOT"
	{"BatSav", VOICE_ID_SAVE_MODE,                     MENU_SAVE          }, // was "SAVE"
	{"Mic",    VOICE_ID_INVALID,                       MENU_MIC           },	
	{"ChDisp", VOICE_ID_INVALID,                       MENU_MDF           }, // was "MDF"
	{"POnMsg", VOICE_ID_INVALID,                       MENU_PONMSG        },
	{"BatTxt", VOICE_ID_INVALID,                       MENU_BAT_TXT       },	
	{"BackLt", VOICE_ID_INVALID,                       MENU_ABR           }, // was "ABR"
	{"BLMin",  VOICE_ID_INVALID,                       MENU_ABR_MIN       },
	{"BLMax",  VOICE_ID_INVALID,                       MENU_ABR_MAX       },
	{"BltTRX", VOICE_ID_INVALID,                       MENU_ABR_ON_TX_RX  },
	{"Beep",   VOICE_ID_BEEP_PROMPT,                   MENU_BEEP          },
#ifdef ENABLE_VOICE
	{"Voice",  VOICE_ID_VOICE_PROMPT,                  MENU_VOICE         },
#endif
	{"Roger",  VOICE_ID_INVALID,                       MENU_ROGER         },
	{"SqTone", VOICE_ID_INVALID,                       MENU_SQL_TONE      }, // squelch tail tone used for RX/TX
	{"1 Call", VOICE_ID_INVALID,                       MENU_1_CALL        },
#ifdef ENABLE_ALARM
	{"AlarmT", VOICE_ID_INVALID,                       MENU_AL_MOD        },
#endif
#ifdef ENABLE_DTMF_CALLING
	{"ANI ID", VOICE_ID_ANI_CODE,                      MENU_ANI_ID        },
	{"UPCode", VOICE_ID_INVALID,                       MENU_UPCODE        },
	{"DWCode", VOICE_ID_INVALID,                       MENU_DWCODE        },
	{"PTT ID", VOICE_ID_INVALID,                       MENU_PTT_ID        },
	{"D ST",   VOICE_ID_INVALID,                       MENU_D_ST          },
    {"D Resp", VOICE_ID_INVALID,                       MENU_D_RSP         },
	{"D Hold", VOICE_ID_INVALID,                       MENU_D_HOLD        },
	{"D Prel", VOICE_ID_INVALID,                       MENU_D_PRE         },
	{"D Decd", VOICE_ID_INVALID,                       MENU_D_DCD         },
	{"D List", VOICE_ID_INVALID,                       MENU_D_LIST        },
#endif
	{"D Live", VOICE_ID_INVALID,                       MENU_D_LIVE_DEC    }, // live DTMF decoder
#ifdef ENABLE_VOX
	{"VOXSen", VOICE_ID_VOX,                           MENU_VOX           }, // VOX Sensibility or OFF
	{"VOXDel", VOICE_ID_VOX,                           MENU_VOX_DELAY     }, // VOX delay
#endif
	{"BatVol", VOICE_ID_INVALID,                       MENU_VOL           }, // was "VOL"
	{"RxMode", VOICE_ID_DUAL_STANDBY,                  MENU_TDR           },
#ifdef ENABLE_PWRON_PASSWORD
	{"Passwd", VOICE_ID_INVALID,                       MENU_PASSWORD      }, // power on password
#endif
#ifdef ENABLE_ENCRYPTION
	{"EncKey", VOICE_ID_INVALID,                       MENU_ENC_KEY       }, // encryption key
	{"MsgEnc", VOICE_ID_INVALID,                       MENU_MSG_ENC       }, // messenger encrypt outgoing messages
#endif
#ifdef ENABLE_MESSENGER
	{"MsgRx",  VOICE_ID_INVALID,                       MENU_MSG_RX        }, // messenger rx
	{"MsgAck", VOICE_ID_INVALID,                       MENU_MSG_ACK       }, // messenger respond ACK
	{"MsgMod", VOICE_ID_INVALID,                       MENU_MSG_MODULATION}, // messenger modulation
#endif
	{"Sql",    VOICE_ID_SQUELCH,                       MENU_SQL           },
	// hidden menu items from here on
	// enabled if pressing both the PTT and upper side button at power-on
	{"F Lock", VOICE_ID_INVALID,                       MENU_F_LOCK        },
	{"Tx 200", VOICE_ID_INVALID,                       MENU_200TX         }, // was "200TX"
	{"Tx 350", VOICE_ID_INVALID,                       MENU_350TX         }, // was "350TX"
	{"Tx 500", VOICE_ID_INVALID,                       MENU_500TX         }, // was "500TX"
	{"350 En", VOICE_ID_INVALID,                       MENU_350EN         }, // was "350EN"
	{"ScraEn", VOICE_ID_INVALID,                       MENU_SCREN         }, // was "SCREN"
#ifdef ENABLE_F_CAL_MENU
	{"FrCali", VOICE_ID_INVALID,                       MENU_F_CALI        }, // reference xtal calibration
#endif
	{"BatCal", VOICE_ID_INVALID,                       MENU_BATCAL        }, // battery voltage calibration
	{"BatTyp", VOICE_ID_INVALID,                       MENU_BATTYP        }, // battery type 1600/2200mAh
	{"Reset",  VOICE_ID_INITIALISATION,                MENU_RESET         }, // might be better to move this to the hidden menu items ?

	{"",       VOICE_ID_INVALID,                       0xff               }  // end of list - DO NOT delete or move this this
};

const int CHANNEL_ONLY_SETTINGS[] = {
	MENU_S_ADD1,
	MENU_S_ADD2,
	MENU_DEL_CH,
	MENU_MEM_NAME,
	MENU_1_CALL
};

const int VFO_ONLY_SETTINGS[] = {};

const uint8_t FIRST_HIDDEN_MENU_ITEM = MENU_F_LOCK;

const char gSubMenu_TXP[][5] =
{
	"LOW",
	"MID",
	"HIGH"
};

const char gSubMenu_SFT_D[][4] =
{
	"OFF",
	"+",
	"-"
};

const char gSubMenu_OFF_ON[][4] =
{
	"OFF",
	"ON"
};

const char gSubMenu_SAVE[][4] =
{
	"OFF",
	"50%",
	"67%",
	"75%",
	"80%"
};

#ifdef ENABLE_MESSENGER
	const char gSubMenu_MSG_MODULATION[][10] =
	{
		"FSK 450",
		"FSK 700",
		"AFSK 1.2K"
	};
#endif

const char gSubMenu_RX_AGC[][6] =
{
	"OFF",
	"SLOW",
	"FAST"
};

const char gSubMenu_TOT[][7] =
{
	"30 sec",
	"1 min",
	"2 min",
	"3 min",
	"4 min",
	"5 min",
	"6 min",
	"7 min",
	"8 min",
	"9 min",
	"15 min"
};

const char* const gSubMenu_RXMode[] =
{
	"MAIN\nONLY", 		// TX and RX on main only
	"DUAL RX\nRESPOND", // Watch both and respond
	"CROSS\nBAND", 		// TX on main, RX on secondary
	"MAIN TX\nDUAL RX" 	// always TX on main, but RX on both
};

#ifdef ENABLE_VOICE
	const char gSubMenu_VOICE[][4] =
	{
		"OFF",
		"CHI",
		"ENG"
	};
#endif

const char gSubMenu_SC_REV[][8] =
{
	"TIMEOUT",
	"CARRIER",
	"STOP"
};

const char* const gSubMenu_MDF[] =
{
	"FREQ",
	"CHANNEL\nNUMBER",
	"NAME",
	"NAME\n+\nFREQ"
};

#ifdef ENABLE_ALARM
	const char gSubMenu_AL_MOD[][5] =
	{
		"SITE",
		"TONE"
	};
#endif

#ifdef ENABLE_DTMF_CALLING
const char gSubMenu_D_RSP[][11] =
{
	"DO\nNOTHING",
	"RING",
	"REPLY",
	"BOTH"
};
#endif

const char* const gSubMenu_PTT_ID[] =
{
	"OFF",
	"UP CODE",
	"DOWN CODE",
	"UP+DOWN\nCODE",
	"APOLLO\nQUINDAR"
};

const char gSubMenu_PONMSG[][8] =
{
	"FULL",
	"MESSAGE",
	"VOLTAGE",
	"NONE"
};

const char gSubMenu_ROGER[][6] =
{
	"OFF",
	"ROGER",
	"MDC"
};

const char gSubMenu_RESET[][4] =
{
	"VFO",
	"ALL"
};

const char * const gSubMenu_F_LOCK[] =
{
	"UNLOCK\nALL\nFREQ",
	"FCC HAM\n144-148\n420-450",
	"CE HAM\n144-146\n430-440",
	"GB HAM\n144-148\n430-440",
	"137-174\n400-430",
	"137-174\n400-438",
	"PMR446",
	"DISABLE\nALL"
};

const char gSubMenu_BACKLIGHT[][7] =
{
	"OFF",
	"5 sec",
	"10 sec",
	"20 sec",
	"1 min",
	"2 min",
	"4 min",
	"ON"
};

const char gSubMenu_RX_TX[][6] =
{
	"OFF",
	"TX",
	"RX",
	"TX/RX"
};

const char gSubMenu_BAT_TXT[][8] =
{
	"NONE",
	"VOLTAGE",
	"PERCENT"
};

const char gSubMenu_BATTYP[][9] =
{
	"1600mAh",
	"2200mAh"
};

const char gSubMenu_SCRAMBLER[][7] =
{
	"OFF",
	"2600Hz",
	"2700Hz",
	"2800Hz",
	"2900Hz",
	"3000Hz",
	"3100Hz",
	"3200Hz",
	"3300Hz",
	"3400Hz",
	"3500Hz"
};

const t_sidefunction SIDEFUNCTIONS[] =
{
	{"NONE",			ACTION_OPT_NONE},
	{"FLASH\nLIGHT",	ACTION_OPT_FLASHLIGHT},
	{"POWER",			ACTION_OPT_POWER},
	{"MONITOR",			ACTION_OPT_MONITOR},
	{"SCAN",			ACTION_OPT_SCAN},
#ifdef ENABLE_VOX
	{"VOX",				ACTION_OPT_VOX},
#endif
#ifdef ENABLE_ALARM	
	{"ALARM",			ACTION_OPT_ALARM},
#endif
#ifdef ENABLE_FMRADIO
	{"FM RADIO",		ACTION_OPT_FM},
#endif	
#ifdef ENABLE_TX1750	
	{"1750HZ",			ACTION_OPT_1750},
#endif
	{"LOCK\nKEYPAD",	ACTION_OPT_KEYLOCK},
	{"SWITCH\nVFO",		ACTION_OPT_A_B},
	{"VFO/MR",			ACTION_OPT_VFO_MR},
	{"SWITCH\nDEMODUL",	ACTION_OPT_SWITCH_DEMODUL},
	{"SWITCH\nBANDWID",	ACTION_OPT_BANDWIDTH},
	{"SPECTRUM",		ACTION_OPT_SPECTRUM},
#ifdef ENABLE_BLMIN_TMP_OFF
	{"BLMIN\nTMP OFF",  ACTION_OPT_BLMIN_TMP_OFF}, 		//BackLight Minimum Temporay OFF
#endif
};
const t_sidefunction* gSubMenu_SIDEFUNCTIONS = SIDEFUNCTIONS;
const uint8_t gSubMenu_SIDEFUNCTIONS_size = ARRAY_SIZE(SIDEFUNCTIONS);
void MENU_PrintNotAllowed();

const unsigned int menu_list_width = 6; // max no. of characters on the menu list (left side)
const unsigned int menu_item_x1    = (8 * menu_list_width) + 2;
const unsigned int menu_item_x2    = LCD_WIDTH - 1;

bool    gIsInSubMenu;
uint8_t gMenuCursor;
int UI_MENU_GetCurrentMenuId() {
	if(gMenuCursor < ARRAY_SIZE(MenuList))
		return MenuList[gMenuCursor].menu_id;
	else
		return MenuList[ARRAY_SIZE(MenuList)-1].menu_id;
}

uint8_t UI_MENU_GetMenuIdx(uint8_t id)
{
	for(uint8_t i = 0; i < ARRAY_SIZE(MenuList); i++)
		if(MenuList[i].menu_id == id)
			return i;
	return 0;
}

int32_t gSubMenuSelection;

// edit box
char    edit_original[17]; // a copy of the text before editing so that we can easily test for changes/difference
char    edit[17];
int     edit_index;

void UI_DisplayMenu(void)
{
	unsigned int       i;
	char               String[64];  // bigger cuz we can now do multi-line in one string (use '\n' char)

#ifdef ENABLE_DTMF_CALLING
	char               Contact[16];
#endif

	// clear the screen buffer
	memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

	#if 0
		// original menu layout

		for (i = 0; i < 3; i++)
			if (gMenuCursor > 0 || i > 0)
				if ((gMenuListCount - 1) != gMenuCursor || i != 2)
					UI_PrintString(MenuList[gMenuCursor + i - 1].name, 0, 0, i * 2, 8);

		// invert the current menu list item pixels
		for (i = 0; i < (8 * menu_list_width); i++)
		{
			gFrameBuffer[2][i] ^= 0xFF;
			gFrameBuffer[3][i] ^= 0xFF;
		}

		// draw vertical separating dotted line
		for (i = 0; i < 7; i++)
			gFrameBuffer[i][(8 * menu_list_width) + 1] = 0xAA;

		// draw the little sub-menu triangle marker
		if (gIsInSubMenu)
			memmove(gFrameBuffer[0] + (8 * menu_list_width) + 1, BITMAP_CurrentIndicator, sizeof(BITMAP_CurrentIndicator));

		// draw the menu index number/count
		sprintf(String, "%2u.%u", 1 + gMenuCursor, gMenuListCount);
		UI_PrintStringSmall(String, 2, 0, 6);

	#else
	{	// new menu layout .. experimental & unfinished

		const int menu_index = gMenuCursor;  // current selected menu item
		i = 1;

		if (!gIsInSubMenu)
		{
			while (i < 2)
			{	// leading menu items - small text
				const int k = menu_index + i - 2;
				if (k < 0)
					UI_PrintStringSmall(MenuList[gMenuListCount + k].name, 0, 0, i);  // wrap-a-round
				else
				if (k >= 0 && k < (int)gMenuListCount)
					UI_PrintStringSmall(MenuList[k].name, 0, 0, i);
				i++;
			}

			// current menu item - keep big n fat
			if (menu_index >= 0 && menu_index < (int)gMenuListCount)
				UI_PrintString(MenuList[menu_index].name, 0, 0, 2, 8);
			i++;

			while (i < 4)
			{	// trailing menu item - small text
				const int k = menu_index + i - 2;
				if (k >= 0 && k < (int)gMenuListCount)
					UI_PrintStringSmall(MenuList[k].name, 0, 0, 1 + i);
				else
				if (k >= (int)gMenuListCount)
					UI_PrintStringSmall(MenuList[gMenuListCount - k].name, 0, 0, 1 + i);  // wrap-a-round
				i++;
			}

			// draw the menu index number/count
			sprintf(String, "%2u.%u", 1 + gMenuCursor, gMenuListCount);
			UI_PrintStringSmall(String, 2, 0, 6);
		}
		else
		if (menu_index >= 0 && menu_index < (int)gMenuListCount)
		{	// current menu item
			strcpy(String, MenuList[menu_index].name);
//			strcat(String, ":");
			UI_PrintString(String, 0, 0, 0, 8);
//			UI_PrintStringSmall(String, 0, 0, 0);
		}
	}
	#endif

	// **************

	memset(String, 0, sizeof(String));

	bool already_printed = false;

	// Return if we are not allowed to edit
	if(UI_MENU_IsAllowedToEdit(UI_MENU_GetCurrentMenuId())==false)
	{
		MENU_PrintNotAllowed();
		already_printed = true;
	}
	else
	{
		switch (UI_MENU_GetCurrentMenuId())
		{
			case MENU_SQL:
				sprintf(String, "%d", gSubMenuSelection);
				break;

			case MENU_MIC:
				{	// display the mic gain in actual dB rather than just an index number
					const uint8_t mic = gMicGain_dB2[gSubMenuSelection];
					sprintf(String, "+%u.%01udB", mic / 2, mic % 2);
				}
				break;

			case MENU_STEP: {
				uint16_t step = gStepFrequencyTable[FREQUENCY_GetStepIdxFromSortedIdx(gSubMenuSelection)];
				sprintf(String, "%d.%02ukHz", step / 100, step % 100);
				break;
			}

			case MENU_TXP:
				strcpy(String, gSubMenu_TXP[gSubMenuSelection]);
				break;

			case MENU_R_DCS:
			case MENU_T_DCS:
				if (gSubMenuSelection == 0)
					strcpy(String, "OFF");
				else if (gSubMenuSelection < 105)
					sprintf(String, "D%03oN", DCS_Options[gSubMenuSelection -   1]);
				else
					sprintf(String, "D%03oI", DCS_Options[gSubMenuSelection - 105]);
				break;

			case MENU_R_CTCS:
			case MENU_T_CTCS:
			{
				if (gSubMenuSelection == 0)
					strcpy(String, "OFF");
				else
					sprintf(String, "%u.%uHz", CTCSS_Options[gSubMenuSelection - 1] / 10, CTCSS_Options[gSubMenuSelection - 1] % 10);
				break;
			}

			case MENU_SQL_TONE:
				sprintf(String, "%u.%uHz", CTCSS_Options[gSubMenuSelection] / 10, CTCSS_Options[gSubMenuSelection] % 10);
				break;

			case MENU_SFT_D:
				strcpy(String, gSubMenu_SFT_D[gSubMenuSelection]);
				break;

			case MENU_OFFSET:
			case MENU_RX_OFFSET:
				if (!gIsInSubMenu || gInputBoxIndex == 0)
				{
					sprintf(String, "%3d.%05u", gSubMenuSelection / 100000, abs(gSubMenuSelection) % 100000);
					UI_PrintString(String, menu_item_x1, menu_item_x2, 1, 8);
				}
				else
				{
					const char * ascii = INPUTBOX_GetAscii();
					sprintf(String, "%.3s.%.3s  ",ascii, ascii + 3);
					UI_PrintString(String, menu_item_x1, menu_item_x2, 1, 8);
				}

				UI_PrintString("MHz",  menu_item_x1, menu_item_x2, 3, 8);

				already_printed = true;
				break;
			#ifdef ENABLE_PWRON_PASSWORD
				case MENU_PASSWORD:
					if (!gIsInSubMenu || gInputBoxIndex == 0)
					{
						if((unsigned int)gSubMenuSelection >= PASSWORD_OFF)
						{
							strcpy(String, "OFF");
						}
						else
						{
							strcpy(String, "****");
						}
					}
					else
					{
						const char * ascii = INPUTBOX_GetAscii();
						sprintf(String, "%.4s  ",ascii);
					}
					break;
			#endif

			case MENU_W_N:
				strcpy(String, bwNames[gSubMenuSelection]);
				break;

			case MENU_SCR:
				strcpy(String, gSubMenu_SCRAMBLER[gSubMenuSelection]);
				#if 1
					if (gSubMenuSelection > 0 && gSetting_ScrambleEnable)
						BK4819_EnableScramble(gSubMenuSelection - 1);
					else
						BK4819_DisableScramble();
				#endif
				break;

			#ifdef ENABLE_VOX
				case MENU_VOX:
					if (gSubMenuSelection == 0)
						strcpy(String, "OFF");
					else
						sprintf(String, "%d", gSubMenuSelection);
					break;
				case MENU_VOX_DELAY:
					sprintf(String, "%d", gSubMenuSelection);
					break;
			#endif

			case MENU_ABR:
				strcpy(String, gSubMenu_BACKLIGHT[gSubMenuSelection]);
				break;

			case MENU_ABR_MIN:
			case MENU_ABR_MAX:
				sprintf(String, "%d", gSubMenuSelection);
				if(gIsInSubMenu)
					BACKLIGHT_SetBrightness(gSubMenuSelection);
				break;	

			case MENU_AM:
				strcpy(String, gModulationStr[gSubMenuSelection]);
				break;
			
			case MENU_RX_AGC:
				strcpy(String, gSubMenu_RX_AGC[gSubMenuSelection]);
				break;

			case MENU_AUTOLK:
				strcpy(String, (gSubMenuSelection == 0) ? "OFF" : "AUTO");
				break;

			case MENU_COMPAND:
			case MENU_ABR_ON_TX_RX:
				strcpy(String, gSubMenu_RX_TX[gSubMenuSelection]);
				break;

			case MENU_BCL:
			case MENU_BEEP:
			case MENU_S_ADD1:
			case MENU_S_ADD2:
			case MENU_D_ST:
	#ifdef ENABLE_DTMF_CALLING
			case MENU_D_DCD:
	#endif
			case MENU_D_LIVE_DEC:
			#ifdef ENABLE_NOAA
				case MENU_NOAA_S:
			#endif
			#ifdef ENABLE_ENCRYPTION
				case MENU_MSG_ENC:
			#endif
			#ifdef ENABLE_MESSENGER
				case MENU_MSG_RX:
				case MENU_MSG_ACK:
			#endif
			case MENU_350TX:
			case MENU_200TX:
			case MENU_500TX:
			case MENU_350EN:
			case MENU_SCREN:
				strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
				break;
			case MENU_MEM_CH:
			case MENU_1_CALL:
			case MENU_DEL_CH:
			{
				UI_GenerateChannelStringEx(String, 1, gSubMenuSelection);
				UI_PrintString(String, menu_item_x1, menu_item_x2, 0, 8);

				SETTINGS_FetchChannelName(String, gSubMenuSelection);
				if (String[0] == 0)
					strcpy(String, "--");
				UI_PrintString(String, menu_item_x1, menu_item_x2, 2, 8);

				if (!gAskForConfirmation)
				{	// show the frequency so that the user knows the channels frequency
					const uint32_t frequency = BOARD_fetchChannelFrequency(gSubMenuSelection);
					sprintf(String, "%u.%05u", frequency / 100000, frequency % 100000);
					UI_PrintString(String, menu_item_x1, menu_item_x2, 4, 8);
				}

				already_printed = true;
				break;
			}

			case MENU_MEM_NAME:
			{
				UI_GenerateChannelStringEx(String, 1, gSubMenuSelection);
				UI_PrintString(String, menu_item_x1, menu_item_x2, 0, 8);


				const uint32_t frequency = BOARD_fetchChannelFrequency(gSubMenuSelection);

				if (!gIsInSubMenu || edit_index < 0)
				{	// show the channel name
					SETTINGS_FetchChannelName(String, gSubMenuSelection);
					if (String[0] == 0)
						strcpy(String, "--");
					UI_PrintString(String, menu_item_x1, menu_item_x2, 2, 8);
				}
				else
				{	// show the channel name being edited
					UI_PrintString(edit, menu_item_x1, 0, 2, 8);
					if (edit_index < 10)
						UI_PrintString(     "^", menu_item_x1 + (8 * edit_index), 0, 4, 8);  // show the cursor
				}

				if (!gAskForConfirmation)
				{	// show the frequency so that the user knows the channels frequency
					sprintf(String, "%u.%05u", frequency / 100000, frequency % 100000);
					if (!gIsInSubMenu || edit_index < 0)
						UI_PrintString(String, menu_item_x1, menu_item_x2, 4, 8);
					else
						UI_PrintString(String, menu_item_x1, menu_item_x2, 5, 8);
				}

				already_printed = true;
				break;
			}
			#ifdef ENABLE_ENCRYPTION
				case MENU_ENC_KEY:
				{
					if (!gIsInSubMenu)
					{	// show placeholder in main menu
						strcpy(String, "****");
						UI_PrintString(String, menu_item_x1, menu_item_x2, 2, 8);
					}
					else
					{	// show the key being edited
						if (edit_index != -1 || gAskForConfirmation) {
							UI_PrintString(edit, (menu_item_x1 -2), 0, 2, 8);
							// show the cursor
							if(edit_index < 10)
								UI_PrintString(     "^", (menu_item_x1 -2) + (8 * edit_index), 0, 4, 8);  
						}
						else{
							strcpy(String, "hashed value");
							UI_PrintStringSmall(String, 20, 0, 5);

							memset(String, 0, sizeof(String));
							
							CRYPTO_DisplayHash(gEeprom.ENC_KEY, String, sizeof(gEeprom.ENC_KEY));
							UI_PrintString(String, (menu_item_x1 -2), 0, 2, 8);
						}			
					}

					already_printed = true;
					break;
				}
			#endif

			#ifdef ENABLE_MESSENGER
				case MENU_MSG_MODULATION:
					strcpy(String, gSubMenu_MSG_MODULATION[gSubMenuSelection]);
					break;
			#endif

			case MENU_SAVE:
				strcpy(String, gSubMenu_SAVE[gSubMenuSelection]);
				break;

			case MENU_TDR:
				strcpy(String, gSubMenu_RXMode[gSubMenuSelection]);
				break;

			case MENU_TOT:
				strcpy(String, gSubMenu_TOT[gSubMenuSelection]);
				break;

			#ifdef ENABLE_VOICE
				case MENU_VOICE:
					strcpy(String, gSubMenu_VOICE[gSubMenuSelection]);
					break;
			#endif

			case MENU_SC_REV:
				strcpy(String, gSubMenu_SC_REV[gSubMenuSelection]);
				break;

			case MENU_MDF:
				strcpy(String, gSubMenu_MDF[gSubMenuSelection]);
				break;

			#ifdef ENABLE_ALARM
				case MENU_AL_MOD:
					sprintf(String, gSubMenu_AL_MOD[gSubMenuSelection]);
					break;
			#endif

	#ifdef ENABLE_DTMF_CALLING
			case MENU_ANI_ID:
				strcpy(String, gEeprom.ANI_DTMF_ID);
				break;
	#endif
			case MENU_UPCODE:
				strcpy(String, gEeprom.DTMF_UP_CODE);
				break;

			case MENU_DWCODE:
				strcpy(String, gEeprom.DTMF_DOWN_CODE);
				break;

	#ifdef ENABLE_DTMF_CALLING
			case MENU_D_RSP:
				strcpy(String, gSubMenu_D_RSP[gSubMenuSelection]);
				break;

			case MENU_D_HOLD:
				sprintf(String, "%ds", gSubMenuSelection);
				break;
	#endif
			case MENU_D_PRE:
				sprintf(String, "%d*10ms", gSubMenuSelection);
				break;

			case MENU_PTT_ID:
				strcpy(String, gSubMenu_PTT_ID[gSubMenuSelection]);
				break;

			case MENU_BAT_TXT:
				strcpy(String, gSubMenu_BAT_TXT[gSubMenuSelection]);
				break;

	#ifdef ENABLE_DTMF_CALLING
			case MENU_D_LIST:
				gIsDtmfContactValid = DTMF_GetContact((int)gSubMenuSelection - 1, Contact);
				if (!gIsDtmfContactValid)
					strcpy(String, "NULL");
				else
					memmove(String, Contact, 8);
				break;
	#endif

			case MENU_PONMSG:
				strcpy(String, gSubMenu_PONMSG[gSubMenuSelection]);
				break;

			case MENU_ROGER:
				strcpy(String, gSubMenu_ROGER[gSubMenuSelection]);
				break;

			case MENU_VOL:
				sprintf(String, "%u.%02uV\n%u%%",
					gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100,
					BATTERY_VoltsToPercent(gBatteryVoltageAverage));
				break;

			case MENU_RESET:
				strcpy(String, gSubMenu_RESET[gSubMenuSelection]);
				break;

			case MENU_F_LOCK:
				strcpy(String, gSubMenu_F_LOCK[gSubMenuSelection]);
				break;

			#ifdef ENABLE_F_CAL_MENU
				case MENU_F_CALI:
					{
						const uint32_t value   = 22656 + gSubMenuSelection;
						const uint32_t xtal_Hz = (0x4f0000u + value) * 5;

						writeXtalFreqCal(gSubMenuSelection, false);

						sprintf(String, "%d\n%u.%06u\nMHz",
							gSubMenuSelection,
							xtal_Hz / 1000000, xtal_Hz % 1000000);
					}
					break;
			#endif

			case MENU_BATCAL:
			{
				const uint16_t vol = (uint32_t)gBatteryVoltageAverage * gBatteryCalibration[3] / gSubMenuSelection;
				sprintf(String, "%u.%02uV\n%u", vol / 100, vol % 100, gSubMenuSelection);
				break;
			}

			case MENU_BATTYP:
				strcpy(String, gSubMenu_BATTYP[gSubMenuSelection]);
				break;	

			case MENU_F1SHRT:
			case MENU_F1LONG:
			case MENU_F2SHRT:
			case MENU_F2LONG:
			case MENU_MLONG:
				strcpy(String, gSubMenu_SIDEFUNCTIONS[gSubMenuSelection].name);
				break;

		}
	}

	if (!already_printed)
	{	// we now do multi-line text in a single string

		unsigned int y;
		unsigned int lines = 1;
		unsigned int len   = strlen(String);
		bool         small = false;

		if (len > 0)
		{
			// count number of lines
			for (i = 0; i < len; i++)
			{
				if (String[i] == '\n' && i < (len - 1))
				{	// found new line char
					lines++;
					String[i] = 0;  // null terminate the line
				}
			}

			if (lines > 3)
			{	// use small text
				small = true;
				if (lines > 7)
					lines = 7;
			}

			// center vertically'ish
			if (small)
				y = 3 - ((lines + 0) / 2);  // untested
			else
				y = 2 - ((lines + 0) / 2);

			// draw the text lines
			for (i = 0; i < len && lines > 0; lines--)
			{
				if (small)
					UI_PrintStringSmall(String + i, menu_item_x1, menu_item_x2, y);
				else
					UI_PrintString(String + i, menu_item_x1, menu_item_x2, y, 8);

				// look for start of next line
				while (i < len && String[i] >= 32)
					i++;

				// hop over the null term char(s)
				while (i < len && String[i] < 32)
					i++;

				y += small ? 1 : 2;
			}
		}
	}

	if ((UI_MENU_GetCurrentMenuId() == MENU_R_CTCS || UI_MENU_GetCurrentMenuId() == MENU_R_DCS) && gCssBackgroundScan)
		UI_PrintString("SCAN", menu_item_x1, menu_item_x2, 4, 8);
		

	if (UI_MENU_GetCurrentMenuId() == MENU_UPCODE)
		if (strlen(gEeprom.DTMF_UP_CODE) > 8)
			UI_PrintString(gEeprom.DTMF_UP_CODE + 8, menu_item_x1, menu_item_x2, 4, 8);

	if (UI_MENU_GetCurrentMenuId() == MENU_DWCODE)
		if (strlen(gEeprom.DTMF_DOWN_CODE) > 8)
			UI_PrintString(gEeprom.DTMF_DOWN_CODE + 8, menu_item_x1, menu_item_x2, 4, 8);

#ifdef ENABLE_DTMF_CALLING
	if (UI_MENU_GetCurrentMenuId() == MENU_D_LIST && gIsDtmfContactValid)
	{
		Contact[11] = 0;
		memmove(&gDTMF_ID, Contact + 8, 4);
		sprintf(String, "ID:%s", Contact + 8);
		UI_PrintString(String, menu_item_x1, menu_item_x2, 4, 8);
	}
#endif

	if (UI_MENU_GetCurrentMenuId() == MENU_R_CTCS ||
	    UI_MENU_GetCurrentMenuId() == MENU_T_CTCS ||
	    UI_MENU_GetCurrentMenuId() == MENU_R_DCS  ||
	    UI_MENU_GetCurrentMenuId() == MENU_T_DCS  ||
		UI_MENU_GetCurrentMenuId() == MENU_SQL_TONE
#ifdef ENABLE_DTMF_CALLING
	    || UI_MENU_GetCurrentMenuId() == MENU_D_LIST
#endif		
		)

	{
		sprintf(String, "%2d", gSubMenuSelection);
		UI_PrintStringSmall(String, 105, 0, 0);
	}

	if ((UI_MENU_GetCurrentMenuId() == MENU_RESET    ||
	     UI_MENU_GetCurrentMenuId() == MENU_MEM_CH   ||
		 #ifdef ENABLE_ENCRYPTION
			UI_MENU_GetCurrentMenuId() == MENU_ENC_KEY  ||
		 #endif
	     UI_MENU_GetCurrentMenuId() == MENU_MEM_NAME ||
	     UI_MENU_GetCurrentMenuId() == MENU_DEL_CH) && gAskForConfirmation)
	{	// display confirmation
		strcpy(String, (gAskForConfirmation == 1) ? "SURE?" : "WAIT!");
		UI_PrintString(String, menu_item_x1, menu_item_x2, 5, 8);
	}

	ST7565_BlitFullScreen();
}

void MENU_PrintNotAllowed()
{
	char String[8];
	strcpy(String, "NOT");
	UI_PrintString(String, menu_item_x1, menu_item_x2, 0, 8);
	strcpy(String, "ALLOWED");
	UI_PrintString(String, menu_item_x1, menu_item_x2, 2, 8);
}

bool UI_MENU_IsAllowedToEdit(int menu_id)
{
	bool isChannelOnlySetting;
	bool isVfoOnlySetting;
	
	isChannelOnlySetting =
		IsValueInArray(menu_id, CHANNEL_ONLY_SETTINGS, sizeof(CHANNEL_ONLY_SETTINGS));
	isVfoOnlySetting =
		IsValueInArray(menu_id, VFO_ONLY_SETTINGS, sizeof(VFO_ONLY_SETTINGS));

	if (isChannelOnlySetting && !IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
	{
		return false;
	}
	// if we are in channel mode and 
	else if (isVfoOnlySetting && IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
	{
		return false;
	}
	// otherwise we can edit this field
	else
	{
		return true;
	}
	
}