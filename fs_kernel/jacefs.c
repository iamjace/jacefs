/*
******************************************************************************
- file    		jacefs.c 
- author  		jace
- version 		V1.2
- create date	2018.11.11
- brief 
	jacefs 源码。
    
******************************************************************************
- release note:

2018.11.11 V1.0
    创建工程，实现jacefs文件系统。
    增加文件创建接口，日前版本仅用一个页来记录文件信息，最大文件数 JACEFS_FILE_NUM 定义为一页可存文件数

2019.3.27 V1.1
    增加文件读取接口

2019.5.4  V1.2
	完善文件系统，包括文件读、写等功能。

******************************************************************************
*/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "console.h"
#include "crc16.h"
#include "jacefs.h"
#include "jacefs_port.h"


#define DEBUG_FS 1

#if DEBUG_FS
#define FS_LOG(fmt,args...)	do{\
								/*os_printk("%s,%s(),%d:" ,__FILE__, __FUNCTION__,__LINE__);*/\
                                os_printk("%s(),%d:" , __FUNCTION__,__LINE__);\
								os_printk(fmt,##args);\
							}while(0)
#define FS_INFO(fmt,args...)	do{\
								os_printk(fmt,##args);\
							}while(0)								
#else
#define FS_LOG(fmt,args...)
#define FS_INFO(fmt,args...)
#endif


//
//----------------------文件系统宏定义---------------
//

//空间块起始页（相对于 FS_HW_START_ADDR 的页数，而不是实际的物理页）
#define SPACE_DESC_START_PAGE (0) 

//文件系统总页数
#define FS_TOTAL_SIZE (FS_HW_TOTAL_SIZE) 

//文件系统总大小
#define FS_TOTAL_PAGE (FS_HW_PAGE_NUM) 

//页大小                            
#define FS_PAGE_SIZE (FS_HW_PAGE_SIZE) 
                            
//1页可装文件描述数量(前4字节为 2字节CRC16+2字节文件数)
#define FILE_DESC_PER_PAGE ((FS_PAGE_SIZE-4)/sizeof(jacefs_fd_t)) 

//文件描述使用页(系统格式化时默认只用1页，在文件数增加到2页后再动态分配空间)
#define FILE_DESC_PAGE_NUM_DEFAULT (2) 

//空间块中，页使用描述占用字节(2字节可描述256MB，4字节可描述 16777216 MB)
#define SPACE_BYTE_PER_PAGE 2 

//空间块占用页
#define SPACE_DESC_PAGE_NUM (FS_TOTAL_PAGE/(FS_PAGE_SIZE/SPACE_BYTE_PER_PAGE)+1) 

//文件系统最多可管理页
#define FS_MANAGE_PAGE_MAX (0x1<<(SPACE_BYTE_PER_PAGE*8)) 

//系统最少页：空间管理块+文件描述块
#define FS_MANAGE_PAGE_MIN (SPACE_DESC_PAGE_NUM+FILE_DESC_PAGE_NUM_DEFAULT)

#if (FS_MANAGE_PAGE_MAX<FS_TOTAL_PAGE)
    #error  "jacefs page over size!!!"
#elif (FS_TOTAL_PAGE<=FS_MANAGE_PAGE_MIN)
	#error  "jacefs page too little!!!"
#endif

//文件描述块
#define FILE_DESC_START_PAGE (SPACE_DESC_START_PAGE+SPACE_DESC_PAGE_NUM)

//页使用描述（目前仅支持2字节的方式）
#if SPACE_BYTE_PER_PAGE==2
    typedef uint16_t page_desc_val_t;
//#elif SPACE_BYTE_PER_PAGE==4
//    typedef uint32_t page_desc_val_t;
#else
//    #error  "SPACE_BYTE_PER_PAGE value must be 4 or 2!"
	#error  "SPACE_BYTE_PER_PAGE value must be 2!"
#endif

//页转成地址
#define PAGE_TO_ADDR(page) ((page)*FS_PAGE_SIZE+FS_HW_START_ADDR)

//
//----------------------文件系统数据结构---------------
//
//页使用标识
typedef enum{
    PAGE_NOT_USED		=0xffff,
    PAGE_SPACE_BLOCK	=0xfffe,
    PAGE_FILE_DESC		=0xfffd,
    
    //0xfffc~0xfffa 预留
    
    PAGE_FILE_END		=0xfff9,
    PAGE_FILE_USING_MAX	=0xfff8,
    PAGE_FILE_USING_MIN	=0x0,
}fs_page_status_t;

//文件描述
typedef struct{
    uint16_t id;			/* 文件ID */    
	uint16_t app_id;        /* 文件属于的APP ID */  
    uint32_t size;          /* 文件大小 */
    uint32_t wsize;         /* 文件已写入大小 */
	
#if SPACE_BYTE_PER_PAGE==2
    uint16_t start_page;    /* 文件存储开始页 */
#else
	#error  "SPACE_BYTE_PER_PAGE value must be 2!"
#endif
	
//    uint8_t flag;          /* 文件标识 */
    uint8_t reserved[2];    
}jacefs_fd_t;

typedef struct{
    uint16_t crc16;
    uint16_t file_num;
}jacefs_fd_hdr_t;


//
//----------------------文件系统变量---------------
//
static bool m_fs_ready=false;
static uint32_t m_fs_remainig_size=0;
static uint8_t m_swap_buf[FS_PAGE_SIZE];


