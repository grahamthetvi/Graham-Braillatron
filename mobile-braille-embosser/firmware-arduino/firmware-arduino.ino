#include <USB.h>
#include <USBHIDKeyboard.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ESP32 Native Watchdog Headers (Replaces avr/wdt.h)
#include "esp_task_wdt.h"
#include "esp_arduino_version.h"

// ESP32 Native USB HID Keyboard Instance
USBHIDKeyboard Keyboard;

// Nordic UART Service (NUS) UUIDs for broad iOS BLE terminal compatibility
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define WATCHDOG_TIMEOUT_MS 5000

bool deviceConnected = false;

// -----------------------------------------------------------------------------
// ESP32 Watchdog Functions
// -----------------------------------------------------------------------------
void initWatchdog(uint32_t timeoutMs) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t twdt_config = {
      .timeout_ms = timeoutMs,
      .idle_core_mask = (1 << configNUM_CORES) - 1,
      .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&twdt_config);
#else
  esp_task_wdt_init(timeoutMs / 1000, true);
#endif
  esp_task_wdt_add(NULL); // Register main loop thread to watchdog
}

void resetWatchdog() {
  esp_task_wdt_reset();
}

// -----------------------------------------------------------------------------
// BLE Callbacks
// -----------------------------------------------------------------------------
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    BLEDevice::startAdvertising(); // Automatically restart advertising
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue().c_str();

    if (rxValue.length() > 0) {
      // Direct output to USB target host as keyboard typing
      Keyboard.print(rxValue);
    }
  }
};

// -----------------------------------------------------------------------------
// Setup & Main Loop
// -----------------------------------------------------------------------------
void setup() {
  // 1. Initialize ESP32-S3 Native USB HID Stack
  Keyboard.begin();
  USB.begin();

  // 2. Initialize Hardware Watchdog
  initWatchdog(WATCHDOG_TIMEOUT_MS);

  // 3. Initialize BLE Stack
  BLEDevice::init("ESP32-S3 Keyboard Bridge");
  
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // RX Characteristic (iPhone -> ESP32-S3)
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxCharacteristic->setCallbacks(new RxCallbacks());

  // TX Characteristic (ESP32-S3 -> iPhone)
  BLECharacteristic *pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  // 4. Start BLE Advertising with iOS-optimized timing
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
}

void loop() {
  // Feed the ESP32 Task Watchdog
  resetWatchdog();
  
  delay(10);
}