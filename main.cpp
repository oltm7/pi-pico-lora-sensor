#include <pico/stdlib.h>
#include <stdint.h>
#include "hardware/gpio.h"
#include "hardware/spi.h"

#include <RadioLib.h>
#include <hal/RPiPico/PicoHal.h>

// Controls whether the Pico only receives (false) or only transmits (true)
#define I_AM_TRANSMITTER false

#define INITIAL_DELAY 500
#define LED_DELAY_MS 100
#define NUM_BYTES 255
#define RX_TIMEOUT 0 // ms

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

// define pins to be used for interfacing with the SX1262
#define SPI_PORT spi0
#define SPI_MISO 4
#define SPI_MOSI 3
#define SPI_SCK 2

#define RFM_NSS 26
#define RFM_RST 22
#define RFM_BUSY 14
#define RFM_DIO1 15

#define SX126X_DIO2_AS_RF_SWITCH
#define TCXO_VOLTAGE 3.1

// create a new instance of the HAL class
PicoHal* hal = new PicoHal(SPI_PORT, SPI_MISO, SPI_MOSI, SPI_SCK);
SX1262 radio = new Module(hal, RFM_NSS, RFM_DIO1, RFM_RST, RFM_BUSY);

// now we can create the radio module
// NSS pin:  26
// BUSY pin:  14
// RESET pin:  22
// DIO1 pin:  15

void init_all_hw(void) {
  // Initialise LED
  pico_led_init();

  // Initialise SPI0 port
  spi_init(spi0, 4 * 1000 * 1000); // SPI at 4MHz
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
  // carrier frequency:           915.2 MHz
  // bandwidth:                   125.0 kHz
  // spreading factor:            7 
  // coding rate:                 5
  // sync word:                   0x34 (public network/LoRaWAN)
  // output power:                -5 dBm
  // preamble length:             8 symbols
  // radio.begin(915.2, 125.0, 7, 5, 0x34, -5, 8);
  int state = radio.begin(915.2, 125.0, 7, 5, 0x34, -5, 8); //+ TCXO_VOLTAGE
  if (state == RADIOLIB_ERR_NONE) {
    printf("success!");
  } else {
    printf("failed, code ");
    printf("%d\n", state);
    while (true) { sleep_ms(10); }
  }
  // == Possibly not needed because of processor directive == 
  // Set DIO2 as RF Switch (internal control of the RF Switch)
  radio.setDio2AsRfSwitch(true);
}

#if I_AM_TRANSMITTER
int main() {
  sleep_ms(INITIAL_DELAY);
  int state;
  init_all_hw();
  // Super loop
  for(;;) {
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
    }
  }
  return(0);
}
#else
int main() {
  sleep_ms(INITIAL_DELAY);
  int16_t status;
  uint8_t rx_buf[NUM_BYTES];
  init_all_hw();
  printf("[SX1262] Listening for data ... ");
  // Super loop
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