// CanTrip2Structs.h
#pragma once

#ifndef _CanTrip2Structs_h
    #define _CanTrip2Structs_h

#include "../AbstractCanMessageSender.h"
#include "../../Helpers/PacketGenerator.h"
#include "../../Helpers/IntValueOnTwoBytes.h"

// CANID: 261
const uint16_t CAN_ID_TRIP2 = 0x261;

// Read left to right in documentation
struct CanTrip2Struct {
    uint8_t AverageSpeed;
    uint8_t MileageAfterResetByte1;
    uint8_t MileageAfterResetByte2;
    uint8_t LitersPer100KmByte1;
    uint8_t LitersPer100KmByte2;
    uint8_t Field6;
    uint8_t Field7;
};

union CanTrip2Packet {
    CanTrip2Struct data;
    uint8_t CanTrip2Packet[sizeof(CanTrip2Struct)];
};

#pragma region Sender class
class CanTrip2Sender
{
    AbstractCanMessageSender * canMessageSender;

public:
    CanTrip2Sender(AbstractCanMessageSender * object)
    {
        canMessageSender = object;
    }

    void SendTripInfo(int mileageAfterReset, uint8_t lper100km, uint8_t averageSpeed)
    {
        PacketGenerator<CanTrip2Packet> generator;

        IntValueOnTwoBytes intValueOnTwoBytes;

        intValueOnTwoBytes .value = mileageAfterReset;
        generator.packet.data.MileageAfterResetByte1 = intValueOnTwoBytes.bytes[1];
        generator.packet.data.MileageAfterResetByte2 = intValueOnTwoBytes.bytes[0];

        intValueOnTwoBytes.value = lper100km;
        generator.packet.data.LitersPer100KmByte1 = intValueOnTwoBytes.bytes[1];
        generator.packet.data.LitersPer100KmByte2 = intValueOnTwoBytes.bytes[0];

        generator.packet.data.AverageSpeed = averageSpeed;

        unsigned char *serializedPacket = generator.GetSerializedPacket();
        canMessageSender->SendMessage(CAN_ID_TRIP2, 0, sizeof(CanTrip2Packet), serializedPacket);
    }

};
#pragma endregion

#endif
