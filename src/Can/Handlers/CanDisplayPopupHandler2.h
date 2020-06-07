// CanDisplayPopupHandler.h
#ifndef _CanDisplayPopupHandler_h
#define _CanDisplayPopupHandler_h

#include <cppQueue.h>
#include <ArduinoLog.h>
#include "../AbstractCanMessageSender.h"
#include "../Structs/CanDisplayStructs.h"
#include "../../Helpers/CanDisplayPopupItem.h"

const int CAN_POPUP_MESSAGE_TIME = 4000;
const int CAN_POPUP_DOOR_MESSAGE_TIME = 5000; //it is not used anywhere, but remove this line requires a lot of changes in other files

class CanDisplayPopupHandler {


public:
	CanDisplayPopupHandler() {
		canMessageSender = NULL;
		popupMessageQueue = NULL;
		canSemaphore = NULL;
	}

	CanDisplayPopupHandler(AbstractCanMessageSender * msgSender) {

		canMessageSender = msgSender;
		popupMessageQueue = new Queue(sizeof(CanDisplayPopupItem), 20, FIFO); // Instantiate queue for popup messages
		canSemaphore = xSemaphoreCreateMutex();
	}

	void QueueNewMessage(CanDisplayPopupItem item) {

		if (item.MessageType == CAN_POPUP_MSG_RISK_OF_ICE) {
			if (!riskOfIceShown) {
				PushPopupMsg(&item, canSemaphore, popupMessageQueue);
				riskOfIceShown = true;
			}
			return;
		}

		if (item.MessageType
				== CAN_POPUP_MSG_DOORS_BOOT_BONNET_REAR_SCREEN_AND_FUEL_TANK_OPEN
				&& item.DoorStatus1 != 0) {
			if (IsPopupVisible() && canBeVisible)
				HideCurrentPopupMessage();
			ShowCanPopupMessage(item.Category, item.MessageType, item.KmToDisplay, item.DoorStatus1, item.DoorStatus2, item.DisplayTimeInMilliSeconds);
			canBeVisible = false;
			popupVisible = true;
			return;

		}

		if (item.MessageType
				== CAN_POPUP_MSG_DOORS_BOOT_BONNET_REAR_SCREEN_AND_FUEL_TANK_OPEN
				&& item.DoorStatus1 == 0 && !canBeVisible) {
			HideCanPopupMessage(item.MessageType, item.DoorStatus1,
					item.DisplayTimeInMilliSeconds);
			canBeVisible = true;
			popupVisible = false;
			return;

		}

		if (item.MessageType
				== CAN_POPUP_MSG_DOORS_BOOT_BONNET_REAR_SCREEN_AND_FUEL_TANK_OPEN
				&& item.DoorStatus1 == 0) {
			//we need to ignore frames when doors are closed and popup just hides (commented line 85 in VanCarStatusWithTripComputerHandler.h because we need to know when door changes status from opened to closed),
			//otherwise we are sending unnecessary frames
			return;
		}

		if (item.MessageType
				== CAN_POPUP_MSG_AUTOMATIC_HEADLAMP_LIGHTING_ACTIVATED) {
			if (GetEngineRunning() && !automaticLightingShownOnEngineRunning) {
				PushPopupMsg(&item, canSemaphore, popupMessageQueue);
				automaticLightingShownOnEngineRunning = true;
			}

			if (GetIgnition() && !automaticLightingShownOnIgnition) {
				PushPopupMsg(&item, canSemaphore, popupMessageQueue);
				automaticLightingShownOnIgnition = true;
				itemlight = item;

			}

			if (automaticLightingShownOnEngineRunning)
				return;
			if (automaticLightingShownOnIgnition)
				return;
		}

		if (item.MessageType
				== CAN_POPUP_MSG_AUTOMATIC_DOOR_LOCKING_ACTIVATED) {
			if (GetEngineRunning() && !automaticDoorLockShownOnEngineRunning) {
				PushPopupMsg(&item, canSemaphore, popupMessageQueue);
				automaticDoorLockShownOnEngineRunning = true;
			}

			if (GetIgnition() && !automaticDoorLockShownOnIgnition) {
				PushPopupMsg(&item, canSemaphore, popupMessageQueue);
				automaticDoorLockShownOnIgnition = true;
				itemdoor = item;

			}

			if (automaticDoorLockShownOnEngineRunning)
				return;
			if (automaticDoorLockShownOnIgnition)
				return;
		}

		if ((millis() - previousCanPopupTime) > chillTime * 1000
				|| (lastPopupMessage.MessageType != item.MessageType)
				|| item.MessageType == CAN_POPUP_MSG_HANDBRAKE
				|| item.MessageType
						== CAN_POPUP_MSG_ENGINE_OIL_PRESSURE_FAULT_STOP_THE_VEHICLE) {

			PushPopupMsg(&item, canSemaphore, popupMessageQueue);
			previousCanPopupTime = millis();
		}

	}

