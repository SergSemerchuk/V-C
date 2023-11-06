#pragma once

#ifndef _CanTripInfoHandler_h
    #define _CanTripInfoHandler_h

#include "../Structs/CanTrip0Structs.h"
#include "../Structs/CanTrip1Structs.h"
#include "../Structs/CanTrip2Structs.h"
#include "../AbstractCanMessageSender.h"

class CanTripInfoHandler
{
    const int CAN_TRIP_INTERVAL = 333;
    const int CAN_TRIP_SEND_COUNT = 10;

    AbstractCanMessageSender *canMessageSender;

    unsigned long previousTrip0Time = millis();

    int Speed = 0;
    int Rpm = 0;
    int Trip1Distance = 0;
    int Trip1Speed = 0;
    int Trip1Consumption = 0;
    int Trip2Distance = 0;
    int Trip2Speed = 0;
    int Trip2Consumption = 0;
    int FuelConsumption = 0;
    int FuelLeftToPump = 0;
    uint8_t TripButtonPressed = 0;

    int IsSendingEnabled = 1;
    uint8_t ValueToUpdate = 1;
    bool PreventTripChange = false;

    void SendCanTripInfo0(int kmToGasStation, int lper100km, int kmtoFinish, uint8_t button)
    {
        CanTrip0PacketSender tripInfoSender(canMessageSender);
        tripInfoSender.SendTripInfo(kmToGasStation, lper100km, kmtoFinish, button);
    }

    void SendCanTripInfo1(int mileageAfterReset, uint8_t lper100km, uint8_t averageSpeed)
    {
        CanTrip1PacketSender tripInfoSender(canMessageSender);
        tripInfoSender.SendTripInfo(mileageAfterReset, lper100km, averageSpeed);
    }

    void SendCanTripInfo2(int mileageAfterReset, uint8_t lper100km, uint8_t averageSpeed)
    {
        CanTrip2Sender tripInfoSender(canMessageSender);
        tripInfoSender.SendTripInfo(mileageAfterReset, lper100km, averageSpeed);
    }

    public:
    CanTripInfoHandler(AbstractCanMessageSender * object)
    {
        canMessageSender = object;
        IsSendingEnabled = 1;
    }

    void TripResetHappened()
    {
        // After we reset the trip data we have a message with the trip button status set to 1.
        // To prevent the display from cycling the trip data we swallow this status
        PreventTripChange = true;
    }

    void TripButtonPress()
    {
        if (PreventTripChange)
        {
            PreventTripChange = false;
            return;
        }
        if (TripButtonPressed == 0 && IsSendingEnabled == 1)
        {
            IsSendingEnabled = 0;
            TripButtonPressed = 1;
            uint8_t messageSentCount = 0;

            // we have to send the "trip button pressed" state several times to change the trip computer on the display
            // the display changes the trip computer after we don't send the "trip button pressed" state any more
            while (messageSentCount < CAN_TRIP_SEND_COUNT)
            {
                SendCanTripInfo0(FuelLeftToPump, FuelConsumption, Speed, TripButtonPressed);

                messageSentCount++;
                vTaskDelay(5 / portTICK_PERIOD_MS);
            }
            messageSentCount = 0;
            while (messageSentCount < CAN_TRIP_SEND_COUNT)
            {
                SendCanTripInfo0(FuelLeftToPump, FuelConsumption, Speed, 0);

                messageSentCount++;
                vTaskDelay(5 / portTICK_PERIOD_MS);
            }
            TripButtonPressed = 0;
            IsSendingEnabled = 1;
        }
    }

    void SetTripData(int rpm, int speed,
                     int trip1Distance, int trip1Speed, int trip1Consumption,
                     int trip2Distance, int trip2Speed, int trip2Consumption,
                     int fuelConsumption,
                     int fuelLeftToPump
    )
    {
        Rpm = rpm;
        Speed = speed;
        Trip1Distance = trip1Distance;
        Trip1Speed = trip1Speed;
        Trip1Consumption = trip1Consumption;
        Trip2Distance = trip2Distance;
        Trip2Speed = trip2Speed;
        Trip2Consumption = trip2Consumption;
        FuelConsumption = fuelConsumption;
        FuelLeftToPump = fuelLeftToPump;
    }

    void Process(unsigned long currentTime)
    {
        if (IsSendingEnabled == 1 && currentTime - previousTrip0Time > CAN_TRIP_INTERVAL)
        {
            previousTrip0Time = currentTime;

            if (FuelConsumption == 0xFFFF)
            {
                FuelConsumption = 0;
            }
            if (Trip1Consumption == 0xFFFF)
            {
                Trip1Consumption = 0;
            }

            switch (ValueToUpdate)
            {
                case 1:
                {
                    SendCanTripInfo0(FuelLeftToPump, FuelConsumption, Speed, TripButtonPressed);
                    break;
                }
                case 2:
                {
                    SendCanTripInfo1(Trip1Distance, Trip1Consumption, Trip1Speed);
                    break;
                }
                case 3:
                {
                    SendCanTripInfo2(Rpm, FuelConsumption, Speed);
                    ValueToUpdate = 0;
                    break;
                }
                default:
                {
                    ValueToUpdate = 0;
                }
            }
            ValueToUpdate++;
        }
    }
};

#endif
