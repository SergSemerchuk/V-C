#pragma region Includes

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp32_arduino_rmt_van_rx.h>
#include <ArduinoLog.h>

#include "Config.h"
#include "src/SerialPort/AbstractSerial.h"

#ifdef USE_BLUETOOTH_SERIAL
    #include <BluetoothSerial.h>
    #include "src/SerialPort/BluetoothSerialAbs.h"
#else
    #include "src/SerialPort/HardwareSerialAbs.h"
#endif

#include "src/Can/CanMessageSenderEsp32Arduino.h"
#include "src/Can/Structs/CanDisplayStructs.h"
#include "src/Can/Structs/CanDisplayStatusStructs.h"
#include "src/Can/Structs/CanDash1Structs.h"
#include "src/Can/Structs/CanIgnitionStructs.h"
#include "src/Can/Structs/CanMenuStructs.h"
#include "src/Can/Structs/CanParkingAidStructs.h"
#include "src/Can/Handlers/CanRadioRd4DiagHandler.h"
#include "src/Can/Handlers/CanRadioRemoteMessageHandler.h"
#include "src/Can/Handlers/CanVinHandler.h"
#include "src/Can/Handlers/CanTripInfoHandler.h"
#include "src/Can/Handlers/CanStatusOfFunctionsHandler.h"
#include "src/Can/Handlers/CanWarningLogHandler.h"
#include "src/Can/Handlers/CanSpeedAndRpmHandler.h"
#include "src/Can/Handlers/CanDash2MessageHandler.h"
#include "src/Can/Handlers/CanDash3MessageHandler.h"
#include "src/Can/Handlers/CanDash4MessageHandler.h"
#include "src/Can/Handlers/CanParkingAidHandler.h"
#include "src/Can/Handlers/CanDisplayPopupHandler2.h"
#ifdef USE_NEW_AIRCON_DISPLAY_SENDER
#include "src/Can/Handlers/CanAirConOnDisplayHandler.h"
#else
#include "src/Can/Handlers/CanAirConOnDisplayHandlerOrig.h"
#endif

#include "src/Van/AbstractVanMessageSender.h"

#if HW_VERSION == 14
#include "src/Van/VanMessageSender.h"
#include "src/Van/VanWriterContainer.h"
#endif

#include "src/Van/Structs/VanVinStructs.h"

#include "src/Helpers/CanDisplayPopupItem.h"
#include "src/Helpers/DoorStatus.h"
#include "src/Helpers/VanDataToBridgeToCan.h"
#include "src/Helpers/VanIgnitionDataToBridgeToCan.h"

#include "src/Van/VanHandlerContainer.h"

#pragma endregion

const uint8_t VAN_DATA_RX_RMT_CHANNEL = 0;

#if HW_VERSION == 11
const uint8_t VAN_DATA_RX_PIN = 21;
const VAN_LINE_LEVEL VAN_DATA_RX_LINE_LEVEL = VAN_LINE_LEVEL_HIGH;

const uint8_t CAN_RX_PIN = 33;
const uint8_t CAN_TX_PIN = 32;
#elif HW_VERSION == 14
const uint8_t VAN_DATA_RX_PIN = 21;
const VAN_LINE_LEVEL VAN_DATA_RX_LINE_LEVEL = VAN_LINE_LEVEL_HIGH;

const uint8_t CAN_RX_PIN = 18;
const uint8_t CAN_TX_PIN = 15;

TaskHandle_t VANWriteTask;
#endif

const uint8_t VAN_DATA_RX_LED_INDICATOR_PIN = 2;
const bool SILENT_MODE = false;

bool reverseEngaged = false;

ESP32_RMT_VAN_RX VAN_RX;

struct VanVinToBridgeToCan
{
    uint8_t Vin[17] = { 0 };
};

TaskHandle_t CANSendIgnitionTask;
TaskHandle_t CANSendDataTask;

TaskHandle_t VANReadTask;
TaskHandle_t CANReadTask;

const uint8_t QUEUE_SIZE = 1;
QueueHandle_t dataQueue;
QueueHandle_t ignitionQueue;
QueueHandle_t vinQueue;

