// CanAirConOnDisplayHandler.h
#pragma once

#ifndef _CanAirConOnDisplayHandler_h
#define _CanAirConOnDisplayHandler_h

#include "../Structs/CanAirConOnDisplayStructs.h"
#include "../AbstractCanMessageSender.h"
#include "../Handlers/CanMessageHandlerBase.h"

class CanAirConOnDisplayHandler : public CanMessageHandlerBase
{
    static const int CAN_AIRCON_INTERVAL = 10;
    static const int FAN_SPEED_CHANGED_TIMEOUT = 50;

    uint8_t FanSpeed;
    uint8_t FanSpeedChangedCounter;

    float prevTemperatureLeft;
    float prevTemperatureRight;
    uint8_t prevDirection;
    uint8_t prevAutoMode;
    uint8_t prevAcOff;
    uint8_t prevOff;
    uint8_t prevWindshield;
    uint8_t prevFanSpeed;
    uint8_t prevRecyclingOn;
    unsigned long prevFanSpeedChanged;

    bool sendMessageOnCan;

    CanAirConditionOnDisplayPacketSender* acSender;

    virtual void InternalProcess()
    {
        if (sendMessageOnCan)
        {
            acSender->SendACDataToDisplay(
                prevTemperatureLeft,
                prevTemperatureRight,
                prevDirection,
                prevAutoMode,
                prevAcOff,
                prevOff,
                prevWindshield,
                prevFanSpeed,
                prevRecyclingOn
            );
        }
    }

public:
    CanAirConOnDisplayHandler(AbstractCanMessageSender* object) : CanMessageHandlerBase(object, CAN_AIRCON_INTERVAL)
    {
        acSender = new CanAirConditionOnDisplayPacketSender(object);
        FanSpeedChangedCounter = 0;
    }

    void SetData(
        float temperatureLeft,
        float temperatureRight,
        uint8_t direction,
        uint8_t autoMode,
        uint8_t acOff,
        uint8_t off,
        uint8_t windshield,
        uint8_t fanSpeed,
        uint8_t recyclingOn
    )
    {
        // add some tolerance on the fan speed to avoid 'flickering'

        // spare some bandwidth on CAN bus
        sendMessageOnCan =
            prevTemperatureLeft != temperatureLeft ||
            prevTemperatureRight != temperatureRight ||
            prevDirection != direction ||
            prevAutoMode != autoMode ||
            prevAcOff != acOff ||
            prevOff != off ||
            prevWindshield != windshield ||
            prevFanSpeed != fanSpeed ||
            prevRecyclingOn != recyclingOn;

        if (sendMessageOnCan)
        {
            prevTemperatureLeft = temperatureLeft;
            prevTemperatureRight = temperatureRight;
            prevDirection = direction;
            prevAutoMode = autoMode;
            prevAcOff = acOff;
            prevOff = off;
            prevWindshield = windshield;
            prevFanSpeed = fanSpeed;
            prevRecyclingOn = recyclingOn;
        }
    }
};

#endif