	void Process(unsigned long currentTime) {

		if (GetEngineRunning() && !automaticLightingShownOnEngineRunning
				&& automaticLightingShownOnIgnition) {
			PushPopupMsg(&itemlight, canSemaphore, popupMessageQueue);
			automaticLightingShownOnEngineRunning = true;
		}

		if (canBeVisible && !IsPopupVisible()
				&& !popupMessageQueue->isEmpty()) {

			xSemaphoreTake(canSemaphore, portMAX_DELAY);
			popupMessageQueue->pop(&currentPopupMessage);
			xSemaphoreGive(canSemaphore);

			ShowCanPopupMessage(currentPopupMessage.Category, currentPopupMessage.MessageType, currentPopupMessage.KmToDisplay, currentPopupMessage.DoorStatus1, currentPopupMessage.DoorStatus2, currentPopupMessage.Counter);
			lastPopupMessage = currentPopupMessage;
		}

		if (((millis() - canDisplayPopupStartTime) > CAN_POPUP_MESSAGE_TIME)
				&& popupVisible) {
			HideCanPopupMessage(currentPopupMessage.MessageType,
					currentPopupMessage.DoorStatus1,
					currentPopupMessage.Counter);

			canDisplayPopupStartTime = 0;
		}

	}

	void ShowCanPopupMessage(uint8_t category, uint8_t messageType,
			int kmToDisplay, uint8_t doorStatus1, uint8_t doorStatus2,
			int counter) {

		CanDisplayPacketSender displayMessageSender(canMessageSender);
		canDisplayPopupStartTime = millis();

		popupVisible = true;
		uint8_t messageSentCount = 0;

		while (messageSentCount < CAN_POPUP_MESSAGE_SEND_COUNT) {
			displayMessageSender.ShowPopup(category, messageType, kmToDisplay,
					doorStatus1, doorStatus2);
			messageSentCount++;

			vTaskDelay(5 / portTICK_PERIOD_MS);
		}

	}

	void ShowCanDoorPopUp() {
		ShowCanPopupMessage(currentPopupMessage.Category,currentPopupMessage.MessageType, currentPopupMessage.KmToDisplay, currentPopupMessage.DoorStatus1,	currentPopupMessage.DoorStatus2, currentPopupMessage.Counter);
	}

	void HideCanPopupMessage(uint8_t messageType, uint8_t doorStatus, int counter) {
		CanDisplayPacketSender displayMessageSender(canMessageSender);
		uint8_t messageSentCount = 0;
		while (messageSentCount < CAN_POPUP_MESSAGE_SEND_COUNT) {
			displayMessageSender.HidePopup(messageType);
			messageSentCount++;
			vTaskDelay(5 / portTICK_PERIOD_MS);
		}
		lastPopupMessage.DisplayTimeInMilliSeconds = 0;
		popupVisible = false;
		previousCanPopupTime = millis();

	}

	void HideCurrentPopupMessage() {
		HideCanPopupMessage(lastPopupMessage.MessageType,
				lastPopupMessage.DoorStatus1, lastPopupMessage.Counter);
	}

	bool IsPopupVisible() {
		return popupVisible;
	}

	void SetEngineRunning(bool enrunn) {
		enginerunning = enrunn;
	}

	bool GetEngineRunning() {
		return enginerunning;
	}

	void SetIgnition(bool ign) {
		ignition = ign;
	}

	bool GetIgnition() {
		return ignition;
	}

	void Reset() {
		lastPopupMessage.IsInited = false;
		xSemaphoreTake(canSemaphore, portMAX_DELAY);
		popupMessageQueue->flush();
		xSemaphoreGive(canSemaphore);
		lastPopupMessage.DisplayTimeInMilliSeconds = 0;
		lastPopupMessage.Visible = false;
		lastPopupMessage.Visible = false;
		riskOfIceShown = false;
		canDisplayPopupStartTime = 0;
		automaticLightingShownOnEngineRunning = false;
		automaticLightingShownOnIgnition = false;
		automaticDoorLockShownOnEngineRunning = false;
		automaticDoorLockShownOnIgnition = false;
		enginerunning = false;

		ResetSeatBeltWarning();
		HideCurrentPopupMessage();
	}

	void ResetSeatBeltWarning() {
		seatbeltWarningShown = false;
		if (lastPopupMessage.MessageType
				== CAN_POPUP_MSG_FRONT_SEAT_BELTS_NOT_FASTENED
				&& IsPopupVisible()) {
			HideCanPopupMessage(lastPopupMessage.MessageType,
					lastPopupMessage.DoorStatus1, lastPopupMessage.Counter);
		}
	}
private:

	AbstractCanMessageSender *canMessageSender;
	//ByteAcceptanceHandler* byteAcceptanceHandler;

	bool riskOfIceShown = false;
	bool seatbeltWarningShown = false;
	bool automaticLightingShownOnIgnition = false;
	bool automaticLightingShownOnEngineRunning = false;
	bool automaticDoorLockShownOnEngineRunning = false;
	bool automaticDoorLockShownOnIgnition = false;
	bool popupVisible = false;
	bool enginerunning = false;
	bool ignition = false;
	bool canBeVisible = true;
	unsigned long canDisplayPopupStartTime = 0;
	unsigned long previousCanPopupTime = millis();
	CanDisplayPopupItem currentPopupMessage;
	CanDisplayPopupItem lastPopupMessage;
	CanDisplayPopupItem itemlight;
	CanDisplayPopupItem itemdoor;
	Queue *popupMessageQueue;
	SemaphoreHandle_t canSemaphore;

	
	const uint8_t CAN_POPUP_MESSAGE_SEND_COUNT = 2;
	const int chillTime = 10;//time to wait between display popups with the same ID (it's annoying when the same popups display a long time)

	void PushPopupMsg(CanDisplayPopupItem *item, SemaphoreHandle_t sem,
			Queue *q) {
		xSemaphoreTake(sem, portMAX_DELAY);
		q->push(item);
		xSemaphoreGive(sem);
	}

};

#endif
