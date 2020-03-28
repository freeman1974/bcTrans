#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/ipc.h>

#include <time.h>
#include <sys/time.h>
#include <linux/rtc.h>
#include <pthread.h>

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>  // htons
#include <netdb.h>
#include <arpa/inet.h>
#include <getopt.h>

#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/avutil.h"
#include "libavutil/mathematics.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/samplefmt.h"
#include "libavdevice/avdevice.h"
#include "libavfilter/avfilter.h"
#include "libavutil/error.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"

#include "ffcrop.h"

static THREAD_RUN_S gThrdRunControl[MAX_CROP_LINE];
static int gCmdTransFlag=0;
extern int FF_main(int argc, char **argv);
#include "transcoding.c"

void *FF_FuncTransThrd(void *args)
{
	FF_CROP_S *p=(FF_CROP_S*)args;
	int id=p->id;
	char* inurl=p->inUrl;
	char* outurl=p->outUrl;

	char logfile[128]={0};
	char logmsg[1000]={0};
	sprintf(logfile,"trans_%03d.log",id);
	sprintf(logmsg,"%s(%d,%s--->%s)\n",__FUNCTION__,id,inurl,outurl);
	Log_MsgLine(logfile,logmsg);

	AVOutputFormat *ofmt = NULL;
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
	AVPacket pkt;
	int ret=0, i;
	int videoindex=-1;
	int frame_index=0;
	int64_t start_time=0;
	AVStream *in_stream,*out_stream;
	AVCodec *avcodec=NULL;
	AVCodecContext *avcodec_ctx=NULL;
	AVRational tb,tb_q={1,AV_TIME_BASE};
	int64_t pts_time,now_time;

	av_log_set_level(AV_LOG_ERROR);
	//av_register_all();
	avformat_network_init();

	while (gThrdRunControl[id].state != FF_TRANS_EXIT) {
		sprintf(logmsg,"ffmpeg init\n");	Log_MsgLine(logfile,logmsg);

		if ((ret = avformat_open_input(&ifmt_ctx, inurl, 0, NULL)) < 0) {
			ret=-2; goto End;
		}
		sprintf(logmsg,"avformat_open_input(%s) success\n",inurl);	Log_MsgLine(logfile,logmsg);

		if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
			ret=-3; goto End;
		}

		for(i=0; i<ifmt_ctx->nb_streams; i++)
			if(ifmt_ctx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
				videoindex=i;
				break;
			}

		//av_dump_format(ifmt_ctx, 0, in_filename, 0);

		avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", outurl);
		if (!ofmt_ctx) {
			ret = -4;	goto End;
		}
		sprintf(logmsg,"avformat_alloc_output_context2(%s) success\n",outurl);	Log_MsgLine(logfile,logmsg);

		ofmt = ofmt_ctx->oformat;
		for (i = 0; i < ifmt_ctx->nb_streams; i++) {
			in_stream = ifmt_ctx->streams[i];
			avcodec = avcodec_find_encoder(in_stream->codecpar->codec_id);
			avcodec_ctx = avcodec_alloc_context3(avcodec);
			if (!avcodec_ctx) {
				ret = -5;	goto End;
			}
			out_stream = avformat_new_stream(ofmt_ctx, avcodec);
			//out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
			if (!out_stream) {
				ret = -5;	goto End;
			}

//Using AVStream.codec to pass codec parameters to muxers is deprecated, use AVStream.codecpar instead.
			//ret = avcodec_parameters_from_context(out_stream->codecpar, avcodec_ctx);
			ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
			//ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
			if (ret < 0) {
				ret=-6; goto End;
			}

			out_stream->codecpar->codec_tag = 0;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER){
				//out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;	//this is the key to avoid crash!
				avcodec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			}
		}

		//av_dump_format(ofmt_ctx, 0, out_filename, 1);
		if (!(ofmt->flags & AVFMT_NOFILE)) {
			ret = avio_open(&ofmt_ctx->pb, outurl, AVIO_FLAG_WRITE);
			if (ret < 0) {
				ret=-7; goto End;
			}
		}

		ret = avformat_write_header(ofmt_ctx, NULL);
		if (ret < 0) {
			ret=-8; goto End;
		}

		//sprintf(logmsg,"trancoding...\n");Log_MsgLine(logfile,logmsg);
		//Error during writing data to output file. Error code: Broken pipe
		start_time=av_gettime();

		while(gThrdRunControl[id].state != FF_TRANS_EXIT){
			ret = av_read_frame(ifmt_ctx, &pkt);
			if (ret<0){
				sprintf(logmsg,"av_read_frame() error. ret=%d\n",ret);	Log_MsgLine(logfile,logmsg);
				ret=-101;break;
			}

			if(pkt.pts==AV_NOPTS_VALUE){
				tb=ifmt_ctx->streams[videoindex]->time_base;
				pts_time=(double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
				pkt.pts=(double)(frame_index*pts_time)/(double)(av_q2d(tb)*AV_TIME_BASE);
				pkt.dts=pkt.pts;
				pkt.duration=(double)pts_time/(double)(av_q2d(tb)*AV_TIME_BASE);
			}

			if(pkt.stream_index==videoindex){
				tb=ifmt_ctx->streams[videoindex]->time_base;
				pts_time = av_rescale_q(pkt.dts, tb, tb_q);
				now_time = av_gettime() - start_time;
				if (pts_time > now_time)
					av_usleep(pts_time - now_time);
			}

			in_stream  = ifmt_ctx->streams[pkt.stream_index];
			out_stream = ofmt_ctx->streams[pkt.stream_index];
			pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
				(enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
			pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
				(enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
			pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
			pkt.pos = -1;

			if(pkt.stream_index==videoindex){
				frame_index++;
			}

			ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
			if (ret<0){
				fprintf(stderr, "Error during writing data to output file. "
						"Error code: %s\n", av_err2str(ret));
				sprintf(logmsg,"av_interleaved_write_frame() error. ret=%d\n",ret);
				Log_MsgLine(logfile,logmsg);
				ret=-102;break;
			}

			//av_free_packet(&pkt);
			av_packet_unref(&pkt);
		}

End:
		sprintf(logmsg,"ffmpeg deinit. errorno=%d\n",ret);	Log_MsgLine(logfile,logmsg);
		if (ofmt_ctx){
			av_write_trailer(ofmt_ctx);
			if (!(ofmt->flags & AVFMT_NOFILE))
				avio_close(ofmt_ctx->pb);
			avformat_free_context(ofmt_ctx);
			ofmt_ctx=NULL;
		}
		if (ifmt_ctx){
			avformat_close_input(&ifmt_ctx);
			ifmt_ctx=NULL;
		}

		usleep(200000);
	}

	avformat_network_deinit();

	sprintf(logmsg,"Exit. ret=%d\n",ret);	Log_MsgLine(logfile,logmsg);
	return args;
}

void* FF_TransThrd(void* args)
{
	FF_CROP_S *p=(FF_CROP_S*)args;
	int ret=0;
	char sCmd[400]={0};
	int tryCnt=2;

	sprintf(sCmd,"nohup ./fftool -d -i %s -c:v copy "
		"-c:a copy -f flv %s 1>/dev/null 2>&1 ",p->inUrl,p->outUrl);
	gCmdTransFlag=1;

	while (gThrdRunControl[p->id].state != FF_TRANS_EXIT) {
		LogMsg("[%s][%s][%03d]%s\n",__FUNCTION__,p->SecName,p->id,sCmd);
		ret=system(sCmd);

		LogMsg("[%s][%03d]break out![ret=%d] Try it again after %d s.\n",p->SecName,p->id, ret,tryCnt);
		sleep(tryCnt);

		//if (tryCnt++ >=120) tryCnt=2;
	}

	return args;
}

int FF_TransProd(void* args)
{
	FF_CROP_S *pArgs=(FF_CROP_S*)args;

	int i=0;
	int argc=10;
	char arg[argc][128];
	char *argv[argc];
	char **p=(char **)&argv;
	char sCmd[400]={0};

	memset(arg,0,sizeof(arg));
	strcpy(arg[0],"ffcrop");
	strcpy(arg[1],"-i");
	strcpy(arg[2],pArgs->inUrl);
	strcpy(arg[3],"-vcodec");
	strcpy(arg[4],"copy");
	strcpy(arg[5],"-c:a");
	strcpy(arg[6],"copy");
	strcpy(arg[7],"-f");
	strcpy(arg[8],"flv");
	strcpy(arg[9],pArgs->outUrl);

	for (i=0; i<argc; i++){
		p[i]=arg[i];
		strcat(sCmd, p[i]);	strcat(sCmd, " ");
	}
	LogMsg("FF_TransProd[%03d] %s\n",pArgs->id,sCmd);

	int pid=fork();
	if (pid<0){
		LogMsg("[%03d] fork() failed. ret=%d\n",pArgs->id, pid);
	}
	else if (pid==0){
		while(1){
			FF_main(argc,p);
			sleep(2);
		}
	}
	else{
		LogMsg("[%03d] forked. pid=%d\n",pArgs->id, pid);
		gThrdRunControl[pArgs->id].ffpid=pid;
	}

	return pid;
}

int FF_SwCropProd(void* args)
{
	int ret=0;
	FF_CROP_S *pArgs=(FF_CROP_S*)args;
	char sCmd[400]={0};

	int i=0;
	int argc=16;
	char arg[argc][128];
	char *argv[argc];
	char **p=(char **)&argv;
// -vcodec libx264 -b:v 300000 -profile:v main -preset medium -c:a copy
	memset(arg,0,sizeof(arg));
	strcpy(arg[0],"ffcrop");
	strcpy(arg[1],"-i");
	strcpy(arg[2],pArgs->inUrl);
	strcpy(arg[3],"-loglevel");
	strcpy(arg[4],"error");
	strcpy(arg[5],"-vf");
	strcpy(arg[6],pArgs->filter);	//watermark or crop parameters
	strcpy(arg[7],"-vcodec");
	strcpy(arg[8],"libx264");
	strcpy(arg[9],"-b:v");
	strcpy(arg[10],"350000");
//	strcpy(arg[9],"-profile:v");
//	strcpy(arg[10],"main");
//	strcpy(arg[11],"-preset");
//	strcpy(arg[12],"medium");
	strcpy(arg[11],"-c:a");
	strcpy(arg[12],"copy");
	strcpy(arg[13],"-f");
	strcpy(arg[14],"flv");
	strcpy(arg[15],pArgs->outUrl);

	for (i=0; i<argc; i++){
		p[i]=arg[i];
		strcat(sCmd, p[i]);	strcat(sCmd, " ");
	}
	LogMsg("[%03d] %s\n",pArgs->id,sCmd);

	int pid=fork();
	if (pid<0){
		LogMsg("[%03d] fork() failed. ret=%d\n",pArgs->id, pid);
	}
	else if (pid==0){
		while(1){
			ret=FF_main(argc,p);
			LogMsg("[%03d] FF_main() exit. ret=%d, sleep 2s and try again!\n",pArgs->id, ret);
			sleep(2);
		}
	}
	else{
		int status=0;

		LogMsg("[%03d] forked. pid=%d\n",pArgs->id, pid);
		gThrdRunControl[pArgs->id].ffpid=pid;

		//wait(&status);
		//LogMsg("[%03d] child return. status=%d\n",pArgs->id, status);

		//signal(SIGCHLD, SIG_IGN);
	}

	return pid;
}

#if 0
void* FF_SwCropThrd(void* args)
{
	FF_CROP_S *p=(FF_CROP_S*)args;
	int ret=0;
	char sCmd[400]={0};

//	sprintf(sCmd,"nohup ./fftool -i %s -vf %s -vcodec libx264 -b:v 300000 -profile:"
//		"v main -preset medium -c:a copy -f flv %s 1>/dev/null 2>&1 &",p->inUrl,p->filter,p->outUrl);
	sprintf(sCmd,"nohup ./fftool -i %s -vf %s -vcodec libx264 -b:v 350000 "
		"-c:a copy -f flv %s 1>/dev/null 2>&1 &",p->inUrl,p->filter,p->outUrl);

	LogMsg("[%03d]%s\n",p->id,sCmd);
	ret=system(sCmd);

	while (gThrdRunControl[p->id].state != FF_TRANS_EXIT) {

		sleep(1);
	}

	return args;
}
#endif
void* FF_SwCropThrd(void* args)
{
	FF_CROP_S *p=(FF_CROP_S*)args;
	int ret=0;
	char sCmd[400]={0};
	int tryCnt=2;

//		"-x264-params \"profile=main:preset=veryfast:tune=zerolatency:qp=24:crf=18:crf_max=42\" "
	sprintf(sCmd,"nohup ./fftool -d -i %s -g 50 -sc_threshold 0 -vf %s -vcodec libx264 -b:v 300k -bufsize 300k -maxrate 400k "
		"-x264-params \"profile=high:preset=superfast:tune=zerolatency:qp=26:crf=20:crf_max=44\" "
		"-c:a copy -f flv %s 1>/dev/null 2>&1 ",p->inUrl,p->filter,p->outUrl);
// 		"-x264-params \"profile=baseline:preset=superfast:tune=zerolatency:qp=26:crf=20:crf_max=44\" "
//	sprintf(sCmd,"./fftool -d -i %s -g 50 -sc_threshold 0 -vf %s -vcodec libx264 -b:v 300k -bufsize 300k -maxrate 400k "
//	sprintf(sCmd,"./fftool -d -i %s -vf %s -vcodec libx264 -b:v 300k -bufsize 300k -maxrate 400k "
//		"-c:a copy -f flv %s",p->inUrl,p->filter,p->outUrl);
	gCmdTransFlag=1;

	while (gThrdRunControl[p->id].state != FF_TRANS_EXIT) {
		LogMsg("[%s][%s][%03d]%s\n",__FUNCTION__,p->SecName,p->id,sCmd);
		ret=system(sCmd);

		LogMsg("[%s][%03d]break out![ret=%d] Try it again after %d s.\n",p->SecName,p->id, ret,tryCnt);
		sleep(tryCnt);

		//if (tryCnt++ >=120) tryCnt=2;
	}

	return args;
}

void* FF_SwCropHDThrd(void* args)
{
	FF_CROP_S *p=(FF_CROP_S*)args;
	int ret=0;
	char sCmd[400]={0};
	int tryCnt=2;

	sprintf(sCmd,"nohup ./fftool -d -i %s -g 50 -sc_threshold 0 -vf %s -vcodec libx264 -b:v 800k -bufsize 800k -maxrate 1000k "
		"-x264-params \"profile=high:preset=superfast:tune=zerolatency:qp=26:crf=20:crf_max=44\" "
		"-c:a copy -f flv %s 1>/dev/null 2>&1 ",p->inUrl,p->filter,p->outUrl);
	gCmdTransFlag=1;

	while (gThrdRunControl[p->id].state != FF_TRANS_EXIT) {
		LogMsg("[%s][%s][%03d]%s\n",__FUNCTION__,p->SecName,p->id,sCmd);
		ret=system(sCmd);

		LogMsg("[%s][%03d]break out![ret=%d] Try it again after %d s.\n",p->SecName,p->id, ret,tryCnt);
		sleep(tryCnt);

		//if (tryCnt++ >=120) tryCnt=2;
	}

	return args;
}

int FF_HwCropProd(void* args)
{
	int ret=0;
	FF_CROP_S *pArgs=(FF_CROP_S*)args;
	char sCmd[400]={0};

	int i=0;
	int argc=16;
	char arg[argc][128];
	char *argv[argc];
	char **p=(char **)&argv;
	memset(arg,0,sizeof(arg));
	strcpy(arg[0],"ffcrop");
	strcpy(arg[1],"-i");
	strcpy(arg[2],pArgs->inUrl);
	strcpy(arg[3],"-loglevel");
	strcpy(arg[4],"error");
	strcpy(arg[5],"-vf");
	strcpy(arg[6],pArgs->filter);	//watermark or crop parameters
	strcpy(arg[7],"-vcodec");
	strcpy(arg[8],"h264_nvenc");
	strcpy(arg[9],"-b:v");
	strcpy(arg[10],"600000");
//	strcpy(arg[9],"-profile:v");
//	strcpy(arg[10],"main");
//	strcpy(arg[11],"-preset");
//	strcpy(arg[12],"medium");
	strcpy(arg[11],"-c:a");
	strcpy(arg[12],"copy");
	strcpy(arg[13],"-f");
	strcpy(arg[14],"flv");
	strcpy(arg[15],pArgs->outUrl);

	for (i=0; i<argc; i++){
		p[i]=arg[i];
		strcat(sCmd, p[i]);	strcat(sCmd, " ");
	}
	LogMsg("[%03d] %s\n",pArgs->id,sCmd);

	int pid=fork();
	if (pid<0){
		LogMsg("[%03d] fork() failed. ret=%d,%s()\n",pArgs->id, pid, __FUNCTION__);
	}
	else if (pid==0){

		while(1){
			ret=FF_main(argc,p);
			LogMsg("[%03d] FF_main() exit. ret=%d, sleep 2s and try again!\n",pArgs->id, ret);

			sleep(2);
		}
	}
	else{
		int status=0;
		LogMsg("[%03d] forked. pid=%d,%s()\n",pArgs->id, pid, __FUNCTION__);
		gThrdRunControl[pArgs->id].ffpid=pid;
		//wait(&status);
		//LogMsg("[%03d] child return. status=%d. sleep 2s and try again\n",pArgs->id, status);

		//signal(SIGCHLD, SIG_IGN);
	}

	return pid;
}

#if 0
void* FF_HwCropThrd(void* args)
{
	FF_CROP_S *p=(FF_CROP_S*)args;
	int ret=0;
	char sCmd[400]={0};

//	sprintf(sCmd,"nohup ./fftool_cuda -i %s -vf %s -vcodec h264_nvenc -b:v 500000 -profile:"
//		"v main -preset medium -c:a copy -f flv %s 1>/dev/null 2>&1 &",p->inUrl,p->filter,p->outUrl);
	sprintf(sCmd,"nohup ./fftool_cuda -i %s -vf %s -vcodec h264_nvenc -b:v 600000"
			"-c:a copy -f flv %s 1>/dev/null 2>&1 &",p->inUrl,p->filter,p->outUrl);
	LogMsg("[%03d]%s\n",p->id,sCmd);
	ret=system(sCmd);

	while (gThrdRunControl[p->id].state != FF_TRANS_EXIT) {

		sleep(1);
	}

	return args;
}
#endif
void* FF_HwCropThrd(void* args)
{
	FF_CROP_S *p=(FF_CROP_S*)args;
	int ret=0;
	char sCmd[400]={0};
	int tryCnt=2;

	sprintf(sCmd,"nohup ./fftool_cuda -d -i %s -vf %s -vcodec h264_nvenc -b:v 600000"
			"-c:a copy -f flv %s 1>/dev/null 2>&1 ",p->inUrl,p->filter,p->outUrl);
	gCmdTransFlag=1;

	while (gThrdRunControl[p->id].state != FF_TRANS_EXIT) {
		LogMsg("[%s][%03d]%s\n",p->SecName,p->id,sCmd);
		ret=system(sCmd);

		LogMsg("[%s][%03d]break out![ret=%d] Try it again after %d s.\n",p->SecName,p->id, ret,tryCnt);
		sleep(tryCnt);

		//if (tryCnt++ >=120) tryCnt=2;
	}

	return args;
}

void* FF_SwVodThrd(void* args)
{
	FF_CROP_S *p=(FF_CROP_S*)args;
	char sCmd[400]={0};

	if (strlen(p->filter)>=1)
		sprintf(sCmd,"nohup ./fftool -d -i %s -r 25 -vf %s -vcodec libx264 -b:v 400k -bufsize 400k -maxrate 500k "
		"-x264-params \"profile=main:preset=veryfast:tune=zerolatency:qp=24:crf=18:crf_max=42\" "
			" -c:a copy -f flv %s 1>/dev/null 2>&1 ",p->inUrl,p->filter,p->outUrl);
	else
		sprintf(sCmd,"nohup ./fftool -d -i %s -r 25 -vcodec libx264 -b:v 400k -bufsize 400k -maxrate 500k "
		"-x264-params \"profile=main:preset=veryfast:tune=zerolatency:qp=24:crf=18:crf_max=42\" "
			" -c:a copy -f flv %s 1>/dev/null 2>&1 ",p->inUrl,p->outUrl);
	gCmdTransFlag=1;

	while (gThrdRunControl[p->id].state != FF_TRANS_EXIT) {
		LogMsg("[%s][%03d]%s\n",p->SecName,p->id,sCmd);
		system(sCmd);
		LogMsg("[%s][%03d]finished. Transcode again.\n",p->SecName,p->id);
	}

	return args;
}

void* FF_SwVodThrd2(void* args)
{
	FF_CROP_S *p=(FF_CROP_S*)args;
	char sCmd[400]={0};

	if (strlen(p->filter)>=1)
		sprintf(sCmd,"nohup ./fftool -d -i %s -r 25 -vf %s -vcodec libx264 -b:v 200k -bufsize 400k -maxrate 300k "
		"-x264-params \"profile=main:preset=veryfast:tune=zerolatency:qp=24:crf=18:crf_max=42\" "
			" -c:a copy -f flv %s_v2 1>/dev/null 2>&1 ",p->inUrl,p->filter,p->outUrl);
	else
		sprintf(sCmd,"nohup ./fftool -d -i %s -r 25 -vcodec libx264 -b:v 200k -bufsize 400k -maxrate 300k "
		"-x264-params \"profile=main:preset=veryfast:tune=zerolatency:qp=24:crf=18:crf_max=42\" "
			" -c:a copy -f flv %s_v2 1>/dev/null 2>&1 ",p->inUrl,p->outUrl);
	gCmdTransFlag=1;

	while (gThrdRunControl[p->id].state != FF_TRANS_EXIT) {
		LogMsg("[%s][%03d]%s\n",p->SecName,p->id,sCmd);
		system(sCmd);
		LogMsg("[%s][%03d]finished. Transcode again.\n",p->SecName,p->id);
	}

	return args;
}


int osCreatePidFile(char *filename, int pid)
{
	FILE *fp=NULL;
	char line[128]={0};

	fp = fopen(filename, "w+");
	if (fp)
	{
		sprintf(line, "%d", pid);
		fputs(line, fp);
		fclose(fp);

		return 0;
	}

	return -1;
}


void ProgExit(int sig)
{
	int i=0;
	char sCmd[256]={0};

	printf("pid:%d receive signal:%d,exit!\n",getpid(),sig);
	LogMsg("pid:%d receive signal:%d,exit!\n",getpid(),sig);

	for (i=1; i<MAX_CROP_LINE; i++){
		//exit process if exist
		if (gThrdRunControl[i].ffpid>0){
			util_kill(gThrdRunControl[i].ffpid);
			LogMsg("[%03d] process killed.\n",i);
		}

		//exit thread if exist
		if (gThrdRunControl[i].state==FF_TRANS_RUN){
			if (gThrdRunControl[i].tid>0)
				pthread_join(gThrdRunControl[i].tid, NULL);

			gThrdRunControl[i].state=FF_TRANS_EXIT;
			LogMsg("[%03d] thread exit.\n",i);
		}
	}

	//exit if use fftool to do crop
	if (gCmdTransFlag){
		system("pkill fftool");
		system("pkill fftool_cuda");
		sleep(1);
		system("pkill fftool");
		system("pkill fftool_cuda");
	}

	LogMsg("ProgExit.\n");
	remove(FFCROP_PID_FILE);
	exit(-sig);
}

int do_main(PCONFIG pCfg)
{
	int ret=0;
	FF_CROP_S ffcrop;
	char key[16]={0},str[128]={0};
	int i=0;
	char oPostfix[28]={0};

	int count=0;
	int hwCodecCnt=0;
	char sCut[] = {'"',',','\0'};
	char sCut2[] = {'"','\0'};
	char *pSubStr[4]={0};
	THREAD_RUN_S *pThrdRun=NULL;
	int j=0;
	char section[64]={0};
	int is_HD=0;	//default is SD. x1: SD, x2:HD
	int slen=0;

	memset(&gThrdRunControl,0,sizeof(gThrdRunControl));

//vod process based on [vod] config
	memset(&ffcrop,0,sizeof(ffcrop));
	strcpy(section,"vod");
	for (i=1; i<MAX_VOD_LINE; i++){
		sprintf(key,"%03d",i);
		memset(str,0,sizeof(str));
		ret=cfg_getstring(pCfg, section,key,NULL,str);
		if (ret!=0){
			continue;
		}

		pSubStr[0] = strtok(str, sCut);
		pSubStr[1] = strtok(NULL, sCut);
		pSubStr[2] = strtok(NULL, sCut);
		LogMsg("vodfile=%s, rtmpurl=%s, filter=%s\n",pSubStr[0],pSubStr[1], pSubStr[2]);
		if (!pSubStr[0]||!pSubStr[1])
			continue;

		//HD(high quality streaming)
		pThrdRun=(THREAD_RUN_S *)&gThrdRunControl[count];
		strcpy(pThrdRun->crop.SecName,section);
		strcpy(pThrdRun->crop.inUrl, pSubStr[0]);
		strcpy(pThrdRun->crop.outUrl, pSubStr[1]);
		if (pSubStr[2])
			strcpy(pThrdRun->crop.filter, pSubStr[2]);

		pThrdRun->crop.id=count+1;
		pThrdRun->ffpid=-1;
		pThrdRun->tid=-1;
		pThrdRun->state=FF_TRANS_INIT;

		ret=pthread_create(&pThrdRun->tid, NULL, FF_SwVodThrd, (void *)&pThrdRun->crop);
		if (ret<0){
			pThrdRun->state=FF_TRANS_EXIT;
			LogMsg("[%s][%03d]Fail to create thread:FF_SwVodThrd for HD stream. ret=%d\n",section,i,ret);
		}
		else{
			pThrdRun->state=FF_TRANS_RUN;
			LogMsg("[%s][%03d]create thread:FF_SwVodThrd success for HD stream!\n",section,i);
			count++;
		}

		//SD(low quality streaming)
		pThrdRun=(THREAD_RUN_S *)&gThrdRunControl[count];
		strcpy(pThrdRun->crop.SecName,section);
		strcpy(pThrdRun->crop.inUrl, pSubStr[0]);
		strcpy(pThrdRun->crop.outUrl, pSubStr[1]);
		if (pSubStr[2])
			strcpy(pThrdRun->crop.filter, pSubStr[2]);

		pThrdRun->crop.id=count+1;
		pThrdRun->ffpid=-1;
		pThrdRun->tid=-1;
		pThrdRun->state=FF_TRANS_INIT;

		ret=pthread_create(&pThrdRun->tid, NULL, FF_SwVodThrd2, (void *)&pThrdRun->crop);
		if (ret<0){
			pThrdRun->state=FF_TRANS_EXIT;
			LogMsg("[%s][%03d]Fail to create thread:FF_SwVodThrd2 for SD stream. ret=%d\n",section,i,ret);
		}
		else{
			pThrdRun->state=FF_TRANS_RUN;
			LogMsg("[%s][%03d]create thread:FF_SwVodThrd2 success for SD stream!\n",section,i);
			count++;
		}

	}

//forward process based on [pull_forward] config
	memset(&ffcrop,0,sizeof(ffcrop));
	strcpy(section,"pull_forward");
	for (i=1; i<MAX_FORWARD_LINE; i++){
		sprintf(key,"%03d",i);
		memset(str,0,sizeof(str));
		ret=cfg_getstring(pCfg, section,key,NULL,str);
		if (ret!=0){
			continue;
		}

		pSubStr[0] = strtok(str, sCut);
		pSubStr[1] = strtok(NULL, sCut);
		pSubStr[2] = strtok(NULL, sCut);
		LogMsg("input=%s, output=%s, filter=%s\n",pSubStr[0],pSubStr[1], pSubStr[2]);
		if (!pSubStr[0]||!pSubStr[1])
			continue;

		pThrdRun=(THREAD_RUN_S *)&gThrdRunControl[count];
		strcpy(pThrdRun->crop.SecName,section);
		strcpy(pThrdRun->crop.inUrl, pSubStr[0]);
		strcpy(pThrdRun->crop.outUrl, pSubStr[1]);
		slen=strlen(pSubStr[0]);
		if (pSubStr[0][slen-1] == '2')
			is_HD=1;

		pThrdRun->crop.id=count+1;
		pThrdRun->ffpid=-1;
		pThrdRun->tid=-1;
		pThrdRun->state=FF_TRANS_INIT;

		if ((pSubStr[2])&&(strlen(pSubStr[2])>1)){
			if ((hwCodecCnt<2)&&(!strcmp(ffcrop.hwaccel,"on"))){
				sprintf(pThrdRun->crop.filter,"\"%s\"", pSubStr[2]);
				ret=pthread_create(&pThrdRun->tid, NULL, FF_HwCropThrd, (void *)&pThrdRun->crop);
				hwCodecCnt++;
			}else{
				sprintf(pThrdRun->crop.filter,"\"%s\"", pSubStr[2]);
				if (is_HD){
					ret=pthread_create(&pThrdRun->tid, NULL, FF_SwCropHDThrd, (void *)&pThrdRun->crop);
				}
				else
					ret=pthread_create(&pThrdRun->tid, NULL, FF_SwCropThrd, (void *)&pThrdRun->crop);

				//sprintf(pThrdRun->crop.filter,"%s", pSubStr[2]);
				//ret=pthread_create(&pThrdRun->tid, NULL, FF_FilterTransThrd, (void *)&pThrdRun->crop);
			}
		}
		else
			ret=pthread_create(&pThrdRun->tid, NULL, FF_FuncTransThrd, (void *)&pThrdRun->crop);
			//ret=pthread_create(&pThrdRun->tid, NULL, FF_TransThrd, (void *)&pThrdRun->crop);

		if (ret<0){
			pThrdRun->state=FF_TRANS_EXIT;
			LogMsg("[%s][%03d]Fail to create thread for %s stream. ret=%d\n",section,i,is_HD?"HD":"SD",ret);
		}
		else{
			pThrdRun->state=FF_TRANS_RUN;
			LogMsg("[%s][%03d]create thread success for %s stream.\n",section,i,is_HD?"HD":"SD");
			count++;
		}

	}

//load crop config. max suppor
	for (j=1; j<=MAX_CLUSTER_NUM; j++){
		memset(&ffcrop,0,sizeof(ffcrop));
		sprintf(section,"cluster#%d",j);

		ret=cfg_getstring(pCfg, section, "hwaccel", "off", ffcrop.hwaccel);
		//to avoid some mistakes. we exame conditions carefully.
		if ((!ret)&&(!strcmp(ffcrop.hwaccel,"on"))&&(strcmp(CODEC,"x264"))){
			strcpy(ffcrop.hwaccel,"on");
			LogMsg("hwaccel enabled.\n");
		}
		ret=cfg_getstring(pCfg, section, "thread", "on", ffcrop.thread);
		if ((!ret)&&(!strcmp(ffcrop.thread,"on")))
			LogMsg("thread enabled.\n");

		ret=cfg_getstring(pCfg, section, "iRtmpDomain", NULL, ffcrop.inUrl);
		if (ret!=0){
			LogMsg("[%s]iRtmpDomain not found. skip [%s]\n",section, section);
			continue;
		}
		LogMsg("[%s]iRtmpDomain=%s\n", section,ffcrop.inUrl);

		ret=cfg_getstring(pCfg, section, "oRtmpDomain",NULL, ffcrop.outUrl);
		if (ret!=0){
			LogMsg("[%s]oRtmpDomain not found. skip [%s]\n",section, section);
			continue;
		}
		LogMsg("[%s]oRtmpDomain=%s\n", section,ffcrop.outUrl);

		memset(oPostfix,0,sizeof(oPostfix));
		ret=cfg_getstring(pCfg, section, "oPostfix",NULL, oPostfix);
		if (ret!=0){
			//LogMsg("[%s]oPostfix not found. skip [%s]\n",section, section);
			//continue;		//support no postfix(pull-forward senario)
		}
		LogMsg("[%s]oPostfix=%s\n", section,oPostfix);

	//startup thread for crop. one crop one thread.
		for (i=1; i<MAX_CROP_LINE; i++){
			sprintf(key,"%03d",i);
			memset(str,0,sizeof(str));
			ret=cfg_getstring(pCfg, section,key,NULL,str);
			if (ret!=0){
				//break;
				continue;
			}
			pThrdRun=(THREAD_RUN_S *)&gThrdRunControl[count];
			strcpy(pThrdRun->crop.SecName,section);
			strcpy(pThrdRun->crop.inUrl, ffcrop.inUrl);
			strcpy(pThrdRun->crop.outUrl, ffcrop.outUrl);

		    pSubStr[0] = strtok(str, sCut);
		    pSubStr[1] = strtok(NULL, sCut);
			pSubStr[2] = strtok(NULL, sCut);
			pSubStr[3] = strtok(NULL, sCut);
			//printf("%s,%s,%s,%s\n",pSubStr[0],pSubStr[1],pSubStr[2],pSubStr[3]);
			//LogMsg("name=%s, value=%s\n",pSubStr[0],pSubStr[2]);

			if (!pSubStr[0])
				continue;
			slen=strlen(pSubStr[0]);
			if (pSubStr[0][slen-1] == '2')
				is_HD=1;
			strcat(pThrdRun->crop.inUrl,pSubStr[0]);
			strcat(pThrdRun->crop.outUrl,pSubStr[0]);
			if (strlen(oPostfix)>0)
				strcat(pThrdRun->crop.outUrl,oPostfix);
			//LogMsg("[%03d] inUrl=%s, outUrl=%s, %s\n",pThrdRun->crop.id,
			//	pThrdRun->crop.inUrl,pThrdRun->crop.outUrl,pSubStr[2]);

			pThrdRun->crop.id=count+1;
			pThrdRun->ffpid=-1;
			pThrdRun->tid=-1;
			pThrdRun->state=FF_TRANS_INIT;
		//#if 0	//use thread to implement
			if (!strcmp(ffcrop.thread,"on")){
				if (pSubStr[2])
					sprintf(pThrdRun->crop.filter,"\"%s\"", pSubStr[2]);
				if (strlen(pThrdRun->crop.filter)>1){
					if ((hwCodecCnt<2)&&(!strcmp(ffcrop.hwaccel,"on"))){
						ret=pthread_create(&pThrdRun->tid, NULL, FF_HwCropThrd, (void *)&pThrdRun->crop);
						hwCodecCnt++;
					}else{
						if (is_HD){
							ret=pthread_create(&pThrdRun->tid, NULL, FF_SwCropHDThrd, (void *)&pThrdRun->crop);
						}
						else
							ret=pthread_create(&pThrdRun->tid, NULL, FF_SwCropThrd, (void *)&pThrdRun->crop);
					}
				}
				else
					ret=pthread_create(&pThrdRun->tid, NULL, FF_TransThrd, (void *)&pThrdRun->crop);

				if (ret<0){
					pThrdRun->state=FF_TRANS_EXIT;
					LogMsg("[%s][%03d]Fail to create thread for %s stream. ret=%d\n",section,i,is_HD?"HD":"SD",ret);
				}
				else{
					pThrdRun->state=FF_TRANS_RUN;
					LogMsg("[%s][%03d]create thread success for %s stream.\n",section,i,is_HD?"HD":"SD");
					count++;
				}
			}
		//#else		//use process to implement. for effeciency consideration, we do not use process.
			else{
				if (pSubStr[2])
					strcpy(pThrdRun->crop.filter,pSubStr[2]);
				if (strlen(pThrdRun->crop.filter)>1){
					if ((hwCodecCnt<2)&&(!strcmp(ffcrop.hwaccel,"on"))){
						ret=FF_HwCropProd((void *)&pThrdRun->crop);
						hwCodecCnt++;
					}else{
						ret=FF_SwCropProd((void *)&pThrdRun->crop);
					}
				}
				else
					ret=FF_TransProd((void *)&pThrdRun->crop);
				if (ret<0){
					//gThrdRunControl[i].state=FF_TRANS_EXIT;
					LogMsg("[%s][%03d] Fail to create process! ret=%d\n",section,count+1,ret);
				}
				else{
					//gThrdRunControl[i].state=FF_TRANS_RUN;
					LogMsg("[%s][%03d] create process success!\n",section,count+1);
					count++;
				}
			}
		//#endif

		}
	}

	LogMsg("%d crop lines found.\n",count);

	return 0;
}


int main(int argc, char **argv)
{
	int ret=0;
	PCONFIG pCfg=NULL;

//print program info
	printf("build-time=%s, version=%s, encoder=%s\n",BNO,FFCROP_VER,CODEC);
	LogMsg("build-time=%s, version=%s, encoder=%s\n",BNO,FFCROP_VER,CODEC);
	if (strcmp(CODEC,"x264")){
		printf("!!!please make sure cuda/nvidia drivers installed in your machine!!!\n");
		LogMsg("!!!please make sure cuda/nvidia drivers installed in your machine!!!\n");
	}

//check config file
	char *cfgfile=FFCROP_CONF_FILE;
	char c=0;
    while ((c = getopt(argc, argv, "h:p:t:c:")) != -1) {
        switch (c) {
        case 'h':
		case '?':
            printf("usage:\n# sh run.sh\n");
            return 0;
		case 'c':
			cfgfile=optarg;
			break;
        default:
            break;
        }
    }

//check pid to avoid duplicate run
	if(access(FFCROP_PID_FILE, F_OK) >= 0){
		printf("%s is alread running.\n",argv[0]);
		LogMsg("%s is alread running.\n",argv[0]);
		return -101;
	}

	ret=cfg_init (&pCfg, cfgfile, 0);
	if (ret!=0){
		LogMsg("Failed to load %s\n",cfgfile);
		return -1;
	}
	LogMsg("Load %s successfully!\n",cfgfile);

	pid_t  pid;
	int status = 0;
	LogMsg("start daemon mode...\n");
	pid = fork();
	if(pid < 0) {
		return -201;
	}

	// grandpa
	if(pid > 0) {
		waitpid(pid, &status, 0);
		LogMsg("grandpa process exit.\n");
		exit(0);
	}

	// father
	pid = fork();
	if(pid < 0) {
		return -203;
	}

	if(pid > 0) {
		LogMsg("father process exit\n");
		exit(0);
	}

	// son
	LogMsg("son(daemon) process running.\n");

	//install signal handler
	signal(SIGINT , ProgExit);
	signal(SIGTERM, ProgExit);

	do_main(pCfg);
	cfg_done(pCfg);

	pid=getpid();
	//Cfg_WriteInt(FFCROP_PID_FILE,"ffcrop","pid",pid);
	osCreatePidFile(FFCROP_PID_FILE,pid);
	LogMsg("%s is created. pid=%d\n",FFCROP_PID_FILE, pid);

	//main loop
	for (;;){

		sleep(10);
	}

	return 0;
}

