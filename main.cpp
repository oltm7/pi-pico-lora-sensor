#include <pico/stdlib.h>
#include <cstdint>
#include <cmath>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/adc.h"

#include <RadioLib.h>
#include <hal/RPiPico/PicoHal.h>

// Controls whether the Pico only receives (false) or only transmits (true)
#define I_AM_TRANSMITTER true

// Time between sensor reads (minutes)
#define SAMPLE_PERIOD 30

// Soil moisture sensor voltage settle time (on time per read)
#define SENSOR_TIME 500 // ms
// Soil moisture sensor transistor beta
#define SENSOR_BETA 230 // ratio between I_C and I_B
// Soil moisture number of repeat reads for averaging
#define SENSOR_REPEAT 3

// ADC reference voltage
#define ADC_VREF 3.3 // V

// ADC bits
#define ADC_RES 12 // bits

// Transmission behaviour settings
#define INITIAL_DELAY 500 // ms
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
#define LORA_SYNC_WORD 0x08
  // output power:                -5 dBm
#define TX_POWER_DBM -5 // dBm
  // preamble length:             8 symbols
#define LORA_PREAMBLE_LENGTH 8 // symbols
#define TCXO_VOLTAGE 3.1 // UNUSED, just running on default voltage
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

// define length of transmit message buffer
#define MSG_LEN 7 // bytes

void init_all_hw(SX1262& radio);
int pico_led_init(void);
void pico_set_led(bool led_on);
void sensors_init(void);
uint16_t en_read_sensor(int sensor);
uint8_t v_bat_read(void);
uint8_t temp_read(void);
uint16_t multi_sample(uint8_t n_adc, uint8_t n, uint16_t rest_ms);
void sleep_minutes(int n);

#if I_AM_TRANSMITTER
int main() {
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
  uint8_t v_bat, temp;
  uint8_t msg[MSG_LEN] = {0};
  for(;;) {
    // read both sensor values
    // TODO: Perform e.g. 3 reads per sensor and send average values. Maybe should shorten sensor read time...
    res_sens_a = en_read_sensor(SENS_IN_A);
    res_sens_b = en_read_sensor(SENS_IN_B);
    v_bat = v_bat_read();
    temp = temp_read();
    printf("Sensor A: %d ohms, Sensor B: %d ohms, V_BAT: %d, TEMP: %d deg C \n", 
      res_sens_a, res_sens_b, v_bat, temp);
    // build a packet
    msg[0] = 0x47; // header
    // TODO Message length
    msg[1] = (res_sens_a & 0xFF00) >> 8; // A MSB
    msg[2] = (res_sens_a & 0x00FF); // A LSB
    msg[3] = (res_sens_b & 0xFF00) >> 8; // B MSB
    msg[4] = (res_sens_b & 0x00FF); // B LSB
    msg[5] = (v_bat); // 8-bit battery voltage w.r.t 3.3V
    msg[6] = (temp); // Temperature in degrees celsius

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
  // create a new instance of the HAL class
  PicoHal* hal = new PicoHal(SPI_PORT, SPI_MISO, SPI_MOSI, SPI_SCK);
  SX1262 radio = new Module(hal, RFM_NSS, RFM_DIO1, RFM_RST, RFM_BUSY);

  sleep_ms(INITIAL_DELAY);

  int16_t status;
  uint8_t rx_buf[NUM_BYTES];

  init_all_hw(radio);
  printf("[SX1262] Listening for data ... ");

  for(;;) {
    // attempt to receive a packet
    memset(rx_buf, 0, NUM_BYTES);  // Zero-initialise buffer
    status = radio.receive(rx_buf, NUM_BYTES, RX_TIMEOUT);
    if(status == RADIOLIB_ERR_NONE) {
      // the packet was successfully received
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
        printf("Sensor A: %u kiloohms, Sensor B: %u kiloohms\n", sens_a, sens_b);
      }
    } else if (status != RADIOLIB_ERR_RX_TIMEOUT) {
      printf("Failure to Receive, Code: %d\n", status);
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
  
  // Initialise ADC inputs
  adc_init();
  adc_gpio_init(SENS_IN_A);
  adc_gpio_init(SENS_IN_B);
  adc_gpio_init(VBAT_BUF);
  adc_set_temp_sensor_enabled(true);
}

uint16_t multi_sample(uint8_t n_adc, uint8_t n, uint16_t rest_ms){
  uint16_t raw_sum = 0;
  for (int i = 0; i < n; i++){
    adc_select_input(n_adc);
    raw_sum += adc_read();
    sleep_ms(rest_ms);
  }
  float raw_avg = (float)raw_sum / (float)n;
  uint16_t avg = lroundf(raw_avg);
  return avg;
}

// Enable and read from the given sensor. Translates the ADC value from raw
// to resistance (kilo-ohms).
uint16_t en_read_sensor(int sensor){
  uint16_t value = 0;
  float voltage, resistance;
  if (sensor == SENS_IN_A){
    gpio_put(EN_SENS_A, 1); // Enable sensor
    sleep_ms(SENSOR_TIME); // Allow voltage to settle
    value = multi_sample(0, 3, SENSOR_TIME);
    gpio_put(EN_SENS_A, 0); // Disable sensor
  } else if (sensor == SENS_IN_B){
    gpio_put(EN_SENS_B, 1); // Enable sensor
    sleep_ms(SENSOR_TIME); // Allow voltage to settle
    value = multi_sample(1, 3, SENSOR_TIME);
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

// Returns battery voltage value as an 8-bit value relative to 3.3V.
// We do three reads and average them first for a better result.
uint8_t v_bat_read(void){
  int n = 3; // Number of sample points
  uint16_t avg = multi_sample(2, n, 30);
  uint8_t value = ((2 * avg) / pow(2, 12)) * pow(2, 8); // avg x2 because of voltage divider
  return value;
}

// Return MCU temperature as 8-bit value in degrees Celsius.
uint8_t temp_read(void){
  uint16_t raw = multi_sample(4, 3, 100);
  float voltage = (3.3 / pow(2, 12)) * raw;
  printf("Raw: %d, Voltage: %f\n", raw, voltage);
  float temp = 27.0f - (voltage - 0.706f) / 0.001721f;
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
        LORA_PREAMBLE_LENGTH
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
  // == Possibly not needed because of processor directive == 
  // Set DIO2 as RF Switch (internal control of the RF Switch)
  radio.setDio2AsRfSwitch(true);
}
