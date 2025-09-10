#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

//clock
#define PIN_XVCLK 28
#define VSYNC 27
#define HREF 26
#define PCLK 22

// I2C defines
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define OV7675_ADDR 0x21

volatile bool frame_started = false;
volatile bool frame_done = false;

volatile uint32_t write_index = 0;
uint8_t image[320*240*2] = {0};
//uint8_t framebuf[120*160] = {0}; // 仮のバッファ
// 画像サイズ (QQVGA 160x120, YUV422 → 2byte/pixel)

#define WIDTH  320
#define HEIGHT 240
#define FRAME_SIZE (WIDTH * HEIGHT * 2)



// void I2C_setup(){
//     i2c_init(I2C_PORT, 100 * 1000);
//     gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
//     gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
//     gpio_pull_up(I2C_SDA);
//     gpio_pull_up(I2C_SCL);
// }

// void ov7675_power_on() {
//     // gpio_init(PIN_PWDN);
//     // gpio_set_dir(PIN_PWDN, true);

//     // // PWDNをHighに保持
//     // gpio_put(PIN_PWDN, 1);

//     // XVCLKを12MHzで出力
//     clock_gpio_init(PIN_XVCLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 12 * MHZ);

//     // sleep_ms(5); // 電源安定待ち

//     // // PWDNをLowにして動作開始
//     // gpio_put(PIN_PWDN, 0);

//     sleep_ms(20); // I2C初期化まで待機
// }

void Gen_clock(){
    //pwm設定
    gpio_set_function(28, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(28);
    pwm_set_clkdiv_int_frac(slice_num, 1, 0);
    pwm_set_wrap(slice_num, 9);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 5);

    pwm_set_enabled(slice_num, true);
}

void ov7675_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(i2c1, OV7675_ADDR, buf, 2, false);
}

uint8_t ov7675_read(uint8_t reg) {
    uint8_t val;
    i2c_write_blocking(i2c1, OV7675_ADDR, &reg, 1, true);
    i2c_read_blocking(i2c1, OV7675_ADDR, &val, 1, false);
    return val;
}

void ov7675_init() {
    // // Soft reset
    // ov7675_write(0x12, 0x80); 
    // printf("soft-reset\n"); 
    // sleep_ms(100);

    // // 2. 出力フォーマット: RGB, QQVGA
    // // COM7 (0x12): RGBモード + QQVGA
    // // bit[7]=reset, bit[4]=QCIF/QQVGA, bit[2:0]=フォーマット
    // ov7675_write(0x12, 0x14); // QVGA + RGB565

    // //com15 RGB565フォーマット
    // ov7675_write(0x40, 0x10);

    // // 1. ホワイトバランス、ゲイン、露出の自動制御を有効化
    // //    COM8: AGC(ゲイン), AWB(ホワイトバランス), AEC(露出) を有効にする
    // ov7675_write(0x13, 0x8F); // [cite: 1096]

    // // // COM13 (0x3D): YUV出力設定
    // // ov7675_write(0x3D, 0xC0); // UV swap, YUV422 enable

    // // // TSLB (0x3A): UYVY順序など
    // // ov7675_write(0x3A, 0x04); // UYVY

    // // // 3. クロック設定
    // // ov7675_write(0x11, 0x01); // PCLK = XCLK/2 程度に分周

    // // QVGA用のデフォルト設定（一般的な設定値）
    // ov7675_write(0x11, 0x01); // クロック分周

    // // 4. スケーラ設定 (QQVGA = QVGA/2)
    // ov7675_write(0x0C, 0x04); // DCW (Downsample control)
    // ov7675_write(0x3e, 0x00); // COM14
    // ov7675_write(0x72, 0x22); // X_SCALER
    // ov7675_write(0x73, 0xF2); // Y_SCALER

    // // 5. ウィンドウ (例: QVGAからクロップして縮小)
    // ov7675_write(0x17, 0x16); // HSTART
    // ov7675_write(0x18, 0x04); // HSTOP
    // ov7675_write(0x32, 0x80); // HREF
    // ov7675_write(0x19, 0x02); // VSTART
    // ov7675_write(0x1A, 0x7A); // VSTOP
    // ov7675_write(0x03, 0x0A); // VREF

    ov7675_write(0x12, 0x80); // Soft reset
    sleep_ms(100);

    // --- フォーマット設定: QVGA, RGB565 ---
    ov7675_write(0x12, 0x14); // COM7: bit[4]=QVGA選択, bit[2]=RGBモード
    ov7675_write(0x40, 0x10); // COM15: RGB565フォーマット

    // QVGA用のデフォルト設定（一般的な設定値）
    ov7675_write(0x11, 0x01); // クロック分周
    ov7675_write(0x3e, 0x00); // COM14
    
    // ウィンドウ設定
    ov7675_write(0x17, 0x13); // HSTART
    ov7675_write(0x18, 0x01); // HSTOP
    ov7675_write(0x32, 0xb6); // HREF
    ov7675_write(0x19, 0x02); // VSTART
    ov7675_write(0x1a, 0x7a); // VSTOP
    ov7675_write(0x03, 0x0a); // VREF
}

