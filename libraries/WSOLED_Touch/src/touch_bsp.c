#include <stdio.h>
#include "touch_bsp.h"
#include "WSOLED_Core.h"
#include "driver/i2c.h"

#define TOUCH_HOST  ((i2c_port_t)WSOLED_CORE_I2C_PORT)
#define EXAMPLE_LCD_H_RES  536
#define EXAMPLE_LCD_V_RES  240
#define I2C_ADDR_FT3168 0x38
uint8_t I2C_writr_buff(uint8_t addr,uint8_t reg,uint8_t *buf,uint8_t len);
uint8_t I2C_read_buff(uint8_t addr,uint8_t reg,uint8_t *buf,uint8_t len);
uint8_t I2C_read_buff(uint8_t addr,uint8_t reg,uint8_t *buf,uint8_t len);

void Touch_Init(void)
{
  Core_I2CBusInit();

  uint8_t data = 0x00;
  I2C_writr_buff(I2C_ADDR_FT3168,0x00,&data,1); //切换正常模式

}
uint8_t getTouch(uint16_t *x,uint16_t *y)
{
  uint8_t data;
  uint8_t buf[4];
  I2C_read_buff(I2C_ADDR_FT3168,0x02,&data,1);
  if(data)
  {
    I2C_read_buff(I2C_ADDR_FT3168,0x03,buf,4);
    *y = (((uint16_t)buf[0] & 0x0f)<<8) | (uint16_t)buf[1];
    *x = (((uint16_t)buf[2] & 0x0f)<<8) | (uint16_t)buf[3];
    if(*x > EXAMPLE_LCD_H_RES)
    *x = EXAMPLE_LCD_H_RES;
    if(*y > EXAMPLE_LCD_V_RES)
    *y = EXAMPLE_LCD_V_RES;
    *y = EXAMPLE_LCD_V_RES - *y;
    return 1;
  }
  return 0;
}
uint8_t I2C_writr_buff(uint8_t addr,uint8_t reg,uint8_t *buf,uint8_t len)
{
  uint8_t ret;
  uint8_t *pbuf = (uint8_t*)malloc(len+1);
  pbuf[0] = reg;
  for(uint8_t i = 0; i<len; i++)
  {
    pbuf[i+1] = buf[i];
  }
  ret = i2c_master_write_to_device(TOUCH_HOST,addr,pbuf,len+1,1000);
  free(pbuf);
  pbuf = NULL;
  return ret;
}
uint8_t I2C_read_buff(uint8_t addr,uint8_t reg,uint8_t *buf,uint8_t len)
{
  uint8_t ret;
  ret = i2c_master_write_read_device(TOUCH_HOST,addr,&reg,1,buf,len,1000);
  return ret;
}
