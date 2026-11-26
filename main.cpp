#include <pico/stdlib.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/adc.h"

#include <RadioLib.h>
#include <hal/RPiPico/PicoHal.h>

// Controls whether the Pico only receives (false) or only transmits (true)
#define I_AM_TRANSMITTER false

// Time between sensor reads (minutes). 0 = send continuously (debugging only).
// Set back to 5 for deployment.
#define SAMPLE_PERIOD 5

// Soil moisture sensor voltage settle time (on time per read)
#define SENSOR_TIME 500 // ms
// Soil moisture sensor transistor beta
#define SENSOR_BETA 230 // ratio between I_C and I_B
// Soil moisture number of repeat reads for averaging
#define SENSOR_REPEAT 3

// ADC reference voltage
#define ADC_VREF 3.3f // V
#define VBAT_MAX_V     5.0f //V  // value that maps to 255 (cell status)

// ADC bits
#define ADC_RES 12 // bits

// Transmission behaviour settings
#define INITIAL_DELAY 2000 // ms
#define LED_DELAY_MS 100
#define NUM_BYTES 255
#define RX_TIMEOUT 0 // ms

// ========= SX1262 Hardware settings =========
  // carrier frequency:           915.2 MHz
#define C_FREQ_MHZ 915.2 // MHz
  // bandwidth:                   125.0 kHz
#define BW_KHZ 125.0 // kHz
  // spreading factor:            7 
#define LORA_SF 7
  // coding rate:                 5
#define LORA_CR 5
  // sync word:                   e.g. 0x34 (public network/LoRaWAN)
#define LORA_SYNC_WORD 0x34
  // output power:                -5 dBm
#define TX_POWER_DBM 10 // dBm
  // preamble length:             8 symbols
#define LORA_PREAMBLE_LENGTH 8 // symbols
#define TCXO_VOLTAGE 1.8
// ===========================================

// define pins to be used for interfacing with the SX1262
#define SPI_PORT spi0
#define SPI_MISO 4
#define SPI_MOSI 3
#define SPI_SCK 2

#define RFM_NSS 21
#define RFM_RST 22
#define RFM_BUSY 14
#define RFM_DIO1 15
#define SX126X_DIO2_AS_RF_SWITCH

// define sensor IO pins
#define EN_SENS_A 18 
#define EN_SENS_B 19
#define SENS_IN_A 26
#define SENS_IN_B 27
#define VBAT_BUF 28

// define cell status input pins
#define CELL_PPR 7
#define CELL_CHG 8
#define CELL_OC 17
#define CELL_OD 16

// define length of transmit message buffer
#define MSG_LEN 8 // bytes

void init_all_hw(SX1262& radio);
int pico_led_init(void);
void pico_set_led(bool led_on);
void sensors_init(void);
uint16_t en_read_sensor(int sensor);
uint8_t v_bat_read(void);
uint8_t temp_read(void);
uint16_t multi_sample(uint8_t n_adc, uint8_t n, uint16_t rest_ms);
void sleep_minutes(int n);
uint8_t cell_get_status(void);
void printIrqStatus(SX1262& radio);

