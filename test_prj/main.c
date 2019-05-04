/**
  ******************************************************************************
  * @file    main.c 
  * @editor  jace
  * @version V1.0
  * @date    2017.12.03
  * @brief   
  ******************************************************************************
	
	测试jacefs工程。
	
  ******************************************************************************
  *
  *　　　　　　┏┓　　　┏┓+ +
  *　　　　　┏┛┻━━━┛┻┓ + +
  *　　　　　┃　　　　　　　┃
  *　　　　　┃　　　━　　　┃ ++ + + +
  *　　　　 ████━████ ┃+
  *　　　　　┃　　　　　　　┃ +
  *　　　　　┃　　　┻　　　┃
  *　　　　　┃　　　　　　　┃ + +
  *　　　　　┗━┓　　　┏━┛
  *　　　　　　　┃　　　┃
  *　　　　　　　┃　　　┃ + + + +
  *　　　　　　　┃　　　┃　　　　Code is far away from bug with the animal protecting
  *　　　　　　　┃　　　┃ + 　　　　神兽保佑,代码无bug
  *　　　　　　　┃　　　┃
  *　　　　　　　┃　　　┃　　+
  *　　　　　　　┃　 　　┗━━━┓ + +
  *　　　　　　　┃ 　　　　　　　┣┓
  *　　　　　　　┃ 　　　　　　　┏┛
  *　　　　　　　┗┓┓┏━┳┓┏┛ + + + +
  *　　　　　　　　┃┫┫　┃┫┫
  *　　　　　　　　┗┻┛　┗┻┛+ + + +
  ******************************************************************************
  */
#include <string.h>
#include <stdlib.h>
#include "console.h"
#include "jacefs.h"
#include "jacefs_port.h"

#define DEBUG_MAIN 1 

#if DEBUG_MAIN
#define MAIN_LOG(fmt,args...)	do{\
								/*os_printk("%s,%s(),%d:" ,__FILE__, __FUNCTION__,__LINE__);*/\
                                os_printk("%s(),%d:" , __FUNCTION__,__LINE__);\
								os_printk(fmt,##args);\
							}while(0)
#define MAIN_INFO(fmt,args...)	do{\
								os_printk(fmt,##args);\
							}while(0)								
#else
#define MAIN_LOG(fmt,args...)
#define MAIN_INFO(fmt,args...)
#endif

int main(void)
{
    MAIN_LOG("system starting...\r\n");
    
    jacefs_init();
    jacefs_self_test();
	
	return 0;
}

