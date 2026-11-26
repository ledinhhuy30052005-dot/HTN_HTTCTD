#include <FS.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <WebServer.h>

const char *ssid = "TANG03";
const char *password = "0976152886";

/* ---- UART2 noi voi STM32 (ESP32 DevKit) ----
 *   ESP32 GPIO17 (TX2) -> STM32 PA10 (RX1)
 *   ESP32 GPIO16 (RX2) <- STM32 PA9  (TX1)
 *   GND chung                                  */
#define STM32_RX_PIN 16
#define STM32_TX_PIN 17
#define STM32_BAUD 115200

/* ---- Giao thuc (khop voi bootloader STM32) ---- */
#define START_BYTE 0xAA
#define ACK 0x79
#define NACK 0x1F
#define BLOCK_SIZE 256

/* Cho phan hoi cua STM32 cho moi goi.
 * PHAI lon hon timeout nhan byte cua STM32 (1000ms) de neu goi bi cut thi STM32 kip
 * gui NACK truoc khi ESP32 gui lai (tranh hai goi bi tron vao nhau). */
#define ACK_TIMEOUT_MS 2000
#define MAX_RETRY 5 /* so lan gui toi da cho moi goi */

/* ---- Kiem tra file .bin truoc khi gui ---- */
#define APP_ADDR 0x0800C800UL        /* dia chi App (trang 50) */
#define MAX_FW_SIZE (76UL * 1024UL) /* trang 50..125. Chip 64KB: doi thanh 14UL * 1024UL */
#define CHECK_FW_IMAGE 1             /* 1 = tu choi file khong link o 0x0800C800, 0 = tat */

WebServer server(80);
File uploadFile;
String lastResult = "Chua gui lan nao";

enum AckResult
{
    ACK_OK,
    ACK_NACK,
    ACK_TIMEOUT
};

uint16_t simpleCRC(uint8_t *data, int len)
{
    uint16_t crc = 0;
    for (int i = 0; i < len; i++)
    {
        crc += data[i];
    }
    return crc;
}

/* Bo het byte con ton trong bo dem nhan (ACK cu, byte rac) */
void flushSerial2()
{
    while (Serial2.available())
    {
        Serial2.read();
    }
}

/* Cho ACK hoac NACK. Byte khac (rac) thi bo qua va tiep tuc cho. */
AckResult waitForAck(uint32_t timeout = ACK_TIMEOUT_MS)
{
    uint32_t start = millis();
    while (millis() - start < timeout)
    {
        if (Serial2.available())
        {
            uint8_t resp = Serial2.read();
            if (resp == ACK)
                return ACK_OK;
            if (resp == NACK)
                return ACK_NACK;
        }
        else
        {
            delay(1);
        }
    }
    return ACK_TIMEOUT;
}

void handleUpload()
{
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        uploadFile = SPIFFS.open("/firmware.bin", FILE_WRITE);
        if (!uploadFile)
        {
            Serial.println("Cant open file /firmware.bin");
            return;
        }
        Serial.println("Upload...");
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (uploadFile && uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            Serial.println("SPIFFS write error");
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (uploadFile)
        {
            uploadFile.close();
            Serial.printf("Upload done. %u bytes\n", (unsigned)upload.totalSize);
        }
    }
}

/* Gui 1 goi, tra ve true khi STM32 ACK.
 * - NACK hoac khong tra loi: gui lai (toi da MAX_RETRY lan). */
bool sendBlock(const uint8_t *data, int len, int blockNo, int blocks)
{
    uint8_t packet[BLOCK_SIZE + 5];
    uint16_t crc = simpleCRC((uint8_t *)data, len);

    packet[0] = START_BYTE;
    packet[1] = (len >> 8) & 0xFF;
    packet[2] = len & 0xFF;
    memcpy(&packet[3], data, len);
    packet[3 + len] = (crc >> 8) & 0xFF;
    packet[4 + len] = crc & 0xFF;

    for (int attempt = 1; attempt <= MAX_RETRY; attempt++)
    {
        flushSerial2(); /* khong de ACK/NACK cu bi doc nham thanh phan hoi cua goi nay */
        Serial2.write(packet, len + 5);
        Serial2.flush();

        AckResult r = waitForAck();
        if (r == ACK_OK)
        {
            Serial.printf("Block %d/%d: OK\n", blockNo + 1, blocks);
            return true;
        }
        Serial.printf("Block %d/%d: %s (lan %d/%d)\n", blockNo + 1, blocks,
                      r == ACK_NACK ? "NACK" : "khong phan hoi", attempt, MAX_RETRY);
        delay(5);
    }
    return false;
}

