/*
* (c) Domen Puncer, Visionect, d.o.o.
* BSD License
*
* v0.2 add support for SDHC
*/

#include <stdio.h>
#include "diskio.h"
#include "sdcard_spi.h"
#include "integer.h"


/*
* Code is split into 2 parts:
* - generic SPI code: adapt for your MCU
* - sd card code, with crc7 and crc16 calculations
*   there's lots of it, but it's simple
*/


//===========================================================================//

SPI_InitTypeDef  SPI_InitStructure;

hwif hw;

//===========================================================================//


void sd_spi_init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  
  /* Enable SPI clock, SPI1: APB2, SPI2: APB1 */
  RCC_APBPeriphClockCmd_SPI_SD(RCC_APBPeriph_SPI_SD, ENABLE);
  
  /* Configure I/O for Flash Chip select */
  GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_SPI_SD_CS;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIO_SPI_SD_CS, &GPIO_InitStructure);
  
  /* Configure SPI pins: SCK and MOSI with default alternate function (not re-mapped) push-pull */
  GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_SPI_SD_SCK | GPIO_Pin_SPI_SD_MOSI;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
  GPIO_Init(GPIO_SPI_SD, &GPIO_InitStructure);
  /* Configure MISO as Input with internal pull-up */
  GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_SPI_SD_MISO;
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
  GPIO_Init(GPIO_SPI_SD, &GPIO_InitStructure);
  
  
  /* SPI configuration */
  SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
  SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
  SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
  SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
  SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
  SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_SPI_SD; // 72000kHz/256=281kHz < 400kHz
  SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
  SPI_InitStructure.SPI_CRCPolynomial = 7;
  
  SPI_Init(SPI_SD, &SPI_InitStructure);
  SPI_CalculateCRC(SPI_SD, DISABLE);
  SPI_Cmd(SPI_SD, ENABLE);
  
#pragma diag_suppress=Pe550
  uint32_t dummyread;
  /* drain SPI */
  while (SPI_I2S_GetFlagStatus(SPI_SD, SPI_I2S_FLAG_TXE) == RESET) { ; }
  dummyread = SPI_I2S_ReceiveData(SPI_SD);
#pragma diag_default=Pe550
  
  spi_set_speed(SD_SPEED_400KHZ);
}

void spi_set_speed(enum sd_speed speed)
{
  if (speed == SD_SPEED_400KHZ) {
    //SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_128;
    CLEAR_BIT(SPI_SD->CR1, SPI_BaudRatePrescaler_2);
    SET_BIT(SPI_SD->CR1, SPI_BaudRatePrescaler_128);
    
  } else if (speed == SD_SPEED_25MHZ) {
    //SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    CLEAR_BIT(SPI_SD->CR1, SPI_BaudRatePrescaler_128);
    SET_BIT(SPI_SD->CR1, SPI_BaudRatePrescaler_2);
  }
  // ^ with /2 APB1 this will be 15mhz/234k at 60mhz
  // 18/281 at 72. which is ok, 100<x<400khz, and <25mhz
  /*
  SPI_Cmd(SPI_SD, DISABLE);
  SPI_InitStructure.SPI_CRCPolynomial = 7;
  SPI_Init(SPI_SD, &SPI_InitStructure);
  SPI_Cmd(SPI_SD, ENABLE);
  */
}

u8 spi_txrx(u8 data)
{
  /* RXNE always happens after TXE, so if this function is used
  * we don't need to check for TXE */
  SPI_SD->DR = data;
  WAIT_FOR_RX;
  
  return SPI_SD->DR;
}

u16 spi_txrx16(u16 data)
{
  u16 r;
  
  SPI_SD->DR = data>>8;
  WAIT_FOR_RX;
  r = SPI_SD->DR <<8;
  
  SPI_SD->DR = data;
  WAIT_FOR_RX;
  r |= SPI_SD->DR;
  
  return r;
}