AbstractCanMessageSender* CANInterface;
CanDisplayPopupHandler* canPopupHandler;
CanVinHandler* canVinHandler;
CanTripInfoHandler* tripInfoHandler;
CanAirConOnDisplayHandler* canAirConOnDisplayHandler;
CanRadioRemoteMessageHandler* canRadioRemoteMessageHandler;
CanStatusOfFunctionsHandler* canStatusOfFunctionsHandler;
CanWarningLogHandler* canWarningLogHandler;
CanSpeedAndRpmHandler* canSpeedAndRpmHandler;
CanDash2MessageHandler* canDash2MessageHandler;
CanDash3MessageHandler* canDash3MessageHandler;
CanDash4MessageHandler* canDash4MessageHandler;
CanIgnitionPacketSender* radioIgnition;
CanDashIgnitionPacketSender* dashIgnition;
CanRadioRd4DiagHandler* canRadioDiag;
CanParkingAidHandler* canParkingAid;

VanHandlerContainer* vanHandlerContainer;

AbsSer *serialPort;

#ifdef USE_BLUETOOTH_SERIAL
    BluetoothSerial SerialBT;
#endif

void PrintArrayToSerial(const uint8_t vanMessage[], uint8_t vanMessageLength)
{
    char tmp[3];
    for (uint8_t i = 0; i < vanMessageLength; i++)
    {
        snprintf(tmp, 3, "%02X", vanMessage[i]);
        if (i != vanMessageLength - 1)
        {
            serialPort->print(tmp);
            serialPort->print(" ");
        }
        else
        {
            serialPort->println(tmp);
        }
    }
}

