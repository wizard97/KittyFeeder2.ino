/*
  FeederCompartment.h - Class for managing each of the feeding compartments
  Created by D. Aaron Wisner, March 27, 2016.
  Released into the public domain.
*/
#ifndef FEED_COMPART_H
#define FEED_COMPART_H

#include "Arduino.h"
#include "Servo.h"
#include "TimeLib.h"
#include <EEPROM.h>
#include "FeederUtils.h"


// ms to open and close door
#define DOOR_SPEED 3000
// Mins door should stay open
#define DOOR_OPEN_TIME 3

#define FEED_COMPART_EE_SIZE sizeof(EECompartSettings)


// Make a struct so we can memcpy it out of EEPROM
typedef struct EECompartSettings
{
    boolean enabled;
    //Ignore months and year fields
    uint8_t Minute;
    uint8_t Hour;
    uint8_t Wday;   // day of week, sunday is day 1
    //crc MUST BE LAST ELEMENT IN STRUCT!
    uint32_t crc;
} FeedCompartSettings;

class FeedCompart
{
    typedef enum DoorState
    {
        CLOSED,
        OPENING,
        OPEN,
        CLOSING,
    } DoorState;

private:
    const uint16_t eepromLoc;
    EECompartSettings settings;
    Servo doorServo;
    const uint16_t servoPin;
    DoorState currDoorState;
    // Timestamp of state change for door
    unsigned long msStateChange;
    // Servo open and close positions
    const uint16_t openDeg, closeDeg;

    uint8_t id;
    static uint8_t _id_counter;

    bool loadSettingsFromEE();
    void saveSettingsToEE();
    uint32_t generateCrc();

public:
    FeedCompart(uint16_t servoPin, uint16_t eepromLoc, uint16_t closeDeg, uint16_t openDeg);
    ~FeedCompart();
    Servo &getServo();
    bool isEnabled();
    void enable();
    void service();
    void begin();
};

//Default it to zero
uint8_t FeedCompart::_id_counter = 0;

FeedCompart::FeedCompart(uint16_t servoPin, uint16_t eepromLoc, uint16_t closeDeg, uint16_t openDeg)
: doorServo(), servoPin(servoPin), eepromLoc(eepromLoc),
openDeg(openDeg), closeDeg(closeDeg), id(++_id_counter)
{
    currDoorState = CLOSED;
    msStateChange = 0;
}

FeedCompart::~FeedCompart()
{
    doorServo.write(closeDeg);
    doorServo.detach();
    saveSettingsToEE();
}


void FeedCompart::begin()
{
    // If loading settings failed, set to sensible defaults
    if(!loadSettingsFromEE())
    {
        time_t curr = now();
        //Default to now
        settings.Minute = minute(curr);
        settings.Hour = hour(curr);
        settings.Wday = weekday(curr);

        settings.enabled = false;
        saveSettingsToEE();
        STOREM(LOG_ERROR,"Feed Door %d: EEPROM corrupt, setting alarm time to now.", id);
        PRINTM();
    }

    doorServo.attach(servoPin);
    doorServo.write(closeDeg);
    STOREM(LOG_DEBUG, "Feed Door %d: Servo attached to pin %d", id, servoPin);
    PRINTM();
    // No idea why I'm having to do this...
    char tmp[5];
    strcpy(tmp, dayShortStr(settings.Wday));

    STOREM(LOG_DEBUG, "Feed Door %d: %s with set time: %s %d:%d",
        id, settings.enabled ? "ENABLED" : "DISABLED", tmp,
        settings.Hour, settings.Minute);
    PRINTM();
}

void FeedCompart::service()
{
    time_t curr = now();

    if ((settings.enabled && weekday(curr) == settings.Wday)) {
        // figure out the time_t the door should open
        tmElements_t to_open;
        breakTime(curr, to_open);
        // We know the day is correct, so just fil in the rest
        to_open.Hour = settings.Hour;
        to_open.Minute = settings.Minute;
        to_open.Second = 0;
        // This is the timestamp it should open
        time_t set =  makeTime(to_open);

        // Run the state machine for the door
        switch(currDoorState)
        {
            case CLOSED:
                if (curr >= set && curr < set + 60*DOOR_OPEN_TIME) {
                    msStateChange = millis();
                    currDoorState = OPENING;
                    STOREM(LOG_DEBUG, "Feeder %d opening!", id);
                    PRINTM();
                }
                break;

            case OPENING:
                if (doorServo.read() != openDeg) {
                    doorServo.write(map((long int)(millis() - msStateChange), 0,
                        DOOR_SPEED, closeDeg, openDeg));
                } else {
                    msStateChange = millis();
                    currDoorState = OPEN;
                }
                break;

            case OPEN:
                if (curr >= set + 60*DOOR_OPEN_TIME) {
                    msStateChange = millis();
                    currDoorState = CLOSING;
                    STOREM(LOG_DEBUG, "Feeder %d closing!", id);
                    PRINTM();
                }
                break;

            case CLOSING:
                if (doorServo.read() != closeDeg) {
                    doorServo.write(map((long int)(millis() - msStateChange), 0,
                        DOOR_SPEED, openDeg, closeDeg));
                } else {
                    msStateChange = millis();
                    currDoorState = CLOSED;
                    // disable feed
                    settings.enabled = false;
                    saveSettingsToEE();
                }
                break;

            default:
                msStateChange = millis();
                currDoorState = CLOSED;
                break;
        }
    } else {
        currDoorState = CLOSED;
    }

}

void FeedCompart::enable()
{
    settings.enabled = true;
    saveSettingsToEE();
}

Servo &FeedCompart::getServo()
{
    return doorServo;
}

bool FeedCompart::isEnabled()
{
    return settings.enabled;
}

bool FeedCompart::loadSettingsFromEE()
{
    // Some crazy pointer casting to perform a memcpy so we can use the EEPROM macro
    for (int i=0; i<sizeof(settings); i++)
    {
        ((unsigned char*)&settings)[i] = EEPROM[eepromLoc + i];
    }
    settings.Wday = constrain(settings.Wday, 1, 7);
    settings.Hour = constrain(settings.Hour, 0, 23);
    settings.Minute = constrain(settings.Minute, 0, 59);

    return generateCrc() == settings.crc;
}

void FeedCompart::saveSettingsToEE()
{
    // update crc
    settings.crc = generateCrc();
    // Some crazy pointer casting to perform a memcpy so we can use the EEPROM macro
    for (int i=0; i<sizeof(settings); i++)
    {
        EEPROM[eepromLoc + i] = ((unsigned char*)&settings)[i];
    }
}

// Code found on Arduino EEPROM example page
uint32_t FeedCompart::generateCrc() {

    // generate crc, ignoring the last crc element in settings struct
    return EEGenerateCrc(eepromLoc, FEED_COMPART_EE_SIZE-sizeof(settings.crc));
}

#endif
