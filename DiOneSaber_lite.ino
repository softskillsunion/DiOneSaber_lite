#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>

#include <Bounce2.h>
#include <Timer.h>
#include <Adafruit_NeoPixel.h>

#include <I2Cdev.h>
#include <MPU6050_6Axis_MotionApps20.h>

#include <Wire.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson

/***************↓↓↓↓↓ 更改設定值 ↓↓↓↓↓***************/
// 劍刃燈珠數量
#define NUM_LEDS 80 // 劍長1000mm(連接8P燈條10支)
/***************↑↑↑↑↑ 更改設定值 ↑↑↑↑↑***************/

/***************************************************************************************************
 * Arduino 針腳定義
 */
// GPIO16 D0 未使用
// Pin 12 接 DFPlayer Mini TX、Pin 13 接 DFPlayer Mini RX
#define RX_PIN 12        // D6 設定軟體模擬 RX 腳位
#define TX_PIN 13        // D7 設定軟體模擬 TX 腳位
#define BUSY_PIN 2       // D4 LOW 表示 DFPlayer Mini 撥放中
#define BLADELED_PIN 14  // D5 刀鋒燈條的訊號 PIN，若有水晶燈珠則並聯於此
#define BUTTONLED_PIN 15 // D8 按鈕 LED 的電源 PIN，有PWM
#define BUTTON_PIN 0     // D3 按鈕訊號輸入 PIN，GPIO0 及 GPIO2 有 PULLUP 上拉電阻
#define SCL 5            // D1，MPU6050 SCL
#define SDA 4            // D2，MPU6050 SDA
/************************************/

String json;
char hostString[16] = {0};

/***************************************************************************************************
 * Button variables
 */
unsigned long btnPressTime;   // 按鈕被按下的起始時間
unsigned long btnReleaseTime; // 按鈕按釋放的時間
uint8_t btnState;             // 1 為按鈕按住； 2 為按鈕放開； 0 為初始或按住時完成事件
uint8_t btnClickCounter;      // 記錄按下次數
Bounce debouncer = Bounce();  // Instantiate a Bounce object.

/***************************************************************************************************
 * LEDStrip variables
 */
uint8_t bladeState;       // 0 為關閉； 1 為開啟
uint8_t BladeColors = 5;  // 設定刀鋒可變顏色數量，對應 changeColor 函式變更設定，0紅、1綠、2藍、3黃、4紫
uint8_t DefaultColor = 1; // 設定刀鋒開機預設顏色，利用按鈕配合 BladeColors 0~4 切換 5 種顏色，9 則代表藍牙設定的顏色
uint8_t DFPlayerState;    // 紀錄 DFPlayer Mini 播放狀態，1為待機，0表示播放中
uint32_t Color = 65280;   // 預設劍刃顏色為綠色(十進制)
Adafruit_NeoPixel LEDStrip(NUM_LEDS, BLADELED_PIN, NEO_RGB + NEO_KHZ800);

/***************************************************************************************************
 * MPU6050 variables
 */
uint8_t mpuStatus; // holds actual interrupt status byte from MPU
uint8_t lastgx, lastgy, lastgz;
bool isSwing = 0;            // 0 is not swing; 1 is swing;
bool isPlaySwing;            // swing 狀態是否執行
unsigned long lastSwing;     // swing 前次時間
uint8_t swingDuration = 500; // swing 播放至少的時間
MPU6050 mpu;

/***************************************************************************************************
 * DFPlayer Mini variables
 */
uint8_t track = 3;                               // 1.開劍刃、2.關劍刃、3.待機電流、4.揮舞
SoftwareSerial mySoftwareSerial(RX_PIN, TX_PIN); // Set ESP8266 RX and TX pin.
uint8_t playVolume = 28;                         // 播放音量
DFRobotDFPlayerMini myDFPlayer;                  // Instantiate the DFPlayer object.

ESP8266WiFiMulti wifiMulti;                        // 宣告 Wi-Fi 連線物件
ESP8266WebServer server;                           // 宣告 WEB Server 物件
WebSocketsServer webSocket = WebSocketsServer(81); // WEB Server 加入 webSocket 於 port 81

// PROGMEM 是將大量資料從 SRAM 搬到 Flash，當要使用時再從 Flash 搬回來。
char webpage[] PROGMEM = R"=====(
<html>

