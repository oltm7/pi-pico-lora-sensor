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

// Soil moisture sensor voltage settle time (on time per read)
#define SENSOR_TIME 2000 // ms

// Soil moisture sensor transistor beta
#define SENSOR_BETA 222 // ratio between I_C and I_B

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
  // sync word:                   0x34 (public network/LoRaWAN)
#define LORA_SYNC_WORD 0x34
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

void init_all_hw(SX1262& radio);
int pico_led_init(void);
void pico_set_led(bool led_on);
void sensors_init(void);
float en_read_sensor(int sensor);

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
  float res_sens_a;
  float res_sens_b;

  for(;;) {
    // read both sensor values
    res_sens_a = en_read_sensor(SENS_IN_A);
    res_sens_b = en_read_sensor(SENS_IN_B);
    printf("Sensor A: %.2f ohms, Sensor B: %.2f ohms\n", res_sens_a, res_sens_b);
    // send a packet
    printf("[SX1262] Transmitting packet ... ");
    state = radio.transmit("Hello World!");
    if(state == RADIOLIB_ERR_NONE) {
      // the packet was successfully transmitted
      printf("success!\n");
      pico_set_led(true);
      sleep_ms(LED_DELAY_MS);
      pico_set_led(false);

      // wait for a half second before transmitting again
      hal->delay(500);

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
      printf("Received data: %s\n", rx_buf);
      pico_set_led(true);
      sleep_ms(LED_DELAY_MS);
      pico_set_led(false);
    } else {
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
}

// Enable and read from the given sensor. Translates the ADC value from raw
// to resistance (ohms).
float en_read_sensor(int sensor){
  uint16_t value = 0;
  float voltage, resistance;
  if (sensor == SENS_IN_A){
    gpio_put(EN_SENS_A, 1); // Enable sensor
    sleep_ms(SENSOR_TIME); // Allow voltage to settle
    adc_select_input(0);
    value = adc_read();
    gpio_put(EN_SENS_A, 0); // Disable sensor
  } else if (sensor == SENS_IN_B){
    gpio_put(EN_SENS_B, 1); // Enable sensor
    sleep_ms(SENSOR_TIME); // Allow voltage to settle
    adc_select_input(1);
    value = adc_read();
    gpio_put(EN_SENS_B, 0); // Disable sensor
  }
  // Convert value to resistance (ohms)
  voltage = (value*ADC_VREF)/(pow(2, ADC_RES)-1);
  printf("Voltage: %.2f\n", voltage);
  resistance = ((1e3*(1+SENSOR_BETA)*(2.6-voltage))/voltage)-100e3;
  if (resistance < 0){
    resistance = 0;
  }
  return resistance;
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
