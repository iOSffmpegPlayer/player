//
//  FSFFPLAYViewController.h
//  TestPlayWithFFMPEGAndSDL
//  参考ffplay.c文件改写
//  Created by  on 12-12-14.
//  Copyright (c) 2012年 __MyCompanyName__. All rights reserved.
//

#import <UIKit/UIKit.h>

#include "config.h"
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include "libavutil/avstring.h"
#include "libavutil/colorspace.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#if CONFIG_AVFILTER
# include "libavfilter/avcodec.h"
# include "libavfilter/avfilter.h"
# include "libavfilter/avfiltergraph.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
#endif

#include <SDL.h>
#include <SDL_thread.h>

#include "cmdutils.h"

#include <assert.h>

#import "KxMovieGLView.h"
#import "KxMovieDecoder.h"

//const int program_birth_year = 2003;

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 5

/* SDL audio buffer size, in samples. Should be small to have precise
 A/V sync as SDL does not have hardware buffer fullness info. */
#define SDL_AUDIO_BUFFER_SIZE 1024

/* no AV sync correction is done if below the AV sync threshold */
#define AV_SYNC_THRESHOLD 0.01
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

static int sws_flags = SWS_BICUBIC;


typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int abort_request;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;



#define VIDEO_PICTURE_QUEUE_SIZE 4
#define SUBPICTURE_QUEUE_SIZE 4

typedef struct VideoPicture {
    double pts;                                  ///< presentation time stamp for this picture
    int64_t pos;                                 ///< byte position in file
    int skip;
    SDL_Overlay *bmp;
    int width, height; /* source height & width */
    AVRational sample_aspect_ratio;
    int allocated;
    int reallocate;
    
#if CONFIG_AVFILTER
    AVFilterBufferRef *picref;
#endif
} VideoPicture;

typedef struct SubPicture {
    double pts; /* presentation time stamp for this picture */
    AVSubtitle sub;
} SubPicture;

typedef struct AudioParams {
    int freq;
    int channels;
    int channel_layout;
    enum AVSampleFormat fmt;
} AudioParams;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct VideoState {
    SDL_Thread *read_tid;
    SDL_Thread *video_tid;
    SDL_Thread *refresh_tid;
    AVInputFormat *iformat;
    int no_background;
    int abort_request;
    int force_refresh;
    int paused;
    int last_paused;
    int que_attachments_req;
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    int read_pause_return;
    AVFormatContext *ic;
    
    int audio_stream;
    
    int av_sync_type;
    double external_clock; /* external clock base */
    int64_t external_clock_time;
    
    double audio_clock;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    PacketQueue audioq;
    int audio_hw_buf_size;
    DECLARE_ALIGNED(16,uint8_t,audio_buf2)[AVCODEC_MAX_AUDIO_FRAME_SIZE * 4];
    uint8_t silence_buf[SDL_AUDIO_BUFFER_SIZE];
    uint8_t *audio_buf;
    uint8_t *audio_buf1;
    unsigned int audio_buf_size; /* in bytes */
    int audio_buf_index; /* in bytes */
    int audio_write_buf_size;
    AVPacket audio_pkt_temp;
    AVPacket audio_pkt;
    struct AudioParams audio_src;
    struct AudioParams audio_tgt;
    struct SwrContext *swr_ctx;
    double audio_current_pts;
    double audio_current_pts_drift;
    int frame_drops_early;
    int frame_drops_late;
    AVFrame *frame;
    
    enum ShowMode {
        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
    } show_mode;
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
    RDFTContext *rdft;
    int rdft_bits;
    FFTSample *rdft_data;
    int xpos;
    
    SDL_Thread *subtitle_tid;
    int subtitle_stream;
    int subtitle_stream_changed;
    AVStream *subtitle_st;
    PacketQueue subtitleq;
    SubPicture subpq[SUBPICTURE_QUEUE_SIZE];
    int subpq_size, subpq_rindex, subpq_windex;
    SDL_mutex *subpq_mutex;
    SDL_cond *subpq_cond;
    
    double frame_timer;
    double frame_last_pts;
    double frame_last_duration;
    double frame_last_dropped_pts;
    double frame_last_returned_time;
    double frame_last_filter_delay;
    int64_t frame_last_dropped_pos;
    double video_clock;                          ///< pts of last decoded frame / predicted pts of next decoded frame
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;
    double video_current_pts;                    ///< current displayed pts (different from video_clock if frame fifos are used)
    double video_current_pts_drift;              ///< video_current_pts - time (av_gettime) at which we updated video_current_pts - used to have running video pts
    int64_t video_current_pos;                   ///< current displayed file pos
    VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int pictq_size, pictq_rindex, pictq_windex;
    SDL_mutex *pictq_mutex;
    SDL_cond *pictq_cond;
#if !CONFIG_AVFILTER
    struct SwsContext *img_convert_ctx;
#endif
    
    char filename[1024];
    int width, height, xleft, ytop;
    int step;
    
#if CONFIG_AVFILTER
    AVFilterContext *in_video_filter;           ///< the first filter in the video chain
    AVFilterContext *out_video_filter;          ///< the last filter in the video chain
    int use_dr1;
    FrameBuffer *buffer_pool;
#endif
    
    int refresh;
    int last_video_stream, last_audio_stream, last_subtitle_stream;
    
    SDL_cond *continue_read_thread;
} VideoState;