void CANReadTaskFunction(void * parameter)
{
    uint8_t canReadMessage[20] = { 0 };
    uint8_t canReadMessageLength = 0;
    uint16_t canId = 0;

    for (;;)
    {
        canId = 0;
        canReadMessageLength = 0;
        CANInterface->ReadMessage(&canId, &canReadMessageLength, canReadMessage);

        if (canId > 0)
        {
            if (canId == CAN_ID_MENU_BUTTONS)
            {
                CanMenuPacket packet = DeSerialize<CanMenuPacket>(canReadMessage);
                if (packet.data.EscOkField.esc == 1 && canPopupHandler->IsPopupVisible())
                {
                    canPopupHandler->HideCurrentPopupMessage();
                }
            }
            if (canId == CAN_ID_RADIO_RD4_DIAG_ANSWER)
            {
                canRadioDiag->ProcessReceivedCanMessage(canId, canReadMessageLength, canReadMessage);
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

void CANSendDataTaskFunction(void * parameter)
{
    unsigned long currentTime = millis();
    uint8_t ignition = 0;
    uint16_t trip0Icon1Data = 0;
    uint16_t trip0Icon2Data = 0;
    uint16_t trip0Icon3Data = 0;

    uint16_t trip1Icon1Data = 0;
    uint16_t trip1Icon2Data = 0;
    uint16_t trip1Icon3Data = 0;

    uint16_t trip2Icon1Data = 0;
    uint16_t trip2Icon2Data = 0;
    uint16_t trip2Icon3Data = 0;

    VanDataToBridgeToCan dataToBridgeReceived;
    VanDataToBridgeToCan dataToBridge;

    for (;;)
    {
        currentTime = millis();

        #ifdef USE_IGNITION_SIGNAL_FROM_VAN_BUS
            ignition = dataToBridge.Ignition;
        #else
            ignition = 1;

            canSpeedAndRpmHandler->SetData(dataToBridge.Speed, dataToBridge.Rpm);
            canSpeedAndRpmHandler->Process(currentTime);
        #endif // USE_IGNITION_SIGNAL_FROM_VAN_BUS

        if (xQueueReceive(dataQueue, &dataToBridgeReceived, portMAX_DELAY) == pdTRUE)
        {
            dataToBridge = dataToBridgeReceived;

            #pragma  region SpeedAndRpm

            canSpeedAndRpmHandler->SetData(dataToBridge.Speed, dataToBridge.Rpm);
            canSpeedAndRpmHandler->Process(currentTime);

            #pragma endregion

            #pragma region TripInfo

#if DISPLAY_MODE == 1
            trip0Icon1Data = dataToBridge.FuelLeftToPump; //the distance remaining to be travelled
            trip0Icon2Data = dataToBridge.FuelConsumption; //the current consumption
            trip0Icon3Data = dataToBridge.Trip1Distance; //the range

            trip1Icon1Data = dataToBridge.Trip1Distance;
            trip1Icon2Data = dataToBridge.Trip1Consumption;
            trip1Icon3Data = dataToBridge.Trip1Speed;

            trip2Icon1Data = dataToBridge.Trip2Distance;
            trip2Icon2Data = dataToBridge.Trip2Consumption;
            trip2Icon3Data = dataToBridge.Trip2Speed;
#endif
#if DISPLAY_MODE == 2
            trip0Icon1Data = round(FUEL_TANK_CAPACITY_IN_LITERS * dataToBridge.FuelLevel / 100);
            trip0Icon2Data = dataToBridge.FuelConsumption; //the current consumption
            trip0Icon3Data = dataToBridge.Speed;

            trip1Icon1Data = dataToBridge.Trip1Distance;
            trip1Icon2Data = dataToBridge.Trip1Consumption;
            trip1Icon3Data = dataToBridge.Trip1Speed;

            trip2Icon1Data = dataToBridge.Rpm;
            trip2Icon2Data = dataToBridge.FuelConsumption;
            trip2Icon3Data = dataToBridge.Speed;

            if (dataToBridge.LeftStickButtonPressed)
            {
                trip0Icon1Data = dataToBridge.FuelLevel;
                trip0Icon3Data = dataToBridge.OilTemperature;
            }
#endif
            tripInfoHandler->SetTripData(
                trip2Icon1Data,
                trip0Icon3Data,
                trip1Icon1Data,
                trip1Icon3Data,
                trip1Icon2Data,
                trip2Icon1Data,
                trip2Icon3Data,
                trip2Icon2Data,
                trip0Icon2Data,
                trip0Icon1Data
            );
            tripInfoHandler->Process(currentTime);

            #pragma endregion

            #pragma region PopupMessage

            if (dataToBridge.Rpm > 500) {
            	canPopupHandler->SetEngineRunning(true);
            }

            canPopupHandler->Process(currentTime);

            #pragma endregion

            #pragma region Radio remote

            canRadioRemoteMessageHandler->Process(currentTime);

            #pragma endregion

            #pragma region AirCon

            if (!canPopupHandler->IsPopupVisible())
            {
#ifdef USE_NEW_AIRCON_DISPLAY_SENDER
                canAirConOnDisplayHandler->SetData(
                    dataToBridge.InternalTemperature, 
                    dataToBridge.InternalTemperature, 
                    0, 
                    0, // auto mode
                    dataToBridge.IsHeatingPanelPoweredOn == 1 && dataToBridge.IsAirConRunning == 0, //displays: a/c off
                    dataToBridge.IsHeatingPanelPoweredOn == 0, // displays: off
                    dataToBridge.IsWindowHeatingOn == 1, // displays: windshield icon
                    dataToBridge.AirConFanSpeed,
                    dataToBridge.IsAirRecyclingOn);
                canAirConOnDisplayHandler->Process(currentTime);
#else

                canAirConOnDisplayHandler->SendCanAirConToDisplay(
                    currentTime,
                    dataToBridge.InternalTemperature,
                    dataToBridge.InternalTemperature,
                    dataToBridge.AirConDirection,
                    0, // auto mode
                    dataToBridge.IsHeatingPanelPoweredOn == 1 && dataToBridge.IsAirConEnabled == 0, //displays: a/c off
                    dataToBridge.IsHeatingPanelPoweredOn == 0, // displays: off
                    dataToBridge.IsWindowHeatingOn == 1, // displays: windshield icon
                    dataToBridge.AirConFanSpeed,
                    dataToBridge.IsAirRecyclingOn);
#endif
            }

            #pragma endregion

            #pragma region Lights

            canDash2MessageHandler->SetData(
                dataToBridge.LightStatuses,
                dataToBridge.DashIcons1Field,
                ignition
            );
            canDash2MessageHandler->Process(currentTime);

            #pragma endregion

            #pragma region Dash icons

            canDash3MessageHandler->SetData(
                dataToBridge.DashIcons1Field
            );
            canDash3MessageHandler->Process(currentTime);

            canDash4MessageHandler->SetData(dataToBridge.FuelLevel);
            canDash4MessageHandler->Process(currentTime);

            #pragma endregion
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

void CANSendIgnitionTaskFunction(void * parameter)
{
    VanIgnitionDataToBridgeToCan dataToBridge;
    VanVinToBridgeToCan vinDataToBridge;

    unsigned long currentTime = millis();
    uint8_t economyMode = 0;
    uint8_t ignition = 0;
    uint8_t brightness = 15;
    int8_t externalTemperature = 0;


    for (;;)
    {
        xQueueReceive(ignitionQueue, &dataToBridge, 0);

        currentTime = millis();

        ignition = 0;
        #ifdef USE_IGNITION_SIGNAL_FROM_VAN_BUS
            if (dataToBridge.Ignition == 1)
            {
                ignition = 1;
            }
            else
            {
                canPopupHandler->Reset();
                canStatusOfFunctionsHandler->Reset();
                canWarningLogHandler->Reset();
            }
            economyMode = 0;
            if (dataToBridge.EconomyModeActive == 1)
            {
                economyMode = 1;
            }
        #else
            ignition = 1;
            economyMode = 0;
        #endif // USE_IGNITION_SIGNAL_FROM_VAN_BUS

        if (dataToBridge.NightMode)
        {
            brightness = 7;
        }
        else
        {
            brightness = 15;
        }

        #pragma region Ignition signal for radio

        radioIgnition->SendIgnition(economyMode, brightness, dataToBridge.DashboardLightingEnabled);

        #pragma endregion

        vTaskDelay(3 / portTICK_PERIOD_MS);

        #pragma region Ignition signal for display

        externalTemperature = dataToBridge.OutsideTemperature;

        if (dataToBridge.LeftStickButtonPressed)
        {
            externalTemperature = (dataToBridge.InternalTemperature + 0.5);
        }

        dashIgnition->SendIgnition(
            ignition, 
            dataToBridge.WaterTemperature, 
            externalTemperature, 
            dataToBridge.MileageByte1, 
            dataToBridge.MileageByte2, 
            dataToBridge.MileageByte3);

        canPopupHandler->SetIgnition(true);

        if (ignition == 1 &&
            dataToBridge.OutsideTemperature <= 3 && 
            dataToBridge.OutsideTemperature >= -3 && 
            currentTime > 10000)
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

        if (!canVinHandler->IsVinSet())
        {
            if (xQueueReceive(vinQueue, &vinDataToBridge, portMAX_DELAY) == pdTRUE)
            {
                canVinHandler->SetVin(vinDataToBridge.Vin);
            }
        }

        canVinHandler->Process(currentTime);

        #pragma endregion

#pragma region Parking aid
        reverseEngaged = dataToBridge.IsReverseEngaged;

        if (dataToBridge.IsReverseEngaged && dataToBridge.HaveDataFromParkingAid == 1)
        {
            canParkingAid->SetData(dataToBridge.IsReverseEngaged, dataToBridge.IsTrailerPresent, dataToBridge.ExteriorRearLeftDistanceInCm, dataToBridge.ExteriorRearRightDistanceInCm, dataToBridge.InteriorRearLeftDistanceInCm, dataToBridge.InteriorRearRightDistanceInCm, currentTime);
            canParkingAid->Process(currentTime);
        }
#pragma endregion

        canStatusOfFunctionsHandler->Init();
        canWarningLogHandler->Init();

        vTaskDelay(65 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

void VANReadTaskFunction(void * parameter)
{
    uint8_t identByte1;
    uint8_t identByte2;
    uint8_t vanMessageLengthWithoutId;

    uint8_t vanMessageLength;
    uint8_t vanMessage[34];
    uint8_t vanMessageWithoutId[32];

    DoorStatus doorStatus;
    doorStatus.asByte = 0;

    VanDataToBridgeToCan dataToBridge;
    VanIgnitionDataToBridgeToCan ignitionDataToBridge;
    VanVinToBridgeToCan vinDataToBridge;

    for (;;)
    {
        {
            VAN_RX.Receive(&vanMessageLength, vanMessage);
            ///*
            if (serialPort->available() > 0) {
                vanMessageLength = 0;
                uint8_t inChar = (uint8_t)serialPort->read();
                if (inChar == 'v') { // got a sync byte?
                    while (serialPort->available()) {
                        vanMessage[vanMessageLength] = serialPort->read();
                        vanMessageLength++;
                    }
                }
                if (inChar == 'n') {
                    canRadioDiag->GetVin();
                }
            }
            //*/
            if (vanMessageLength > 0 && vanMessage[0] == 0x0E)
            {
                identByte1 = vanMessage[1];
                identByte2 = vanMessage[2];
                vanMessageLengthWithoutId = vanMessageLength - 5;

                //make a copy of the buffer excluding the ids and the crc (otherwise deserializing the packet gives wrong results)
                memcpy(vanMessageWithoutId, vanMessage + 3, vanMessageLengthWithoutId);

                if (!VAN_RX.IsCrcOk(vanMessage, vanMessageLength))
                {
                    Log.error("CRC ERROR\n");
                    //PrintArrayToSerial(vanMessage, vanMessageLength);
                    continue;
                }

                const bool vanMessageHandled = vanHandlerContainer->ProcessMessage(identByte1, identByte2, vanMessageWithoutId, vanMessageLengthWithoutId, dataToBridge, ignitionDataToBridge, doorStatus);

                if (vanMessageHandled)
                {
                    xQueueOverwrite(ignitionQueue, (void*)&ignitionDataToBridge);
                    xQueueOverwrite(dataQueue, (void*)&dataToBridge);
                }

                #pragma region Vin
                if (IsVanIdent(identByte1, identByte2, VAN_ID_VIN))
                {
                    if (!canVinHandler->IsVinSet())
                    {
                        memcpy(vinDataToBridge.Vin, vanMessageWithoutId, vanMessageLengthWithoutId);
                        xQueueOverwrite(vinQueue, (void*)&vinDataToBridge);
                    }
                }
                #pragma endregion

                if (!SILENT_MODE 
                    //&& (IsVanIdent(identByte1, identByte2, VAN_ID_RADIO_REMOTE))
                    //&& (IsVanIdent(identByte1, identByte2, VAN_ID_AIR_CONDITIONER_1))
                    //&& (IsVanIdent(identByte1, identByte2, VAN_ID_DISPLAY_POPUP))
                    //&& identByte1 == 0x8a
                    //|| (IsVanIdent(identByte1, identByte2, VAN_ID_AIR_CONDITIONER_2))
                    )
                {
                    PrintArrayToSerial(vanMessage, vanMessageLength);
                }
            }
            vanMessageLength = 0;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

#if HW_VERSION == 14
void VANWriteTaskFunction(void* parameter)
{
    const int SCK_PIN = 25;
    const int MISO_PIN = 5;
    const int MOSI_PIN = 33;
    const int VAN_PIN = 32;

    unsigned long currentTime = 0;
    uint8_t ignition = 0;
    VanIgnitionDataToBridgeToCan dataToBridge;

    SPIClass* spi = new SPIClass();
    spi->begin(SCK_PIN, MISO_PIN, MOSI_PIN, VAN_PIN);

    AbstractVanMessageSender* VANInterface = new VanMessageSender(VAN_PIN, spi);
    VANInterface->begin();

    VanWriterContainer* vanWriterContainer = new VanWriterContainer(VANInterface);

    for (;;)
    {
        currentTime = millis();

        xQueueReceive(ignitionQueue, &dataToBridge, 0);

        ignition = 1;
#ifdef USE_IGNITION_SIGNAL_FROM_VAN_BUS
        ignition = dataToBridge.Ignition == 1;
#else
        dataToBridge.Ignition = ignition;
#endif
        vanWriterContainer->Process(dataToBridge, currentTime);

        vTaskDelay(20 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}
#endif

void setup()
{
    uint64_t macAddress;
    macAddress = ESP.getEfuseMac();
    uint16_t uniqueIdForBluetooth = (uint16_t)(macAddress >> 32);
    char bluetoothDeviceName[27];
    snprintf(bluetoothDeviceName, 27, "ESP32 VAN-CAN Bridge %04X", uniqueIdForBluetooth);

#ifdef USE_BLUETOOTH_SERIAL
    serialPort = new BluetoothSerAbs(SerialBT, bluetoothDeviceName);
#else
    serialPort = new HwSerAbs(Serial);
#endif

    //serialPort->begin(115200);
    //serialPort->begin(230400);
    serialPort->begin(500000);
    serialPort->println(bluetoothDeviceName);

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
        Log.begin(LOG_LEVEL_SILENT, serialPort);
    }
    else
    {
        //Log.begin(LOG_LEVEL_WARNING, serialPort);
        //Log.begin(LOG_LEVEL_VERBOSE, serialPort);
        Log.begin(LOG_LEVEL_SILENT, serialPort);
    }

    VAN_RX.Init(VAN_DATA_RX_RMT_CHANNEL, VAN_DATA_RX_PIN, VAN_DATA_RX_LED_INDICATOR_PIN, VAN_DATA_RX_LINE_LEVEL, VAN_NETWORK_TYPE_COMFORT);

    //CANInterface = new CanMessageSender(CAN_RX_PIN, CAN_TX_PIN);
    CANInterface = new CanMessageSenderEsp32Arduino(CAN_RX_PIN, CAN_TX_PIN);
    CANInterface->Init();

    canPopupHandler = new CanDisplayPopupHandler(CANInterface);
    canVinHandler = new CanVinHandler(CANInterface);
    tripInfoHandler = new CanTripInfoHandler(CANInterface);
    canAirConOnDisplayHandler = new CanAirConOnDisplayHandler(CANInterface);
    canRadioRemoteMessageHandler = new CanRadioRemoteMessageHandler(CANInterface);
    canStatusOfFunctionsHandler = new CanStatusOfFunctionsHandler(CANInterface);
    canWarningLogHandler = new CanWarningLogHandler(CANInterface);
    canSpeedAndRpmHandler = new CanSpeedAndRpmHandler(CANInterface);
    canDash2MessageHandler = new CanDash2MessageHandler(CANInterface);
    canDash3MessageHandler = new CanDash3MessageHandler(CANInterface);
    canDash4MessageHandler = new CanDash4MessageHandler(CANInterface);
    radioIgnition = new CanIgnitionPacketSender(CANInterface);
    dashIgnition = new CanDashIgnitionPacketSender(CANInterface);
    canRadioDiag = new CanRadioRd4DiagHandler(CANInterface, serialPort);
    canParkingAid = new CanParkingAidHandler(CANInterface);

    vanHandlerContainer = new VanHandlerContainer(
        canPopupHandler,
        tripInfoHandler,
        canStatusOfFunctionsHandler,
        canWarningLogHandler,
        canRadioRemoteMessageHandler);

    dataQueue = xQueueCreate(QUEUE_SIZE, sizeof(VanDataToBridgeToCan));
    ignitionQueue = xQueueCreate(QUEUE_SIZE, sizeof(VanIgnitionDataToBridgeToCan));
    vinQueue = xQueueCreate(QUEUE_SIZE, sizeof(VanVinToBridgeToCan));

    xTaskCreatePinnedToCore(
        CANSendIgnitionTaskFunction,    // Function to implement the task
        "CANSendIgnitionTask",          // Name of the task
        15000,                          // Stack size in words
        NULL,                           // Task input parameter
        2,                              // Priority of the task
        &CANSendIgnitionTask,           // Task handle.
        0);                             // Core where the task should run

    xTaskCreatePinnedToCore(
        CANSendDataTaskFunction,        // Function to implement the task
        "CANSendDataTask",              // Name of the task
        15000,                          // Stack size in words
        NULL,                           // Task input parameter 
        0,                              // Priority of the task
        &CANSendDataTask,               // Task handle.
        0);                             // Core where the task should run

    xTaskCreatePinnedToCore(
        VANReadTaskFunction,            // Function to implement the task
        "VANReadTask",                  // Name of the task
        20000,                          // Stack size in words
        NULL,                           // Task input parameter
        1,                              // Priority of the task
        &VANReadTask,                   // Task handle.
        1);                             // Core where the task should run

    xTaskCreatePinnedToCore(
        CANReadTaskFunction,            // Function to implement the task
        "CANReadTask",                  // Name of the task
        10000,                          // Stack size in words
        NULL,                           // Task input parameter
        0,                              // Priority of the task
        &CANReadTask,                   // Task handle.
        1);                             // Core where the task should run

#if HW_VERSION == 14
    xTaskCreatePinnedToCore(
        VANWriteTaskFunction,                        // Function to implement the task
        "VANWriteTask",                  // Name of the task
        20000,                          // Stack size in words
        NULL,                           // Task input parameter
        1,                              // Priority of the task
        &VANWriteTask,                   // Task handle.
        1);                             // Core where the task should run
#endif

    esp_task_wdt_init(TASK_WATCHDOG_TIMEOUT, true);
    esp_task_wdt_add(VANReadTask);
}

void loop()
{
    vTaskDelay(100 / portTICK_PERIOD_MS);
}
