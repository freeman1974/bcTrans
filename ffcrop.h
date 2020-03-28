#ifndef __FFCROP_H__
#define __FFCROP_H__

#include "utils.h"
#include <pthread.h>

#ifdef __cplusplus
       extern "C" {
#endif

#define FFCROP_VER "v1.0.0"

#define FFCROP_CONF_FILE "trans.conf"
#define FFCROP_PID_FILE "trans.pid"
#define FFCROP_LOG_FILE "trans.log"
#define MAX_CLUSTER_NUM 	12
#define MAX_VOD_LINE 		100
#define MAX_FORWARD_LINE 	100
#define MAX_CROP_LINE 	(320+1)


#if 1
#define LogMsg(fmt,args...)  do{char info[2000];sprintf(info,": " fmt , ## args);Log_MsgLine(FFCROP_LOG_FILE,info);}while(0)
#else
#define LogMsg(fmt,args...)
#endif

#define FF_TRANS_INIT 0
#define FF_TRANS_RUN  1
#define FF_TRANS_EXIT 2
#define FF_READ_TIMEOUT_ERROR  (-30)
#define FF_WRITE_TIMEOUT_ERROR (-40)
typedef struct{
	char SecName[64];
	int id;
	char hwaccel[16];	//use hwaccel or not
	char thread[16];	//use thread or not
	char inUrl[128];
	char outUrl[128];
	char filter[128];
}FF_CROP_S;

typedef struct{
	pthread_t tid;
	int state;
	pid_t  ffpid;
	FF_CROP_S crop;

}THREAD_RUN_S;


#ifdef __cplusplus
}
#endif

#endif

