
#include "contiki.h"
#include "spi-arch.h"
#include "spi.h"
#include "dev/ssi.h"
#include "dev/leds.h"
#include "dev/ioc.h"
#include "deca_device_api.h"
#include "deca_regs.h"

decaIrqStatus_t dw1000_irq_onoff = 0;


void usleep (int microseconds) {
  clock_delay_usec(microseconds);
}

void dw1000_irq_callback (uint8_t port, uint8_t pin) {
  // leds_toggle(LEDS_BLUE);

  GPIO_SET_PIN(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(6));
  do {
    dwt_isr();
  } while (GPIO_READ_PIN(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN))); // while IRQ line active


  GPIO_CLR_PIN(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(6));
}

void
dw1000_init()
{
  spi_cs_init(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);
  SPI_CS_SET(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);

  // Setup GPIO for antenna selection
  {
    uint8_t buf[4];
    buf[0] = 0x70;  // set GPIO 0,1,2 to output (set mask bits)
    dwt_writetodevice(GPIO_CTRL_ID, GPIO_DIR_OFFSET, 1, &buf[0]);

    // set GPIO1 (antenna P6) active for now
    buf[0] = 0x22;
    dwt_writetodevice(GPIO_CTRL_ID, GPIO_DOUT_OFFSET, 1, &buf[0]);
  }

  // Init the DW1000 IRQ interrupt pin
  GPIO_SOFTWARE_CONTROL(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
  GPIO_SET_INPUT(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
  GPIO_DETECT_EDGE(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
  GPIO_TRIGGER_SINGLE_EDGE(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
  GPIO_DETECT_RISING(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
  GPIO_ENABLE_INTERRUPT(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
  ioc_set_over(DW1000_IRQ_PORT_NUM, DW1000_IRQ_PIN, IOC_OVERRIDE_DIS);
  nvic_interrupt_enable(NVIC_INT_GPIO_PORT_C);
  gpio_register_callback(dw1000_irq_callback, DW1000_IRQ_PORT_NUM, DW1000_IRQ_PIN);
  dw1000_irq_onoff = 1;

  // Setup DW1000 reset pin
  // Make it input unless we want to use it
  GPIO_SOFTWARE_CONTROL(GPIO_PORT_TO_BASE(DW1000_RST_N_PORT_NUM), GPIO_PIN_MASK(DW1000_RST_N_PIN));
  GPIO_SET_INPUT(GPIO_PORT_TO_BASE(DW1000_RST_N_PORT_NUM), GPIO_PIN_MASK(DW1000_RST_N_PIN));

  GPIO_SOFTWARE_CONTROL(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(6));
  GPIO_SET_OUTPUT(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(6));
  GPIO_CLR_PIN(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(6));
}

int readfromspi(uint16_t headerLength,
                const uint8_t *headerBuffer,
                uint32_t readlength,
                uint8_t *readBuffer) {
  int i;

  // spi_set_mode(SSI_CR0_FRF_MOTOROLA, SSI_CR0_SPO, SSI_CR0_SPH, 8);
  spi_set_mode(SSI_CR0_FRF_MOTOROLA, 0, 0, 8);

  SPI_CS_CLR(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);

  // leds_on(LEDS_RED);
  // leds_on(LEDS_GREEN);

  for (i=0; i<headerLength; i++) {
    SPI_WRITE(headerBuffer[i]);
  }

  SPI_FLUSH();

  for (i=0; i<readlength; i++) {
    SPI_READ(readBuffer[i]);
  }

  SPI_CS_SET(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);

  return 0;

}

int writetospi(uint16_t headerLength,
               const uint8_t *headerBuffer,
               uint32_t bodylength,
               const uint8_t *bodyBuffer) {
  int i;

  // spi_set_mode(SSI_CR0_FRF_MOTOROLA, SSI_CR0_SPO, SSI_CR0_SPH, 8);
  spi_set_mode(SSI_CR0_FRF_MOTOROLA, 0, 0, 8);
  SPI_CS_CLR(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);

  for (i=0; i<headerLength; i++) {
    // SPI_WRITE(headerBuffer[i]);
    SPI_WRITE_FAST(headerBuffer[i]);
  }

  for (i=0; i<bodylength; i++) {
    // SPI_WRITE(bodyBuffer[i]);
    SPI_WRITE_FAST(bodyBuffer[i]);
  }

  SPI_WAITFOREOTx();

  SPI_CS_SET(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);

  return 0;
}

// Select the active antenna
//
// antenna_number    part_identifier   GPIO
// ------------------------------------------
// 0                 P5                GPIO2
// 1                 P6                GPIO1
// 2                 P7                GPIO0
void dw1000_choose_antenna (uint8_t antenna_number) {
  uint8_t buf[4];
  buf[0] = 0x70;  // set GPIO 0,1,2 to output (set mask bits)
  dwt_writetodevice(GPIO_CTRL_ID, GPIO_DIR_OFFSET, 1, &buf[0]);

  // set GPIO1 (antenna P6) active for now
  buf[0] = ((1 << (2-antenna_number)) << 4) | (1 << (2-antenna_number));
  dwt_writetodevice(GPIO_CTRL_ID, GPIO_DOUT_OFFSET, 1, &buf[0]);
}

void dw1000_populate_eui (uint8_t *eui_buf, uint8_t id) {
  eui_buf[0] = id;
  eui_buf[1] = 0x55;
  eui_buf[2] = 0x44;
  eui_buf[3] = 'N';
  eui_buf[4] = 'P';
  eui_buf[5] = 0xe5;
  eui_buf[6] = 0x98;
  eui_buf[7] = 0xc0;
}

void dw1000_reset () {
  GPIO_SET_OUTPUT(GPIO_PORT_TO_BASE(DW1000_RST_N_PORT_NUM), GPIO_PIN_MASK(DW1000_RST_N_PIN));

  // clr
  GPIO_CLR_PIN(GPIO_PORT_TO_BASE(DW1000_RST_N_PORT_NUM), GPIO_PIN_MASK(DW1000_RST_N_PIN));
  usleep(100000);

  GPIO_SET_INPUT(GPIO_PORT_TO_BASE(DW1000_RST_N_PORT_NUM), GPIO_PIN_MASK(DW1000_RST_N_PIN));
}

void port_SPIx_clear_chip_select () {
  SPI_CS_CLR(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);
}

void port_SPIx_set_chip_select () {
  SPI_CS_SET(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);
}

void setup_DW1000RSTnIRQ () {

}

int portGetTickCount () {
  return (int) clock_time();
}

// "Mutex" functions
// Disable the DW1000 interrupt if it is on
decaIrqStatus_t decamutexon (void) {
  // if (dw1000_irq_onoff == 1) {
  //   GPIO_DISABLE_INTERRUPT(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
  //   dw1000_irq_onoff = 0;
  //   return 1;
  // }
  return 0;
}

void decamutexoff (decaIrqStatus_t s) {
  // if (s) {
  //   GPIO_ENABLE_INTERRUPT(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
  //   dw1000_irq_onoff = 1;
  // }
}