/**
@brief : 格式化为 jacefs
		
@param : 无

@retval:
- FS_RET_SUCCESS 格式化成功 
- FS_UNKNOW_ERR   格式化失败
*/
static jacefs_error_t format_to_jacefs(void)
{
    int i;
//    uint32_t space_block_page=SPACE_DESC_START_PAGE;
    page_desc_val_t desc_val;
    uint32_t page;
    uint32_t offset;
    
    //空间块、文件描述块 擦除...
    for(i=0;i<(SPACE_DESC_PAGE_NUM+FILE_DESC_PAGE_NUM_DEFAULT);i++)
    {
        page=SPACE_DESC_START_PAGE+i;
        if(fs_port_control(FS_CTL_ERASE_PAGE,&page)!=FS_RET_SUCCESS)
        {
            FS_LOG("erase page=%d error!\r\n",page);
            return FS_RET_UNKNOW_ERR;
        }
    }
    
    //空间块：空间块写入标识
    desc_val=PAGE_SPACE_BLOCK;
    for(i=0;i<SPACE_DESC_PAGE_NUM;i++)
    {
        offset=sizeof(page_desc_val_t)*i;
        if(fs_port_write( PAGE_TO_ADDR(SPACE_DESC_START_PAGE)+offset,
            sizeof(page_desc_val_t),(uint8_t*)&desc_val)!=sizeof(page_desc_val_t))
        {
            FS_LOG("write err!\r\n");
            return FS_RET_UNKNOW_ERR;
        }
    }
    
    //空间块：文件描述块写标识--只写系统默认的文件描述页
    desc_val=PAGE_FILE_DESC;
	for(i=SPACE_DESC_PAGE_NUM;i<(SPACE_DESC_PAGE_NUM+FILE_DESC_PAGE_NUM_DEFAULT);i++)
    {
        offset=sizeof(page_desc_val_t)*i;
        if(fs_port_write( PAGE_TO_ADDR(SPACE_DESC_START_PAGE)+offset,
            sizeof(page_desc_val_t),(uint8_t*)&desc_val)!=sizeof(page_desc_val_t))
        {
            FS_LOG("write err!\r\n");
            return FS_RET_UNKNOW_ERR;
        }
    }
    
    //文件描述块校验
    jacefs_fd_hdr_t hd;
    hd.file_num=0;
    hd.crc16=crc16_compute((uint8_t*)&hd.file_num,sizeof(jacefs_fd_hdr_t)-2,0);
	for(i=0;i<FILE_DESC_PAGE_NUM_DEFAULT;i++)
    {
		if(fs_port_write( PAGE_TO_ADDR(FILE_DESC_START_PAGE+i),
			sizeof(jacefs_fd_hdr_t),(uint8_t*)&hd)!=sizeof(jacefs_fd_hdr_t))
		{
			FS_LOG("write err!\r\n");
			return FS_RET_UNKNOW_ERR;
		}
	}
    
    return FS_RET_SUCCESS;
}

/**
@brief : 计算剩余空间
		
@param : 无

@retval:
- @jacefs_error_t
*/
static jacefs_error_t calculate_remaining_size()
{
    if(m_fs_ready!=true)
    {
        return FS_RET_NOT_READY;
    }
    
    page_desc_val_t page_desc_val;
    uint32_t addr;
    uint32_t page;
    
    m_fs_remainig_size=0;
    page=0;
    
    for(int i=0;i<SPACE_DESC_PAGE_NUM;i++)
    {
        addr=PAGE_TO_ADDR(SPACE_DESC_START_PAGE+i);
        for(int j=0;j<(FS_PAGE_SIZE/SPACE_BYTE_PER_PAGE);j++)
        {
            if(fs_port_read(addr+sizeof(page_desc_val_t)*j,
                sizeof(page_desc_val_t),(uint8_t*)&page_desc_val)<=0)
            {
                FS_LOG("read error!\r\n");
                return FS_RET_UNKNOW_ERR;
            }

            if(page_desc_val==PAGE_NOT_USED)
            {
                m_fs_remainig_size+=FS_PAGE_SIZE;
            }
            
			//空间描述块不一定用的完，只判断总页数内的空间
            page++;
            if(page>=FS_TOTAL_PAGE)
                break;
        }
    }
    
    FS_LOG("remaining size=%d(%d KB),used=%d(%d KB)\r\n",
        m_fs_remainig_size,m_fs_remainig_size/1024,
        FS_TOTAL_SIZE-m_fs_remainig_size,(FS_TOTAL_SIZE-m_fs_remainig_size)/1024);
    
    return FS_RET_SUCCESS;
}


/**
@brief : 寻找文件描述页
		
@param : 
- start_page 距离 SPACE_DESC_START_PAGE 的页数，从该页开始寻找后面的文件描述页

@retval:
- >0 文件描述页下标
- ==0 没有找到
*/
static uint32_t find_file_desc_page(uint32_t start_page)
{
    page_desc_val_t page_desc_val;
    uint32_t addr;
	
    for(uint32_t i=start_page;i<FS_TOTAL_PAGE;i++)
    {
        addr=PAGE_TO_ADDR(SPACE_DESC_START_PAGE)+i*sizeof(page_desc_val_t);
        
		if(fs_port_read(addr,sizeof(page_desc_val_t),(uint8_t*)&page_desc_val)
			!=sizeof(page_desc_val_t))
		{
			break;
		}

		if(page_desc_val==PAGE_FILE_DESC)
		{
			FS_LOG("found page=%d\r\n",i);
			return i;
		}
    }
    
//	FS_LOG("not found !\r\n");
    return 0;
}

/**
@brief : 读取文件某页链接的下一页
		
@param : 
- page 文件页

@retval:
- >0 文件链接的下一页
- ==0 没有找到
*/
static page_desc_val_t get_file_next_page(uint32_t page)
{
    page_desc_val_t page_desc_val;
    uint32_t addr;
	
	addr=PAGE_TO_ADDR(SPACE_DESC_START_PAGE)+page*sizeof(page_desc_val_t);
	
	if(fs_port_read(addr,sizeof(page_desc_val_t),(uint8_t*)&page_desc_val)
		==sizeof(page_desc_val_t))
	{
		return page_desc_val;
	}

    return 0;
}

