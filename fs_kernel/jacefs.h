#ifndef __FS_H__ 
#define __FS_H__ 

#include <stdint.h>

//操作状态
typedef enum{
    FS_RET_SUCCESS			=0,		/* 成功 */
	FS_RET_UNKNOW_ERR		=-1,	/* 未知错误 */
    FS_RET_PARAM_ERR		=-2,	/* 参数错误 */
    FS_RET_NOT_READY		=-3,	/* 未初始化 */
    FS_RET_NO_ENOUGH_SPACE	=-4,	/* 空间不足 */
    FS_RET_NO_ENOUGH_FILE	=-5,	/* 文件列表满 */
	FS_RET_FILE_EXIST		=-6,   	/* 文件已经存在 */
	FS_RET_FILE_NOT_EXIST	=-7,	/* 文件不存在 */
	FS_RET_FILE_OVER_SIZE	=-8,	/* 文件已经超出分配空间大小 */
}jacefs_error_t;

typedef uint16_t jacefs_file_id_t;

#define FS_INVALID_FS_ID (0xffff)


/*
 *  FS使用RAM作空间信息和文件描述区的缓存
 */
#define FS_USE_RAM_CACHE (1)
#if FS_USE_RAM_CACHE
    void jacefs_sync(void);
#else
    #define jacefs_sync()
#endif

/*
 *  FS互斥锁，如果使用多线程则需要打开
 */
#define FS_USE_LOCK (0)

/*
 *  FS操作接口
 */
jacefs_error_t jacefs_init(void);
jacefs_error_t jacefs_create(jacefs_file_id_t *file_id,int size,uint16_t app_id);
jacefs_error_t jacefs_delete(jacefs_file_id_t file_id,uint16_t app_id);
jacefs_error_t jacefs_delete_all(uint16_t app_id,jacefs_file_id_t except_file_id);
jacefs_error_t jacefs_clean_all(void);

int jacefs_append(jacefs_file_id_t file_id,uint16_t app_id,uint8_t *dat,int size);
int jacefs_write(jacefs_file_id_t file_id,uint16_t app_id,uint8_t *dat,int size,int offset);
int jacefs_read(jacefs_file_id_t file_id,uint16_t app_id,uint8_t *dat,int size,int offset);

jacefs_error_t jacefs_find_app_id(jacefs_file_id_t file_id,uint16_t *app_id,int *offset);

/*
 *  FS调试接口
 */
void jacefs_self_test(void);



#endif