<head>
    <!-- 正常顯示繁體中文 -->
    <meta charset='utf-8'>
    <!-- 引用 javascript 函式庫 -->
    <script src='https://cdnjs.cloudflare.com/ajax/libs/jscolor/2.3.3/jscolor.min.js'></script>
    <style type="text/css">
        /* https://codepen.io/sdthornton/pen/wBZdXq */
        body {
            background: #5a5a5a;
            text-align: center;
            align-items: center;
            flex-direction: column;
            font-family: sans-serif;
        }

        .card {
            background: rgb(56, 56, 56);
            border-radius: 2px;
            display: inline-block;
            width: 180px;
            height: 120px;
            margin: 5px;
            position: relative;
            color: floralwhite;
        }

        .card-1 {
            box-shadow: 0 1px 3px rgba(0, 0, 0, 0.12), 0 1px 2px rgba(0, 0, 0, 0.24);
            transition: all 0.3s cubic-bezier(.25, .8, .25, 1);
        }

        .card-1:hover {
            box-shadow: 0 14px 28px rgba(0, 0, 0, 0.25), 0 10px 10px rgba(0, 0, 0, 0.22);
        }

        .title {
            background: #535353;
            border-radius: 5px;
            display: inline-block;
            width: 100%;
            margin: 10px;
            /* overflow: auto; */
            position: relative;
            color: floralwhite;

        }

        .title1 {
            font-size: 35px;
            position: relative;
            float: left;
        }

        .title2 {
            font-size: 25px;
            position: relative;
            float: right;
            top: 15px;
            right: 5px;
        }

        .subtitle {
            display: inline-block;
            width: 100%;
            position: relative;
            color: rgb(190, 190, 190);
            font-size: 18px;
        }

        .subtitle1 {
            position: relative;
            float: left;
        }

        .subtitle2 {
            position: relative;
            float: right;
            right: 5px;
        }

        h1 {
            margin-top: 0.25em;
            margin-bottom: 0;
            position: relative;
        }

        /* Start Select */
        /* https://codepen.io/raubaca/details/VejpQP */

        /* Reset Select */
        select {
            -webkit-appearance: none;
            -moz-appearance: none;
            -ms-appearance: none;
            appearance: none;
            outline: 0;
            box-shadow: none;
            border: 0 !important;
            background: gray;
            background-image: none;
        }

        /* Remove IE arrow */
        select::-ms-expand {
            display: none;
        }

        /* Custom Select */
        .select {
            position: relative;
            display: flex;
            width: 130px;
            height: 2.3em;
            line-height: 3;
            background: #2c3e50;
            overflow: hidden;
            border-radius: .25em;
            top: 10px;
            left: 25px;
        }

        select {
            flex: 1;
            padding: 0 .5em;
            color: #fff;
            cursor: pointer;
        }

        /* Arrow */
        .select::after {
            content: '\25BC';
            position: absolute;
            top: 0;
            right: 0;
            padding: 0 0.5em;
            background: rgb(82, 82, 82);
            cursor: pointer;
            pointer-events: none;
            -webkit-transition: .25s all ease;
            -o-transition: .25s all ease;
            transition: .25s all ease;
        }

        /* Transition */
        .select:hover::after {
            color: #f39c12;
        }

        /* End Select */

        /* Start Checkbox */
        /* https://codepen.io/mburnette/pen/LxNxNg */
        input[type=checkbox] {
            height: 0;
            width: 0;
            visibility: hidden;
        }

        label {
            cursor: pointer;
            text-indent: -9999px;
            width: 130px;
            height: 2.5em;
            background: grey;
            display: block;
            border-radius: 100px;
            position: relative;
            top: 5px;
            left: 25px;
        }

        label:after {
            content: '';
            position: absolute;
            top: 4px;
            left: 5px;
            width: 2em;
            height: 2em;
            background: #fff;
            border-radius: 90px;
            transition: 0.3s;
        }

        input:checked+label {
            background: #bada55;
        }

        input:checked+label:after {
            left: calc(100% - 5px);
            transform: translateX(-100%);
        }

        label:active:after {
            width: 130px;
        }

        /* end Checkbox */
    </style>

    <title>物聯網遠端控制光劍</title>
</head>