/**
@brief : 分配空间
		
@param : 
-req_size	申请的空间大小
-start_page	申请到的起始页地址

@retval:
- @jacefs_error_t
*/
static jacefs_error_t alloc_page(uint32_t req_size,uint16_t *start_page)
{
    if(m_fs_ready!=true)
    {
        return FS_RET_NOT_READY;
    }
    
    page_desc_val_t page_desc_val;
    uint32_t addr;
    uint32_t last_alloc_page;
	
    uint32_t alloc_size;
	alloc_size=0;
    *start_page=0;
	last_alloc_page=0;
	
	for(uint32_t i=0;i<FS_TOTAL_PAGE;i++)
    {
        addr=PAGE_TO_ADDR(SPACE_DESC_START_PAGE)+i*sizeof(page_desc_val_t);
        
		if(fs_port_read(addr,sizeof(page_desc_val_t),(uint8_t*)&page_desc_val)
			!=sizeof(page_desc_val_t))
		{
			FS_LOG("read error!\r\n");
            return FS_RET_UNKNOW_ERR;
		}

		if(page_desc_val==PAGE_NOT_USED)
		{
			if(*start_page==0)
				*start_page=i;
			
			alloc_size+=FS_PAGE_SIZE;
			
			if(m_fs_remainig_size>=FS_PAGE_SIZE)
				m_fs_remainig_size-=FS_PAGE_SIZE;
			else
				m_fs_remainig_size=0;
			
			FS_LOG("alloc page=%d,rem=%d\r\n",i,m_fs_remainig_size);
			
			//上一次分配的页链接到本次分配的页
			if(last_alloc_page!=0)
			{
				addr=PAGE_TO_ADDR(SPACE_DESC_START_PAGE)+last_alloc_page*sizeof(page_desc_val_t);
				
				page_desc_val=i;
				if(fs_port_write(addr,sizeof(page_desc_val_t),(uint8_t*)&page_desc_val)
					!=sizeof(page_desc_val_t))
				{
					FS_LOG("write error!\r\n");
					return FS_RET_UNKNOW_ERR;
				}
			}
			
			if(alloc_size>=req_size)
			{
				addr=PAGE_TO_ADDR(SPACE_DESC_START_PAGE)+i*sizeof(page_desc_val_t);
				
				//文件结束
				page_desc_val=PAGE_FILE_END;
				if(fs_port_write(addr,sizeof(page_desc_val_t),(uint8_t*)&page_desc_val)
					!=sizeof(page_desc_val_t))
				{
					FS_LOG("write error!\r\n");
					return FS_RET_UNKNOW_ERR;
				}
				
				break;
			}
			
			last_alloc_page=i;
		}
    }
    
    FS_LOG("remaining size=%d(%d KB),used=%d(%d KB),req=%d (alloc=%d),page=%d\r\n",
        m_fs_remainig_size,m_fs_remainig_size/1024,
        FS_TOTAL_SIZE-m_fs_remainig_size,(FS_TOTAL_SIZE-m_fs_remainig_size)/1024,
		req_size,alloc_size,*start_page);
    
    return FS_RET_SUCCESS;
}
/**
@brief : 回收空间
		
@param : 
-start_page	回收的起始页

@retval:
- @jacefs_error_t
*/
static jacefs_error_t free_page(uint16_t start_page)
{
    if(m_fs_ready!=true)
    {
        return FS_RET_NOT_READY;
    }

    uint32_t space_desc_addr,
			space_desc_next_addr;
	uint16_t page,next_page;
	uint16_t offset_in_page;
	page_desc_val_t *val;
	
	next_page=start_page;
	
	do
    {
        space_desc_addr=PAGE_TO_ADDR(SPACE_DESC_START_PAGE)
						+(next_page*SPACE_BYTE_PER_PAGE/FS_PAGE_SIZE*FS_PAGE_SIZE);
		
		FS_LOG("page=%d,space_desc_addr=%x!\r\n",next_page,space_desc_addr);
		
		//读取一整页
		if(fs_port_read( space_desc_addr,FS_PAGE_SIZE,m_swap_buf)
				!=FS_PAGE_SIZE )
		{
			FS_LOG("read err!\r\n");
			return FS_RET_UNKNOW_ERR;
		}
		
		//回收空间描述页第一个 空间
		offset_in_page=(next_page*SPACE_BYTE_PER_PAGE)%(FS_PAGE_SIZE);
		val=(page_desc_val_t*)&m_swap_buf[offset_in_page];
		
		FS_LOG("free page=%d,offset_in_page=%d,",
			next_page,offset_in_page);
		
		next_page=*val;
		*val=PAGE_NOT_USED;
		m_fs_remainig_size+=FS_PAGE_SIZE;
		
		FS_INFO("next free page=%d\r\n",
			next_page);
		
		//回收同一空间描述页剩余 空间
free_page_rem:
		if(next_page!=PAGE_FILE_END)
		{
			space_desc_next_addr=PAGE_TO_ADDR(SPACE_DESC_START_PAGE)
						+(next_page*SPACE_BYTE_PER_PAGE/FS_PAGE_SIZE*FS_PAGE_SIZE);
			
			if( space_desc_next_addr == space_desc_addr)
			{
				offset_in_page=(next_page*SPACE_BYTE_PER_PAGE)%(FS_PAGE_SIZE);
				val=(page_desc_val_t*)&m_swap_buf[offset_in_page];
				
				FS_LOG("free page=%d,offset_in_page=%d,",
					next_page,offset_in_page);
				
				next_page=*val;
				*val=PAGE_NOT_USED;
				m_fs_remainig_size+=FS_PAGE_SIZE;
				
				FS_INFO("next free page=%d\r\n",
					next_page);
				
				goto free_page_rem;
			}
			
			//擦除原页
			page=(space_desc_addr-FS_HW_START_ADDR)/FS_PAGE_SIZE;
			if(fs_port_control(FS_CTL_ERASE_PAGE,&page)!=FS_RET_SUCCESS)
			{
				return FS_RET_UNKNOW_ERR;
			}
			
			//写入新数据
			if(fs_port_write( space_desc_addr,FS_PAGE_SIZE,m_swap_buf)
				!=FS_PAGE_SIZE )
			{
				FS_LOG("write error!\r\n");
				return FS_RET_UNKNOW_ERR;
			}
			
			//读取下一面空间描述页
			continue;
		}
		
		//擦除原页
		page=(space_desc_addr-FS_HW_START_ADDR)/FS_PAGE_SIZE;
		if(fs_port_control(FS_CTL_ERASE_PAGE,&page)!=FS_RET_SUCCESS)
		{
			return FS_RET_UNKNOW_ERR;
		}
		
		//写入新数据
		if(fs_port_write(space_desc_addr,FS_PAGE_SIZE,m_swap_buf)
				!=FS_PAGE_SIZE )
		{
			FS_LOG("write error!\r\n");
			return FS_RET_UNKNOW_ERR;
		}
		
		//到此空间回收完成
		break;
		
		
    }
	while(1);
    
    FS_LOG("free space finish,remaining size=%d(%d KB),used=%d(%d KB),start_page=%d\r\n",
        m_fs_remainig_size,m_fs_remainig_size/1024,
        FS_TOTAL_SIZE-m_fs_remainig_size,(FS_TOTAL_SIZE-m_fs_remainig_size)/1024,
		start_page);

    return FS_RET_SUCCESS;
}
/**
@brief : 检查文件是否存在
		
@param : 
-file_id 	查找的文件ID
-app_id		查找的APP ID

@retval:
- true 文件存在
- false 文件不存在
*/
static bool file_exist(jacefs_file_id_t file_id,uint16_t app_id)
{
    jacefs_fd_hdr_t fd_hdr;
    jacefs_fd_t *fd;
    uint32_t page,next_page;
	
    
	next_page=0;
	page=0;
	
	while(1)
	{
		page=find_file_desc_page(next_page);
		
		if(page==0)
			break;
		next_page=page+1;
		
		if(fs_port_read( PAGE_TO_ADDR(page),
			sizeof(jacefs_fd_hdr_t),(uint8_t*)&fd_hdr)!=sizeof(jacefs_fd_hdr_t))
		{
			FS_LOG("read err!\r\n");
			continue;
		}
		
		if(fd_hdr.file_num==0 || fd_hdr.file_num>FILE_DESC_PER_PAGE)
			continue;
		
		
		//把文件描述 复制到交换区（内存）
		if(fs_port_read( PAGE_TO_ADDR(page)+sizeof(jacefs_fd_hdr_t),
			fd_hdr.file_num*sizeof(jacefs_fd_t),m_swap_buf) 
				!=fd_hdr.file_num*sizeof(jacefs_fd_t) )
		{
			FS_LOG("read err!\r\n");
			continue;
		}
		
		fd=(jacefs_fd_t *)m_swap_buf;
		
		for(uint16_t i=0;i<fd_hdr.file_num;i++ )
		{
			if(fd[i].id==file_id && fd[i].app_id==app_id)
			{
				FS_LOG("found file=%d,app_id=%d in page=%d num=%d!\r\n",
					file_id,app_id,page,i);
				return true;
			}
		}
		
	}
	
	FS_LOG("not found file=%d,app_id=%d !\r\n",file_id,app_id);
	return false;
}