@interface FSFFPLAYViewController : UIViewController {
    IBOutlet UIImageView *showImageView;
    
    KxMovieGLView       *_glView;
}

- (void)startPlay;
static NSData * copyFrameData(UInt8 *src, int linesize, int width, int height);

static void imageFromAVPicture(AVPicture pict, int width, int height);

- (void)showVideoThread;

#pragma mark -
#pragma mark 

///

///

void av_noreturn exit_program(int ret);
static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt);
static int packet_queue_put(PacketQueue *q, AVPacket *pkt);
static void packet_queue_init(PacketQueue *q);
static void packet_queue_flush(PacketQueue *q);
static void packet_queue_destroy(PacketQueue *q);
static void packet_queue_abort(PacketQueue *q);
static void packet_queue_start(PacketQueue *q);
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
static inline void fill_rectangle(SDL_Surface *screen,
                                  int x, int y, int w, int h, int color);
static void blend_subrect(AVPicture *dst, const AVSubtitleRect *rect, int imgw, int imgh);
static void free_subpicture(SubPicture *sp);
static void calculate_display_rect(SDL_Rect *rect, int scr_xleft, int scr_ytop, int scr_width, int scr_height, VideoPicture *vp);
static void video_image_display(VideoState *is);

static inline int compute_mod(int a, int b);
static void video_audio_display(VideoState *s);
static void stream_close(VideoState *is);
static void do_exit(VideoState *is);
static void sigterm_handler(int sig);
static int video_open(VideoState *is, int force_set_video_mode);
static void video_display(VideoState *is);
static int refresh_thread(void *opaque);
static double get_audio_clock(VideoState *is);
static double get_video_clock(VideoState *is);
static double get_external_clock(VideoState *is);
static double get_master_clock(VideoState *is);
static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes);
static void stream_toggle_pause(VideoState *is);
static double compute_target_delay(double delay, VideoState *is);
static void pictq_next_picture(VideoState *is);
static void pictq_prev_picture(VideoState *is);
static void update_video_pts(VideoState *is, double pts, int64_t pos);
static void video_refresh(void *opaque);
static void alloc_picture(VideoState *is);
static int queue_picture(VideoState *is, AVFrame *src_frame, double pts1, int64_t pos);
static int get_video_frame(VideoState *is, AVFrame *frame, int64_t *pts, AVPacket *pkt);
static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx);
static int configure_video_filters(AVFilterGraph *graph, VideoState *is, const char *vfilters);

static int video_thread(void *arg);
static int subtitle_thread(void *arg);
static void update_sample_display(VideoState *is, short *samples, int samples_size);
static int synchronize_audio(VideoState *is, int nb_samples);
static int audio_decode_frame(VideoState *is, double *pts_ptr);
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len);
static int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params);
static int stream_component_open(VideoState *is, int stream_index);
static void stream_component_close(VideoState *is, int stream_index);
static int decode_interrupt_cb(void *ctx);
static int read_thread(void *arg)
;
static VideoState *stream_open(const char *filename, AVInputFormat *iformat);
static void stream_cycle_channel(VideoState *is, int codec_type);
static void toggle_full_screen(VideoState *is);
static void toggle_pause(VideoState *is);
static void step_to_next_frame(VideoState *is);
static void toggle_audio_display(VideoState *is);
static void event_loop(VideoState *cur_stream);
static int opt_frame_size(void *optctx, const char *opt, const char *arg);
static int opt_width(void *optctx, const char *opt, const char *arg);
static int opt_height(void *optctx, const char *opt, const char *arg);
static int opt_format(void *optctx, const char *opt, const char *arg);
static int opt_frame_pix_fmt(void *optctx, const char *opt, const char *arg);
static int opt_sync(void *optctx, const char *opt, const char *arg);
static int opt_seek(void *optctx, const char *opt, const char *arg);
static int opt_duration(void *optctx, const char *opt, const char *arg);
static int opt_show_mode(void *optctx, const char *opt, const char *arg);
static void opt_input_file(void *optctx, const char *filename);
static int opt_codec(void *o, const char *opt, const char *arg);
static void show_usage(void);
void show_help_default(const char *opt, const char *arg);
static int lockmgr(void **mtx, enum AVLockOp op);



