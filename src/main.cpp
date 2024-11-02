#include <Arduino.h>
#include <Wire.h>
#include <HID-Project.h>

#define EEPROM_I2C_ADDRESS 0x50  // AT24C256 的 I2C 地址
#define HOTKEY_COUNT 32          // 熱鍵組合數量
#define HOTKEY_SIZE 8            // 每組熱鍵的資料大小（字節）
#define EEPROM_START_ADDRESS 0   // EEPROM 起始地址

#define CMD_SET_HOTKEY 0x01      // 設定熱鍵
#define CMD_RUN_HOTKEY 0x02      // 發送熱鍵
#define CMD_LIST_HOTKEYS 0x03    // 請求熱鍵列表
#define CMD_RESPONSE 0x04        // 裝置回應

byte incomingData[128];          // 接收緩衝區
int dataLength = 0;              // 資料長度
bool dataReady = false;          // 資料是否接收完畢

void processSerialData(byte incomingByte);
void executeCommand();
void processSerialData(byte incomingByte);
void executeCommand();
void setHotkey(byte* data, byte length);
void runHotkey(byte* data, byte length);
void listHotkeys();
void writeEEPROM(int address, byte* data, int length);
void readEEPROM(int address, byte* data, int length);
void sendResponse(const char* message);

struct HotkeyTask {
  bool active;
  unsigned long startTime;
  int delayTime;
  byte keyType;
  byte keyCount;
  byte keyCodes[3];
};

HotkeyTask currentTask = {false, 0, 0, 0, 0, {0, 0, 0}};


void setup() {
  Serial.begin(9600);
  while (!Serial);  // 等待串列埠連接

  Wire.begin();     // 初始化 I2C
  Keyboard.begin(); // 初始化鍵盤模擬
  Consumer.begin(); // 初始化消費者控制
  System.begin();   // 初始化系統控制

  Serial.println("裝置已啟動");
}

void loop() {
  if (Serial.available() > 0) {
    byte incomingByte = Serial.read();
    processSerialData(incomingByte);
  }

  if (dataReady) {
    dataReady = false;
    executeCommand();
  }

  // 處理熱鍵任務
  if (currentTask.active) {
    if (millis() - currentTask.startTime >= currentTask.delayTime) {
      // 釋放按鍵
      switch (currentTask.keyType) {
        case 1: // 鍵盤按鍵
          Keyboard.releaseAll();
          break;
        case 2: // 消費者控制
          Consumer.releaseAll();
          break;
        case 3: // 系統控制
          System.releaseAll();
          break;
      }
      currentTask.active = false;
      Serial.println("熱鍵發送完成");
    }
  }
}


void processSerialData(byte incomingByte) {
  static int index = 0;
  incomingData[index++] = incomingByte;

  if (index >= 2) {
    dataLength = incomingData[1];
    if (index == dataLength + 3) { // 命令字節 + 資料長度 + 資料內容 + 校驗碼
      // 檢查校驗碼
      byte checksum = 0;
      for (int i = 0; i < index - 1; i++) {
        checksum += incomingData[i];
      }
      if (checksum == incomingData[index - 1]) {
        dataReady = true;
      } else {
        Serial.println("校驗碼錯誤");
      }
      index = 0;
    }
  }
}

void executeCommand() {
  byte command = incomingData[0];
  byte length = incomingData[1];
  byte* data = &incomingData[2];

  switch (command) {
    case CMD_SET_HOTKEY:
      setHotkey(data, length);
      break;
    case CMD_RUN_HOTKEY:
      runHotkey(data, length);
      break;
    case CMD_LIST_HOTKEYS:
      listHotkeys();
      break;
    default:
      Serial.println("未知命令");
      break;
  }
}

void setHotkey(byte* data, byte length) {
  if (length < 7) {
    Serial.println("資料長度不足");
    return;
  }
  int index = data[0];
  int keyType = data[1];
  int keyCount = data[2];
  byte keyCodes[3] = {data[3], data[4], data[5]};
  int delayTime = (data[6] << 8) | data[7];

  // 錯誤檢查
  if (index < 0 || index >= HOTKEY_COUNT) {
    Serial.println("索引超出範圍");
    return;
  }
  if (keyType < 1 || keyType > 3) {
    Serial.println("無效的按鍵類型");
    return;
  }
  if (keyCount < 1 || keyCount > 3) {
    Serial.println("按鍵數量不正確");
    return;
  }
  if (delayTime < 0 || delayTime > 5000) {
    Serial.println("延遲時間不正確");
    return;
  }

  byte hotkeyData[HOTKEY_SIZE];
  hotkeyData[0] = keyCount;
  hotkeyData[1] = keyType;
  hotkeyData[2] = keyCodes[0];
  hotkeyData[3] = keyCodes[1];
  hotkeyData[4] = keyCodes[2];
  hotkeyData[5] = data[6]; // 延遲時間高位
  hotkeyData[6] = data[7]; // 延遲時間低位

  int eepromAddress = EEPROM_START_ADDRESS + index * HOTKEY_SIZE;
  writeEEPROM(eepromAddress, hotkeyData, HOTKEY_SIZE);

  sendResponse("設定熱鍵成功");
}

