/* ESP32 VAN bus to CAN bus protocol bridge software for Peugeot 307
 * 
 * The software is distributed under Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
*/
#include <Arduino.h>
#include <esp32_arduino_rmt_van_rx.h>
#include <ArduinoLog.h>
#include <BluetoothSerial.h>
#include "driver/can.h"

#include "cppQueue.h"
#include "Serializer.h"
#include "PacketGenerator.h"

#include "CanMessageSender.h"
#include "CanDisplayStructs.h"
#include "CanDash1Structs.h"
#include "CanDash2Structs.h"
#include "CanIgnitionStructs.h"
#include "CanMenuStructs.h"
#include "CanDoorStatusStructs.h"
#include "CanDisplayPopupItem.h"
#include "CanAirConStructs.h"
#include "CanRadioRemoteStructs.h"
#include "CanVinHandler.h"
#include "CanTripInfoHandler.h"
#include "CanAirConOnDisplayHandler.h"

#include "CanWarningLogStructs.h"
#include "CanStatusOfFunctionsStructs.h"
#include "CanDefineVehicleParameters.h"

#include "CanDisplayPopupHandler.h"
#include "VanCanDisplayPopupMap.h"

#include "VanMessageSender.h"
#include "VanAirConditioner1Structs.h"
#include "VanAirConditioner2Structs.h"
#include "VanDisplayStructs.h"
#include "VanSpeedAndRpmStructs.h"
#include "VanCarStatusWithTripComputerStructs.h"
#include "VanDashboardStructs.h"
#include "VanVinStructs.h"
#include "VanRadioRemoteStructs.h"
#include "VanCanAirConditionerSpeedMap.h";
#include "DoorStatus.h"

//#define UseBlueToothSerial;
#define TestMode;

ESP32_RMT_VAN_RX VAN_RX;

const uint8_t VAN_DATA_RX_RMT_CHANNEL = 0;
const uint8_t VAN_DATA_RX_PIN = 19;
//const uint8_t VAN_DATA_RX_PIN = 35;
const uint8_t VAN_DATA_RX_LED_INDICATOR_PIN = 2;

const uint8_t CAN_RX_PIN = 5;
const uint8_t CAN_TX_PIN = 4;

//const int CAN_RADIO_INTERVAL = 50;
const int CAN_RADIO_INTERVAL = 100;
const int CAN_AIRCON_INTERVAL = 100;

const bool SILENT_MODE = false;

struct VanDataToBridgeToCan
{
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
    int InternalTemperature = 0;
    uint8_t RadioRemoteButton = 0;
    uint8_t RadioRemoteScroll = 0;
    uint8_t IsHeatingPanelPoweredOn = 0; // Displays off
    uint8_t IsAirConEnabled = 0;   // AC enabled, but should consider IsAirConRunning also
    uint8_t IsAirConRunning = 0;
    uint8_t IsWindowHeatingOn = 0;
    uint8_t IsAirRecyclingOn = 0;
    uint8_t AirConFanSpeed = 0;
    uint8_t TripInfoButtonPressed = 0;
};

struct VanVinToBridgeToCan
{
    uint8_t Vin[17] = { 0 };
};

struct VanIgnitionDataToBridgeToCan
{
    int OutsideTemperature = 0;
    uint8_t EconomyModeActive = 0;
    uint8_t Ignition = 0;
};

TaskHandle_t CANSendIgnitionTask;
TaskHandle_t CANSendDataTask;
TaskHandle_t CANSendVinTask;
TaskHandle_t VANReadTask;
TaskHandle_t CANReadTask;

uint8_t queueSize = 1;
QueueHandle_t dataQueue;
QueueHandle_t ignitionQueue;
QueueHandle_t vinQueue;

AbstractCanMessageSender *CANInterface;
CanDisplayPopupHandler *canPopupHandler;
VanCanDisplayPopupMap *popupMapping;
CanVinHandler *canVinHandler;
CanTripInfoHandler *tripInfoHandler;
CanAirConOnDisplayHandler *canAirConOnDisplayHandler;
CanRadioRemoteButtonPacketSender *radioRemoteSender;
VanCanAirConditionerSpeedMap *vanCanAirConditionerSpeedMap;

