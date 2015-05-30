
#include "contiki.h"
#include "spi-arch.h"
#include "spi.h"
#include "dev/ssi.h"
#include "dev/udma.h"
#include "dev/leds.h"
#include "dev/ioc.h"
#include "deca_device_api.h"
#include "deca_regs.h"



/*---------------------------------------------------------------------------*/
/*
 * uDMA transfer threshold. DMA will only be used to read an incoming frame
 * if its size is above this threshold
 */
#define UDMA_RX_SIZE_THRESHOLD 3
#define SSI_FIFO_DEPTH 8

// I don't understand how Contiki doesn't define this or something like it
// somewhere. I think they don't understand the TI channel vs encoding idea
#define UDMA_SSI0_RX_CHANNEL 10
#define UDMA_SSI0_TX_CHANNEL 11
/*---------------------------------------------------------------------------*/

decaIrqStatus_t dw1000_irq_onoff = 0;


void usleep (int microseconds) {
  clock_delay_usec(microseconds);
}

void dw1000_irq_callback (uint8_t port, uint8_t pin) {
  // leds_toggle(LEDS_BLUE);

  do {
    dwt_isr();
  } while (GPIO_READ_PIN(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN))); // while IRQ line active


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

  /*
  // Set up DMA
  // Handles 10.4.1
  //  -- enables DMA controller
  //  -- set up control channel table
  // Also enables DMA interrupts in NVIC controller
  udma_init();

  // Enable DMA on the SSI0 controller
  //REG(SSI0_BASE + SSI_DMACTL) = SSI_DMACTL_RXDMAE | SSI_DMACTL_TXDMAE;

  // 10.4.3.1 - 1
  // Don't care about priority
  // 10.4.3.1 - 2
  udma_channel_use_primary(UDMA_SSI0_RX_CHANNEL);
  // 10.4.3.1 - 3
  udma_channel_use_single(UDMA_SSI0_RX_CHANNEL);
  // 10.4.3.1 - 4
  udma_channel_mask_clr(UDMA_SSI0_RX_CHANNEL);

  // Set the channel's SRC. DST can not be set yet since changes for each transfer
  udma_set_channel_src(UDMA_SSI0_RX_CHANNEL, SPI_RXBUF);


  // 10.4.3.1 - 1, write UDMA_PRIOCLR
  udma_channel_prio_set_default(UDMA_SSI0_TX_CHANNEL);
  // 10.4.3.1 - 2, write UDMA_ALTCLR
  udma_channel_use_primary(UDMA_SSI0_TX_CHANNEL);
  // 10.4.3.1 - 3, write UDMA_USEBURSTCLR
  udma_channel_use_single(UDMA_SSI0_TX_CHANNEL);
  // 10.4.3.1 - 4, write UDMA_REQMASKCLR
  udma_channel_mask_clr(UDMA_SSI0_TX_CHANNEL);

  // Set the channel's DST. SRC can not be set yet since changes for each transfer
  udma_set_channel_dst(UDMA_SSI0_TX_CHANNEL, SPI_TXBUF);

  // Use the legcay mapper b/c it's easier
  REG(UDMA_CHASGN) = 0;
  */
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

  /*
  for (i=0; i<readlength; i++) {
    SPI_READ(readBuffer[i]);
  }
  */
  if (readlength <= SSI_FIFO_DEPTH) {
    // easy mode
    for (i=0; i<readlength; i++) {
      SPI_TXBUF = 0;
    }
    for (i=0; i<readlength; i++) {
      SPI_WAITFOREORx();
      readBuffer[i] = SPI_RXBUF;
    }
  } else {
    // prime the pipeline
    for (i=0; i<SSI_FIFO_DEPTH; i++) {
      SPI_TXBUF = 0;
    }
    while (i < readlength) {
      SPI_WAITFOREORx();
      readBuffer[i-SSI_FIFO_DEPTH] = SPI_RXBUF;
      SPI_WAITFORTxREADY();
      SPI_TXBUF = 0;
      i++;
    }
    i -= SSI_FIFO_DEPTH;
    while (i < readlength) {
      SPI_WAITFOREORx();
      readBuffer[i] = SPI_RXBUF;
      i++;
    }
  }

  SPI_CS_SET(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);

  return 0;

  /*
  if (readlength > UDMA_RX_SIZE_THRESHOLD) {
    // Set the transfer destination's end address
    //udma_set_channel_dst(CC2538_RF_CONF_RX_DMA_CHAN, (uint32_t)(buf) + len - 1);
    udma_set_channel_dst(UDMA_SSI0_RX_CHANNEL, (uint32_t)(readBuffer) + readlength - 1);

    // Configure the control word
    // 10.4.3.2.1
    udma_set_channel_control_word(UDMA_SSI0_RX_CHANNEL,
        UDMA_CHCTL_DSTINC_8 |
        UDMA_CHCTL_DSTSIZE_8 |
        UDMA_CHCTL_SRCINC_NONE |
        UDMA_CHCTL_SRCSIZE_8 |
        UDMA_CHCTL_ARBSIZE_4 |
        udma_xfer_size(readlength) |
        UDMA_CHCTL_XFERMODE_AUTO
        );

    // 10.4.3.3 - 1
    udma_channel_enable(UDMA_SSI0_RX_CHANNEL);

    int i;
    for (i = 0; i < readlength; i++)
      SPI_WRITE_FAST(0);

    // <from rf>
    udma_channel_sw_request(UDMA_SSI0_RX_CHANNEL);

    // Wait for the transfer to complete.
    while (udma_channel_get_mode(UDMA_SSI0_RX_CHANNEL) != UDMA_CHCTL_XFERMODE_STOP)
      ;
  } else {
    for(i = 0; i < len; ++i) {
      ((unsigned char *)(buf))[i] = REG(RFCORE_SFR_RFDATA);
    }
  }
  */
}