void GPIO_set(){
    for(int i = 0; i <= 7; i++){
        gpio_init(i);
        gpio_set_dir(i,GPIO_IN);
    }
}

void cap_pic_init(){
    gpio_init(VSYNC);
    gpio_init(HREF);
    gpio_init(PCLK);
    gpio_set_dir(VSYNC, GPIO_IN);
    gpio_set_dir(HREF, GPIO_IN);
    gpio_set_dir(PCLK, GPIO_IN);
}

// Perform initialisation
int pico_led_init(void) {
#if defined(PICO_DEFAULT_LED_PIN)
    // A device like Pico that uses a GPIO for the LED will define PICO_DEFAULT_LED_PIN
    // so we can use normal GPIO functionality to turn the led on and off
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return PICO_OK;
#elif defined(CYW43_WL_GPIO_LED_PIN)
    // For Pico W devices we need to initialise the driver etc
    return cyw43_arch_init();
#endif
}

// Turn the led on or off
void pico_set_led(bool led_on) {
#if defined(PICO_DEFAULT_LED_PIN)
    // Just set the GPIO on or off
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
#elif defined(CYW43_WL_GPIO_LED_PIN)
    // Ask the wifi "driver" to set the GPIO on or off
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
#endif
}

int main() {
    stdio_init_all();
    sleep_ms(2000); // USBシリアル接続待ち

    int rc = pico_led_init();
    hard_assert(rc == PICO_OK);
    // printf("Waiting for one frame...\n");
    
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    Gen_clock();
    sleep_ms(20); // I2C初期化まで待機

    ov7675_init();//初期化
    GPIO_set();//初期化
    cap_pic_init();//初期化


    uint32_t write_index = 0;
    // int last_vsync = 0;
    // int last_pclk = 0;
    // bool capturing = false;
    // int flag = 0;
    // int flame_state = 0;
    // int byto_state = 0;
    // int vsync_last = 0;
    // int pclk_last = 0;
    // int count = 0;
    // uint8_t byto0 = 0;
    // uint8_t byto1 = 0;
    printf("Ready to capture. Send 'C' to start.\n");
    fflush(stdout); // メッセージを即時送信
    while (getchar() != 'C');
    
    pico_set_led(true); // キャプチャ開始時にLED点灯

    // 1. VSYNC信号を待ってフレームの開始を同期する
    //    VSYNCがLow -> Highになる瞬間がフレームの開始
    while (gpio_get(VSYNC)); // VSYNCがLowになるまで待つ
    while (!gpio_get(VSYNC)); // VSYNCがHighになるまで待つ

    write_index = 0; // バッファのインデックスをリセット

    // 2. 1フレーム分 (120行) のデータを取得する
    for (int y = 0; y < HEIGHT; y++) {
        // 3. HREF信号を待って、有効なラインデータの開始を同期する
        while (!gpio_get(HREF));

        // 4. 1ライン分 (160ピクセル * 2バイト) のデータを取得する
        for (int x = 0; x < WIDTH * 2; x++) {
            // 5. PCLKの立ち上がりエッジを待つ
            while (!gpio_get(PCLK));

            // D0-D7(GPIO 0-7)から8bitデータを一括で読み取る
            uint8_t data = (uint8_t)(gpio_get_all() & 0xFF);
            
            // バッファオーバーフローを防ぎつつ、データを格納
            if (write_index < FRAME_SIZE) {
                image[write_index++] = data;
            }

            // 6. PCLKの立ち下がりエッジを待つ (次のクロックに備える)
            while (gpio_get(PCLK));
        }
    }
    
    pico_set_led(false); // キャプチャ完了時にLED消灯

    printf("---FRAME_START---\n");
    for (int i = 0; i < FRAME_SIZE; i++) {
        // データを16進数2桁 (0埋め) で出力し、スペースで区切る
        printf("%02X ", image[i]);
        
        // 16バイトごと（横幅160pxのYUVなら8ピクセル分）に改行して見やすくする
        if ((i + 1) % 16 == 0) {
            printf("\n");
            sleep_ms(1);
        }
    }
    printf("\n---FRAME_END---\n");
    fflush(stdout); // バッファをフラッシュして即時送信
    
    // 次のキャプチャまで待機
    while(true);
}