#ifdef UseBlueToothSerial
    BluetoothSerial SerialBT;
#endif

uint8_t canMsg[20] = { 0 };
int msgId = 0x0;

void setup()
{

    #ifdef UseBlueToothSerial
    SerialBT.begin("ESP32 Arduino VAN bus monitor"); //Bluetooth device name
    Serial.begin(500000);
    #else
    //Serial.begin(115200);
    Serial.begin(230400);
    //Serial.begin(500000);
    #endif

    // Pass log level, whether to show log level, and print interface.
    /* Available levels are:
        * 0 - LOG_LEVEL_SILENT     no output
        * 1 - LOG_LEVEL_FATAL      fatal errors
        * 2 - LOG_LEVEL_ERROR      all errors
        * 3 - LOG_LEVEL_WARNING    errors, and warnings
        * 4 - LOG_LEVEL_NOTICE     errors, warnings and notices
        * 5 - LOG_LEVEL_TRACE      errors, warnings, notices & traces
        * 6 - LOG_LEVEL_VERBOSE    all
    */
    if (SILENT_MODE)
    {
        #ifdef UseBlueToothSerial
        Log.begin(LOG_LEVEL_SILENT, &SerialBT);
        #else
        Log.begin(LOG_LEVEL_SILENT, &Serial);
        #endif
    }
    else
    {
        #ifdef UseBlueToothSerial
        Log.begin(LOG_LEVEL_VERBOSE, &SerialBT);
        #else
        //Log.begin(LOG_LEVEL_WARNING, &Serial);
        //Log.begin(LOG_LEVEL_VERBOSE, &Serial);
        Log.begin(LOG_LEVEL_SILENT, &Serial);
        #endif
    }

    Log.trace("ESP32 Arduino VAN bus monitor\n");

    VAN_RX.Init(VAN_DATA_RX_RMT_CHANNEL, VAN_DATA_RX_PIN, VAN_DATA_RX_LED_INDICATOR_PIN);

    CANInterface = new CanMessageSender(CAN_RX_PIN, CAN_TX_PIN);
    CANInterface->Init();

    canPopupHandler = new CanDisplayPopupHandler(CANInterface);
    popupMapping = new VanCanDisplayPopupMap();
    canVinHandler = new CanVinHandler(CANInterface);
    tripInfoHandler = new CanTripInfoHandler(CANInterface);
    canAirConOnDisplayHandler = new CanAirConOnDisplayHandler(CANInterface);
    radioRemoteSender = new CanRadioRemoteButtonPacketSender(CANInterface);
    vanCanAirConditionerSpeedMap = new VanCanAirConditionerSpeedMap();

    dataQueue = xQueueCreate(queueSize, sizeof(VanDataToBridgeToCan));
    ignitionQueue = xQueueCreate(1, sizeof(VanIgnitionDataToBridgeToCan));
    vinQueue = xQueueCreate(1, sizeof(VanVinToBridgeToCan));

    xTaskCreatePinnedToCore(
        CANSendIgnitionTaskFunction,    /* Function to implement the task */
        "CANSendIgnitionTask",          /* Name of the task */
        10000,                          /* Stack size in words */
        NULL,                           /* Task input parameter */
        2,                              /* Priority of the task */
        &CANSendIgnitionTask,           /* Task handle. */
        0);                             /* Core where the task should run */

    xTaskCreatePinnedToCore(
        CANSendDataTaskFunction,        /* Function to implement the task */
        "CANSendDataTask",              /* Name of the task */
        10000,                          /* Stack size in words */
        NULL,                           /* Task input parameter */
        0,                              /* Priority of the task */
        &CANSendDataTask,               /* Task handle. */
        0);                             /* Core where the task should run */

    xTaskCreatePinnedToCore(
        VANTask,                        /* Function to implement the task */
        "VANReadTask",                  /* Name of the task */
        10000,                          /* Stack size in words */
        NULL,                           /* Task input parameter */
        0,                              /* Priority of the task */
        &VANReadTask,                   /* Task handle. */
        1);                             /* Core where the task should run */

    //xTaskCreatePinnedToCore(
    //    CANReadTaskFunction,            /* Function to implement the task */
    //    "CANReadTask",                  /* Name of the task */
    //    10000,                          /* Stack size in words */
    //    NULL,                           /* Task input parameter */
    //    0,                              /* Priority of the task */
    //    &CANReadTask,                   /* Task handle. */
    //    1);                             /* Core where the task should run */
}

