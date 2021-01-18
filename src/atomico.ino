#define DEB
#include <EEPROM.h>
#include <TimerOne.h>
#include <PantalaDefines.h>

#define SWITCHPIN 12

#define MAXGATES 8
#define DEFCLOCKLENGHT 10000
#define MINBPM 20
#define DEFAULTBPM 120
#define MAXBPM 200
#define TIMEBASE 48
#define MAXPPQNOPTIONS 11
#define MAXSTEPCOUNT 1536
#define EDITMODETIME 5000
#define SHOWCHANNELTIME 2000
#define BUTTONTIMEDEBOUNCE 150

volatile uint16_t stepCounter;
volatile bool gateStatus[MAXGATES] = {0, 0, 0, 0, 0, 0, 0, 0};
volatile uint32_t gateOff;
uint8_t gatePinArray[MAXGATES] = {2, 4, 6, 8, 3, 5, 7, 9};
//                        {24,16, 8,  4,  2,  1, x2, x4 ,  x8, x16, x32};
int ppqn[MAXPPQNOPTIONS] = {2, 3, 6, 12, 24, 48, 96, 192, 384, 768, MAXSTEPCOUNT};
int gateOption[MAXGATES] = {0, 1, 2, 3, 4, 5, 6, 7};
int myGateOption[MAXGATES] = {0, 3, 4, 5, 6, 7, 8, 9};
int gateConfigSelector;
int8_t queuedGate;
uint32_t buttonDebounce;
uint16_t variableClockLenght = DEFCLOCKLENGHT;

//ENCODER
#define encPinA 10
#define encPinB 11
bool oldEncoderA, readA;
bool oldEncoderB, readB;

bool editMode;
uint32_t editModeOff;

bool showChannel;
uint32_t showChannelOff;

uint16_t bpm = DEFAULTBPM;
uint16_t encoderValue = DEFAULTBPM * 4;

void setup()
{
  Serial.begin(9600);

  for (int i = 0; i < MAXGATES; i++)
    pinMode(gatePinArray[i], OUTPUT);

  //encoder reset
  pinMode(SWITCHPIN, INPUT);
  pinMode(encPinA, INPUT_PULLUP);
  pinMode(encPinB, INPUT_PULLUP);
  oldEncoderA = LOW;
  oldEncoderB = LOW;
  readA = LOW;
  readB = LOW;

  //saveConfig(myGateOption);
  loadConfig();

  Timer1.attachInterrupt(tickInterrupt);
  Timer1.initialize(bpm2microsNppqn(bpm, TIMEBASE));
}

void tickInterrupt()
{
  FWDC(stepCounter, MAXSTEPCOUNT, 1);
  gateOff = micros() + variableClockLenght;
  for (uint8_t i = 0; i < MAXGATES; i++)
    if ((stepCounter % ppqn[gateOption[i]]) == 0)
    {
      gateStatus[i] = true;
      digitalWrite(gatePinArray[i], HIGH);
    }
}

void loop()
{
  //rising button
  if (digitalRead(SWITCHPIN) && (millis() > buttonDebounce))
  {
    if (showChannel)
      digitalWrite(gatePinArray[queuedGate], LOW);
    buttonDebounce = millis() + 250;
    NEXT(queuedGate, MAXGATES);
    editMode = true;
    editModeOff = millis() + EDITMODETIME;
    showChannel = true;
    showChannelOff = millis() + SHOWCHANNELTIME;
    digitalWrite(gatePinArray[queuedGate], HIGH);
  }

  //switch back from edit mode to bpm mode
  if (editModeOff > 0)
  {
    if (millis() > editModeOff)
    {
      editModeOff = 0;
      editMode = false;
    }
  }

  //turn off queued gate led
  if (showChannel && (millis() > showChannelOff))
  {
    showChannel = false;
    digitalWrite(gatePinArray[queuedGate], LOW);
  }

  readEncoder();

  //close gates
  for (uint8_t i = 0; i < MAXGATES; i++)
  {
    if (gateStatus[i] && (micros() > gateOff))
    {
      if (showChannel && (queuedGate == i))
      {
      }
      else
      {
        gateStatus[i] = false;
        digitalWrite(gatePinArray[i], LOW);
      }
    }
  }

  //save all channels as soon as edit mode ends
  if (editMode && (millis() > editModeOff))
  {
    editMode = false;
    saveConfig(gateOption);
  }
}

void readEncoder()
{
  int changeValue = 0;
  // reads its pins current state
  readA = digitalRead(encPinA);
  readB = digitalRead(encPinB);
  // manually compare the four possible states to figure out what if changed
  changeValue = compare4EncoderStates();
  //if there was a change
  if (changeValue != 0)
  {
    oldEncoderA = readA;
    oldEncoderB = readB;
    //not edit mode , change BPM
    if (!editMode)
    {
      encoderValue += changeValue;
      encoderValue = constrain(encoderValue, MINBPM * 4, MAXBPM * 4);
      if ((encoderValue % 4) == 0)
      {
        bpm = encoderValue / 4;
        uint32_t newPeriod;
        newPeriod = bpm2microsNppqn(bpm, TIMEBASE);
        variableClockLenght = min(DEFCLOCKLENGHT, ((newPeriod << 3) / 10)); //80% of period
        Timer1.setPeriod(newPeriod);
      }
    }
    //edit mode , change speed from this gate channel
    else
    {
      if (showChannel)
      {
        showChannel = false;
        digitalWrite(gatePinArray[queuedGate], LOW);
      }
      editModeOff = millis() + EDITMODETIME;
      gateConfigSelector += changeValue;
      gateConfigSelector = gateConfigSelector % 4;
      if (gateConfigSelector == 0)
      {
        gateOption[queuedGate] += changeValue;
        gateOption[queuedGate] = constrain(gateOption[queuedGate], 0, MAXPPQNOPTIONS);
      }
    }
  }
}

// manually compare the four possible states to figure out what changed
int compare4EncoderStates()
{
  int changeValue = 0;
  if (readA != oldEncoderA)
    changeValue = (readA == LOW) ? ((readB == LOW) ? -1 : 1) : ((readB == LOW) ? 1 : -1);
  if (readB != oldEncoderB)
    changeValue = (readB == LOW) ? ((readA == LOW) ? 1 : -1) : ((readA == LOW) ? -1 : 1);
  return changeValue;
}

void saveConfig(int b[])
{
  for (int i = 0; i < MAXGATES; i++)
    EEPROM.write(i, b[i]);
}

void loadConfig()
{
  for (int i = 0; i < MAXGATES; i++)
    gateOption[i] = EEPROM.read(i);
}