bool sendFirmwareToSTM32()
{
    File file = SPIFFS.open("/firmware.bin", FILE_READ);
    if (!file)
    {
        lastResult = "Loi: khong mo duoc /firmware.bin (hay Upload BIN truoc)";
        Serial.println(lastResult);
        return false;
    }

    size_t total = file.size();
    if (total < 8 || total > MAX_FW_SIZE)
    {
        lastResult = "Loi: kich thuoc file khong hop le (" + String((unsigned)total) + " byte)";
        Serial.println(lastResult);
        file.close();
        return false;
    }

#if CHECK_FW_IMAGE
    /* 8 byte dau cua bin = [MSP][Reset_Handler]. File link sai dia chi (vd 0x08000000)
     * se ghi de/lam hong App, nen tu choi gui. */
    uint8_t vec[8];
    file.read(vec, 8);
    file.seek(0);
    uint32_t sp = vec[0] | (vec[1] << 8) | (vec[2] << 16) | ((uint32_t)vec[3] << 24);
    uint32_t pc = vec[4] | (vec[5] << 8) | (vec[6] << 16) | ((uint32_t)vec[7] << 24);
    if ((sp & 0x2FFE0000UL) != 0x20000000UL || !(pc & 1) ||
        pc < APP_ADDR || pc >= APP_ADDR + MAX_FW_SIZE)
    {
        lastResult = "Loi: file .bin khong duoc build o 0x0800C800 (Reset_Handler = 0x" + String(pc, HEX) +
                     "). Sua R/O Base trong Keil roi build lai.";
        Serial.println(lastResult);
        file.close();
        return false;
    }
#endif

    int blocks = (total + BLOCK_SIZE - 1) / BLOCK_SIZE;
    Serial.printf("Gui %u byte, %d khoi...\n", (unsigned)total, blocks);

    uint8_t data[BLOCK_SIZE];
    int blockNo = 0;
    flushSerial2();

    while (file.available())
    {
        int len = file.read(data, BLOCK_SIZE);
        if (len <= 0)
            break;

        if (!sendBlock(data, len, blockNo, blocks))
        {
            lastResult = "Loi o khoi " + String(blockNo + 1) + "/" + String(blocks) +
                         ". STM32 van dang cho. Neu loi o khoi > 1 hay RESET STM32 roi gui lai tu dau (trong 10s).";
            Serial.println(lastResult);
            file.close();
            return false;
        }
        blockNo++;
        delay(10);
    }
    file.close();

    /* Goi ket thuc AA 00 00: STM32 tra ACK roi bao LED va chay App */
    uint8_t endPkt[3] = {START_BYTE, 0x00, 0x00};
    bool endOk = false;
    for (int i = 0; i < 3 && !endOk; i++)
    {
        flushSerial2();
        Serial2.write(endPkt, 3);
        Serial2.flush();
        endOk = (waitForAck() == ACK_OK);
    }

    if (endOk)
    {
        lastResult = "Nap thanh cong: " + String((unsigned)total) + " byte, STM32 da ACK va se chay App.";
    }
    else
    {
        lastResult = "Da gui het nhung STM32 khong ACK goi ket thuc. Kiem tra xem LED bao thanh cong co sang khong.";
    }
    Serial.println(lastResult);
    return endOk;
}

void setup()
{
    Serial.begin(9600);
    Serial2.begin(STM32_BAUD, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);

    SPIFFS.begin(true);

    WiFi.begin(ssid, password);
    Serial.print("WiFi connecting...");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected. IP:");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, []()
              { server.send(200, "text/html", R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Upload Firmware</title>
      <style>
        body {
          background-color: #f0f0f0;
          font-family: Arial, sans-serif;
          text-align: center;
          padding-top: 50px;
        }
        form {
          background: #fff;
          padding: 20px;
          border-radius: 10px;
          display: inline-block;
          box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        input[type="file"] {
          margin-bottom: 15px;
        }
        input[type="submit"], a.button {
          background-color: #4CAF50;
          color: white;
          padding: 10px 20px;
          text-decoration: none;
          border: none;
          border-radius: 5px;
          cursor: pointer;
        }
        a.button {
          display: inline-block;
          margin-top: 15px;
        }
      </style>
    </head>
    <body>
      <form method="POST" action="/upload" enctype="multipart/form-data">
        <h2>OTA - STM32 - ESP32</h2>
        <input type="file" name="file" accept=".bin"><br>
        <input type="submit" value="Upload BIN">
        <br>
        <a href="/send" class="button">Send to STM32</a>
      </form>
    </body>
    </html>
  )rawliteral"); });

    server.on("/upload", HTTP_POST, []()
              { server.send(200, "text/html", R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Upload Firmware</title>
      <style>
        body {
          background-color: #f0f0f0;
          font-family: Arial, sans-serif;
          text-align: center;
          padding-top: 50px;
        }
        form {
          background: #fff;
          padding: 20px;
          border-radius: 10px;
          display: inline-block;
          box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        input[type="submit"], a.button {
          background-color: #4CAF50;
          color: white;
          padding: 10px 20px;
          text-decoration: none;
          border: none;
          border-radius: 5px;
          cursor: pointer;
        }
        a.button {
          display: inline-block;
          margin-top: 15px;
        }
      </style>
    </head>
    <body>
      <form method="POST" action="/upload" enctype="multipart/form-data">
        <h2>OTA - STM32 - ESP32</h2>
        <p>Upload xong. Reset STM32, roi bam nut duoi trong 10 giay.</p>
        <a href="/send" class="button">Send to STM32</a>
      </form>
    </body>
    </html>
  )rawliteral"); }, handleUpload);

    /* Tra ket qua ngay tren trinh duyet sau khi gui xong (trang cho trong luc truyen) */
    server.on("/send", HTTP_GET, []()
              {
    bool ok = sendFirmwareToSTM32();
    server.send(ok ? 200 : 500, "text/plain", lastResult); });

    server.begin();
}

void loop()
{
    server.handleClient();
}