/**
@brief : 获取文件描述
		
@param : 
-file_id 	查找的文件ID
-app_id		查找的APP ID
-fd			返回查找到的文件描述

@retval:
- @jacefs_error_t
*/
static jacefs_error_t get_file_desc(jacefs_file_id_t file_id,uint16_t app_id,jacefs_fd_t *fd)
{
    jacefs_fd_hdr_t fd_hdr;
    uint32_t page,next_page;
	jacefs_fd_t *search_fd;
	
	next_page=0;
	page=0;
	
	while(1)
	{
		page=find_file_desc_page(next_page);
		
		if(page==0)
			break;
		next_page=page+1;
		
		if(fs_port_read( PAGE_TO_ADDR(page),
			sizeof(jacefs_fd_hdr_t),(uint8_t*)&fd_hdr)!=sizeof(jacefs_fd_hdr_t))
		{
			FS_LOG("read err!\r\n");
			continue;
		}
		
		if(fd_hdr.file_num==0 || fd_hdr.file_num>FILE_DESC_PER_PAGE)
			continue;
		
		
		//把文件描述 复制到交换区（内存）
		if(fs_port_read( PAGE_TO_ADDR(page)+sizeof(jacefs_fd_hdr_t),
			fd_hdr.file_num*sizeof(jacefs_fd_t),m_swap_buf) 
				!=fd_hdr.file_num*sizeof(jacefs_fd_t) )
		{
			FS_LOG("read err!\r\n");
			continue;
		}
		
		search_fd=(jacefs_fd_t *)m_swap_buf;
		
		for(uint16_t i=0;i<fd_hdr.file_num;i++ )
		{
			if(search_fd[i].id==file_id && search_fd[i].app_id==app_id)
			{
				if(fd)
					*fd=search_fd[i];
				
				FS_LOG("found file=%d,app_id=%d in page=%d num=%d!\r\n",
					file_id,app_id,page,i);
				return FS_RET_SUCCESS;
			}
		}
	}
	
	FS_LOG("not found file=%d,app_id=%d !\r\n",file_id,app_id);
	return FS_RET_FILE_NOT_EXIST;
}

/**
@brief : 修改文件描述
		
@param : 
-file_id 	文件对应ID
-app_id		文件对应APP ID
-fd			返回对应的文件描述

@retval:
- @jacefs_error_t
*/
static jacefs_error_t set_file_desc(jacefs_fd_t fd)
{
    jacefs_fd_hdr_t fd_hdr;
    uint32_t page,next_page;
	jacefs_fd_t *search_fd;
	
	next_page=0;
	page=0;
	
	while(1)
	{
		page=find_file_desc_page(next_page);
		
		if(page==0)
			break;
		next_page=page+1;
		
		if(fs_port_read( PAGE_TO_ADDR(page),
			sizeof(jacefs_fd_hdr_t),(uint8_t*)&fd_hdr)!=sizeof(jacefs_fd_hdr_t))
		{
			FS_LOG("read err!\r\n");
			continue;
		}
		
		if(fd_hdr.file_num==0 || fd_hdr.file_num>FILE_DESC_PER_PAGE)
			continue;
		
		if(fs_port_read( PAGE_TO_ADDR(page)+sizeof(jacefs_fd_hdr_t),
			fd_hdr.file_num*sizeof(jacefs_fd_t),m_swap_buf) 
				!=fd_hdr.file_num*sizeof(jacefs_fd_t) )
		{
			FS_LOG("read err!\r\n");
			continue;
		}
		
		search_fd=(jacefs_fd_t *)m_swap_buf;
		
		for(uint16_t i=0;i<fd_hdr.file_num;i++ )
		{
			if(search_fd[i].id==fd.id && search_fd[i].app_id==fd.app_id)
			{
				search_fd[i]=fd;
				
				FS_LOG("found & set file=%d,app_id=%d,ws=%d,s=%d,start_page=%d in page=%d num=%d,!\r\n",
					fd.id,fd.app_id,fd.start_page,fd.wsize,fd.size,page,i);
				
				//重新检验，回写
				fd_hdr.crc16=crc16_compute((uint8_t*)&fd_hdr.file_num,sizeof(jacefs_fd_hdr_t)-2,0);
				fd_hdr.crc16=crc16_compute(m_swap_buf,
							fd_hdr.file_num*sizeof(jacefs_fd_t),&fd_hdr.crc16);
				
				//把文件描述从交换区（内存）回写
				if(fs_port_control(FS_CTL_ERASE_PAGE,&page)!=FS_RET_SUCCESS)
				{
					return FS_RET_UNKNOW_ERR;
				}
				
				if(fs_port_write( PAGE_TO_ADDR(page),sizeof(jacefs_fd_hdr_t),(uint8_t*)&fd_hdr)
					!=sizeof(jacefs_fd_hdr_t))
				{
					FS_LOG("write err!\r\n");
					return FS_RET_UNKNOW_ERR;
				}
				
				if(fd_hdr.file_num!=0)
				{
					if(fs_port_write( PAGE_TO_ADDR(page)+sizeof(jacefs_fd_hdr_t),
						fd_hdr.file_num*sizeof(jacefs_fd_t),m_swap_buf)
						!=fd_hdr.file_num*sizeof(jacefs_fd_t))
					{
						FS_LOG("write err!\r\n");
						return FS_RET_UNKNOW_ERR;
					}
				}
				
				return FS_RET_SUCCESS;
			}
		}
	}
	
	FS_LOG("not found file=%d,app_id=%d !\r\n",fd.id,fd.app_id);
	return FS_RET_FILE_NOT_EXIST;
}

