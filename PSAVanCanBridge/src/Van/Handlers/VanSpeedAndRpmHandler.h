// VanSpeedAndRpmHandler.h
#pragma once

#ifndef _VanSpeedAndRpmHandler_h
    #define _VanSpeedAndRpmHandler_h

#include "../Handlers/AbstractVanMessageHandler.h"

#include "../../Helpers/VanDataToBridgeToCan.h"
#include "../../Helpers/VanIgnitionDataToBridgeToCan.h"
#include "../../Helpers/DoorStatus.h"
#include "../../Helpers/Serializer.h"

#include "../Structs/VanSpeedAndRpmStructs.h"

class VanSpeedAndRpmHandler : public AbstractVanMessageHandler {
    ~VanSpeedAndRpmHandler()
    {

    }

public:
    bool ProcessMessage(
        const uint8_t identByte1,
        const uint8_t identByte2,
        const uint8_t vanMessageWithoutId[],
        const uint8_t messageLength,
        VanDataToBridgeToCan *dataToBridge,
        VanIgnitionDataToBridgeToCan *ignitionDataToBridge,
        DoorStatus& doorStatus) override
    {
        if (!(IsVanIdent(identByte1, identByte2, VAN_ID_SPEED_RPM) && messageLength == VAN_ID_SPEED_RPM_LENGTH))
        {
            return false;
        }

        const VanSpeedAndRpmPacket packet = DeSerialize<VanSpeedAndRpmPacket>(vanMessageWithoutId);

        dataToBridge->Rpm = GetRpmFromVanData(packet.data.Rpm.data);
        dataToBridge->Speed = GetSpeedFromVanData(packet.data.Speed.data);
        dataToBridge->Distance = packet.data.Distance.data;

        return true;
    }
};

#endif
