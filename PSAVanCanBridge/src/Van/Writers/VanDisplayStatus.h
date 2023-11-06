// VanDisplayStatus.h
#pragma once

#ifndef _VanDisplayStatus_h
    #define _VanDisplayStatus_h

#include "VanMessageWriterBase.h"
#include "../AbstractVanMessageSender.h"
#include "../../Van/Structs/VanDisplayStatusStructs.h"

class VanDisplayStatus : public VanMessageWriterBase
{
    const static uint16_t SEND_STATUS_INTERVAL = 420;
    const static uint8_t SEND_RESET_COUNT = 1;

    // We use the same channel for the status as the trip computer query because otherwise it clashes somehow
    const static uint8_t  SEND_STATUS_CHANNEL = 0;

    uint8_t _tripButtonState = 0;
    uint8_t _resetTrip = 0;
    uint8_t _resetSent = 0;
    unsigned long _tripButtonPressedTime = 0;
    uint8_t _ignition = 0;

    VanDisplayStatusPacketSender* displayStatusSender;

    void SendStatus(uint8_t resetTrip)
    {
        for (int i = 0; i < SEND_RESET_COUNT; ++i)
        {
            displayStatusSender->SendStatus(SEND_STATUS_CHANNEL, resetTrip);
            delay(5);
        }
    }

    virtual void InternalProcess() override
    {
        if (_ignition)
        {
            if (_resetTrip == 1 && _resetSent == 0)
            {
                SendStatus(1);
                _resetTrip = 0;
                _resetSent = 1;
            }
            else
            {
                displayStatusSender->SendStatus(SEND_STATUS_CHANNEL, 0);
            }
        }
    }

    public:
    VanDisplayStatus(AbstractVanMessageSender* vanMessageSender) : VanMessageWriterBase(vanMessageSender, SEND_STATUS_INTERVAL)
    {
        displayStatusSender = new VanDisplayStatusPacketSender(vanMessageSender);
    }

    void SetData(uint8_t ignition, uint8_t tripButton, unsigned long currentTime)
    {
        _ignition = ignition;
        if (_ignition)
        {
            if (tripButton == 1 && _tripButtonState == 0)
            {
                _tripButtonState = 1;
                _tripButtonPressedTime = currentTime;
            }
            if (tripButton == 0 && _tripButtonState == 1)
            {
                _tripButtonState = 0;
                _resetSent = 0;
                _resetTrip = 0;
            }
            if (tripButton == 1 && _tripButtonState == 1 && currentTime - _tripButtonPressedTime > 2000)
            {
                _resetTrip = 1;
            }
        }
    }

    void Stop()
    {
        displayStatusSender->Disable(SEND_STATUS_CHANNEL);
    }

};

#endif