u32 spi_txrx32(uint32_t data)
{
  union uResult_t {
    uint8_t resultArr[4];
    uint32_t result32;
  } uResult;
  
  SPI_SD->DR = (data>>24);
  WAIT_FOR_RX;
  uResult.resultArr[3] = SPI_SD->DR;
  
  SPI_SD->DR = (data>>16);
  WAIT_FOR_RX;
  uResult.resultArr[2] = SPI_SD->DR;
  
  SPI_SD->DR = (data>>8);
  WAIT_FOR_RX;
  uResult.resultArr[1] = SPI_SD->DR;
  
  SPI_SD->DR = (data);
  WAIT_FOR_RX;
  uResult.resultArr[0] = SPI_SD->DR;
  
  return uResult.result32;
}

void spi_txrxArr(u8 *buf, u16 len, u8 data)
{
  for (u16 i=0; i<len; i++) {
    SPI_SD->DR = data;
    WAIT_FOR_RX;
    
    buf[i] = SPI_SD->DR;
  }
}

/* crc helpers */
u8 crc7_one(u8 t, u8 data)
{
  int i;
  const u8 g = 0x89;
  
  t ^= data;
  for (i=0; i<8; i++) {
    if (t & 0x80)
      t ^= g;
    t <<= 1;
  }
  return t;
}

u8 crc7(const u8 *p, int len)
{
  int j;
  u8 crc = 0;
  for (j=0; j<len; j++)
    crc = crc7_one(crc, p[j]);
  
  return crc>>1;
}

/* http://www.eagleairaust.com.au/code/crc16.htm */
u16 crc16_ccitt(u16 crc, u8 ser_data)
{
  crc  = (u8)(crc >> 8) | (crc << 8);
  crc ^= ser_data;
  crc ^= (u8)(crc & 0xff) >> 4;
  crc ^= (crc << 8) << 4;
  crc ^= ((crc & 0xff) << 4) << 1;
  
  return crc;
}

u16 crc16(const u8 *p, int len)
{
  int i;
  u16 crc = 0;
  
  for (i=0; i<len; i++)
    crc = crc16_ccitt(crc, p[i]);
  
  return crc;
}


/*** sd functions - on top of spi code ***/

void sd_cmd(u8 cmd, u32 arg)
{
  u8 crc = 0;
  crc = crc7_one(crc, 0x40 | cmd);
  crc = crc7_one(crc, arg >> 24);
  crc = crc7_one(crc, arg >> 16);
  crc = crc7_one(crc, arg >> 8);
  crc = crc7_one(crc, arg);
  
  spi_txrx(0x40 | cmd);
  spi_txrx32(arg);
  //spi_txrx(0x95);	/* crc7, for cmd0 */
  spi_txrx(crc | 0x1);	/* crc7, for cmd0 */
}

u8 sd_get_r1()
{
  int tries = 1000;
  u8 r;
  
  while (tries--) {
    r = spi_txrx(0xff);
    if ((r & 0x80) == 0)
      return r;
  }
  return 0xff;
}

u16 sd_get_r2()
{
  int tries = 1000;
  u16 r;
  
  while (tries--) {
    r = spi_txrx(0xff);
    if ((r & 0x80) == 0)
      break;
  }
  if (tries < 0)
    return 0xff;
  r = r<<8 | spi_txrx(0xff);
  
  return r;
}

/*
* r1, then 32-bit reply... same format as r3
*/
u8 sd_get_r7(u32 *r7)
{
  u32 r;
  r = sd_get_r1();
  if (r != 0x01)
    return r;
  
  *r7 = spi_txrx32(0xffffffff);
  return 0x01;
}


/* Nec (=Ncr? which is limited to [0,8]) dummy bytes before lowering CS,
* as described in sandisk doc, 5.4. */
void sd_nec()
{
  spi_txrx32(0xffffffff);
  spi_txrx32(0xffffffff);
}