/**
@brief : 删除文件描述
		
@param : 
-file_id 	删除的文件对应ID
-app_id		删除的文件对应APP ID
-fd			返回对应的文件描述
-use_file_id	true为使用file_id，false为只使用app_id--即删任一和app_id相同的文件

@retval:
- @jacefs_error_t
*/
static jacefs_error_t delete_file_desc(jacefs_file_id_t file_id,uint16_t app_id,jacefs_fd_t *fd,bool use_file_id)
{
    jacefs_fd_hdr_t fd_hdr;
    uint32_t page,next_page;
	jacefs_fd_t *search_fd;
	
	next_page=0;
	page=0;
	
	while(1)
	{
		page=find_file_desc_page(next_page);
		
		if(page==0)
			break;
		next_page=page+1;
		
		if(fs_port_read( PAGE_TO_ADDR(page),
			sizeof(jacefs_fd_hdr_t),(uint8_t*)&fd_hdr)!=sizeof(jacefs_fd_hdr_t))
		{
			FS_LOG("read err!\r\n");
			continue;
		}
		
		if(fd_hdr.file_num==0 || fd_hdr.file_num>FILE_DESC_PER_PAGE)
			continue;
		
		if(fs_port_read( PAGE_TO_ADDR(page)+sizeof(jacefs_fd_hdr_t),
			fd_hdr.file_num*sizeof(jacefs_fd_t),m_swap_buf) 
				!=fd_hdr.file_num*sizeof(jacefs_fd_t) )
		{
			FS_LOG("read err!\r\n");
			continue;
		}
		
		search_fd=(jacefs_fd_t *)m_swap_buf;
		
		for(uint16_t i=0;i<fd_hdr.file_num;i++ )
		{
			if((search_fd[i].id==file_id || !use_file_id) && search_fd[i].app_id==app_id)
			{
				if(fd)
					*fd=search_fd[i];
				
				FS_LOG("found & delete file=%d,app_id=%d,start_page=%d in page=%d num=%d!\r\n",
					fd->id,fd->app_id,fd->start_page,page,i);
				
				//删除文件描述，把后面的文件描述向前移动
				if(i<fd_hdr.file_num-1)
				{
					memcpy(&search_fd[i],&search_fd[i+1],(fd_hdr.file_num-i-1)*sizeof(jacefs_fd_t));
				}
				
				//重新检验，回写
				fd_hdr.file_num--;
				fd_hdr.crc16=crc16_compute((uint8_t*)&fd_hdr.file_num,sizeof(jacefs_fd_hdr_t)-2,0);
				
				if(fd_hdr.file_num!=0)
				{
					fd_hdr.crc16=crc16_compute(m_swap_buf,
								fd_hdr.file_num*sizeof(jacefs_fd_t),&fd_hdr.crc16);
				}
				
				//把文件描述从交换区（内存）回写
				if(fs_port_control(FS_CTL_ERASE_PAGE,&page)!=FS_RET_SUCCESS)
				{
					return FS_RET_UNKNOW_ERR;
				}
				
				if(fs_port_write( PAGE_TO_ADDR(page),sizeof(jacefs_fd_hdr_t),(uint8_t*)&fd_hdr)
					!=sizeof(jacefs_fd_hdr_t))
				{
					FS_LOG("write err!\r\n");
					return FS_RET_UNKNOW_ERR;
				}
				
				if(fd_hdr.file_num!=0)
				{
					if(fs_port_write( PAGE_TO_ADDR(page)+sizeof(jacefs_fd_hdr_t),
						fd_hdr.file_num*sizeof(jacefs_fd_t),m_swap_buf)
						!=fd_hdr.file_num*sizeof(jacefs_fd_t))
					{
						FS_LOG("write err!\r\n");
						return FS_RET_UNKNOW_ERR;
					}
				}
				
				return FS_RET_SUCCESS;
			}
		}
	}
	
	FS_LOG("not found file=%d,app_id=%d !\r\n",file_id,app_id);
	return FS_RET_FILE_NOT_EXIST;
}

