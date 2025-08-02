/* Copyright 2023 fagci
 * https://github.com/fagci
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
#include "app/spectrum.h"

#ifdef ENABLE_SCAN_RANGES
#include "chFrScanner.h"
#endif

#include "driver/backlight.h"
#include "audio.h"
#include "ui/helper.h"
#ifdef ENABLE_SPECTRUM_COPY_VFO
  #include "common.h"
#endif
#include "action.h"

struct FrequencyBandInfo {
    uint32_t lower;
    uint32_t upper;
    uint32_t middle;
};

bool gTailFound;
bool isBlacklistApplied;

#define F_MAX frequencyBandTable[ARRAY_SIZE(frequencyBandTable) - 1].upper

#ifdef ENABLE_SPECTRUM_CHANNEL_SCAN
  Mode appMode;
  //Idea - make this user adjustable to compensate for different antennas, frontends, conditions
  #define UHF_NOISE_FLOOR 40
  #define MAX_ATTENUATION 160
  #define ATTENUATE_STEP  10
  bool    isNormalizationApplied;
  bool    isAttenuationApplied;
  uint8_t  gainOffset[129];
  uint8_t  attenuationOffset[129];
  uint8_t scanChannel[MR_CHANNEL_LAST+3];
  uint8_t scanChannelsCount;
  void ToggleScanList();
  void AutoAdjustResolution();
  void ToggleNormalizeRssi(bool on);
  void Attenuate(uint8_t amount);
#endif

const uint16_t RSSI_MAX_VALUE = 65535;

#define SQUELCH_OFF_DELAY 10

static uint16_t R30, R37, R3D, R43, R47, R48, R7E, R02, R3F;
static uint32_t initialFreq;
static char String[32];

#ifdef ENABLE_SPECTRUM_SHOW_CHANNEL_NAME
  uint32_t lastPeakFrequency;
  bool     isKnownChannel = false;
  int      channel;
  char     channelName[12];
  void     LoadValidMemoryChannels();
#endif

bool isInitialized = false;
bool isListening = true;
bool monitorMode = false;
bool redrawStatus = true;
bool redrawScreen = false;
bool newScanStart = true;
bool preventKeypress = true;
bool audioState = true;

State currentState = SPECTRUM, previousState = SPECTRUM;

PeakInfo peak;
ScanInfo scanInfo;
KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};

#ifdef ENABLE_SCAN_RANGES
#define BLACKLIST_SIZE 200
static uint16_t blacklistFreqs[BLACKLIST_SIZE];
static uint8_t blacklistFreqsIdx;
static bool IsBlacklisted(uint16_t idx);
static uint8_t CurrentScanIndex();
#endif

const char *bwOptions[] = {"  25k", "12.5k", "6.25k"};
const char *scanListOptions[] = {"SL I", "SL II", "ALL"};
const uint8_t modulationTypeTuneSteps[] = {100, 50, 10};
const uint8_t modTypeReg47Values[] = {1, 7, 5};

SpectrumSettings settings = {stepsCount: STEPS_128,
                             scanStepIndex: S_STEP_25_0kHz,
                             frequencyChangeStep: 80000,
                             rssiTriggerLevel: 150,
                             backlightAlwaysOn: false,
                             bw: BK4819_FILTER_BW_WIDE,
                             listenBw: BK4819_FILTER_BW_WIDE,
                             modulationType: false,
                             dbMin: -130,
                             dbMax: -50,
                             scanList: S_SCAN_LIST_ALL};

uint32_t fMeasure = 0;
uint32_t currentFreq, tempFreq;
uint16_t rssiHistory[128];

uint8_t freqInputIndex = 0;
uint8_t freqInputDotIndex = 0;
KEY_Code_t freqInputArr[10];
char freqInputString[11];

uint8_t menuState = 0;
uint16_t listenT = 0;

RegisterSpec registerSpecs[] = {
    {},
    {"LNAs", BK4819_REG_13, 8, 0b11,  1},
    {"LNA",  BK4819_REG_13, 5, 0b111, 1},
    {"PGA",  BK4819_REG_13, 0, 0b111, 1},
    {"MIX",  BK4819_REG_13, 3, 0b11,  1},
    // {"IF", BK4819_REG_3D, 0, 0xFFFF, 0x2aaa},
};

uint16_t statuslineUpdateTimer = 0;

static void RelaunchScan();
static void CheckIfTailFound();
static void ResetInterrupts();

static uint16_t GetRegMenuValue(uint8_t st) {
  RegisterSpec s = registerSpecs[st];
  return (BK4819_ReadRegister(s.num) >> s.offset) & s.mask;
}

static void SetRegMenuValue(uint8_t st, bool add) {
  uint16_t v = GetRegMenuValue(st);
  RegisterSpec s = registerSpecs[st];

  uint16_t reg = BK4819_ReadRegister(s.num);
  if (add && v <= s.mask - s.inc) {
    v += s.inc;
  } else if (!add && v >= 0 + s.inc) {
    v -= s.inc;
  }
  // TODO: use max value for bits count in max value, or reset by additional
  // mask in spec
  reg &= ~(s.mask << s.offset);
  BK4819_WriteRegister(s.num, reg | (v << s.offset));
  redrawScreen = true;
}

// Utility functions

KEY_Code_t GetKey() {
  KEY_Code_t btn = KEYBOARD_Poll();
  if (btn == KEY_INVALID && !GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT)) {
    btn = KEY_PTT;
  }
  return btn;
}

static int clamp(int v, int min, int max) {
  return v <= min ? min : (v >= max ? max : v);
}

static uint8_t my_abs(signed v) { return v > 0 ? v : -v; }

void SetState(State state) {
  previousState = currentState;
  currentState = state;
  redrawScreen = true;
  redrawStatus = true;
}

// Radio functions

static void ToggleAFBit(bool on) {
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_47);
  reg &= ~(1 << 8);
  if (on)
    reg |= on << 8;
  BK4819_WriteRegister(BK4819_REG_47, reg);
}

static void BackupRegisters() {
  R30 = BK4819_ReadRegister(BK4819_REG_30);
  R37 = BK4819_ReadRegister(BK4819_REG_37);
  R3D = BK4819_ReadRegister(BK4819_REG_3D);
  R43 = BK4819_ReadRegister(BK4819_REG_43);
  R47 = BK4819_ReadRegister(BK4819_REG_47);
  R48 = BK4819_ReadRegister(BK4819_REG_48);
  R7E = BK4819_ReadRegister(BK4819_REG_7E);
  R02 = BK4819_ReadRegister(BK4819_REG_02);
  R3F = BK4819_ReadRegister(BK4819_REG_3F);
}

static void RestoreRegisters() {
  BK4819_WriteRegister(BK4819_REG_30, R30);
  BK4819_WriteRegister(BK4819_REG_37, R37);
  BK4819_WriteRegister(BK4819_REG_3D, R3D);
  BK4819_WriteRegister(BK4819_REG_43, R43);
  BK4819_WriteRegister(BK4819_REG_47, R47);
  BK4819_WriteRegister(BK4819_REG_48, R48);
  BK4819_WriteRegister(BK4819_REG_7E, R7E);
  BK4819_WriteRegister(BK4819_REG_02, R02);
  BK4819_WriteRegister(BK4819_REG_3F, R3F);

}

static void ToggleAFDAC(bool on) {
  uint32_t Reg = BK4819_ReadRegister(BK4819_REG_30);
  Reg &= ~(1 << 9);
  if (on)
    Reg |= (1 << 9);
  BK4819_WriteRegister(BK4819_REG_30, Reg);
}

static void SetF(uint32_t f) {
  fMeasure = f;
  BK4819_SetFrequency(fMeasure + gEeprom.RX_OFFSET);
  BK4819_PickRXFilterPathBasedOnFrequency(fMeasure);
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
  BK4819_WriteRegister(BK4819_REG_30, 0);
  BK4819_WriteRegister(BK4819_REG_30, reg);
}

// Spectrum related

bool IsPeakOverLevel() { return peak.rssi >= settings.rssiTriggerLevel; }

void CheckIfTailFound()
{
  uint16_t interrupt_status_bits;
  // if interrupt waiting to be handled
  if(BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
    // reset the interrupt
    BK4819_WriteRegister(BK4819_REG_02, 0);
    // fetch the interrupt status bits
    interrupt_status_bits = BK4819_ReadRegister(BK4819_REG_02);
    // if tail found interrupt
    if (interrupt_status_bits & BK4819_REG_02_CxCSS_TAIL)
    {
        gTailFound = true;
        listenT = 0;
        ResetInterrupts();
    }
  }
}

static void ResetInterrupts()
{
  // disable interupts
  BK4819_WriteRegister(BK4819_REG_3F, 0);
  // reset the interrupt
  BK4819_WriteRegister(BK4819_REG_02, 0);
}

static void ResetPeak() {
  peak.t = 0;
  peak.rssi = 0;
}

bool IsCenterMode() { return settings.scanStepIndex < S_STEP_2_5kHz; }
// scan step in 0.01khz
uint16_t GetScanStep() { return scanStepValues[settings.scanStepIndex]; }

uint16_t GetStepsCount() 
{ 
#ifdef ENABLE_SPECTRUM_CHANNEL_SCAN
  if (appMode==CHANNEL_MODE)
  {
    return scanChannelsCount;
  }
#endif
#ifdef ENABLE_SCAN_RANGES
  if(appMode==SCAN_RANGE_MODE) {
    return (gScanRangeStop - gScanRangeStart) / GetScanStep();
  }
#endif
  return 128 >> settings.stepsCount;
}

uint32_t GetBW() { return GetStepsCount() * GetScanStep(); }
uint32_t GetFStart() {
  return IsCenterMode() ? currentFreq - (GetBW() >> 1) : currentFreq;
}

uint32_t GetFEnd() { return currentFreq + GetBW(); }

static void TuneToPeak() {
  scanInfo.f = peak.f;
  scanInfo.rssi = peak.rssi;
  scanInfo.i = peak.i;
  SetF(scanInfo.f);
}
#ifdef ENABLE_SPECTRUM_COPY_VFO
static void ExitAndCopyToVfo() {
  RestoreRegisters();
  if (appMode==CHANNEL_MODE)
  // channel mode
  {
    gEeprom.MrChannel[gEeprom.TX_VFO]     = scanChannel[peak.i-1];
    gEeprom.ScreenChannel[gEeprom.TX_VFO] = scanChannel[peak.i-1];

    gRequestSaveVFO   = true;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
  }
  else
  // frequency mode
  {
    gTxVfo->STEP_SETTING = FREQUENCY_GetStepIdxFromStepFrequency(GetScanStep());
    gTxVfo->Modulation = settings.modulationType;
    gTxVfo->CHANNEL_BANDWIDTH = settings.listenBw;

    SETTINGS_SetVfoFrequency(peak.f);
  
    gRequestSaveChannel = 1;
  }
 
  // Additional delay to debounce keys
  SYSTEM_DelayMs(200);

  isInitialized = false;
}

uint8_t GetScanStepFromStepFrequency(uint16_t stepFrequency) 
{
	for(uint8_t i = 0; i < ARRAY_SIZE(scanStepValues); i++)
		if(scanStepValues[i] == stepFrequency)
			return i;
	return S_STEP_25_0kHz;
}
#endif

static void DeInitSpectrum() {
  SetF(initialFreq);
  RestoreRegisters();
  gVfoConfigureMode = VFO_CONFIGURE;
  isInitialized = false;
}

uint8_t GetBWRegValueForScan() {
  return scanStepBWRegValues[settings.scanStepIndex];
}
  
uint16_t GetRssi() {
  uint16_t rssi;
    // SYSTICK_DelayUs(800);
  // testing autodelay based on Glitch value

  // testing resolution to sticky squelch issue
  while ((BK4819_ReadRegister(0x63) & 0b11111111) >= 255) {
    SYSTICK_DelayUs(100);
  }
  rssi = BK4819_GetRSSI();
 
  #ifdef ENABLE_SPECTRUM_CHANNEL_SCAN
    if ((appMode==CHANNEL_MODE) && (FREQUENCY_GetBand(fMeasure) > BAND4_174MHz))
    {
      // Increase perceived RSSI for UHF bands to imitate radio squelch
      rssi+=UHF_NOISE_FLOOR;
    }

    rssi+=gainOffset[CurrentScanIndex()];
    rssi-=attenuationOffset[CurrentScanIndex()];

  #endif
  return rssi;
}

static void ToggleAudio(bool on) {
  if (on == audioState) {
    return;
  }
  audioState = on;
  if (on) {
    AUDIO_AudioPathOn();
  } else {
    AUDIO_AudioPathOff();
  }
}

static void ToggleRX(bool on) {
  isListening = on;

  BACKLIGHT_TurnOn();
  // turn on green led only if screen brightness is over 7
  if(gEeprom.BACKLIGHT_MAX > 7)
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on);

  ToggleAudio(on);
  ToggleAFDAC(on);
  ToggleAFBit(on);

  if (on)
  {
    listenT = SQUELCH_OFF_DELAY;
    BK4819_SetFilterBandwidth(settings.listenBw, false);

    gTailFound=false;

    // turn on CSS tail found interrupt
    BK4819_WriteRegister(BK4819_REG_3F, BK4819_REG_02_CxCSS_TAIL);

    // keep backlight on as long as we are receiving
    gBacklightCountdown = 0;
  } else
  {
    if(appMode!=CHANNEL_MODE)
      BK4819_WriteRegister(0x43, GetBWRegValueForScan());
  }
}

// Scan info

static void ResetScanStats() {
  scanInfo.rssi = 0;
  scanInfo.rssiMax = 0;
  scanInfo.iPeak = 0;
  scanInfo.fPeak = 0;
}

static void InitScan() {
  ResetScanStats();
  scanInfo.i = 0;
  scanInfo.f = GetFStart();

  scanInfo.scanStep = GetScanStep();
  scanInfo.measurementsCount = GetStepsCount();
  // prevents phantom channel bar
  if(appMode==CHANNEL_MODE)
    scanInfo.measurementsCount++;
}

// resets modifiers like blacklist, attenuation, normalization
static void ResetModifiers() {
  for (int i = 0; i < 128; ++i) {
    if (rssiHistory[i] == RSSI_MAX_VALUE)
      rssiHistory[i] = 0;
  }
#ifdef ENABLE_SCAN_RANGES
  memset(blacklistFreqs, 0, sizeof(blacklistFreqs));
  blacklistFreqsIdx = 0;
#endif
  if(appMode==CHANNEL_MODE){
      LoadValidMemoryChannels();
      AutoAdjustResolution();
  }
  ToggleNormalizeRssi(false);
  memset(attenuationOffset, 0, sizeof(attenuationOffset));
  isAttenuationApplied = false;
  isBlacklistApplied = false;
  RelaunchScan();
}

static void RelaunchScan() {
  InitScan();
  ResetPeak();
  ToggleRX(false);
#ifdef SPECTRUM_AUTOMATIC_SQUELCH
  settings.rssiTriggerLevel = RSSI_MAX_VALUE;
#endif
  preventKeypress = true;
  scanInfo.rssiMin = RSSI_MAX_VALUE;
}

static void UpdateScanInfo() {
  if (scanInfo.rssi > scanInfo.rssiMax) {
    scanInfo.rssiMax = scanInfo.rssi;
    scanInfo.fPeak = scanInfo.f;
    scanInfo.iPeak = scanInfo.i;
  }
  // add attenuation offset to prevent noise floor lowering when attenuated rx is over
  // essentially we measure non-attenuated lowest rssi
  if (scanInfo.rssi+attenuationOffset[CurrentScanIndex()] < scanInfo.rssiMin) {
    scanInfo.rssiMin = scanInfo.rssi;
    settings.dbMin = Rssi2DBm(scanInfo.rssiMin);
    redrawStatus = true;
  }
}

static void AutoTriggerLevel() {
  if (settings.rssiTriggerLevel == RSSI_MAX_VALUE) {
    settings.rssiTriggerLevel = clamp(scanInfo.rssiMax + 8, 0, RSSI_MAX_VALUE);
  }
}

static void UpdatePeakInfoForce() {
  peak.t = 0;
  peak.rssi = scanInfo.rssiMax;
  peak.f = scanInfo.fPeak;
  peak.i = scanInfo.iPeak;
  #ifdef ENABLE_SPECTRUM_SHOW_CHANNEL_NAME
    LookupChannelInfo();
  #endif
  AutoTriggerLevel();
}

static void UpdatePeakInfo() {
  if (peak.f == 0 || peak.t >= 1024 || peak.rssi < scanInfo.rssiMax)
    UpdatePeakInfoForce();
}

static void Measure() 
{ 
  uint16_t rssi = scanInfo.rssi = GetRssi();
  #ifdef ENABLE_SCAN_RANGES  
    if(scanInfo.measurementsCount > 128) {
      uint8_t idx = CurrentScanIndex();
      if(rssiHistory[idx] < rssi || isListening) 
        rssiHistory[idx] = rssi;
      rssiHistory[(idx+1)%128] = 0;
      return;
    }
  #endif
  rssiHistory[scanInfo.i] = rssi;
}

// Update things by keypress

static uint16_t dbm2rssi(int dBm)
{
  return (dBm + 160)*2;
}

static void ClampRssiTriggerLevel()
{
  settings.rssiTriggerLevel = clamp(settings.rssiTriggerLevel, dbm2rssi(settings.dbMin), dbm2rssi(settings.dbMax));
}

static void UpdateRssiTriggerLevel(bool inc) {
  if (inc)
      settings.rssiTriggerLevel += 2;
  else
      settings.rssiTriggerLevel -= 2;

  ClampRssiTriggerLevel();

  redrawScreen = true;
  redrawStatus = true;
}

static void UpdateDBMax(bool inc) {
  if (inc && settings.dbMax < 10) {
    settings.dbMax += 1;
  } else if (!inc && settings.dbMax > settings.dbMin) {
    settings.dbMax -= 1;
  } else {
    return;
  }

  ClampRssiTriggerLevel();
  redrawStatus = true;
  redrawScreen = true;
  SYSTEM_DelayMs(20);
}

static void UpdateScanStep(bool inc) {
  if (inc && settings.scanStepIndex < S_STEP_100_0kHz) {
    settings.scanStepIndex++;
  } else if (!inc && settings.scanStepIndex > 0) {
    settings.scanStepIndex--;
  } else {
    return;
  }
  settings.frequencyChangeStep = GetBW() >> 1;
  ResetModifiers();
  redrawScreen = true;
}

static void UpdateCurrentFreq(bool inc) {
  if (inc && currentFreq < F_MAX) {
    currentFreq += settings.frequencyChangeStep;
  } else if (!inc && currentFreq > RX_freq_min() && currentFreq > settings.frequencyChangeStep) {
    currentFreq -= settings.frequencyChangeStep;
  } else {
    return;
  }
  ResetModifiers();
  redrawScreen = true;
}

static void UpdateCurrentFreqStill(bool inc) {
  uint8_t offset = modulationTypeTuneSteps[settings.modulationType];
  uint32_t f = fMeasure;
  if (inc && f < F_MAX) {
    f += offset;
  } else if (!inc && f > RX_freq_min()) {
    f -= offset;
  }
  SetF(f);
  redrawScreen = true;
}

static void AutoAdjustFreqChangeStep() {
  settings.frequencyChangeStep = GetBW() >> 1;
}

static void ToggleModulation() {
  if (settings.modulationType < MODULATION_UKNOWN - 1) {
    settings.modulationType++;
  } else {
    settings.modulationType = MODULATION_FM;
  }
  RADIO_SetModulation(settings.modulationType);
  BK4819_InitAGC(gEeprom.RX_AGC, settings.modulationType);
  redrawScreen = true;
}

static void ToggleListeningBW() {
  settings.listenBw = ACTION_NextBandwidth(settings.listenBw, false);
  redrawScreen = true;
}

static void ToggleBacklight() {
  settings.backlightAlwaysOn = !settings.backlightAlwaysOn;
  if (settings.backlightAlwaysOn) {
    BACKLIGHT_TurnOn();
  } else {
    if (!isListening)
      BACKLIGHT_TurnOff();
  }
}

static void ToggleStepsCount() {
  if (settings.stepsCount == STEPS_128) {
    settings.stepsCount = STEPS_16;
  } else {
    settings.stepsCount--;
  }
  AutoAdjustFreqChangeStep();
  ResetModifiers();
  redrawScreen = true;
}

static void ResetFreqInput() {
  tempFreq = 0;
  for (int i = 0; i < 10; ++i) {
    freqInputString[i] = '-';
  }
}

static void FreqInput() {
  freqInputIndex = 0;
  freqInputDotIndex = 0;
  ResetFreqInput();
  SetState(FREQ_INPUT);
}

static void UpdateFreqInput(KEY_Code_t key) {
  if (key != KEY_EXIT && freqInputIndex >= 10) {
    return;
  }
  if (key == KEY_STAR) {
    if (freqInputIndex == 0 || freqInputDotIndex) {
      return;
    }
    freqInputDotIndex = freqInputIndex;
  }
  if (key == KEY_EXIT) {
    freqInputIndex--;
    if(freqInputDotIndex==freqInputIndex)
      freqInputDotIndex = 0;    
  } else {
    freqInputArr[freqInputIndex++] = key;
  }

  ResetFreqInput();

  uint8_t dotIndex =
      freqInputDotIndex == 0 ? freqInputIndex : freqInputDotIndex;

  KEY_Code_t digitKey;
  for (int i = 0; i < 10; ++i) {
    if (i < freqInputIndex) {
      digitKey = freqInputArr[i];
      freqInputString[i] = digitKey <= KEY_9 ? '0' + digitKey-KEY_0 : '.';
    } else {
      freqInputString[i] = '-';
    }
  }

  uint32_t base = 100000; // 1MHz in BK units
  for (int i = dotIndex - 1; i >= 0; --i) {
    tempFreq += (freqInputArr[i]-KEY_0) * base;
    base *= 10;
  }

  base = 10000; // 0.1MHz in BK units
  if (dotIndex < freqInputIndex) {
    for (int i = dotIndex + 1; i < freqInputIndex; ++i) {
      tempFreq += (freqInputArr[i]-KEY_0) * base;
      base /= 10;
    }
  }
  redrawScreen = true;
}

static void Blacklist() {
#ifdef ENABLE_SCAN_RANGES
  blacklistFreqs[blacklistFreqsIdx++ % ARRAY_SIZE(blacklistFreqs)] = peak.i;
  rssiHistory[CurrentScanIndex()] = RSSI_MAX_VALUE;
#endif
  rssiHistory[peak.i] = RSSI_MAX_VALUE;
  isBlacklistApplied = true;
  ResetPeak();
  ToggleRX(false);
  ResetScanStats();
}

#ifdef ENABLE_SCAN_RANGES
static uint8_t CurrentScanIndex()
{
  if(scanInfo.measurementsCount > 128) {
    uint8_t i = (uint32_t)ARRAY_SIZE(rssiHistory) * 1000 / scanInfo.measurementsCount * scanInfo.i / 1000;
    return i;
  }
  else
  {
    return scanInfo.i;
  }
  
}

static bool IsBlacklisted(uint16_t idx)
{
  for(uint8_t i = 0; i < ARRAY_SIZE(blacklistFreqs); i++)
    if(blacklistFreqs[i] == idx)
      return true;
  return false;
}
#endif

// Draw things

// applied x2 to prevent initial rounding
uint8_t Rssi2PX(uint16_t rssi, uint8_t pxMin, uint8_t pxMax) {
  const int DB_MIN = settings.dbMin << 1;
  const int DB_MAX = settings.dbMax << 1;
  const int DB_RANGE = DB_MAX - DB_MIN;

  const uint8_t PX_RANGE = pxMax - pxMin;

  int dbm = clamp(rssi - (160 << 1), DB_MIN, DB_MAX);

  return ((dbm - DB_MIN) * PX_RANGE + DB_RANGE / 2) / DB_RANGE + pxMin;
}

uint8_t Rssi2Y(uint16_t rssi) {
  return DrawingEndY - Rssi2PX(rssi, 0, DrawingEndY);
}

static void DrawSpectrum() {
  for (uint8_t x = 0; x < 128; ++x) {
    uint16_t rssi = rssiHistory[x >> settings.stepsCount];
    if (rssi != RSSI_MAX_VALUE) {
      DrawVLine(Rssi2Y(rssi), DrawingEndY, x, true);
    }
  }
}

static void DrawStatus() {
#ifdef SPECTRUM_EXTRA_VALUES
  sprintf(String, "%d/%d P:%d T:%d", settings.dbMin, settings.dbMax,
          Rssi2DBm(peak.rssi), Rssi2DBm(settings.rssiTriggerLevel));
#elif ENABLE_SPECTRUM_SHOW_CHANNEL_NAME
  if (isKnownChannel)
  {
    sprintf(String, "%d/%d M%i:%s", settings.dbMin, settings.dbMax, channel+1, channelName);
  }
  else
  {
    sprintf(String, "%d/%d", settings.dbMin, settings.dbMax);
  }
  
#else
  sprintf(String, "%d/%d", settings.dbMin, settings.dbMax);
#endif
  GUI_DisplaySmallest(String, 0, 1, true, true);

  BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryCheckCounter++ % 4]);

  uint16_t voltage = (gBatteryVoltages[0] + gBatteryVoltages[1] + gBatteryVoltages[2] +
             gBatteryVoltages[3]) /
            4 * 760 / gBatteryCalibration[3];

  unsigned perc = BATTERY_VoltsToPercent(voltage);

  // sprintf(String, "%d %d", voltage, perc);
  // GUI_DisplaySmallest(String, 48, 1, true, true);

  gStatusLine[116] = 0b00011100;
  gStatusLine[117] = 0b00111110;
  for (int i = 118; i <= 126; i++) {
    gStatusLine[i] = 0b00100010;
  }
  
  for (unsigned i = 127; i >= 118; i--) {
    if (127 - i <= (perc+5)*9/100) {
      gStatusLine[i] = 0b00111110;
    }
  }
}

static void DrawF(uint32_t f) {
  sprintf(String, "%u.%05u", f / 100000, f % 100000);
  UI_PrintStringSmall(String, 8, 127, 0);

  sprintf(String, "%3s", gModulationStr[settings.modulationType]);
  GUI_DisplaySmallest(String, 116, 1, false, true);
  sprintf(String, "%s", bwNames[settings.listenBw]);
  GUI_DisplaySmallest(String, 108, 7, false, true);
}
#ifdef ENABLE_SPECTRUM_SHOW_CHANNEL_NAME
  void LookupChannelInfo() {
    if (lastPeakFrequency == peak.f) 
      return;
    
    lastPeakFrequency = peak.f;
    
    channel = BOARD_gMR_fetchChannel(peak.f);

    isKnownChannel = channel == -1 ? false : true;

    if (isKnownChannel){
      memmove(channelName, gMR_ChannelFrequencyAttributes[channel].Name, sizeof(channelName));
    }

    redrawStatus = true;
  }
#endif

static void DrawNums() {

  if (currentState == SPECTRUM) {
    if(isNormalizationApplied){
      sprintf(String, "N(%ux)", GetStepsCount());
    }
    else {
      sprintf(String, "%ux", GetStepsCount());
    }
    GUI_DisplaySmallest(String, 0, 1, false, true);

    if (appMode==CHANNEL_MODE)
    {
      sprintf(String, "%s", scanListOptions[settings.scanList]);
      GUI_DisplaySmallest(String, 0, 7, false, true);
    }
    else
    {
      sprintf(String, "%u.%02uk", GetScanStep() / 100, GetScanStep() % 100);
      GUI_DisplaySmallest(String, 0, 7, false, true);
    }

  }

  if (appMode==CHANNEL_MODE) 
  {
    sprintf(String, "M:%d", scanChannel[0]+1);
    GUI_DisplaySmallest(String, 0, 49, false, true);

    sprintf(String, "M:%d", scanChannel[GetStepsCount()-1]+1);
    GUI_DisplaySmallest(String, 108, 49, false, true);
  }
  else
  {
    sprintf(String, "%u.%05u", GetFStart() / 100000, GetFStart() % 100000);
    GUI_DisplaySmallest(String, 0, 49, false, true);

    sprintf(String, "%u.%05u", GetFEnd() / 100000, GetFEnd() % 100000);
    GUI_DisplaySmallest(String, 93, 49, false, true);
  }

  if(isAttenuationApplied){
    sprintf(String, "ATT");
    GUI_DisplaySmallest(String, 52, 49, false, true);
  }

  if(isBlacklistApplied){
    sprintf(String, "BL");
    GUI_DisplaySmallest(String, 67, 49, false, true);
  }
}

static void DrawRssiTriggerLevel() {
  if (settings.rssiTriggerLevel == RSSI_MAX_VALUE || monitorMode)
    return;
  uint8_t y = Rssi2Y(settings.rssiTriggerLevel);
  for (uint8_t x = 0; x < 128; x += 2) {
    PutPixel(x, y, true);
  }
}

static void DrawTicks() {
  uint32_t f = GetFStart();
  uint32_t span = GetFEnd() - GetFStart();
  uint32_t step = span / 128;
  for (uint8_t i = 0; i < 128; i += (1 << settings.stepsCount)) {
    f = GetFStart() + span * i / 128;
    uint8_t barValue = 0b00000001;
    (f % 10000) < step && (barValue |= 0b00000010);
    (f % 50000) < step && (barValue |= 0b00000100);
    (f % 100000) < step && (barValue |= 0b00011000);

    gFrameBuffer[5][i] |= barValue;
  }
  memset(gFrameBuffer[5] + 1, 0x80, 3);
  memset(gFrameBuffer[5] + 124, 0x80, 3);

  gFrameBuffer[5][0] = 0xff;
  gFrameBuffer[5][127] = 0xff;
}

static void DrawArrow(uint8_t x) {
  for (signed i = -2; i <= 2; ++i) {
    signed v = x + i;
    if (!(v & 128)) {
      gFrameBuffer[5][v] |= (0b01111000 << my_abs(i)) & 0b01111000;
    }
  }
}

static void OnKeyDown(uint8_t key) {
  if (!isListening)
    BACKLIGHT_TurnOn();

  switch (key) {
  case KEY_3:
    UpdateDBMax(true);
    break;
  case KEY_9:
    UpdateDBMax(false);
    break;
  case KEY_1:
    if(appMode!=CHANNEL_MODE)
    {
      UpdateScanStep(true);
    }
    break;
  case KEY_7:
    if(appMode!=CHANNEL_MODE)
    {
      UpdateScanStep(false);
    }
    break;
  case KEY_2:
    ToggleNormalizeRssi(!isNormalizationApplied);
    break;
  case KEY_8:
    ToggleBacklight();
    break;
  case KEY_UP:
#ifdef ENABLE_SCAN_RANGES
    if(appMode==FREQUENCY_MODE)
    {
#endif
      UpdateCurrentFreq(true);
    }
    else
    {
      ResetModifiers();
    }
    break;
  case KEY_DOWN:
#ifdef ENABLE_SCAN_RANGES
    if(appMode==FREQUENCY_MODE)
    {
#endif
      UpdateCurrentFreq(false);
    }
    else
    {
      ResetModifiers();
    }
    break;
  case KEY_SIDE1:
    Blacklist();
    break;
  case KEY_STAR:
    UpdateRssiTriggerLevel(true);
    break;
  case KEY_F:
    UpdateRssiTriggerLevel(false);
    break;
  case KEY_5:
#ifdef ENABLE_SCAN_RANGES
    if(appMode==FREQUENCY_MODE)
#endif  
      FreqInput();
    break;
  case KEY_0:
    ToggleModulation();
    break;
  case KEY_6:
    ToggleListeningBW();
    break;
  case KEY_4:
    if(appMode==CHANNEL_MODE)
    {
      ToggleScanList();
    }
    else if (appMode!=SCAN_RANGE_MODE)
    {
      ToggleStepsCount();
    }
    break;
  case KEY_SIDE2:
    Attenuate(ATTENUATE_STEP);
    break;
  case KEY_PTT:
    #ifdef ENABLE_SPECTRUM_COPY_VFO
      ExitAndCopyToVfo();
    #else
    SetState(STILL);
    TuneToPeak();
    #endif
    break;
  case KEY_MENU:
    #ifdef ENABLE_SPECTRUM_COPY_VFO
      SetState(STILL);
      TuneToPeak();
    #endif
    break;
  case KEY_EXIT:
    if (menuState) {
      menuState = 0;
      break;
    }
    DeInitSpectrum();
    break;
  default:
    break;
  }
}

static void OnKeyDownFreqInput(uint8_t key) {
  switch (key) {
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
  case KEY_STAR:
    UpdateFreqInput(key);
    break;
  case KEY_EXIT:
    if (freqInputIndex == 0) {
      SetState(previousState);
      break;
    }
    UpdateFreqInput(key);
    break;
  case KEY_MENU:
    if (tempFreq < RX_freq_min() || tempFreq > F_MAX) {
      break;
    }
    SetState(previousState);
    currentFreq = tempFreq;
    if (currentState == SPECTRUM) {
      ResetModifiers();
    } else {
      SetF(currentFreq);
    }
    break;
  default:
    break;
  }
}

void OnKeyDownStill(KEY_Code_t key) {
  switch (key) {
  case KEY_3:
    UpdateDBMax(true);
    break;
  case KEY_9:
    UpdateDBMax(false);
    break;
  case KEY_UP:
    if (menuState) {
      SetRegMenuValue(menuState, true);
      break;
    }
    UpdateCurrentFreqStill(true);
    break;
  case KEY_DOWN:
    if (menuState) {
      SetRegMenuValue(menuState, false);
      break;
    }
    UpdateCurrentFreqStill(false);
    break;
  case KEY_STAR:
    UpdateRssiTriggerLevel(true);
    break;
  case KEY_F:
    UpdateRssiTriggerLevel(false);
    break;
  case KEY_5:
    FreqInput();
    break;
  case KEY_0:
    ToggleModulation();
    break;
  case KEY_6:
    ToggleListeningBW();
    break;
  case KEY_SIDE1:
    monitorMode = !monitorMode;
    break;
  case KEY_SIDE2:
    ToggleBacklight();
    break;
  case KEY_PTT:
    #ifdef ENABLE_SPECTRUM_COPY_VFO
      ExitAndCopyToVfo();
    #endif
    break;
  case KEY_MENU:
    if (menuState == ARRAY_SIZE(registerSpecs) - 1) {
      menuState = 1;
    } else {
      menuState++;
    }
    redrawScreen = true;
    break;
  case KEY_EXIT:
    if (!menuState) {
      SetState(SPECTRUM);
      monitorMode = false;
      RelaunchScan();
      break;
    }
    menuState = 0;
    break;
  default:
    break;
  }
}

static void RenderFreqInput() {
  UI_PrintString(freqInputString, 2, 127, 0, 8);
}

static void RenderStatus() {
  memset(gStatusLine, 0, sizeof(gStatusLine));
  DrawStatus();
  ST7565_BlitStatusLine();
}

static void RenderSpectrum() {
  DrawTicks();
  if((appMode==CHANNEL_MODE)&&(GetStepsCount()<128u))
  {
    DrawArrow(peak.i * (settings.stepsCount + 1));
  }
  else
  {
    DrawArrow(128u * peak.i / GetStepsCount());
  }
  DrawSpectrum();
  DrawRssiTriggerLevel();
  DrawF(peak.f);
  DrawNums();
}

static void RenderStill() {
  DrawF(fMeasure);

  const uint8_t METER_PAD_LEFT = 3;

  for (int i = 0; i < 121; i++) {
    if (i % 10 == 0) {
      gFrameBuffer[2][i + METER_PAD_LEFT] = 0b01110000;
    } else if (i % 5 == 0) {
      gFrameBuffer[2][i + METER_PAD_LEFT] = 0b00110000;
    } else {
      gFrameBuffer[2][i + METER_PAD_LEFT] = 0b00010000;
    }
  }

  uint8_t x = Rssi2PX(scanInfo.rssi, 0, 121);
  for (int i = 0; i < x; ++i) {
    if (i % 5) {
      gFrameBuffer[2][i + METER_PAD_LEFT] |= 0b00000111;
    }
  }

  sLevelAttributes sLevelAtt;
  sLevelAtt = GetSLevelAttributes(scanInfo.rssi, fMeasure);

  if(sLevelAtt.over > 0)
  {
    sprintf(String, "S%2d+%2d", sLevelAtt.sLevel, sLevelAtt.over);
  }
  else
  {
    sprintf(String, "S%2d", sLevelAtt.sLevel);
  }

  GUI_DisplaySmallest(String, 4, 25, false, true);
  sprintf(String, "%d dBm", sLevelAtt.dBmRssi);
  GUI_DisplaySmallest(String, 40, 25, false, true);

  if (!monitorMode) {
    uint8_t x = Rssi2PX(settings.rssiTriggerLevel, 0, 121);
    gFrameBuffer[2][METER_PAD_LEFT + x] = 0b11111111;
  }

  const uint8_t PAD_LEFT = 4;
  const uint8_t CELL_WIDTH = 30;
  uint8_t offset = PAD_LEFT;
  uint8_t row = 4;

  for (int i = 0, idx = 1; idx <= 4; ++i, ++idx) {
    if (idx == 5) {
      row += 2;
      i = 0;
    }
    offset = PAD_LEFT + i * CELL_WIDTH;
    if (menuState == idx) {
      for (int j = 0; j < CELL_WIDTH; ++j) {
        gFrameBuffer[row][j + offset] = 0xFF;
        gFrameBuffer[row + 1][j + offset] = 0xFF;
      }
    }
    sprintf(String, "%s", registerSpecs[idx].name);
    GUI_DisplaySmallest(String, offset + 2, row * 8 + 2, false,
                        menuState != idx);
    sprintf(String, "%u", GetRegMenuValue(idx));
    GUI_DisplaySmallest(String, offset + 2, (row + 1) * 8 + 1, false,
                        menuState != idx);
  }
}

static void Render() {
  memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

  switch (currentState) {
  case SPECTRUM:
    RenderSpectrum();
    break;
  case FREQ_INPUT:
    RenderFreqInput();
    break;
  case STILL:
    RenderStill();
    break;
  }

  ST7565_BlitFullScreen();
}

bool HandleUserInput() {
  kbd.prev = kbd.current;
  kbd.current = GetKey();

  if (kbd.current != KEY_INVALID && kbd.current == kbd.prev) {
    if(kbd.counter < 16)
      kbd.counter++;
    else
      kbd.counter-=3;
    SYSTEM_DelayMs(20);
  }
  else {
    kbd.counter = 0;
  }

  if (kbd.counter == 3 || kbd.counter == 16) {
    switch (currentState) {
    case SPECTRUM:
      OnKeyDown(kbd.current);
      break;
    case FREQ_INPUT:
      OnKeyDownFreqInput(kbd.current);
      break;
    case STILL:
      OnKeyDownStill(kbd.current);
      break;
    }
  }

  return true;
}

static void Scan() {
  if (rssiHistory[scanInfo.i] != RSSI_MAX_VALUE
#ifdef ENABLE_SCAN_RANGES
  && !IsBlacklisted(scanInfo.i)
#endif
  ) {
    SetF(scanInfo.f);
    Measure();
    UpdateScanInfo();
  }
}

static void NextScanStep() {
  ++peak.t;
  #ifdef ENABLE_SPECTRUM_CHANNEL_SCAN
    // channel mode
    if (appMode==CHANNEL_MODE)
    {
      int currentChannel = scanChannel[scanInfo.i];
      scanInfo.f =  gMR_ChannelFrequencyAttributes[currentChannel].Frequency;
      ++scanInfo.i; 
    }
    else
    // frequency mode
    {
      ++scanInfo.i; 
      scanInfo.f += scanInfo.scanStep;
    }
    
  #elif
    ++scanInfo.i;
    scanInfo.f += scanInfo.scanStep;
  #endif

}

static void UpdateScan() {
  Scan();

  if (scanInfo.i < GetStepsCount()) {
    NextScanStep();
    return;
  }

  if(scanInfo.measurementsCount < 128)
    memset(&rssiHistory[scanInfo.measurementsCount], 0, 
      sizeof(rssiHistory) - scanInfo.measurementsCount*sizeof(rssiHistory[0]));

  redrawScreen = true;
  preventKeypress = false;

  UpdatePeakInfo();
  if (IsPeakOverLevel()) {
    ToggleRX(true);
    TuneToPeak();
    return;
  }

  newScanStart = true;
}

static void UpdateStill() {
  Measure();
  redrawScreen = true;
  preventKeypress = false;

  peak.rssi = scanInfo.rssi;
  AutoTriggerLevel();

  ToggleRX(IsPeakOverLevel() || monitorMode);
}

static void UpdateListening() {
  preventKeypress = false;
  if (currentState == STILL) {
    listenT = 0;
  }
  if (listenT) {
    listenT--;
    SYSTEM_DelayMs(1);
    return;
  }

  if (currentState == SPECTRUM) {
    if(appMode!=CHANNEL_MODE)
      BK4819_WriteRegister(0x43, GetBWRegValueForScan());
    Measure();
    BK4819_SetFilterBandwidth(settings.listenBw, false);
  } else {
    Measure();
  }

  peak.rssi = scanInfo.rssi;
  redrawScreen = true;

  CheckIfTailFound();

  if ((IsPeakOverLevel() || monitorMode) && !gTailFound) {
    listenT = SQUELCH_OFF_DELAY;
    return;
  }

  ToggleRX(false);
  ResetScanStats();
}

static void Tick() {
  if (gNextTimeslice_500ms) {
    if (gBacklightCountdown > 0) {
      if (--gBacklightCountdown == 0)
				if (!settings.backlightAlwaysOn)
					BACKLIGHT_TurnOff();   // turn backlight off
	  }
    gNextTimeslice_500ms = false;

#ifdef ENABLE_SCAN_RANGES
    // if a lot of steps then it takes long time
    // we don't want to wait for whole scan
    // listening has it's own timer
    if(GetStepsCount()>128 && !isListening) {
      UpdatePeakInfo();
      if (IsPeakOverLevel()) {
        ToggleRX(true);
        TuneToPeak();
        return;
      }
      redrawScreen = true;
      preventKeypress = false;
    }
  }
#endif

  if (!preventKeypress) {
    HandleUserInput();
  }
  if (newScanStart) {
    InitScan();
    newScanStart = false;
  }
  if (isListening && currentState != FREQ_INPUT) {
    UpdateListening();
  } else {
    if (currentState == SPECTRUM) {
      UpdateScan();
    } else if (currentState == STILL) {
      UpdateStill();
    }
  }
  if (redrawStatus || ++statuslineUpdateTimer > 4096) {
    RenderStatus();
    redrawStatus = false;
    statuslineUpdateTimer = 0;
  }
  if (redrawScreen) {
    Render();
    redrawScreen = false;
  }
}

#ifdef ENABLE_SPECTRUM_CHANNEL_SCAN
void APP_RunSpectrum(Mode mode) {
  // reset modifiers if we launched in a different then previous mode
  if(appMode!=mode){
    ResetModifiers();
  }
  appMode = mode;
#elif
void APP_RunSpectrum() {
#endif
  #ifdef ENABLE_SPECTRUM_CHANNEL_SCAN
    if (appMode==CHANNEL_MODE)
    {
      LoadValidMemoryChannels();
      AutoAdjustResolution();
    }
  #endif
  #ifdef ENABLE_SCAN_RANGES
    if(mode==SCAN_RANGE_MODE) {
      currentFreq = initialFreq = gScanRangeStart;
      for(uint8_t i = 0; i < ARRAY_SIZE(scanStepValues); i++) {
        if(scanStepValues[i] >= gTxVfo->StepFrequency) {
          settings.scanStepIndex = i;
          break;
        }
      }
      settings.stepsCount = STEPS_128;
    }
    else
  #endif

  currentFreq = initialFreq = gTxVfo->pRX->Frequency;

  BackupRegisters();

  ResetInterrupts();

  // turn of GREEN LED if spectrum was started during active RX
  BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);

  isListening = true; // to turn off RX later
  redrawStatus = true;
  redrawScreen = true;
  newScanStart = true;


  ToggleRX(true), ToggleRX(false); // hack to prevent noise when squelch off
  #ifdef ENABLE_SPECTRUM_COPY_VFO
    RADIO_SetModulation(settings.modulationType = gTxVfo->Modulation);
    BK4819_SetFilterBandwidth(settings.listenBw = gTxVfo->CHANNEL_BANDWIDTH, false);
    settings.scanStepIndex = GetScanStepFromStepFrequency(gTxVfo->StepFrequency);
  #elif
    RADIO_SetModulation(settings.modulationType = MODULATION_FM);
    BK4819_SetFilterBandwidth(settings.listenBw = BK4819_FILTER_BW_WIDE, false);
  #endif

  AutoAdjustFreqChangeStep();

  RelaunchScan();

  for (int i = 0; i < 128; ++i) {
    rssiHistory[i] = 0;
  }

  isInitialized = true;

  while (isInitialized) {
    Tick();
  }
}

#ifdef ENABLE_SPECTRUM_CHANNEL_SCAN
  void LoadValidMemoryChannels()
  {
    memset(scanChannel,0,sizeof(scanChannel));
    scanChannelsCount = RADIO_ValidMemoryChannelsCount(true, settings.scanList);
    signed int channelIndex=-1;
    for(int i=0; i < scanChannelsCount; i++)
    {
      int nextChannel;
      nextChannel = RADIO_FindNextChannel((channelIndex)+1, 1, true, settings.scanList);
      
      if (nextChannel == 0xFF)
      {	// no valid channel found
        break;
      }
      else
      {
        channelIndex = nextChannel;
        scanChannel[i]=channelIndex;
      }
    }
  }

  void ToggleScanList()
  {
    if (settings.scanList==S_SCAN_LIST_ALL)
    {
      settings.scanList=S_SCAN_LIST_1;
    }
    else
    {
      settings.scanList++;
    }

    LoadValidMemoryChannels();
    ResetModifiers();
    AutoAdjustResolution();
  }

  void AutoAdjustResolution()
  {
    if (GetStepsCount() <= 64)
    {
      settings.stepsCount = STEPS_64;
    }
    else
    {
      settings.stepsCount = STEPS_128;
    }
  }
  // 2024 by kamilsss655  -> https://github.com/kamilsss655
  // flattens spectrum by bringing all the rssi readings to the peak value
  void ToggleNormalizeRssi(bool on)
  {
    // we don't want to normalize when there is already active signal RX
    if(IsPeakOverLevel() && on)
      return;

    if(on) {
      for(uint8_t i = 0; i < ARRAY_SIZE(rssiHistory); i++)
      {
        gainOffset[i] = peak.rssi - rssiHistory[i];
      }
      isNormalizationApplied = true;
    }
    else {
      memset(gainOffset, 0, sizeof(gainOffset));
      isNormalizationApplied = false;
    }
    RelaunchScan();
  }

  void Attenuate(uint8_t amount)
  {
    // attenuate doesn't work with more than 128 samples,
    // since we select max rssi in such mode ignoring attenuation
     if(scanInfo.measurementsCount > 128)
      return;

    // idea: consider amount to be 10% of rssiMax-rssiMin
    if(attenuationOffset[peak.i] < MAX_ATTENUATION){
      attenuationOffset[peak.i] += amount;
      isAttenuationApplied = true;

      ResetPeak();
      ResetScanStats();
    }
    
  }
#endif