int sd_init(hwif *hw)
{
  int i;
  int r;
  u32 r7;
  u32 r3;
  int tries;
  
  hw->capabilities = 0;
  
  /* start with 100-400 kHz clock */
  spi_set_speed(SD_SPEED_400KHZ);
  
  /* cmd0 - reset.. */
  spi_cs_high();
  /* 74+ clocks with CS high */
  for (i=0; i<5; i++)
    spi_txrx16(0xffff);
  
  /* reset */
  spi_cs_low();
  sd_cmd(0, 0);
  r = sd_get_r1();
  sd_nec();
  spi_cs_high();
  
  if (r == 0xff)
    return -1;
  if (r != 0x01) {
    /* fail */
    return -2;
  }
  /* success */
  
  /* cmd8 - voltage.. */
  /* ask about voltage supply */
  spi_cs_low();
  sd_cmd(8, 0x1aa /* VHS = 1 */);
  r = sd_get_r7(&r7);
  sd_nec();
  spi_cs_high();
  hw->capabilities |= CAP_VER2_00;
  
  if (r == 0xff)
    return -1;
  if (r == 0x01);
  /* success, SD v2.x */
  else if (r & 0x4) {
    hw->capabilities &= ~CAP_VER2_00;
    /* not implemented, SD v1.x */
  } else {
    /* fail */
    return -2;
  } 
  
  /* cmd58 - ocr.. */
  /* ask about voltage supply */
  spi_cs_low();
  sd_cmd(58, 0);
  r = sd_get_r3(&r3);
  sd_nec();
  spi_cs_high();
  
  if (r == 0xff)
    return -1;
  if (r != 0x01 && !(r & 0x4)) { /* allow it to not be implemented - old cards */
    /* fail */
    return -2;
  }
  else {
    int i;
    for (i=4; i<=23; i++)
      if (r3 & 1<<i)
        break;
    for (i=23; i>=4; i--)
      if (r3 & 1<<i)
        break;
    /* CCS shouldn't be valid here yet */

    /* success */
  }
  
  
  /* acmd41 - hcs.. */
  tries = 1000;
  u32 hcs = 0;
  /* say we support SDHC */
  if (hw->capabilities & CAP_VER2_00)
    hcs = 1<<30;
  
  /* needs to be polled until in_idle_state becomes 0 */
  do {
    /* send we don't support SDHC */
    spi_cs_low();
    /* next cmd is ACMD */
    sd_cmd(55, 0);
    r = sd_get_r1();
    sd_nec();
    spi_cs_high();
    if (r == 0xff)
      return -1;
    /* well... it's probably not idle here, but specs aren't clear */
    if (r & 0xfe) {
      /* fail */
      return -2;
    }
    
    spi_cs_low();
    sd_cmd(41, hcs);
    r = sd_get_r1();
    sd_nec();
    spi_cs_high();
    if (r == 0xff)
      return -1;
    if (r & 0xfe) {
      /* fail */
      return -2;
    }
  } while (r != 0 && tries--);
  if (tries == -1) {
    /* timeouted */
    return -2;
  }
  /* success */
  
  /* Seems after this card is initialized which means bit 0 of R1
  * will be cleared. Not too sure. */
  
  
  if (hw->capabilities & CAP_VER2_00) {
    /* cmd58 - ocr, 2nd time.. */
    /* ask about voltage supply */
    spi_cs_low();
    sd_cmd(58, 0);
    r = sd_get_r3(&r3);
    sd_nec();
    spi_cs_high();
    if (r == 0xff)
      return -1;
    if (r & 0xfe) {
      /* fail */
      return -2;
    }
    else {
#if 1
      int i;
      for (i=4; i<=23; i++)
        if (r3 & 1<<i)
          break;
      for (i=23; i>=4; i--)
        if (r3 & 1<<i)
          break;
      /* CCS shouldn't be valid here yet */

      // XXX power up status should be 1 here, since we're finished initializing, but it's not. WHY?
      // that means CCS is invalid, so we'll set CAP_SDHC later
#endif
      if (r3>>30 & 1) {
        hw->capabilities |= CAP_SDHC;
      }
      
      /* success */
    }
  }
  
  /* with SDHC block length is fixed to 1024 */
  if ((hw->capabilities & CAP_SDHC) == 0) {
    /* cmd16 - block length.. */
    spi_cs_low();
    sd_cmd(16, 512);
    r = sd_get_r1();
    sd_nec();
    spi_cs_high();
    if (r == 0xff)
      return -1;
    if (r & 0xfe) {
      /* fail */
      return -2;
    }
    /* success */
  } 
  
  /* cmd59 - enable crc.. */
  /* crc on */
  spi_cs_low();
  sd_cmd(59, 0);
  r = sd_get_r1();
  sd_nec();
  spi_cs_high();
  if (r == 0xff)
    return -1;
  if (r & 0xfe) {
    /* fail */
    return -2;
  }
  /* success */
  
  /* now we can up the clock to <= 25 MHz */
  spi_set_speed(SD_SPEED_25MHZ);
  
  return 0;
}