//
//static int packet_queue_put(PacketQueue *q, AVPacket *pkt);
//static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt);
//static int packet_queue_put(PacketQueue *q, AVPacket *pkt);
//static void packet_queue_init(PacketQueue *q);
//static void packet_queue_flush(PacketQueue *q);
//static void packet_queue_destroy(PacketQueue *q);
//static void packet_queue_abort(PacketQueue *q);
//static void packet_queue_start(PacketQueue *q);
//static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
//static inline void fill_rectangle(SDL_Surface *screen,
//                                  int x, int y, int w, int h, int color, int update);
//static void fill_border(int xleft, int ytop, int width, int height, int x, int y, int w, int h, int color, int update);
//static void blend_subrect(AVPicture *dst, const AVSubtitleRect *rect, int imgw, int imgh);
//static void free_subpicture(SubPicture *sp);
//static void calculate_display_rect(SDL_Rect *rect, int scr_xleft, int scr_ytop, int scr_width, int scr_height, VideoPicture *vp);
//static void video_image_display(VideoState *is);
//static inline int compute_mod(int a, int b);
//static void video_audio_display(VideoState *s);
//static void stream_close(VideoState *is);
//static void do_exit(VideoState *is);
//static void sigterm_handler(int sig);
//static int video_open(VideoState *is, int force_set_video_mode);
//static void video_display(VideoState *is);
//static int refresh_thread(void *opaque);
//static double get_audio_clock(VideoState *is);
//static double get_video_clock(VideoState *is);
//static double get_external_clock(VideoState *is);
//static int get_master_sync_type(VideoState *is);
//static double get_master_clock(VideoState *is);
//static void update_external_clock_pts(VideoState *is, double pts);
//static void check_external_clock_sync(VideoState *is, double pts);
//static void update_external_clock_speed(VideoState *is, double speed);
//static void check_external_clock_speed(VideoState *is);
//static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes);
//static void stream_toggle_pause(VideoState *is);
//static double compute_target_delay(double delay, VideoState *is);
//static void pictq_next_picture(VideoState *is);
//static void pictq_prev_picture(VideoState *is);
//static void update_video_pts(VideoState *is, double pts, int64_t pos, int serial);
//static void video_refresh(void *opaque);
//static void alloc_picture(VideoState *is);
//static int queue_picture(VideoState *is, AVFrame *src_frame, double pts1, int64_t pos, int serial);
//static int get_video_frame(VideoState *is, AVFrame *frame, int64_t *pts, AVPacket *pkt, int *serial);
//static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
//                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx);
//static int configure_video_filters(AVFilterGraph *graph, VideoState *is, const char *vfilters);
//static int video_thread(void *arg);
//static int subtitle_thread(void *arg);
//static void update_sample_display(VideoState *is, short *samples, int samples_size);
//static int synchronize_audio(VideoState *is, int nb_samples);
//static int audio_decode_frame(VideoState *is, double *pts_ptr);
//static void sdl_audio_callback(void *opaque, Uint8 *stream, int len);
//static int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params);
//static int stream_component_open(VideoState *is, int stream_index);
//static void stream_component_close(VideoState *is, int stream_index);
//static int decode_interrupt_cb(void *ctx);
//static int is_realtime(AVFormatContext *s);
//static int read_thread(void *arg);
//static VideoState *stream_open(const char *filename, AVInputFormat *iformat);
//static void stream_cycle_channel(VideoState *is, int codec_type);
//static void toggle_full_screen(VideoState *is);
//static void toggle_pause(VideoState *is);
//static void step_to_next_frame(VideoState *is);
//static void toggle_audio_display(VideoState *is);
//static void event_loop(VideoState *cur_stream);
//static int opt_frame_size(void *optctx, const char *opt, const char *arg);
//static int opt_width(void *optctx, const char *opt, const char *arg);
//static int opt_height(void *optctx, const char *opt, const char *arg);
//static int opt_format(void *optctx, const char *opt, const char *arg);
//static int opt_frame_pix_fmt(void *optctx, const char *opt, const char *arg);
//static int opt_sync(void *optctx, const char *opt, const char *arg);
//static int opt_seek(void *optctx, const char *opt, const char *arg);
//static int opt_duration(void *optctx, const char *opt, const char *arg);
//static int opt_show_mode(void *optctx, const char *opt, const char *arg);
//static void opt_input_file(void *optctx, const char *filename);
//static int opt_codec(void *o, const char *opt, const char *arg);
//static void show_usage(void);
//void show_help_default(const char *opt, const char *arg);
//static int lockmgr(void **mtx, enum AVLockOp op);



@end
