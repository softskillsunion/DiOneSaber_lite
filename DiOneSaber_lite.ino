

#include <Bounce2.h>
#include <Adafruit_NeoPixel.h>

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

void setup()
{
    Serial.begin(115200);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    debouncer.attach(BUTTON_PIN);
    debouncer.interval(30); // interval in milliseconds

    pinMode(BLADELED_PIN, OUTPUT);

    LEDStrip.begin();
    LEDStrip.setBrightness(255);
    LEDStrip.show();
}

void loop()
{
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
                BladeOFF();
            }
            else
            {
                ButtonInitialize(); // 按鈕狀態初始化，跳出判斷式
                bladeState = 1;
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