int sd_read_status(hwif *hw)
{
  u16 r2;
  
  spi_cs_low();
  sd_cmd(13, 0);
  r2 = sd_get_r2();
  sd_nec();
  spi_cs_high();
  if (r2 & 0x8000)
    return -1;
  
  return 0;
}

/* 0xfe marks data start, then len bytes of data and crc16 */
int sd_get_data(hwif *hw, u8 *buf, int len)
{
  int tries = 20000;
  u8 r;
  u16 _crc16;
  u16 calc_crc;
  
  while (tries--) {
    r = spi_txrx(0xff);
    if (r == 0xfe)
      break;
  }
  if (tries < 0)
    return -1;
  
  spi_txrxArr(buf, len, 0xff);
 
  _crc16 = spi_txrx16(0xffff);

  calc_crc = crc16(buf, len);
  if (_crc16 != calc_crc) {
    return -1;
  }
  
  return 0;
}

int sd_put_data(hwif *hw, const u8 *buf, int len)
{
  u8 r;
  int tries = 10;
  u16 crc;
  
  spi_txrx(0xfe); /* data start */
  
  while (len--)
    spi_txrx(*buf++);
  
  crc = crc16(buf, len);
  /* crc16 */
  spi_txrx(crc>>8);
  spi_txrx(crc);
  
  /* normally just one dummy read in between... specs don't say how many */
  while (tries--) {
    r = spi_txrx(0xff);
    if (r != 0xff)
      break;
  }
  if (tries < 0)
    return -1;
  
  /* poll busy, about 300 reads for 256 MB card */
  tries = 100000;
  while (tries--) {
    if (spi_txrx(0xff) == 0xff)
      break;
  }
  if (tries < 0)
    return -2;
  
  /* data accepted, WIN */
  if ((r & 0x1f) == 0x05)
    return 0;
  
  return r;
}

int sd_read_csd(hwif *hw)
{
  u8 buf[16];
  int r;
  int capacity;
  
  spi_cs_low();
  sd_cmd(9, 0);
  r = sd_get_r1();
  if (r == 0xff) {
    spi_cs_high();
    return -1;
  }
  if (r & 0xfe) {
    spi_cs_high();
    return -2;
  }
  
  r = sd_get_data(hw, buf, 16);
  sd_nec();
  spi_cs_high();
  if (r == -1) {
    /* failed to get csd */
    return -3;
  }
  
  if ((buf[0] >> 6) + 1 == 1) {
    /* CSD v1 */
    
    capacity = (((buf[6]&0x3)<<10 | buf[7]<<2 | buf[8]>>6)+1) << (2+(((buf[9]&3) << 1) | buf[10]>>7)) << ((buf[5] & 0xf) - 9);
    /* ^ = (c_size+1) * 2**(c_size_mult+2) * 2**(read_bl_len-9) */
    
  } else {
    /* CSD v2 */
    /* this means the card is HC */
    hw->capabilities |= CAP_SDHC;
    
    capacity = buf[7]<<16 | buf[8]<<8 | buf[9]; /* in 512 kB */
    capacity *= 1024; /* in 512 B sectors */
    
  }
  
  hw->sectors = capacity;
  
  /* if erase_blk_en = 0, then only this many sectors can be erased at once
  * this is NOT yet tested */
  hw->erase_sectors = 1;
  if (((buf[10]>>6)&1) == 0)
    hw->erase_sectors = ((buf[10]&0x3f)<<1 | buf[11]>>7) + 1;
  
  return 0;
}

