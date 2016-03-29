#include "ThermoCooler.h"


ThermoCooler::ThermoCooler(int16_t pin, double (*gettemp)(), uint16_t eepromLoc)
: pin(pin), eepromLoc(eepromLoc), gettemp(gettemp)
{
    enabled = false;
    pwmPercent = 0;
    pinMode(pin, OUTPUT);

}

void ThermoCooler::begin()
{
    if (!loadSettingsFromEE()) {
        last_temp = 0;
        settings.set_temp = 40;
        generateCrc();
        saveSettingsToEE();
        STOREM(LOG_ERROR, "Cooler: Failed to load set temp from EEPROM");
    } else {
        STOREM(LOG_DEBUG, "Cooler: Loaded set temp of %dF from EEPROM", settings.set_temp);
    }
    PRINTM();
        service();
}

void ThermoCooler::service()
{
    // Simple low pass filter
    double current = ( (*gettemp)() + last_temp)/2.0;
    double delta = current - settings.set_temp;

    if (!enabled) {
        digitalWrite(pin, 0);
        pwmPercent = 0;
    } else if (current > settings.set_temp + TC_PWM_DELTA_DEG) {
        // It is over TC_PWM_DELTA_DEG away from set temp
        digitalWrite(pin, 1);
        pwmPercent = 100;
    } else if (current > settings.set_temp - TC_PWM_DELTA_DEG) {
        // It is within TC_PWM_DELTA_DEG, so start using pwm
        double pwm = round(128 + (127*delta)/TC_PWM_DELTA_DEG);
        pwmPercent = (uint16_t)round((pwm*100)/255);
        analogWrite(pin, (int)pwm);
    } else {
        digitalWrite(pin, 0);
        pwmPercent = 0;
    }

    last_temp = current;
}

void ThermoCooler::setTemp(int temp)
{
    settings.set_temp = temp;
    saveSettingsToEE();
}

bool ThermoCooler::loadSettingsFromEE()
{
    // Copy everything
    for (int i=0; i < THERMO_COOLER_EE_SIZE; i++)
    {
        ((char*)&settings)[i] = EEPROM[i];
    }

    return settings.crc == generateCrc();
}


void ThermoCooler::saveSettingsToEE()
{
    settings.crc = generateCrc();

    // Copy everything
    for (int i=0; i < THERMO_COOLER_EE_SIZE; i++)
    {
        EEPROM[i] = ((char*)&settings)[i];
    }
}

uint32_t ThermoCooler::generateCrc()
{
    // generate crc, ignoring the last crc element in settings struct
    return EEGenerateCrc(eepromLoc, THERMO_COOLER_EE_SIZE-sizeof(settings.crc));
}

uint16_t ThermoCooler::getPwmPercent()
{
    return pwmPercent;
}

double ThermoCooler::getTemp()
{
    return last_temp;
}

void ThermoCooler::disable()
{
    enabled = false;
}

void ThermoCooler::enable()
{
    enabled = true;
}