<body>
    <div class="title">
        <span class="title1">物聯網遠端控制光劍</span>
        <a href='https://www.facebook.com/SoftSkillsUnion/' target='_blank' style='color:rgb(190, 190, 190);'><span
                class="title2">自學力激發創造力、軟實力提升競爭力</span></a>
    </div>
    </p>
    <div class="card card-1">
        <div>
            <h1>開關</h1>
        </div>
        <div>
            <input type="checkbox" id="switch" oninput="sendCmnd(this.id, Number(this.checked))" /><label
                for="switch">Toggle</label>
        </div>
    </div>
    <div class="card card-1">
        <div>
            <h1>顏色</h1>
        </div>
        <div>
            <input id="selectColor" class="jscolor" value="FCFCFC" onchange="sendCmnd(this.id, this.value)"
                readonly="true" style="background-color:grey; width: 130px; height: 2.5em;position: relative;top: 10px;">
        </div>
    </div>
    <hr />
    <div class="subtitle">
        <span id="clock" class="subtitle1">Time</span>
        <a href='https://www.facebook.com/SoftSkillsUnion/' target='_blank' style='color:rgb(190, 190, 190);'
            class="subtitle2">軟實力精進聯盟</a>
    </div>

    <!-- 客戶端網頁啟用 webSocket -->
    <script>
        var webSocket;
        window.onload = init();
        function addData(objData) {
            document.getElementById("switch").checked = objData.switch;
            // document.getElementById("selectColor").value = objData.lightColor;
        }
        function sendCmnd(cmnd, value) {
            webSocket.send('{"cmnd":"' + cmnd + '","value":"' + value + '"}');
        }
        function init() {
            webSocket = new WebSocket('ws://' + window.location.hostname + ':81/');
            webSocket.onmessage = function (event) {
                var data = JSON.parse(event.data);
                addData(data);
            }
            showTime();
        }
        function showTime() {
            var today = new Date();
            var hh = today.getHours();
            var mm = today.getMinutes();
            var ss = today.getSeconds();
            mm = checkTime(mm);
            ss = checkTime(ss);
            document.getElementById('clock').innerHTML = hh + ":" + mm + ":" + ss;
            var timeoutId = setTimeout(showTime, 500);
        }
        function checkTime(i) {
            if (i < 10) {
                i = "0" + i;
            }
            return i;
        }
    </script>
</body>


</html>
)=====";

String serializeJson()
{
    String x = "{\"switch\":";
    x += bladeState;
    x += "}";

    Serial.println(x);
    return x;
}

String parseJson(String json, String field)
{
    // 分配空間大小至 https://arduinojson.org/v6/assistant/ 計算
    StaticJsonDocument<100> doc;
    deserializeJson(doc, json);

    return doc[field];
}

// 取得 HEX 轉 Decimal 顏色值
uint32_t HEXtoDEC(String hexcolor)
{
    // hex 16 進制轉 10 進制
    // 開頭帶 '#' 改 &hexcolor[1]
    // https://stackoverflow.com/questions/23576827/arduino-convert-a-string-hex-ffffff-into-3-int
    long number;
    if (hexcolor.substring(0, 1) == "#")
    {
        number = strtol(&hexcolor[1], NULL, 16);
    }
    else
    {
        number = strtol(&hexcolor[0], NULL, 16);
    }
    Serial.println(number);
    return number;
}

void sendJson()
{
    json = serializeJson();
    webSocket.broadcastTXT(json.c_str(), json.length());
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    Serial.print("WStype = ");
    Serial.println(type);
    Serial.printf("Data Length: %d, Payload: ", length);
    Serial.write(payload, length);
    Serial.println("");

    String pl = "";
    // http://www.martyncurrey.com/esp8266-and-the-arduino-ide-part-9-websockets/
    if (type == WStype_CONNECTED) // 連線成功，回傳初始值
    {
        sendJson();
    }
    else if (type == WStype_TEXT) // 接收用戶端訊息做出回應
    {
        for (int i = 0; i < length; i++)
        {
            pl += (char)payload[i];
        }

        String cmnd = parseJson(pl, "cmnd");
        String strValue = parseJson(pl, "value");
        int value = strValue.toInt();

        Serial.print("Cmnd: ");
        Serial.print(cmnd);
        Serial.print(", strValue: ");
        Serial.print(strValue);
        Serial.print(", value: ");
        Serial.println(value);

        if (cmnd == "switch")
        {
            btnPressTime = 0;
            btnClickCounter = 0;
            btnState = 1;
        }
        else if (cmnd == "selectColor")
        {
            Color = HEXtoDEC(strValue);
            DefaultColor = 9;
            if (bladeState)
            {
                setStrip(DefaultColor);
            }
        }
    }
}