/**
@brief : 初始化文件系统

	1 初始化硬件接口
	2 检查空间描述块是否存在
	3 检查文件描述块是否存在、校验通过
	4 计算剩余空间

@param : 无

@retval:
- @jacefs_error_t
*/
jacefs_error_t jacefs_init(void)
{
    int i;
	jacefs_error_t ret;
    
//    if(m_fs_ready==true)
//    {
//        return FS_RET_SUCCESS;
//    }
    
    if(fs_port_init()!=FS_RET_SUCCESS)
    {
        FS_LOG("hw init error!\r\n");
        return FS_RET_UNKNOW_ERR;
    }
    
    //查看文件系统开始页是否已标识为空间块
    page_desc_val_t page_desc_val;
    for(i=0;i<SPACE_DESC_PAGE_NUM;i++)
    {
        if(fs_port_read( PAGE_TO_ADDR(SPACE_DESC_START_PAGE)+sizeof(page_desc_val_t)*i,
            sizeof(page_desc_val_t),(uint8_t*)&page_desc_val)<=0)
        {
            FS_LOG("read error!\r\n");
            return FS_RET_UNKNOW_ERR;
        }
        
        if(PAGE_SPACE_BLOCK!=page_desc_val)
            break;
    }
    
    //系统未配置不正确，恢复默认
    if(i<SPACE_DESC_PAGE_NUM)
    {
fs_format:
        if(format_to_jacefs())
        {
            FS_LOG("format fs error!\r\n");
            return FS_RET_UNKNOW_ERR;
        }
        FS_LOG("format fs success!\r\n");
		goto init_finish;
    }
	
    /* 检查文件描述块是否配置正确 */
	jacefs_fd_hdr_t hd;
	jacefs_fd_t fd;
	uint16_t crc16;
	uint32_t page,next_page;
	
	page=0;
	next_page=0;
	while(1)
	{
		page=find_file_desc_page(next_page);
		
		if(page==0)
			break;
		next_page=page+1;
        
        if(fs_port_read( PAGE_TO_ADDR(page),
            sizeof(jacefs_fd_hdr_t),(uint8_t*)&hd)!=sizeof(jacefs_fd_hdr_t))
        {
            FS_LOG("read err!\r\n");
            return FS_RET_UNKNOW_ERR;
        }
        FS_LOG("page=%d,file_num=%d crc=%x\r\n",page,hd.file_num,hd.crc16);
        
        
        //每页存储的文件数得在合理范围内
        if(hd.file_num>FILE_DESC_PER_PAGE)
        {
            FS_LOG("file num=%d err,too large! \r\n",hd.file_num);
            goto fs_format;
        }
            
        crc16=crc16_compute((uint8_t*)&hd.file_num,sizeof(jacefs_fd_hdr_t)-2,0);
        for(i=0;i<hd.file_num;i++)
        {
            if(fs_port_read( PAGE_TO_ADDR(page)+sizeof(jacefs_fd_hdr_t)+sizeof(jacefs_fd_t)*i,
                sizeof(jacefs_fd_t),(uint8_t*)&fd)!=sizeof(jacefs_fd_t))
            {
                FS_LOG("read err!\r\n");
                return FS_RET_UNKNOW_ERR;
            }
            
            crc16=crc16_compute((uint8_t*)&fd,sizeof(jacefs_fd_t),&crc16);
        }
        
        if(hd.crc16!=crc16)
        {
            FS_LOG("file desc crc16=%x err!\r\n",crc16);
            goto fs_format;//TODO：危险！会导致文件全部丢失！
        }
        FS_LOG("page=%d file desc crc16 pass!\r\n",page);
	}
    
init_finish:
	
    FS_LOG("fs is ready!\r\n");
    m_fs_ready=true;
    
    //计算系统剩余空间
    ret=calculate_remaining_size();
    if(ret!=FS_RET_SUCCESS)
    {
        FS_LOG("calc size err!\r\n");
        return ret;
    }
    
    return FS_RET_SUCCESS;
}

/**
@brief : 文件创建
		
@param : 无

@retval:
- @jacefs_error_t
*/
jacefs_error_t jacefs_create(jacefs_file_id_t *file_id,int size,uint16_t app_id)
{
    if(m_fs_ready!=true)
    {
        return FS_RET_NOT_READY;
    }
    
    if(size>m_fs_remainig_size)
    {
        FS_LOG("no enough space!\r\n");
        return FS_RET_NO_ENOUGH_SPACE;
    }
    
    if(!file_id || size<=0)
    {
        return FS_RET_PARAM_ERR;
    }
	
    FS_LOG("try to create file_id=%d,app_id=%d,size=%d !\r\n",*file_id,app_id,size);
	
    //检查系统是否已存在该文件
	if(file_exist(*file_id,app_id))
	{
		return FS_RET_FILE_EXIST;
	}
	
	//创建文件，分配空间
    jacefs_fd_hdr_t fd_hdr;
    jacefs_fd_t fd;
    uint32_t page,next_page;
	jacefs_error_t ret;
    
	next_page=0;
	ret=FS_RET_SUCCESS;
	
	while(1)
	{
		page=find_file_desc_page(next_page);
		
		if(page==0)
			break;
		next_page=page+1;
		
		if(fs_port_read( PAGE_TO_ADDR(page),
			sizeof(jacefs_fd_hdr_t),(uint8_t*)&fd_hdr)!=sizeof(jacefs_fd_hdr_t))
		{
			FS_LOG("read err!\r\n");
			return FS_RET_UNKNOW_ERR;
		}
		FS_LOG("file_num=%d crc=%x\r\n",fd_hdr.file_num,fd_hdr.crc16);
		
		if(fd_hdr.file_num>=FILE_DESC_PER_PAGE )
		{
			FS_LOG("page=%d,file num=%d ,overflow !\r\n",page,fd_hdr.file_num);
			ret= FS_RET_NO_ENOUGH_FILE;
			continue;
		}
		
		//把文件描述 复制到交换区（内存）
		if(fs_port_read( PAGE_TO_ADDR(page),
			sizeof(jacefs_fd_hdr_t)+fd_hdr.file_num*sizeof(jacefs_fd_t),m_swap_buf) 
				!=sizeof(jacefs_fd_hdr_t)+fd_hdr.file_num*sizeof(jacefs_fd_t) )
		{
			FS_LOG("read err!\r\n");
			return FS_RET_UNKNOW_ERR;
		}
		
		//新的文件
		memset(&fd,0,sizeof(fd));
		fd.app_id=app_id;
		fd.id=*file_id;          //TODO：按APP已有文件数依次增加
		fd.size=size;
		fd.wsize=0;
		if(alloc_page(size,&fd.start_page)!=FS_RET_SUCCESS)
		{
			FS_LOG("alloc page error!\r\n");
            return FS_RET_UNKNOW_ERR;
		}
		memcpy(&m_swap_buf[sizeof(jacefs_fd_hdr_t)+fd_hdr.file_num*sizeof(jacefs_fd_t)],&fd,sizeof(fd));
		
		fd_hdr.file_num++;
		memcpy(m_swap_buf,&fd_hdr,sizeof(jacefs_fd_hdr_t));
		
		fd_hdr.crc16=crc16_compute((uint8_t*)&fd_hdr.file_num,sizeof(jacefs_fd_hdr_t)-2,0);
		fd_hdr.crc16=crc16_compute(&m_swap_buf[sizeof(jacefs_fd_hdr_t)],
						fd_hdr.file_num*sizeof(jacefs_fd_t),&fd_hdr.crc16);
		
		//把文件描述从交换区（内存）回写
		if(fs_port_control(FS_CTL_ERASE_PAGE,&page)!=FS_RET_SUCCESS)
		{
			return FS_RET_UNKNOW_ERR;
		}
		
		if(fs_port_write( PAGE_TO_ADDR(page),
			sizeof(jacefs_fd_hdr_t)+fd_hdr.file_num*sizeof(jacefs_fd_t),m_swap_buf)
			!=sizeof(jacefs_fd_hdr_t)+fd_hdr.file_num*sizeof(jacefs_fd_t))
		{
			FS_LOG("write err!\r\n");
			return FS_RET_UNKNOW_ERR;
		}
		break;
	}
    
    return ret;
}