void runHotkey(byte* data, byte length) {
  if (currentTask.active) {
    Serial.println("已有熱鍵正在發送，請稍後再試");
    return;
  }

  if (length < 1) {
    Serial.println("資料長度不足");
    return;
  }
  int index = data[0];
  if (index < 0 || index >= HOTKEY_COUNT) {
    Serial.println("索引超出範圍");
    return;
  }

  int eepromAddress = EEPROM_START_ADDRESS + index * HOTKEY_SIZE;
  byte hotkeyData[HOTKEY_SIZE];
  readEEPROM(eepromAddress, hotkeyData, HOTKEY_SIZE);

  byte keyCount = hotkeyData[0];
  byte keyType = hotkeyData[1];
  byte* keyCodes = &hotkeyData[2];
  int delayTime = (hotkeyData[5] << 8) | hotkeyData[6];

  // 保存熱鍵任務狀態
  currentTask.active = true;
  currentTask.startTime = millis();
  currentTask.delayTime = delayTime;
  currentTask.keyType = keyType;
  currentTask.keyCount = keyCount;
  for (int i = 0; i < 3; i++) {
    currentTask.keyCodes[i] = keyCodes[i];
  }

  // 按下按鍵
  switch (keyType) {
    case 1: // 鍵盤按鍵
      for (int i = 0; i < keyCount; i++) {
        Keyboard.press(currentTask.keyCodes[i]);
      }
      break;
    case 2: // 消費者控制
      for (int i = 0; i < keyCount; i++) {
        Consumer.press(currentTask.keyCodes[i]);
      }
      break;
    case 3: // 系統控制
      for (int i = 0; i < keyCount; i++) {
        System.press(currentTask.keyCodes[i]);
      }
      break;
    default:
      Serial.println("未知的按鍵類型");
      currentTask.active = false;
      return;
  }

  Serial.print("開始發送熱鍵，索引：");
  Serial.println(index);
}


void listHotkeys() {
  // 這裡可以根據需要，將熱鍵列表回傳給 Qt 應用程式
  sendResponse("熱鍵列表功能未實現");
}
/*
void writeEEPROM(int address, byte* data, int length) {
  Wire.beginTransmission(EEPROM_I2C_ADDRESS);
  Wire.write((address >> 8) & 0xFF);
  Wire.write(address & 0xFF);
  for (int i = 0; i < length; i++) {
    Wire.write(data[i]);
  }
  Wire.endTransmission();
  delay(10);
} */
void writeEEPROM(int address, byte* data, int length) {
  // 發送寫入指令和資料
  Wire.beginTransmission(EEPROM_I2C_ADDRESS);
  Wire.write((address >> 8) & 0xFF);  // 高位地址
  Wire.write(address & 0xFF);         // 低位地址
  for (int i = 0; i < length; i++) {
    Wire.write(data[i]);
  }
  Wire.endTransmission();

  // 非阻塞式等待 EEPROM 完成寫入
  unsigned long startTime = millis();
  while (true) {
    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    byte error = Wire.endTransmission();
    if (error == 0) {
      // EEPROM 已應答，寫入完成
      break;
    }
    if (millis() - startTime > 10) {
      // 超過最大等待時間，寫入可能失敗
      Serial.println("EEPROM 寫入超時");
      break;
    }
    // 稍作延遲，避免佔用過多 CPU 資源
    delayMicroseconds(100);
  }
}


void readEEPROM(int address, byte* data, int length) {
  Wire.beginTransmission(EEPROM_I2C_ADDRESS);
  Wire.write((address >> 8) & 0xFF);
  Wire.write(address & 0xFF);
  Wire.endTransmission();

  Wire.requestFrom(EEPROM_I2C_ADDRESS, length);
  int i = 0;
  while (Wire.available() && i < length) {
    data[i++] = Wire.read();
  }
}

void sendResponse(const char* message) {
  Serial.write(CMD_RESPONSE);
  byte length = strlen(message);
  Serial.write(length);
  Serial.write((const uint8_t*)message, length);
  byte checksum = CMD_RESPONSE + length;
  for (int i = 0; i < length; i++) {
    checksum += message[i];
  }
  Serial.write(checksum);
}
