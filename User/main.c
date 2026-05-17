/**
  ******************************************************************************
  * @file    main.c
  * @author  fire
  * @version V1.0
  * @date    2015-xx-xx
  * @brief   OV5640
  ******************************************************************************
  * @attention
  *
  * STM32  F429
  * :http://www.firebbs.cn
  *    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */
  
#include "stm32f4xx.h"
#include "./usart/bsp_debug_usart.h"
#include "./sdram/bsp_sdram.h"
#include "./lcd/bsp_lcd.h"
#include "./camera/bsp_ov5640.h"
#include "./systick/bsp_SysTick.h"
#include "./camera/ov5640_AF.h"
#include "./key/bsp_key.h" 

#include "./App/netconf.h"
#include "./App/camtcp_capture.h"
#include "./Bsp/ETH/stm32f429_phy.h"
#include "./Bsp/ETH/stm32f429_eth.h"

/**/
uint32_t Task_Delay[NumOfTask];

uint8_t dispBuf[100];
OV5640_IDTypeDef OV5640_Camera_ID;
uint8_t fps=0;



#define FRAME_RATE_DISPLAY 	1

static int mode_state = 1;//
void Camera_Mode_Config(void)
{
	cam_mode.frame_rate = FRAME_RATE_15FPS,	
	

	cam_mode.cam_isp_sx = 0;
	cam_mode.cam_isp_sy = 0;	
	
	cam_mode.cam_isp_width = 1920;
	cam_mode.cam_isp_height = 1080;
	

	cam_mode.scaling = 1;      //
	cam_mode.cam_out_sx = 16;	//
	cam_mode.cam_out_sy = 4;	  //
	cam_mode.cam_out_width = 480;
	cam_mode.cam_out_height = 272;
	
	//LCD
	cam_mode.lcd_sx = 0;
	cam_mode.lcd_sy = 0;
	cam_mode.lcd_scan = 5; //
	
	//
	cam_mode.light_mode = 0;//
	cam_mode.saturation = 0;	
	cam_mode.brightness = 0;
	cam_mode.contrast = 0;
	cam_mode.effect = 0;		//
	cam_mode.exposure = 0;		

	cam_mode.auto_focus = 1;//
}


void Camera_Mode_Reconfig(void)
{
	cam_mode.frame_rate = FRAME_RATE_15FPS,	
	
	//ISP
	cam_mode.cam_isp_sx = 0;
	cam_mode.cam_isp_sy = 0;	
	
	cam_mode.cam_isp_width = 1920;
	cam_mode.cam_isp_height = 1080;
	
	//
	cam_mode.scaling = 1;      //
	cam_mode.cam_out_sx = 16;	//
	cam_mode.cam_out_sy = 4;	  //
	cam_mode.cam_out_width = 480;
	cam_mode.cam_out_height = 272
  ;
	
	//LCD??
	cam_mode.lcd_sx = 0;
	cam_mode.lcd_sy = 0;
	cam_mode.lcd_scan = 5; //
	
	//
	cam_mode.light_mode = 0;//
	cam_mode.saturation = 0;	
	cam_mode.brightness = 3;
	cam_mode.contrast = 0;
	cam_mode.effect = 0;		//
	cam_mode.exposure = 0;		

	cam_mode.auto_focus = 1;//
}

extern __attribute__ ((at(0xD0100000))) uint32_t cam_buff0[800*480];
extern int cur_index;
extern volatile uint32_t g_ms_ticks;
/**
  * @brief  ??????
  * @param  ??
  * @retval ??
  */
