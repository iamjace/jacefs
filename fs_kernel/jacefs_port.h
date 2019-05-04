#ifndef __FS_PORT_H__ 
#define __FS_PORT_H__ 

#include <stdint.h>

#define FS_USE_RAM 1

/* 硬件属性 */
#if FS_USE_RAM
    extern uint8_t m_fs_ram[];
    #define FS_HW_START_ADDR ((uint32_t)m_fs_ram)
    #define FS_HW_TOTAL_SIZE (10*1024) //20KB
    #define FS_HW_END_ADDR (FS_HW_START_ADDR+FS_HW_TOTAL_SIZE-1) 

    #define FS_HW_PAGE_SIZE 40//(512) //页大小 512B
    #define FS_HW_START_PAGE (0) //开始页
    #define FS_HW_PAGE_NUM (FS_HW_TOTAL_SIZE/FS_HW_PAGE_SIZE)   //总页
#else
    #define FS_HW_START_ADDR (0)
    #define FS_HW_TOTAL_SIZE (2*1024*1024) //2MB
    #define FS_HW_END_ADDR (FS_HW_START_ADDR+FS_HW_TOTAL_SIZE-1) 

    #define FS_HW_PAGE_SIZE (4096) //页大小 4KB
    #define FS_HW_START_PAGE (FS_HW_START_ADDR/FS_HW_PAGE_SIZE) //开始页
    #define FS_HW_PAGE_NUM (FS_HW_TOTAL_SIZE/FS_HW_PAGE_SIZE)   //总页
#endif

int fs_port_init(void);
int fs_port_deinit(void);

int fs_port_read(uint32_t addr,int size,uint8_t *rbuf);
int fs_port_write(uint32_t addr,int size,uint8_t *wbuf);


typedef enum{
    FS_CTL_ERASE_PAGE=0, //擦除页
}fs_port_ctl_t;

int fs_port_control(fs_port_ctl_t,void *param);


int fs_port_self_test(void );

#endif