#if I_AM_TRANSMITTER
int main() {
  stdio_init_all();
  // create a new instance of the HAL class
  PicoHal* hal = new PicoHal(SPI_PORT, SPI_MISO, SPI_MOSI, SPI_SCK);
  SX1262 radio = new Module(hal, RFM_NSS, RFM_DIO1, RFM_RST, RFM_BUSY);

  sleep_ms(INITIAL_DELAY);
  init_all_hw(radio);
  // Flash lights to display functionality
  en_read_sensor(SENS_IN_A);
  en_read_sensor(SENS_IN_B);

  int state; // Store radio state
  uint16_t res_sens_a, res_sens_b;
  uint8_t v_bat, temp, status;
  uint8_t msg[MSG_LEN] = {0};
  for(;;) {
    // read both sensor values
    res_sens_a = en_read_sensor(SENS_IN_A);
    res_sens_b = en_read_sensor(SENS_IN_B);
    v_bat = v_bat_read();
    temp = temp_read();
    status = cell_get_status();
    printf("Sensor A: %d kohm, Sensor B: %d kohm, V_BAT: %d, TEMP: %d deg C, Cell Status: %d \n", 
      res_sens_a, res_sens_b, v_bat, temp, status);
    // build a packet
    msg[0] = 0x47; // header
    // TODO Message length
    msg[1] = (res_sens_a & 0xFF00) >> 8; // A MSB
    msg[2] = (res_sens_a & 0x00FF); // A LSB
    msg[3] = (res_sens_b & 0xFF00) >> 8; // B MSB
    msg[4] = (res_sens_b & 0x00FF); // B LSB
    msg[5] = v_bat; // 8-bit battery voltage from 0V to 5V (0-255)
    msg[6] = temp; // Temperature in degrees celsius
    msg[7] = status; // Cell monitor status bits: [MSB->LSB] PPR, CHG, OD, OC
    // TODO CRC-8
    printf("[SX1262] Transmitting packet ... ");
    state = radio.transmit(msg, MSG_LEN);
    if(state == RADIOLIB_ERR_NONE) {
      // the packet was successfully transmitted
      printf("success!\n");
      pico_set_led(true);
      sleep_ms(LED_DELAY_MS);
      pico_set_led(false);

      // wait for a time before transmitting again
      sleep_minutes(SAMPLE_PERIOD);

    } else {
      printf("failed, code %d\n", state);
      sleep_ms(1000);
    }
  }
  return(0);
}
#else
int main() {
  stdio_init_all();
  // create a new instance of the HAL class
  PicoHal* hal = new PicoHal(SPI_PORT, SPI_MISO, SPI_MOSI, SPI_SCK);
  SX1262 radio = new Module(hal, RFM_NSS, RFM_DIO1, RFM_RST, RFM_BUSY);

  sleep_ms(INITIAL_DELAY);

  uint8_t rx_buf[NUM_BYTES];

  printf("Connecting to SX1262 ...");

  init_all_hw(radio);

  printf("[SX1262] Listening for data ... \n");

  uint32_t n_ok = 0, n_to = 0, n_err = 0, t_rep = 0;
  for (;;) {
    // attempt to receive a packet
    memset(rx_buf, 0, NUM_BYTES);  // Zero-initialise buffer
    int16_t st = radio.receive(rx_buf, NUM_BYTES, RX_TIMEOUT);
    if (st == RADIOLIB_ERR_NONE) {
      n_ok++;
      double radio_rssi = (double)radio.getRSSI();
      double radio_snr = (double)radio.getSNR();
      printf("RX #%lu len=%u RSSI=%.1f SNR=%.1f\n", (unsigned long)n_ok,
             (unsigned)radio.getPacketLength(),
             radio_rssi, radio_snr);
      printf("Received data: ");
      for (int i = 0; i < MSG_LEN; i++) {
        printf("%02X ", rx_buf[i]);
      }
      printf("\n");
      pico_set_led(true);
      sleep_ms(LED_DELAY_MS);
      pico_set_led(false);
      if (rx_buf[0] != 0x47) {
        printf("Bad header!\n");
      } else {
        uint16_t sens_a = (rx_buf[1] << 8) | rx_buf[2];
        uint16_t sens_b = (rx_buf[3] << 8) | rx_buf[4];
        uint8_t v_bat = rx_buf[5];
        uint8_t temp = rx_buf[6];
        uint8_t cell_status = rx_buf[7];
        float v_bat_voltage = 5.0f * (v_bat / 255.0f);
        printf("cell voltage: %.2f V, temp: %d C, status: 0x%02X, Sensor A: %u kohm, Sensor B: %u kohm\n",
               (double)v_bat_voltage, temp, cell_status, sens_a, sens_b);
        printf("RSSI,SNR,VBAT,TEMP,STATUS,SENSA,SENSB\n");
        printf("%.1f,%.1f,%.1f,%d,%02X,%u,%u\n", 
          radio_rssi,radio_snr,(double)v_bat_voltage, temp, cell_status, sens_a, sens_b);
      }
    } else if (st == RADIOLIB_ERR_RX_TIMEOUT) {
      n_to++;
    } else {
      n_err++;
      printf("receive error %d\n", st);   // -7 = CRC error
    }
  }
  return(0);
}
#endif

// Onboard LED initialisation
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

// Soil moisture sensors (A and B) input and output pin initialisation.
void sensors_init(void) {
  // Initialise sensor enable lines A and B
  gpio_init(EN_SENS_A);
  gpio_set_dir(EN_SENS_A, GPIO_OUT);
  gpio_put(EN_SENS_A, 0);

  gpio_init(EN_SENS_B);
  gpio_set_dir(EN_SENS_B, GPIO_OUT);
  gpio_put(EN_SENS_B, 0);
  
  // Initialise cell status pins (OC/OD are CMOS push-pull so no pullups)
  gpio_init(CELL_PPR);
  gpio_pull_up(CELL_PPR);
  gpio_set_dir(CELL_PPR, GPIO_IN);

  gpio_init(CELL_CHG);
  gpio_pull_up(CELL_CHG);
  gpio_set_dir(CELL_CHG, GPIO_IN);

  gpio_init(CELL_OC);
  gpio_set_dir(CELL_OC, GPIO_IN);

  gpio_init(CELL_OD);
  gpio_set_dir(CELL_OD, GPIO_IN);

  // Initialise ADC inputs
  adc_init();
  adc_gpio_init(SENS_IN_A);
  adc_gpio_init(SENS_IN_B);
  adc_gpio_init(VBAT_BUF);
  adc_set_temp_sensor_enabled(true);
}

