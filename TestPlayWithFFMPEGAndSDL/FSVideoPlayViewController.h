//
//  FSVideoPlayViewController.h
//  TestPlayWithFFMPEGAndSDL
//
//  Created by  on 12-12-13.
//  Copyright (c) 2012年 __MyCompanyName__. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <libavformat/avformat.h>
#import <libavcodec/avcodec.h>
#import <libswscale/swscale.h>
#include "SDL.h"
#include <SDL_thread.h>
#import <stdio.h>
#import <stdlib.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)
#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0
#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20
#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT (SDL_USEREVENT + 2)
#define VIDEO_PICTURE_QUEUE_SIZE 1
#define DEFAULT_AV_SYNC_TYPE AV_SYNC_VIDEO_MASTER

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;
typedef struct VideoPicture {
    SDL_Overlay *bmp;
    int width, height; /* source height & width */
    int allocated;
    double pts;
} VideoPicture;
typedef struct VideoState {
    
    AVFormatContext *pFormatCtx;
    int             videoStream, audioStream;
    
    int             av_sync_type;
    double          external_clock; /* external clock base */
    int64_t         external_clock_time;
    int             seek_req;
    int             seek_flags;
    int64_t         seek_pos;
    double          audio_clock;
    AVStream        *audio_st;
    PacketQueue     audioq;
    DECLARE_ALIGNED(16, uint8_t, audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2]);
    unsigned int    audio_buf_size;
    unsigned int    audio_buf_index;
    AVPacket        audio_pkt;
    uint8_t         *audio_pkt_data;
    int             audio_pkt_size;
    int             audio_hw_buf_size;  
    double          audio_diff_cum; /* used for AV difference average computation */
    double          audio_diff_avg_coef;
    double          audio_diff_threshold;
    int             audio_diff_avg_count;
    double          frame_timer;
    double          frame_last_pts;
    double          frame_last_delay;
    double          video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
    double          video_current_pts; ///<current displayed pts (different from video_clock if frame fifos are used)
    int64_t         video_current_pts_time;  ///<time (av_gettime) at which we updated video_current_pts - used to have running video pts
    AVStream        *video_st;
    PacketQueue     videoq;
    
    VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int             pictq_size, pictq_rindex, pictq_windex;
    SDL_mutex       *pictq_mutex;
    SDL_cond        *pictq_cond;
    SDL_Thread      *parse_tid;
    SDL_Thread      *video_tid;
    char            filename[1024];
    int             quit;
} VideoState;
enum {
    AV_SYNC_AUDIO_MASTER,
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_MASTER,
};

@interface FSVideoPlayViewController : UIViewController {
    
}

- (void)startPlayVideo;

#pragma mark -
#pragma c

void packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
static void packet_queue_flush(PacketQueue *q);
double get_audio_clock(VideoState *is);
double get_video_clock(VideoState *is);
double get_external_clock(VideoState *is);
double get_master_clock(VideoState *is);
int synchronize_audio(VideoState *is, short *samples,
                      int samples_size, double pts);
int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr);
void audio_callback(void *userdata, Uint8 *stream, int len);
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque);
static void schedule_refresh(VideoState *is, int delay);
void video_display(VideoState *is);
void video_refresh_timer(void *userdata);
void alloc_picture(void *userdata);
int queue_picture(VideoState *is, AVFrame *pFrame, double pts);
double synchronize_video(VideoState *is, AVFrame *src_frame, double pts);
int our_get_buffer(struct AVCodecContext *c, AVFrame *pic);
void our_release_buffer(struct AVCodecContext *c, AVFrame *pic);
int video_thread(void *arg);
int stream_component_open(VideoState *is, int stream_index);
int decode_interrupt_cb(void);
int decode_thread(void *arg);
void stream_seek(VideoState *is, int64_t pos, int rel);



@end
