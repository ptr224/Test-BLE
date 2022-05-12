#include <WiFi.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>
#include <AsyncDelay.h>
#include <qrcode.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

char deviceType[] = "TestBT";
char deviceID[sizeof(deviceType) + 7]; // + 6 id

TFT_eSPI tft(TFT_WIDTH, TFT_HEIGHT);

//

AsyncDelay ble_beacon;
bool _BLEClientConnected = false;

NimBLECharacteristic *pBatteryLevelCharacteristic;
uint8_t level = 57;

class MyServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer* pServer)
    {
        _BLEClientConnected = true;
    };

    void onDisconnect(NimBLEServer* pServer)
    {
        _BLEClientConnected = false;
    }
};

//

void espDelay(int ms) //! Long time delay, it is recommended to use shallow sleep, which can effectively reduce the current consumption
{
    esp_sleep_enable_timer_wakeup(ms * 1000);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_light_sleep_start();
}

//

void drawQR(char *qrcodeContent)
{
    // Prepara interfaccia

    tft.setRotation(0);
    tft.setCursor(0, 0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(TC_DATUM);
    
    auto cX = tft.width() / 2;
    auto cY = tft.height() / 2;
    auto interLine = tft.fontHeight();

    // Crea QR code

    const uint8_t VERSION = 3;
    const uint8_t MODULE_SIZE = 3;

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(VERSION)];
    qrcode_initText(&qrcode, qrcodeData, VERSION, ECC_MEDIUM, qrcodeContent);

    const auto size = (qrcode.size + 2u) * MODULE_SIZE;
    const auto offsetX = (tft.width() - size) / 2;
    const auto offsetY = (tft.height() - size - interLine * 5) / 2 + interLine;

    tft.fillRect(offsetX, offsetY, size, size, TFT_WHITE);

    for (uint8_t y = 0; y < qrcode.size; y++)
    {
        for (uint8_t x = 0; x < qrcode.size; x++)
        {
            if (qrcode_getModule(&qrcode, x, y))
            {
                tft.fillRect((x + 1) * MODULE_SIZE + offsetX, (y + 1) * MODULE_SIZE + offsetY, MODULE_SIZE, MODULE_SIZE, TFT_BLACK);
            }
        }
    }
    
    // Stampa testo sotto QR

    tft.drawString(qrcodeContent, cX, offsetY + size + interLine * 2);
}

//

void setup()
{
    Serial.begin(115200);
    
    // Crea ID da indirizzo MAC della macchina

    uint32_t id = 0;
    for (int i = 0; i < 17; i += 8)
    {
        id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }

    sprintf(deviceID, "%s-%u", deviceType, id);

    // Spegni WiFi

    WiFi.mode(WIFI_OFF);

    // Configura BLE

    NimBLEDevice::init(deviceID);
    //NimBLEDevice::setPower(esp_power_level_t::ESP_PWR_LVL_P7);

    // Create the BLE Server

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create the BLE Service

    NimBLEService *pBatteryService = pServer->createService(NimBLEUUID((uint16_t)0x180F));
    pBatteryLevelCharacteristic = pBatteryService->createCharacteristic(NimBLEUUID((uint16_t)0x2A19), NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    NimBLEDescriptor *pBatteryLevelDescriptor = pBatteryLevelCharacteristic->createDescriptor(NimBLEUUID((uint16_t)0x2901), NIMBLE_PROPERTY::READ);
    pBatteryLevelDescriptor->setValue("Percentage 0 - 100");
    
    pBatteryService->start();

    // Start advertising

    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(pBatteryService->getUUID());
    //pAdvertising->setAppearance(512); // https://developer.nordicsemi.com/nRF5_SDK/nRF51_SDK_v4.x.x/doc/html/group___b_l_e___a_p_p_e_a_r_a_n_c_e_s.html
    pAdvertising->start();

    ble_beacon.start(5000, AsyncDelay::units_t::MILLIS);

    // Configura schermo

    tft.init();
    drawQR(deviceID);
}

void loop()
{
    if (ble_beacon.isExpired())
    {
        pBatteryLevelCharacteristic->setValue(&level, 1);
        pBatteryLevelCharacteristic->notify();

        if (++level > 100)
            level = 0;

        ble_beacon.repeat();
    }
}