// Reads status bits from the cell management system to monitor PPR (power present),
// CHG (charging), OC (overcharged), and OD (overdischarged).
// returns: [MSB->LSB] PPR, CHG, OD, OC, 0, 0, 0, 0
uint8_t cell_get_status(void){
  uint8_t status = 0;
  status = (gpio_get(CELL_PPR) << 7) | (gpio_get(CELL_CHG) << 6) | 
            (gpio_get(CELL_OD) << 5) | (gpio_get(CELL_OC) << 4);
  return status;
}

uint16_t multi_sample(uint8_t n_adc, uint8_t n, uint16_t rest_ms){
  uint32_t raw_sum = 0;
  for (int i = 0; i < n; i++){
    adc_select_input(n_adc);
    raw_sum += adc_read();
    sleep_ms(rest_ms);
  }
  float raw_avg = (float)raw_sum / (float)n;
  uint16_t avg = lroundf(raw_avg);
  return avg;
}

void printIrqStatus(SX1262& radio) {
    uint32_t irq = radio.getIrqFlags();
    printf("SX1262 IRQ Status: 0x%04lX [ ", irq);

    if (irq == 0) {
        printf("NONE ");
    } else {
        if (irq & RADIOLIB_SX126X_IRQ_TX_DONE)           printf("TX_DONE ");
        if (irq & RADIOLIB_SX126X_IRQ_RX_DONE)           printf("RX_DONE ");
        if (irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED) printf("PREAMBLE_DETECTED ");
        if (irq & RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID)   printf("SYNC_WORD_VALID ");
        if (irq & RADIOLIB_SX126X_IRQ_HEADER_VALID)      printf("HEADER_VALID ");
        if (irq & RADIOLIB_SX126X_IRQ_HEADER_ERR)        printf("HEADER_ERR ");
        if (irq & RADIOLIB_SX126X_IRQ_CRC_ERR)           printf("CRC_ERR ");
        if (irq & RADIOLIB_SX126X_IRQ_CAD_DONE)          printf("CAD_DONE ");
        if (irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED)      printf("CAD_DETECTED ");
        if (irq & RADIOLIB_SX126X_IRQ_TIMEOUT)           printf("TIMEOUT ");
        if (irq & RADIOLIB_SX126X_IRQ_LR_FHSS_HOP)       printf("LR_FHSS_HOP ");
    }
    printf("]\n");
}

// Enable and read from the given sensor. Translates the ADC value from raw
// to resistance (kilo-ohms).
uint16_t en_read_sensor(int sensor){
  uint16_t value = 0;
  float voltage, resistance;
  if (sensor == SENS_IN_A){
    gpio_put(EN_SENS_A, 1); // Enable sensor
    sleep_ms(SENSOR_TIME); // Allow voltage to settle
    value = multi_sample(0, SENSOR_REPEAT, SENSOR_TIME);
    gpio_put(EN_SENS_A, 0); // Disable sensor
  } else if (sensor == SENS_IN_B){
    gpio_put(EN_SENS_B, 1); // Enable sensor
    sleep_ms(SENSOR_TIME); // Allow voltage to settle
    value = multi_sample(1, SENSOR_REPEAT, SENSOR_TIME);
    gpio_put(EN_SENS_B, 0); // Disable sensor
  }
  // Convert value to resistance (ohms)
  voltage = (value*ADC_VREF)/(pow(2, ADC_RES)-1);
  printf("Voltage: %.2f\n", voltage);
  resistance = ((1e3*(1+SENSOR_BETA)*(2.6-voltage))/voltage)-100e3;
  resistance = resistance / 1e3; // Return resistance in kilo-ohms
  // Clamp resistance value within uint16_t range
  if (resistance < 0){
    resistance = 0;
  } else if (resistance > pow(2, 16) - 1){
    resistance = pow(2, 16) - 1;
  }
  return static_cast<uint16_t>(resistance);
}