/**
@brief : 删除文件
		
@param : 
-file_id 
-app_id 

@retval:
- @jacefs_error_t
*/
jacefs_error_t jacefs_delete(jacefs_file_id_t file_id,uint16_t app_id)
{
	if(m_fs_ready!=true)
    {
        return FS_RET_NOT_READY;
    }
    
	FS_LOG("delete file=%d,app_id=%d !\r\n",file_id,app_id);
	
	jacefs_fd_t fd;
	
    //删除文件 描述
	if(delete_file_desc(file_id,app_id,&fd,true)!=FS_RET_SUCCESS)
	{
		return FS_RET_FILE_NOT_EXIST;
	}
	
	//释放空间
	free_page(fd.start_page);
	
    return FS_RET_SUCCESS;
}

/**
@brief : 删除文件，同一个APP ID的文件都删除，用于APP卸载时使用
		
@param : 
-app_id 

@retval:
- @jacefs_error_t
*/
jacefs_error_t jacefs_delete_by_appid(uint16_t app_id)
{
	if(m_fs_ready!=true)
    {
        return FS_RET_NOT_READY;
    }
    
	FS_LOG("delete app_id=%d !\r\n",app_id);
	
	jacefs_fd_t fd;
	
	do{
		//删除文件 描述
		if(delete_file_desc(0,app_id,&fd,false)!=FS_RET_SUCCESS)
		{
			break;
		}
		
		//释放空间
		free_page(fd.start_page);
	}while(1);
	
    return FS_RET_SUCCESS;
}

/**
@brief : 文件追加数据
		
@param : 
-file_id
-app_id 
-dat 		数据
-size		字节

@retval:
- <0，@jacefs_error_t
- >0，写入数据大小，字节
*/
int jacefs_append(jacefs_file_id_t file_id,uint16_t app_id,uint8_t *dat,int size)
{
    if(m_fs_ready!=true)
    {
        return FS_RET_NOT_READY;
    }
	
    if(!dat || size<=0)
    {
        return FS_RET_PARAM_ERR;
    }
	
	jacefs_fd_t fd;
	if(get_file_desc(file_id,app_id,&fd)!=FS_RET_SUCCESS)
	{
		return FS_RET_FILE_NOT_EXIST;
	}
	
	FS_LOG("before write id=%d,app_id=%d,start_page=%d,wsize=%d,size=%d\r\n",
		fd.id,fd.app_id,fd.start_page,fd.wsize,fd.size);
	
	if(fd.wsize+size > fd.size)
	{
		FS_LOG("over size=%d\r\n",fd.wsize+size);
		return FS_RET_FILE_OVER_SIZE;
	}
	
	//文件写入
	uint32_t off_page,
			 off_bytes_in_page;
	uint32_t addr;
	int remaining_size,wsize,already_write;
	
	remaining_size=size;
	already_write=0;
	
	do{
		off_page=fd.start_page;
		if(fd.wsize>=FS_PAGE_SIZE)
		{
			for(int i=0;i<fd.wsize/FS_PAGE_SIZE;i++)
				off_page=get_file_next_page(off_page);
		}
		FS_LOG("write page=%d\r\n",off_page);
		
		off_bytes_in_page=fd.wsize%FS_PAGE_SIZE;
		
		wsize=FS_PAGE_SIZE-off_bytes_in_page;
		if(wsize>=remaining_size)
		{
			wsize=remaining_size;
		}
		
		if(off_bytes_in_page==0)
		{
			if(fs_port_control(FS_CTL_ERASE_PAGE,&off_page)!=FS_RET_SUCCESS)
			{
				FS_LOG("erase page=%d error!\r\n",off_page);
				return FS_RET_UNKNOW_ERR;
			}
		}
		else
		{
			//TODO: 如果 off_bytes_in_page 不为0，应该把内容读出，再擦除，才能再写入
		}
		
		addr=PAGE_TO_ADDR(off_page)+off_bytes_in_page;
		if(fs_port_write(addr,wsize,&dat[already_write])!=wsize)
		{
			FS_LOG("write add=%x error!\r\n",addr);
			return FS_RET_UNKNOW_ERR;
		}
		
		remaining_size-=wsize;
		fd.wsize+=wsize;
		already_write+=wsize;
		
		FS_LOG("write size=%d,remain=%d \r\n",wsize,remaining_size);
		
	}while(remaining_size>0);
	
	//更新文件描述
	set_file_desc(fd);
	
	FS_LOG("after write id=%d,app_id=%d,start_page=%d,wsize=%d,size=%d\r\n",
		fd.id,fd.app_id,fd.start_page,fd.wsize,fd.size);
	
    return size;
}

/**
@brief : 文件根据偏移写入数据
		
@param : 
-file_id
-app_id 
-dat 		数据
-size		字节
-offset		偏移字节

@retval:
- <0，@jacefs_error_t
- >0，写入数据大小，字节
*/
int jacefs_write(jacefs_file_id_t file_id,uint16_t app_id,uint8_t *dat,int size,int offset)
{
    if(m_fs_ready!=true)
    {
        return FS_RET_NOT_READY;
    }
    
    return 0;
}  

/**
@brief : 文件读取
		
@param : 
-file_id
-app_id 
-dat 		数据
-size		字节
-offset		偏移字节

@retval:
- <0，@jacefs_error_t
- >0，读取数据大小，字节
*/
int jacefs_read(jacefs_file_id_t file_id,uint16_t app_id,uint8_t *dat,int size,int offset)
{
    if(m_fs_ready!=true)
    {
        return FS_RET_NOT_READY;
    }
	
    if(!dat || size<=0)
    {
        return FS_RET_PARAM_ERR;
    }
	
	jacefs_fd_t fd;
	if(get_file_desc(file_id,app_id,&fd)!=FS_RET_SUCCESS)
	{
		return FS_RET_FILE_NOT_EXIST;
	}
	
	FS_LOG("read id=%d,app_id=%d,start_page=%d,wsize=%d,size=%d\r\n",
		fd.id,fd.app_id,fd.start_page,fd.wsize,fd.size);
	
	if(offset >= fd.wsize)
	{
		FS_LOG("offset =%d err !\r\n",offset);
		return FS_RET_FILE_OVER_SIZE;
	}
	
	if(offset+size > fd.wsize)
	{
		size=fd.wsize-offset;
		FS_LOG("read size set to %d !\r\n",size);
	}
	
	
	//文件读取
	uint32_t off_page,
			 off_bytes_in_page;
	uint32_t addr;
	int remaining_size,rsize,already_read;
	
	remaining_size=size;
	already_read=0;
	
	do{
		off_page=fd.start_page;
		if(offset>=FS_PAGE_SIZE)
		{
			for(int i=0;i<offset/FS_PAGE_SIZE;i++)
				off_page=get_file_next_page(off_page);
		}
		FS_LOG("read page=%d\r\n",off_page);
		
		off_bytes_in_page=offset%FS_PAGE_SIZE;
		
		rsize=FS_PAGE_SIZE-off_bytes_in_page;
		if(rsize>=remaining_size)
		{
			rsize=remaining_size;
		}
		
		addr=PAGE_TO_ADDR(off_page)+off_bytes_in_page;
		if(fs_port_read(addr,rsize,&dat[already_read])!=rsize)
		{
			FS_LOG("read add=%x error!\r\n",addr);
			return FS_RET_UNKNOW_ERR;
		}
		
		remaining_size-=rsize;
		offset+=rsize;
		already_read+=rsize;
		
		FS_LOG("read size=%d,remain=%d \r\n",rsize,remaining_size);
		
	}while(remaining_size>0);
	
    return size;
}

