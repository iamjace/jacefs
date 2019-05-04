/**
  ******************************************************************************
  * @file    console.c 
  * @editor  jace
  * @version V1.0
  * @date    2017.09.22
  * @brief   
  ******************************************************************************
    打印函数的实现

  ******************************************************************************
  */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include "console.h"

//for system kernel
void os_printk(const char *fmt, ...)
{   
    va_list ap;
    uint8_t buf[200];
    
    /*
     * Initialize the pointer to the variable length argument list.
     */
    va_start(ap, fmt);
    
    buf[vsnprintf((char*)buf , sizeof(buf)-1 ,fmt , ap)]= 0;
	
	//TODO:replace this function
    //uart_put_string(buf);
	printf(buf);
    
    /*
     * Cleanup the variable length argument list.
     */
    va_end(ap);
}