void setup()
{
    Serial.begin(115200);

    pinMode(BUSY_PIN, INPUT);

    Wire.begin(SDA, SCL);
    mySoftwareSerial.begin(9600);

    if (!myDFPlayer.begin(mySoftwareSerial))
    { //Use softwareSerial to communicate with mp3.
        while (true)
            ;
    }
    // DFPlayer Mini 撥放器初始音量
    myDFPlayer.volume(playVolume);

    mpu.initialize();

    mpu.setIntMotionEnabled(true); // INT_ENABLE register enable interrupt source  motion detection
    mpu.setIntZeroMotionEnabled(true);
    mpu.setIntFIFOBufferOverflowEnabled(false);
    mpu.setIntI2CMasterEnabled(false);
    mpu.setIntDataReadyEnabled(false);
    mpu.setMotionDetectionThreshold(10); // 1mg/LSB
    mpu.setMotionDetectionDuration(2);   // number of consecutive samples above threshold to trigger int

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    debouncer.attach(BUTTON_PIN);
    debouncer.interval(30); // interval in milliseconds

    pinMode(BLADELED_PIN, OUTPUT);

    LEDStrip.begin();
    LEDStrip.setBrightness(255);
    LEDStrip.show();

    // Wi-Fi 登入帳密
    wifiMulti.addAP("SSU", "24209346");
    wifiMulti.addAP("SoftSkillsUnion", "24209346");
    wifiMulti.addAP("SoftSkillsUnion_M", "24209346");

    Serial.println("Connecting ...");
    while (wifiMulti.run() != WL_CONNECTED)
    { // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
        delay(1000);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID()); // Tell us what network we're connected to
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP()); // Send the IP address of the ESP8266 to the computer

    // Start the mDNS responder for http://di-one_saber_XXXXXX.local
    sprintf(hostString, "di-one_saber_%06x", ESP.getChipId());
    Serial.print("Hostname: ");
    Serial.println(hostString);
    // WiFi.hostname(hostString);

    if (!MDNS.begin(hostString))
    {
        Serial.println("Error setting up MDNS responder!");
    }
    Serial.println("mDNS responder started");

    MDNS.addService("http", "tcp", 80);

    server.on("/", []()
              { server.send_P(200, "text/html", webpage); });
    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    Serial.println("Di-One Saber is Done！");
}