int sd_read_cid(hwif *hw)
{
  u8 buf[16];
  int r;
  
  spi_cs_low();
  sd_cmd(10, 0);
  r = sd_get_r1();
  if (r == 0xff) {
    spi_cs_high();
    return -1;
  }
  if (r & 0xfe) {
    spi_cs_high();
    return -2;
  }
  
  r = sd_get_data(hw, buf, 16);
  sd_nec();
  spi_cs_high();
  if (r == -1) {
    /* failed to get cid */
    return -3;
  }
  
  return 0;
}


int sd_readsector(hwif *hw, u32 address, u8 *buf)
{
  int r;
  
  spi_cs_low();
  if (hw->capabilities & CAP_SDHC)
    sd_cmd(17, address); /* read single block */
  else
    sd_cmd(17, address*512); /* read single block */
  
  r = sd_get_r1();
  if (r == 0xff) {
    spi_cs_high();
    r = -1;
    return r;
  }
  if (r & 0xfe) {
    spi_cs_high();
    r = -2;
    return r;
  }
  
  r = sd_get_data(hw, buf, 512);
  sd_nec();
  spi_cs_high();
  if (r == -1) {
    r = -3;
    return r;
  }
  
  return 0;
}

int sd_writesector(hwif *hw, u32 address, const u8 *buf)
{
  int r;
  
  spi_cs_low();
  if (hw->capabilities & CAP_SDHC)
    sd_cmd(24, address); /* write block */
  else
    sd_cmd(24, address*512); /* write block */
  
  r = sd_get_r1();
  if (r == 0xff) {
    spi_cs_high();
    r = -1;
    return r;
  }
  if (r & 0xfe) {
    spi_cs_high();
    r = -2;
    return r;
  }
  
  spi_txrx(0xff); /* Nwr (>= 1) high bytes */
  r = sd_put_data(hw, buf, 512);
  sd_nec();
  spi_cs_high();
  if (r != 0) {
    r = -3;
    return r;
  }
  
  /* efsl code is weird shit, 0 is error in there?
  * not that it's properly handled or anything,
  * and the return type is char, fucking efsl */
  return 0;
}


/*** public API - on top of sd/spi code ***/

int hwif_init(hwif* hw)
{
  int tries = 10;
  
  if (hw->initialized)
    return 0;
  
  while (tries--) {
    if (sd_init(hw) == 0)
      break;
  }
  if (tries == -1)
    return -1;
  
  /* read status register */
  sd_read_status(hw);
  
  sd_read_cid(hw);
  if (sd_read_csd(hw) != 0)
    return -1;
  
  hw->initialized = 1;
  return 0;
}

int sd_read(hwif* hw, u32 address, u8 *buf)
{
  int r;
  int tries = 10;
  
  r = sd_readsector(hw, address, buf);
  
  while (r < 0 && tries--) {
    if (sd_init(hw) != 0)
      continue;
    
    /* read status register */
    sd_read_status(hw);
    
    r = sd_readsector(hw, address, buf);
  }
  //if (tries == -1)
  //printf("%s: couldn't read sector %li\n", __func__, address);
  
  return r;
}

int sd_write(hwif* hw, u32 address,const u8 *buf)
{
  int r;
  int tries = 10;
  
  r = sd_writesector(hw, address, buf);
  
  while (r < 0 && tries--) {
    if (sd_init(hw) != 0)
      continue;
    
    /* read status register */
    sd_read_status(hw);
    
    r = sd_writesector(hw, address, buf);
  }
  //if (tries == -1)
  //printf("%s: couldn't write sector %li\n", __func__, address);
  
  return r;
}