// Returns battery voltage value as an 8-bit value (0-255 maps to 0-5V).
// We do three reads and average them first for a better result.
uint8_t v_bat_read(void){
  adc_select_input(2);
  sleep_ms(500);
  (void)adc_read(); // discard first measurement
  uint16_t avg = multi_sample(2, 3, 1000);
  float v_adc  = (avg * 2.89f) / 4095.0f; // Manual vref calibration estimate of 2.89 applied
  float v_bat  = v_adc * 2.0f;
  float scaled = (v_bat / 5.0f) * 255.0f;
  printf("V_BAT: raw: %d, v_adc: %f, v_bat: %f V, scaled: %f (0-255 wrt 0-5V)\n", avg, v_adc, v_bat, scaled);
  if (scaled < 0.0f)   scaled = 0.0f;
  if (scaled > 255.0f) scaled = 255.0f;
  return (uint8_t)lroundf(scaled);
}

// Return MCU temperature as 8-bit value in degrees Celsius.
uint8_t temp_read(void){
  adc_select_input(4);
  sleep_us(1000);
  (void)adc_read(); // discard first measurement
  uint16_t raw = multi_sample(4, 3, 100);
  float voltage = (ADC_VREF / 4095.0f) * raw;
  printf("Raw: %d, Voltage: %f\n", raw, voltage);
  float temp = (27.0f - (voltage - 0.706f) / 0.001721f);
  if (temp < 0.0f){
    temp = 0.0f;
  } else if (temp > 125.0f){
    temp = 125.0f;
  }
  uint8_t temp_short = lroundf(temp);
  return temp_short;
}

// Naive "sleep" command, doesn't actually lower power draw at all, however
// 25mA is manageable (if wasteful) on a >1500mAh system with solar.
void sleep_minutes(int n) {
    for (int i = 0; i < n; i++) {
        sleep_ms(60000); // 1 minute per chunk
    }
}

// Main hardware initialisation function
void init_all_hw(SX1262& radio) {
  // Initialise LED
  pico_led_init();

  // Initialise soil moisture sensor IOs
  sensors_init();

  // Initialise SPI0 port
  spi_init(spi0, 2 * 1000 * 1000); // SPI speed
  gpio_set_function(SPI_MISO, GPIO_FUNC_SPI);
  gpio_set_function(SPI_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(SPI_SCK, GPIO_FUNC_SPI);

  // Initialise RFM_NSS as a high output (aka Chip Select)
  gpio_init(RFM_NSS);
  gpio_set_dir(RFM_NSS, GPIO_OUT);
  gpio_put(RFM_NSS, 1);

  gpio_init(RFM_BUSY);
  gpio_set_dir(RFM_BUSY, GPIO_IN);

  gpio_init(RFM_DIO1);
  gpio_set_dir(RFM_DIO1, GPIO_IN);

  printf("[SX1262] Initializing ... ");

  pico_set_led(true);
  sleep_ms(500);
  pico_set_led(false);

  // Reset radio
  gpio_init(RFM_RST);
  gpio_set_dir(RFM_RST, GPIO_OUT);

  gpio_put(RFM_RST, 0);
  sleep_ms(20);
  gpio_put(RFM_RST, 1);
  sleep_ms(50);

  // Wait for BUSY
  uint32_t timeout = 500;
  while(gpio_get(RFM_BUSY) && timeout--) {
    sleep_ms(1);
  }

  printf("BUSY=%d DIO1=%d\n",
        gpio_get(RFM_BUSY),
        gpio_get(RFM_DIO1));

  int state;
  int attempts = 0;

  do {

    state = radio.begin(
        C_FREQ_MHZ,
        BW_KHZ,
        LORA_SF,
        LORA_CR,
        LORA_SYNC_WORD,
        TX_POWER_DBM,
        LORA_PREAMBLE_LENGTH,
        TCXO_VOLTAGE
    );

    if(state != RADIOLIB_ERR_NONE) {

        printf("Init failed (%d), retry %d\n", state, attempts);

        gpio_put(RFM_RST, 0);
        sleep_ms(20);
        gpio_put(RFM_RST, 1); 
        sleep_ms(50);

        timeout = 500;
        while(gpio_get(RFM_BUSY) && timeout--) {
            sleep_ms(1);
        }

        sleep_ms(100);
    }

    attempts++;

  } while(state != RADIOLIB_ERR_NONE);

  printf("Radio init success!\n");
  // Set DIO2 as RF Switch (internal control of the RF Switch)
  radio.setDio2AsRfSwitch(true);
}