int writetospi(uint16_t headerLength,
               const uint8_t *headerBuffer,
               uint32_t bodylength,
               const uint8_t *bodyBuffer) {
  int i;

  // spi_set_mode(SSI_CR0_FRF_MOTOROLA, SSI_CR0_SPO, SSI_CR0_SPH, 8);
  spi_set_mode(SSI_CR0_FRF_MOTOROLA, 0, 0, 8);
  SPI_CS_CLR(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);

  if ((headerLength + bodylength) <= SSI_FIFO_DEPTH) {
    // Bypass contiki SPI mechanism b/c we won't exceed the fifo depth here
    SPI_WAITFORTxREADY();
    for (i=0; i<headerLength; i++) {
      SPI_TXBUF = headerBuffer[i];
    }
    for (i=0; i<bodylength; i++) {
      SPI_TXBUF = bodyBuffer[i];
    }
    SPI_WAITFOREOTx();
  } else {
    for (i=0; i<headerLength; i++) {
      SPI_WRITE_FAST(headerBuffer[i]);
    }

    if (bodylength > SSI_FIFO_DEPTH) {
      // Slow path
      for (i=0; i<bodylength; i++) {
        SPI_WRITE_FAST(bodyBuffer[i]);
      }
      SPI_WAITFOREOTx();
    } else {

      // Bypass contiki SPI mechanism b/c we won't exceed the fifo depth here
      SPI_WAITFORTxREADY();
      for (i=0; i<bodylength; i++) {
        //SPI_WRITE_FAST(bodyBuffer[i]);
        SPI_TXBUF = bodyBuffer[i];
      }
      SPI_WAITFOREOTx();
    }
  }

  SPI_CS_SET(DW1000_CS_N_PORT_NUM, DW1000_CS_N_PIN);

  return 0;

      /*
      REG(SSI0_BASE + SSI_DMACTL) = SSI_DMACTL_TXDMAE;
      udma_set_channel_dst(UDMA_SSI0_TX_CHANNEL, SPI_TXBUF);
      udma_set_channel_src(UDMA_SSI0_TX_CHANNEL, (uint32_t)(bodyBuffer) + bodylength - 1);

      // Configure the control word
      // 10.4.3.2.1
      udma_set_channel_control_word(UDMA_SSI0_TX_CHANNEL,
          UDMA_CHCTL_DSTINC_NONE |
          UDMA_CHCTL_DSTSIZE_8 |
          UDMA_CHCTL_SRCINC_8 |
          UDMA_CHCTL_SRCSIZE_8 |
          UDMA_CHCTL_ARBSIZE_4 |
          udma_xfer_size(bodylength) |
          UDMA_CHCTL_XFERMODE_AUTO
          );

      // 10.4.3.3 - 1, write UDMA_ENASET
      udma_channel_enable(UDMA_SSI0_TX_CHANNEL);

      // Wait for the transfer to complete.
      while (udma_channel_get_mode(UDMA_SSI0_TX_CHANNEL) != UDMA_CHCTL_XFERMODE_STOP)
        ;
      */
}

// Select the active antenna
//
// antenna_number    part_identifier   GPIO
// ------------------------------------------
// 0                 P5                GPIO2
// 1                 P6                GPIO1
// 2                 P7                GPIO0
void dw1000_choose_antenna (uint8_t antenna_number) {
  //Assign all GPIO pins to act as GPIO
  uint8_t buf[4];
  memset(buf, 0, 4);
  dwt_writetodevice(GPIO_CTRL_ID, GPIO_MODE_OFFSET, GPIO_MODE_LEN, buf);

  buf[0] = 0x70;  // set GPIO 0,1,2 to output (set mask bits)
  dwt_writetodevice(GPIO_CTRL_ID, GPIO_DIR_OFFSET, GPIO_DIR_LEN, buf);

  // set GPIO1 (antenna P6) active for now
  buf[0] = 0x70 | (1 << (2-antenna_number));
  dwt_writetodevice(GPIO_CTRL_ID, GPIO_DOUT_OFFSET, GPIO_DOUT_LEN, buf);
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
  if (dw1000_irq_onoff == 1) {
    GPIO_DISABLE_INTERRUPT(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
    dw1000_irq_onoff = 0;
    return 1;
  }
  return 0;
}

void decamutexoff (decaIrqStatus_t s) {
  if (s) {
    GPIO_ENABLE_INTERRUPT(GPIO_PORT_TO_BASE(DW1000_IRQ_PORT_NUM), GPIO_PIN_MASK(DW1000_IRQ_PIN));
    dw1000_irq_onoff = 1;
  }
}