void CANReadTaskFunction(void * parameter)
{
    static uint32_t lastCanReadMillis = 0;
    unsigned long currentTime = millis();
    uint8_t canReadMessage[20] = { 0 };
    uint8_t canReadMessageLength = 0;

    for (;;)
    {
        currentTime = millis();
        CANInterface->ReadMessage(&canReadMessageLength, canReadMessage);

        printf("CAN READ: ");
        for (size_t i = 0; i < canReadMessageLength; i++)
        {
            if (i != canReadMessageLength - 1)
            {
                printf("%02X ", canReadMessage[i]);
            }
            else
            {
                printf("%02X", canReadMessage[i]);
            }
        }
        printf("\n");
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void CANSendDataTaskFunction(void * parameter)
{
    unsigned long currentTime = millis();
    unsigned long previousRadioTime = millis();
    unsigned long previousCanPopupTime = millis();
    unsigned long previousAirConTime = millis();

    VanDataToBridgeToCan dataToBridgeReceived;
    VanDataToBridgeToCan dataToBridge;

    for (;;)
    {
        currentTime = millis();

        if (xQueueReceive(dataQueue, &dataToBridgeReceived, portMAX_DELAY) == pdTRUE)
        {
            dataToBridge = dataToBridgeReceived;

            #pragma region TripInfo

            //if (dataToBridge.TripInfoButtonPressed == 1)
            //{
            //    Log.trace("tripInfoPress\n");

            //    tripInfoHandler->TripButtonPress(dataToBridge.TripInfoButtonPressed);
            //}
            tripInfoHandler->SetTripData(
                dataToBridge.Rpm,
                dataToBridge.Speed,
                dataToBridge.Trip1Distance,
                dataToBridge.Trip1Speed,
                dataToBridge.Trip1Consumption,
                dataToBridge.Trip2Distance,
                dataToBridge.Trip2Speed,
                dataToBridge.Trip2Consumption,
                dataToBridge.FuelConsumption,
                dataToBridge.FuelLeftToPump
            );
            tripInfoHandler->Process(currentTime);

            #pragma endregion

            #pragma region PopupMessage

            if (currentTime - previousCanPopupTime > 200)
            {
                previousCanPopupTime = currentTime;
                canPopupHandler->Process(currentTime);
            }

            #pragma endregion

            #pragma region Radio remote

            if (currentTime - previousRadioTime > CAN_RADIO_INTERVAL)
            {
                previousRadioTime = currentTime;
                radioRemoteSender->SendAsByte(dataToBridge.RadioRemoteButton, dataToBridge.RadioRemoteScroll);
            }

            #pragma endregion

            #pragma region AirCon

            if (currentTime - previousAirConTime > CAN_AIRCON_INTERVAL)
            {
                previousAirConTime = currentTime;

                if (!canPopupHandler->IsPopupVisible())
                {
                    canAirConOnDisplayHandler->SendCanAirConToDisplay(
                        dataToBridge.InternalTemperature, 
                        dataToBridge.InternalTemperature, 
                        0, 
                        0, // auto mode
                        dataToBridge.IsHeatingPanelPoweredOn == 1 && dataToBridge.IsAirConRunning == 0, //displays: a/c off
                        dataToBridge.IsHeatingPanelPoweredOn == 0, // displays: off
                        dataToBridge.IsWindowHeatingOn == 1, // displays: windshield icon
                        dataToBridge.AirConFanSpeed,
                        dataToBridge.IsAirRecyclingOn);
                }
            }

            #pragma endregion
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void CANSendIgnitionTaskFunction(void * parameter)
{
    VanIgnitionDataToBridgeToCan dataToBridge;
    VanVinToBridgeToCan vinDataToBridge;

    unsigned long currentTime = millis();
    unsigned long lastVinTime = millis();
    uint8_t economyMode = 0;
    uint8_t ignition = 0;

    for (;;)
    {
        xQueueReceive(ignitionQueue, &dataToBridge, 0);

        currentTime = millis();

        #ifndef TestMode
            ignition = 0;
            if (dataToBridge.Ignition == 1)
            {
                ignition = 1;
            }
            else
            {
                canPopupHandler->Reset();
            }
            economyMode = 0;
            if (dataToBridge.EconomyModeActive == 1)
            {
                economyMode = 1;
            }
        #else
            ignition = 1;
            economyMode = 0;
        #endif // TestMode

        #pragma region Ignition signal for radio

        if (ignition == 1)
        {
            CanIgnitionPacketSender radioIgnition(CANInterface);
            radioIgnition.SendIgnition(economyMode, 127);
        }
        #pragma endregion

        vTaskDelay(3 / portTICK_PERIOD_MS);

        #pragma region Ignition signal for display

        CanDashIgnitionPacketSender dashIgnition(CANInterface);
        dashIgnition.SendIgnition(ignition, dataToBridge.OutsideTemperature);

        if (dataToBridge.OutsideTemperature <= 3 && dataToBridge.OutsideTemperature >= -3 && currentTime > 10000)
        {
            if (!canPopupHandler->IsPopupVisible())
            {
                CanDisplayPopupItem item;
                item.DisplayTimeInMilliSeconds = CAN_POPUP_MESSAGE_TIME;
                item.Category = CAN_POPUP_MSG_SHOW_CATEGORY1;
                item.MessageType = CAN_POPUP_MSG_RISK_OF_ICE;
                item.DoorStatus1 = 0;
                item.DoorStatus2 = 0;
                canPopupHandler->QueueNewMessage(item);
            }
        }

        #pragma endregion

        #pragma region VIN sending

        if (currentTime - lastVinTime > 500)
        {
            lastVinTime = currentTime;
            if (!canVinHandler->IsVinSet())
            {
                if (xQueueReceive(vinQueue, &vinDataToBridge, portMAX_DELAY) == pdTRUE)
                {
                    canVinHandler->SetVin(vinDataToBridge.Vin);
                }
            }

            canVinHandler->Process(currentTime);
        }

        #pragma endregion

        vTaskDelay(65 / portTICK_PERIOD_MS);
    }
}

void CANSendVinTaskFunction(void* parameter)
{
    VanVinToBridgeToCan vinDataToBridge;
    unsigned long currentTime = millis();

    for (;;)
    {
        if (!canVinHandler->IsVinSet())
        {
            xQueueReceive(vinQueue, &vinDataToBridge, portMAX_DELAY);
            canVinHandler->SetVin(vinDataToBridge.Vin);
        }

        currentTime = millis();

        canVinHandler->Process(currentTime);

        vTaskDelay(400 / portTICK_PERIOD_MS);
    }
}

void VANTask(void * parameter)
{
    uint32_t currentTime = millis();
    uint32_t lastMillis = 0;
    uint8_t identByte1;
    uint8_t identByte2;
    uint8_t crcByte1;
    uint8_t crcByte2;
    uint16_t crcValue;

    uint8_t vanMessageLength;
    uint8_t vanMessage[34];
    uint8_t vanMessageWithoutId[32];

    DoorStatus doorStatus;
    doorStatus.asByte = 0;

    VanDataToBridgeToCan dataToBridge;
    VanIgnitionDataToBridgeToCan ignitionDataToBridge;
    VanVinToBridgeToCan vinDataToBridge;
    char tmp[3];

    for (;;)
    {
        if (millis() - lastMillis > 10)
        {
            if (dataToBridge.TripInfoButtonPressed == 1)
            {
                dataToBridge.TripInfoButtonPressed = 0;
            }
            VAN_RX.Receive(&vanMessageLength, vanMessage);
            ///*
            if (Serial.available() > 0) {
                ///*
                vanMessageLength = 0;
                uint8_t inChar = (uint8_t)Serial.read();
                if (inChar == 'v') { // got a sync byte?
                    while (Serial.available()) {
                        vanMessage[vanMessageLength] = Serial.read();
                        vanMessageLength++;
                    }
                }
                else if (inChar == 's') { // got a sync byte?
                    uint8_t canMsgLength = 0;
                    while (Serial.available()) {
                        canMsg[canMsgLength] = Serial.read();
                        canMsgLength++;
                    }

                    for (size_t i = 0; i < canMsgLength; i++)
                    {
                        if (i != canMsgLength - 1)
                        {
                            printf("%02X ", canMsg[i]);
                        }
                        else
                        {
                            printf("%02X", canMsg[i]);
                        }
                    }
                    uint16_t canId = (canMsg[1] & 0xFF) | (canMsg[0] << 4);//concatenate the two bytes
                    //printf("\nid: %02X \n", canId);
                    printf("\n");

                    SendSampleCan(canId, canMsg[2], canMsg[3], canMsg[4], canMsg[5], canMsg[6], canMsg[7], canMsg[8], canMsg[9]);
                }
                else if (inChar == 't')
                {
                    tripInfoHandler->TripButtonPress(1);
                }
                else if (inChar == 'm')
                {
                    SendCanRadioButton(CONST_MODE_BUTTON);
                }
                else if (inChar == 'c')
                {
                    SendSampleCan(msgId, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
                    msgId++;
                }
                else if (inChar == 'x')
                {
                    SendSampleCan(msgId, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
                    msgId--;
                }
            }
            //*/
            if (vanMessageLength > 0 && vanMessage[0] == 0x0E)
            {
                identByte1 = vanMessage[1];
                identByte2 = vanMessage[2];

                //make a copy of the buffer excluding the ids and the crc (otherwise deserializing the packet gives wrong results)
                memcpy(vanMessageWithoutId, vanMessage + 3, vanMessageLength - 5);

                if (!VAN_RX.IsCrcOk(vanMessage, vanMessageLength))
                {
                    Log.error("CRC ERROR\n");
                    /*
                    for (size_t i = 0; i < vanMessageLength - 3; i++)
                    {
                        snprintf(tmp, 3, "%02X", vanMessageWithoutId[i]);
                        #ifdef UseBlueToothSerial
                        SerialBT.print(tmp);
                        SerialBT.print(" ");
                        #else
                        Serial.print(tmp);
                        Serial.print(" ");
                        #endif // UseBluetoothSerial
                    }
                    #ifdef UseBlueToothSerial
                    SerialBT.println();
                    #else
                    Serial.println();
                    #endif // UseBluetoothSerial
                    Log.error("CRC byte 1: %X\n", crcByte1);
                    Log.error("CRC byte 2: %X\n", crcByte2);
                    Log.error("CRC : %X\n", crcValue);
                    */
                    continue;
                }

                #pragma region Popup message
                if (IsVanIdent(identByte1, identByte2, VAN_ID_DISPLAY_POPUP))
                {
                    VanDisplayPacket packet = DeSerialize<VanDisplayPacket>(vanMessageWithoutId);
                    if (packet.data.Message != 0xFF)
                    {
                        CanDisplayPopupItem item;
                        item.DisplayTimeInMilliSeconds = CAN_POPUP_MESSAGE_TIME;
                        item.Category = popupMapping->GetCanCategoryFromVanMessage(packet.data.Message);
                        item.MessageType = popupMapping->GetCanMessageIdFromVanMessage(packet.data.Message);
                        item.DoorStatus1 = 0;
                        item.DoorStatus2 = 0;
                        canPopupHandler->QueueNewMessage(item);
                    }
                    if (packet.data.Field5.seatbelt_warning)
                    {
                        if (dataToBridge.Speed > 10)
                        {
                            CanDisplayPopupItem item;
                            item.DisplayTimeInMilliSeconds = CAN_POPUP_MESSAGE_TIME;
                            item.Category = CAN_POPUP_MSG_SHOW_CATEGORY1;
                            item.MessageType = CAN_POPUP_MSG_FRONT_SEAT_BELTS_NOT_FASTENED;
                            item.DoorStatus1 = CAN_POPUP_SEAT_BELTS_OF_DRIVER;
                            item.DoorStatus2 = 0;
                            canPopupHandler->QueueNewMessage(item);
                        }
                        else
                        {
                            canPopupHandler->ResetSeatBeltWarning();
                        }
                    }
                    else
                    {
                        canPopupHandler->ResetSeatBeltWarning();
                    }
                    if (packet.data.Field6.left_stick_button)
                    {
                        dataToBridge.TripInfoButtonPressed = 1;
                        tripInfoHandler->TripButtonPress(1);
                    }
                }
                #pragma endregion
                #pragma region Speed and RPM
                else if (IsVanIdent(identByte1, identByte2, VAN_ID_SPEED_RPM))
                {
                    VanSpeedAndRpmPacket packet = DeSerialize<VanSpeedAndRpmPacket>(vanMessageWithoutId);
                    if (packet.data.Rpm.data == 0xFFFF)
                    {
                        dataToBridge.Rpm = 0;
                        dataToBridge.Speed = 0;
                    }
                    else
                    {
                        dataToBridge.Rpm = VanDecodeRpmOrSpeed(packet.data.Rpm.data) / 8;
                        dataToBridge.Speed = VanDecodeRpmOrSpeed(packet.data.Speed.data) / 100;
                    }
                }
                #pragma endregion
                #pragma region Trip computer
                else if (IsVanIdent(identByte1, identByte2, VAN_ID_CARSTATUS))
                {
                    VanCarStatusWithTripComputerPacket packet = DeSerialize<VanCarStatusWithTripComputerPacket>(vanMessageWithoutId);

                    dataToBridge.Trip1Consumption = SwapHiByteAndLoByte(packet.data.Trip1FuelConsumption.data);
                    dataToBridge.Trip1Distance = SwapHiByteAndLoByte(packet.data.Trip1Distance.data);
                    dataToBridge.Trip1Speed = packet.data.Trip1Speed;
                    dataToBridge.Trip2Consumption = SwapHiByteAndLoByte(packet.data.Trip2FuelConsumption.data);
                    dataToBridge.Trip2Distance = SwapHiByteAndLoByte(packet.data.Trip2Distance.data);
                    dataToBridge.Trip2Speed = packet.data.Trip2Speed;
                    dataToBridge.FuelConsumption = SwapHiByteAndLoByte(packet.data.FuelConsumption.data);
                    dataToBridge.FuelLeftToPump = SwapHiByteAndLoByte(packet.data.FuelLeftToPumpInKm.data);
                    dataToBridge.TripInfoButtonPressed = packet.data.Field10.TripButton;

                    if (dataToBridge.TripInfoButtonPressed)
                    {
                        tripInfoHandler->TripButtonPress(dataToBridge.TripInfoButtonPressed);
                    }

                    doorStatus.status.FrontLeft = packet.data.Doors.FrontLeft;
                    doorStatus.status.FrontRight = packet.data.Doors.FrontRight;
                    doorStatus.status.RearLeft = packet.data.Doors.RearLeft;
                    doorStatus.status.RearRight = packet.data.Doors.RearRight;
                    doorStatus.status.BootLid = packet.data.Doors.BootLid;

                    if (doorStatus.asByte != 0)
                    {
                        CanDisplayPopupItem item;
                        item.DisplayTimeInMilliSeconds = CAN_POPUP_DOOR_MESSAGE_TIME;
                        item.Category = CAN_POPUP_MSG_SHOW_CATEGORY1;
                        item.MessageType = CAN_POPUP_MSG_DOORS_BOOT_BONNET_REAR_SCREEN_AND_FUEL_TANK_OPEN;
                        item.DoorStatus1 = doorStatus.asByte;
                        item.DoorStatus2 = 0;
                        canPopupHandler->QueueNewMessage(item);
                    }
                }
                #pragma endregion
                #pragma region Outside temperature
                else if (IsVanIdent(identByte1, identByte2, VAN_ID_DASHBOARD))
                {
                    VanDashboardPacket packet = DeSerialize<VanDashboardPacket>(vanMessageWithoutId);
                    ignitionDataToBridge.OutsideTemperature = GetTemperatureFromVANByte(packet.data.ExternalTemperature.value);
                    ignitionDataToBridge.EconomyModeActive = packet.data.Field1.economy_mode;
                    ignitionDataToBridge.Ignition = packet.data.Field1.ignition_on || packet.data.Field1.accesories_on || packet.data.Field1.engine_running;
                    xQueueOverwrite(ignitionQueue, (void*)& ignitionDataToBridge);
                }
                #pragma endregion
                #pragma region Vin
                else if (IsVanIdent(identByte1, identByte2, VAN_ID_VIN))
                {
                    if (!canVinHandler->IsVinSet())
                    {
                        memcpy(vinDataToBridge.Vin, vanMessageWithoutId, vanMessageLength - 5);
                        xQueueOverwrite(vinQueue, (void*)& vinDataToBridge);
                    }
                }
                #pragma endregion
                #pragma region AirCon1
                else if (IsVanIdent(identByte1, identByte2, VAN_ID_AIR_CONDITIONER_1))
                {
                    VanAirConditioner1Packet packet = DeSerialize<VanAirConditioner1Packet>(vanMessageWithoutId);
                    if (
                           vanMessageWithoutId[0] == 0x00 && (packet.data.FanSpeed == 0x00)  // off
                        || vanMessageWithoutId[0] == 0x00 && (packet.data.FanSpeed == 0x0E)  // off + rear window heating
                        || vanMessageWithoutId[0] == 0x01 && (packet.data.FanSpeed == 0x0E)  // off + rear window heating toggle
                        || vanMessageWithoutId[0] == 0x04 && (packet.data.FanSpeed == 0x00)  // off + recycle
                        || vanMessageWithoutId[0] == 0x04 && (packet.data.FanSpeed == 0x0E)  // off + rear window heating + recycle
                        || vanMessageWithoutId[0] == 0x05 && (packet.data.FanSpeed == 0x00)  // off + rear window heating + recycle toggle
                        || vanMessageWithoutId[0] == 0x05 && (packet.data.FanSpeed == 0x0E)  // off + rear window heating + recycle toggle
                    )
                    {
                        dataToBridge.IsHeatingPanelPoweredOn = 0;
                        dataToBridge.IsAirConEnabled = 0;
                        dataToBridge.IsAirConRunning = 0;
                        dataToBridge.IsWindowHeatingOn = 0;
                        dataToBridge.AirConFanSpeed = 0;
                    }
                    else
                    {
                        dataToBridge.IsHeatingPanelPoweredOn = 1;
                        dataToBridge.IsAirConEnabled = packet.data.Status.aircon_on_if_necessary;
                        dataToBridge.IsAirRecyclingOn = packet.data.Status.recycling_on;

                        dataToBridge.AirConFanSpeed = vanCanAirConditionerSpeedMap->GetFanSpeedFromVANByte(packet.data.FanSpeed, dataToBridge.IsAirConEnabled, dataToBridge.IsWindowHeatingOn, dataToBridge.IsAirRecyclingOn);
                    }
                }
                #pragma endregion
                #pragma region AirCon2
                else if (IsVanIdent(identByte1, identByte2, VAN_ID_AIR_CONDITIONER_2))
                {
                    VanAirConditioner2Packet packet = DeSerialize<VanAirConditioner2Packet>(vanMessageWithoutId);
                    if (dataToBridge.IsHeatingPanelPoweredOn == 1)
                    {
                        dataToBridge.IsAirConRunning = packet.data.Status.ac_on && packet.data.Status.ac_compressor_running;
                        dataToBridge.IsWindowHeatingOn = packet.data.Status.rear_window_heating_on;
                    }

                    dataToBridge.InternalTemperature = GetInternalTemperature(packet.data.InternalTemperature);
                }
                #pragma endregion
                #pragma region Radio remote
                else if (IsVanIdent(identByte1, identByte2, VAN_ID_RADIO_REMOTE))
                {
                    VanRadioRemotePacket packet = DeSerialize<VanRadioRemotePacket>(vanMessageWithoutId);
                    dataToBridge.RadioRemoteButton = packet.VanRadioRemotePacket[0];
                    dataToBridge.RadioRemoteScroll = packet.VanRadioRemotePacket[1];

                    if (packet.data.RemoteButton.seek_down_pressed && packet.data.RemoteButton.seek_up_pressed)
                    {
                        dataToBridge.TripInfoButtonPressed = 1;
                        tripInfoHandler->TripButtonPress(1);
                    }
                }
                #pragma endregion

                if (!SILENT_MODE 
                    //&& (IsVanIdent(identByte1, identByte2, VAN_ID_AIR_CONDITIONER_1))
                    //&& (IsVanIdent(identByte1, identByte2, VAN_ID_DISPLAY_POPUP))
                    //&& identByte1 == 0x8a
                    //|| (IsVanIdent(identByte1, identByte2, VAN_ID_AIR_CONDITIONER_2))
                    )
                {
                    ///*
                    for (size_t i = 0; i < vanMessageLength; i++)
                    {
                        snprintf(tmp, 3, "%02X", vanMessage[i]);
                        if (i != vanMessageLength - 1)
                        {
                            #ifdef UseBlueToothSerial
                            SerialBT.print(tmp);
                            SerialBT.print(" ");
                            #else
                            Serial.print(tmp);
                            Serial.print(" ");
                            #endif // UseBluetoothSerial
                        }
                        else
                        {
                            #ifdef UseBlueToothSerial
                            SerialBT.println(tmp);
                            #else
                            Serial.println(tmp);
                            #endif // UseBluetoothSerial
                        }
                    }
                    //*/
                }
                xQueueOverwrite(dataQueue, (void *)&dataToBridge);
            }
            vanMessageLength = 0;
        }
    }
}

/* Concatenates the two bytes and removes the last digit and compares the result to the IDENT */
bool IsVanIdent(uint8_t byte1, uint8_t byte2, uint16_t ident)
{
    bool result = false;
    int combined = byte1 << 8 | byte2;
    result = combined >> 4 == ident;
    return result;
}

int SwapHiByteAndLoByte(int input)
{
    return ((input & 0xff) << 8) | ((input >> 8) & 0xff);
}

void SendSampleCan(unsigned long canId, uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4, uint8_t byte5, uint8_t byte6, uint8_t byte7)
{
    PacketGenerator<CanDisplayPacket> generator;

    unsigned char *serializedPacket = generator.GetSerializedPacket();
    /*
    serializedPacket[0] = 255;
    serializedPacket[1] = 255;
    serializedPacket[2] = 255;
    serializedPacket[3] = 255;
    serializedPacket[4] = 255;
    serializedPacket[5] = 255;
    serializedPacket[6] = 255;
    serializedPacket[7] = 255;
    */
    serializedPacket[0] = byte0;
    serializedPacket[1] = byte1;
    serializedPacket[2] = byte2;
    serializedPacket[3] = byte3;
    serializedPacket[4] = byte4;
    serializedPacket[5] = byte5;
    serializedPacket[6] = byte6;
    serializedPacket[7] = byte7;
    Serial.println(canId,HEX);
    SendCANMessage(canId, 0, 8, serializedPacket);
}

void SendCANMessage(unsigned long canId, uint8_t ext, uint8_t sizeOfByteArray, unsigned char *byteArray)
{
    CANInterface->SendMessage(canId, ext, sizeOfByteArray, byteArray);
}

void SendCanRadioButton(int buttonId)
{
    CanRadioButtonPacketSender radioButtonSender(CANInterface);
    radioButtonSender.SendButtonCode(buttonId);
}

void loop()
{
    delay(100);
}