int main(void)
{
  uint8_t focus_status = 0;
	
	
  Debug_USART_Config();   
	
	/* 
	*/
	SysTick_Init();

	
	/**/
  LCD_Init();
  LCD_LayerInit();
  LTDC_Cmd(ENABLE);
	
	/**/
//  LCD_SetLayer(LCD_BACKGROUND_LAYER);  
//	LCD_SetTransparency(0xFF);
//	LCD_Clear(LCD_COLOR_BLACK);
	
  /**/
	LCD_SetLayer(LCD_FOREGROUND_LAYER); 
	/**/
  LCD_SetTransparency(0xFF);
	LCD_Clear(TRANSPARENCY);
	
//	LCD_SetColors(LCD_COLOR_RED,TRANSPARENCY);

//	LCD_ClearLine(LINE(18));
//  LCD_DisplayStringLine_EN_CH(LINE(18),(uint8_t* )" ??:WVGA 800x480");

  CAMERA_DEBUG("STM32F429 DCMI ????OV5640????");

  /* Ethernet + LwIP (NO_SYS=1 raw API) */
  ETH_BSP_Config();
  LwIP_Init();
  camtcp_init();
	

  /* GPIOIIC */
  OV5640_HW_Init();   

  /* ?ID */
  OV5640_ReadID(&OV5640_Camera_ID);

   if(OV5640_Camera_ID.PIDH  == 0x56)
  {
//    sprintf((char*)dispBuf, "              OV5640 ?????,ID:0x%x", OV5640_Camera_ID.PIDH);
//		LCD_DisplayStringLine_EN_CH(LINE(0),(uint8_t*)dispBuf);
    CAMERA_DEBUG("%x %x",OV5640_Camera_ID.PIDH ,OV5640_Camera_ID.PIDL);

  }
  else
  {
    LCD_SetTextColor(LCD_COLOR_RED);
    LCD_DisplayStringLine_EN_CH(LINE(0),(uint8_t*) "         ??��??OV5640???????????????");
    CAMERA_DEBUG("??��??OV5640????????????????????");

    while(1);  
  }

  
  
  
  OV5640_RGB565Config();
  OV5640_USER_Config();
  OV5640_FOCUS_AD5820_Init();
	OV5640_Init();
	if(cam_mode.auto_focus ==1)
	{
		OV5640_FOCUS_AD5820_Constant_Focus();
		focus_status = 1;
	}
	//DCMI
  DCMI_Cmd(ENABLE); 
  DCMI_CaptureCmd(ENABLE); 

	/*DMALCD*/
  while(1)
	{
    /* Poll ethernet RX and lwIP timers (NO_SYS=1) */
    if (ETH_CheckFrameReceived() != 0)
    {
      LwIP_Pkt_Handle();
    }
    LwIP_Periodic_Handle(g_ms_ticks);
    camtcp_poll();

    if( Key_Scan(KEY1_GPIO_PORT,KEY1_PIN) == KEY_ON  )//30FPS320*240
    {      
      //  
      CAMERA_DEBUG("K1");
      
      //OV5640_Capture_Control(DISABLE);
      DCMI_Stop();
//      //Cam mode
      if(mode_state == 1)
      {
        mode_state = 0;
        Camera_Mode_Reconfig();
      }
      else
      {
        mode_state = 1;
        Camera_Mode_Config();
      }
////      LCD_LayerInit_Cam(0, 400-cam_mode.cam_out_width/2, 400-cam_mode.cam_out_width/2 + cam_mode.cam_out_width,
////                        240-cam_mode.cam_out_height/2,240-cam_mode.cam_out_height/2+cam_mode.cam_out_height,
////                        LCD_FB_START_ADDRESS,RGB565); 
//      HAL_DCMI_Start_DMA((uint32_t)cam_buff0,
//                        cam_mode.cam_out_height*cam_mode.cam_out_width/2);       
      //LCD_LayerCamInit(LCD_FRAME_BUFFER,cam_mode.cam_out_width, cam_mode.cam_out_height);      
      OV5640_USER_Config();

      if(cam_mode.auto_focus ==1)
      {
        OV5640_AUTO_FOCUS();
        focus_status = 1;
      }
      DCMI_Start(); 
      /*DMALCD*/
      //OV5640_Init();		
//      OV5640_Capture_Control(ENABLE);
    }     

    /* KEY2: edge-trigger + debounce (one press => one capture request) */
    {
      static uint32_t key2_last_ms = 0;
      if (Key_Scan(KEY2_GPIO_PORT, KEY2_PIN) == KEY_ON)
      {
        uint32_t now = g_ms_ticks;
        if ((now - key2_last_ms) >= 300) /* 300ms debounce window */
        {
          key2_last_ms = now;
          if (camtcp_is_connected() && !camtcp_is_busy())
          {
            camtcp_request_capture();
            printf("\r\nCapture requested, waiting next frame...\r\n");
          }
          else
          {
            printf("\r\nTCP not connected or busy.\r\n");
          }
        }
      }
    }

    /* Print result once received */
    if (g_camtcp_result_ready)
    {
      g_camtcp_result_ready = 0;
      printf("\r\nAI result: frame_seq=%lu okng=%u boxes=%u\r\n",
             (unsigned long)g_camtcp_result.frame_seq,
             (unsigned)g_camtcp_result.okng,
             (unsigned)g_camtcp_result.num_boxes);
    }
//		
#if FRAME_RATE_DISPLAY		
		if(Task_Delay[0]==0)
		{
			/**/
			CAMERA_DEBUG("\r\n???:%.1f/s \r\n", (double)fps/5.0);
			//????
			fps =0;			
			
			Task_Delay[0]=5000; //1ms


		}
			
#endif
		
	}




}



/*********************************************END OF FILE**********************/