//自测
void jacefs_self_test(void)
{
    os_printk("\r\n");
    os_printk("SPACE_DESC_START_PAGE=%d\r\n",SPACE_DESC_START_PAGE);
    os_printk("FS_TOTAL_PAGE=%d\r\n",FS_TOTAL_PAGE);
    os_printk("FS_TOTAL_SIZE=%d (%dKB)\r\n",FS_TOTAL_SIZE,FS_TOTAL_SIZE/1024);
    os_printk("FS_PAGE_SIZE=%d\r\n\r\n",FS_PAGE_SIZE);
    
    os_printk("FILE_DESC_PER_PAGE=%d\r\n",FILE_DESC_PER_PAGE);
    os_printk("SPACE_BYTE_PER_PAGE=%d\r\n",SPACE_BYTE_PER_PAGE);
    os_printk("SPACE_DESC_PAGE_NUM=%d\r\n",SPACE_DESC_PAGE_NUM);
    os_printk("FS_MANAGE_PAGE_MAX=%d\r\n",FS_MANAGE_PAGE_MAX);
    os_printk("FS_MANAGE_PAGE_MIN=%d\r\n",FS_MANAGE_PAGE_MIN);
    os_printk("\r\n");
	
//测试读
#define __printf_all_()\
{\
	page_desc_val_t rbuf[20];\
	for(i=0;i<FS_MANAGE_PAGE_MIN+10/*FS_TOTAL_PAGE*/;i++)\
	{\
		addr=PAGE_TO_ADDR(i);\
		os_printk("addr=%x ",addr);\
		fs_port_read(addr,sizeof(rbuf),(uint8_t*)rbuf);\
		for(j=0;j<sizeof(rbuf)/sizeof(page_desc_val_t);j++)\
		{\
			os_printk("%04x ",rbuf[j]);\
		}\
		os_printk("\r\n");\
	}\
}\
os_printk("\r\n");	
	
    int i,j;
    uint32_t addr;
    
//    __printf_all_();
	
	
	//再初始化
	jacefs_init();
    __printf_all_();
	
	
	//创建文件 1
	jacefs_file_id_t f_id;
	uint16_t app_id;
	
//
//测试创建与回收
#if 0
	app_id=1;
	f_id=1;
	jacefs_create(&f_id,155,app_id);
	__printf_all_();
	
	//创建文件2
	f_id=2;
	jacefs_create(&f_id,596+1024,app_id);
	__printf_all_();
	
	//创建文件4
	f_id=4;
	jacefs_create(&f_id,596+1024+1024+1024,app_id);
	__printf_all_();
	
	//创建文件5
	f_id=5;
	jacefs_create(&f_id,20,app_id);
	__printf_all_();
	
	//创建文件6
	f_id=6;
	jacefs_create(&f_id,256,app_id);
	__printf_all_();
	
	//删除文件1
	f_id=1;
	jacefs_delete(f_id,app_id);
	__printf_all_();
	
	//创建文件3
	f_id=3;
	jacefs_create(&f_id,596+1024+1024,app_id);
	__printf_all_();
	
	//删除文件3
	f_id=3;
	jacefs_delete(f_id,app_id);
	__printf_all_();
	
	//删除文件2
	f_id=2;
	jacefs_delete(f_id,app_id);
	__printf_all_();
	
	//删除文件4
	f_id=4;
	jacefs_delete(f_id,app_id);
	__printf_all_();
	
	return;
#endif

//
//测试删除
#if 0
	app_id=2;
	f_id=1;
	jacefs_create(&f_id,155,app_id);
	__printf_all_();
	
	//创建文件2
	f_id=2;
	jacefs_create(&f_id,596,app_id);
	__printf_all_();
	
	//创建文件5
	f_id=5;
	jacefs_create(&f_id,20,app_id);
	__printf_all_();
	
	//创建文件6
	app_id=1;
	f_id=6;
	jacefs_create(&f_id,256,app_id);
	__printf_all_();
	
	//删除
	jacefs_delete_by_appid(2);
	__printf_all_();
	
	jacefs_delete(6,1);
	__printf_all_();
	
	return;
#endif

//
//测试读写
#if 1
	f_id=1;
	jacefs_create(&f_id,155,app_id);
	__printf_all_();
	
	//创建文件2
	f_id=2;
	jacefs_create(&f_id,196,app_id);
	__printf_all_();
	
	//删除文件1
	f_id=1;
	jacefs_delete(f_id,app_id);
	__printf_all_();
	
	//创建文件1
	f_id=1;
	jacefs_create(&f_id,155+1024,app_id);
	__printf_all_();
	
	f_id=1;
	
	//文件1写
	uint8_t wdat[90]={1,2,3,4,5,6,7,8,9,10};
	for(i=0;i<sizeof(wdat);i++)
		wdat[i]=i;
	
	jacefs_append(f_id,app_id,wdat,sizeof(wdat));
	__printf_all_();
	
	jacefs_append(f_id,app_id,wdat,sizeof(wdat));
	__printf_all_();
	
	//文件1读
	uint8_t rdat[180];
	jacefs_read(f_id,app_id,rdat,sizeof(rdat),0);
	for(i=0;i<sizeof(rdat);i+=2)
	{
		if(i%40==0 && i)
			os_printk("\r\n");
		uint16_t da=*(uint16_t*)&rdat[i];
		os_printk("%04x ",da);
		
	}
	os_printk("\r\n");
	
	return;
#endif
	
}