void loop()
{
    MDNS.update();
    webSocket.loop();
    server.handleClient();

    DFPlayerState = digitalRead(BUSY_PIN);

    // 開啟刀鋒，撥放 Swing 音效
    if (bladeState == 1)
    {
        int16_t gx, gy, gz;
        mpu.getRotation(&gx, &gy, &gz);
        gx = abs(map(gx, -32768, 32768, -10, 10));
        gy = abs(map(gy, -32768, 32768, -10, 10));
        gz = abs(map(gz, -32768, 32768, -10, 10));

        // Serial.print("gx:");
        // Serial.print(gx);
        // Serial.print("gy:");
        // Serial.print(gy);
        // Serial.print("gz:");
        // Serial.println(gz);

        if (!isSwing && !isPlaySwing)
        {
            if (((gx > 9) && (lastgx < gx)) || ((gy > 9) && (lastgy < gy)) || ((gz > 9) && (lastgz < gz)))
            {
                isSwing = 1;
            }
        }
        lastgx = gx;
        lastgy = gy;
        lastgz = gz;

        if (isSwing)
        {
            // 揮舞效果非執行中
            if (!isPlaySwing)
            {
                playTrack(4);
                isPlaySwing = 1;
                lastSwing = millis();
            }
            else
            {
                if (millis() - lastSwing > swingDuration)
                {
                    initSwing();
                }
            }
        }
    }

    // 更新消除按鈕彈跳物件
    bool changed = debouncer.update();

    // 按鈕狀態發生改變
    if (changed)
    {
        // 取得按鈕狀態
        int sensorVal = debouncer.read();
        if (sensorVal == LOW)
        { // 按下按鈕
            btnPressTime = millis();
            btnState = 1;
            //若是 Click 計次大於 0，則兩次 Click 的間隔須小於 0.8 秒，
            //才符合計入第二次 Click 的條件，不符合即計次歸 0
            unsigned long butOnceAgainDuration = btnPressTime - btnReleaseTime; // 放開按鈕後到下一次在度按下按鈕時經過的時間
            if (btnClickCounter > 0 && butOnceAgainDuration > 0.8 * 1000)
            {
                btnClickCounter = 0;
            }
        }
        else
        { // 放開按鈕
            btnReleaseTime = millis();
            unsigned long butReleaseDuration = btnReleaseTime - btnPressTime; // 計算按住按鈕到放開的間隔時間
            if (btnState == 1)
            {
                btnState = 2; // btnState 要為 1 放開才變 2。開關劍刃 btnState 改為 0 表示已執行其功能，則仍為 0
                if (butReleaseDuration < 0.5 * 1000)
                { // 一次 Click 小於 0.5 秒才算，超過重新計算
                    btnClickCounter++;
                }
                else
                {
                    btnClickCounter = 0;
                }
            }
        }
    }

    // 按鈕持續被按住，且不為設定模式，則執行開關刀鋒
    // btnState = 0 就能讓按鈕還是按住的狀態，執行程式後跳離判斷式
    if (btnState == 1)
    {
        unsigned long butPressDuration = millis() - btnPressTime;
        // 進入長按前的輕點次數為 0，且按住超過 0.5 秒
        if (btnClickCounter == 0 && butPressDuration >= 500)
        {

            if (bladeState == 1)
            { // 狀態為開啟時，則關閉刀鋒
                //Serial.println("Turn off the ledstrip.");
                ButtonInitialize(); // 按鈕狀態初始化，跳出判斷式
                bladeState = 0;
                delay(100);
                myDFPlayer.playMp3Folder(2);
                delay(100);
                BladeOFF();
                // delay(500);
            }
            else
            {
                ButtonInitialize(); // 按鈕狀態初始化，跳出判斷式
                bladeState = 1;
                myDFPlayer.playMp3Folder(1);
                BladeON();
            }
        }
    }

    // 按鈕放開後，刀鋒為開啟狀態，且連點兩下，切換顏色
    if (btnState == 2 && bladeState == 1 && btnClickCounter == 2)
    {
        DefaultColor = DefaultColor + 1;
        if (DefaultColor >= BladeColors)
        {
            DefaultColor = 0;
        }
        setStrip(DefaultColor);
        ButtonInitialize(); // 按鈕狀態初始化，跳出判斷式
    }

    // 刀鋒狀態為開啟、撥放器為待機，則撥放 Hum 音效
    if (bladeState == 1 && DFPlayerState == 1)
    {
        playTrack(3);
    }
}

// 初始化 Swing 狀態
void initSwing()
{
    isSwing = 0;
    isPlaySwing = 0;
}

void playTrack(int trackIndex)
{
    myDFPlayer.playMp3Folder(trackIndex);
    delay(200);
}

// 按鈕狀態初始化，跳出判斷式。每次判斷點擊成功，執行對應效果後都需初始化
void ButtonInitialize()
{
    btnState = 0;
    btnClickCounter = 0;
}

void BladeON()
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        setPixel(i, DefaultColor);
        LEDStrip.show();
        delay(15);
    }
    delay(100);
}

void BladeOFF()
{
    for (int i = NUM_LEDS - 1; i > -1; i--)
    {
        LEDStrip.setPixelColor(i, 0);
        delay(15);
        LEDStrip.show();
    }
}

void setPixel(uint8_t Pixel, uint8_t ColorSerialNum)
{
    switch (ColorSerialNum)
    {
    case 0:
        LEDStrip.setPixelColor(Pixel, LEDStrip.Color(255, 0, 0));
        break;
    case 1:
        LEDStrip.setPixelColor(Pixel, LEDStrip.Color(0, 255, 0));
        break;
    case 2:
        LEDStrip.setPixelColor(Pixel, LEDStrip.Color(0, 0, 255));
        break;
    case 3:
        LEDStrip.setPixelColor(Pixel, LEDStrip.Color(255, 255, 0));
        break;
    case 4:
        LEDStrip.setPixelColor(Pixel, LEDStrip.Color(255, 0, 255));
        break;
    default:
        LEDStrip.setPixelColor(Pixel, Color);
        break;
    }
}

// 變色特效中，設定燈條整串顏色變色時使用
void setStrip(uint8_t ColorSerialNum)
{
    for (uint8_t i = 0; i < NUM_LEDS; i++)
    {
        setPixel(i, ColorSerialNum);
    }
    LEDStrip.show();
}