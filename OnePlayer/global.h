#ifndef GLOBAL_H_
#define GLOBAL_H_

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#include <SDL.h>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
};

constexpr auto MAX_QUEUE_SIZE = (15 * 1024 * 1024);
constexpr auto MIN_FRAMES = 25;
constexpr auto EXTERNAL_CLOCK_MIN_FRAMES = 2;
constexpr auto EXTERNAL_CLOCK_MAX_FRAMES = 10;

/* Minimum SDL audio buffer size, in samples. */
constexpr auto SDL_AUDIO_MIN_BUFFER_SIZE = 512;
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
constexpr auto SDL_AUDIO_MAX_CALLBACKS_PER_SEC = 30;

/* Step size for volume control in dB */
constexpr auto SDL_VOLUME_STEP = (0.75);

/* Step size for volume control in PERCENT */
constexpr auto SDL_VOLUME_PER = 5;

constexpr auto STARTUP_VOLUME = 50;

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
constexpr auto AUDIO_DIFF_AVG_NB = 20;

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
constexpr auto REFRESH_RATE = 0.01;

constexpr auto CURSOR_HIDE_DELAY = 1000000;

constexpr auto USE_ONEPASS_SUBTITLE_RENDER = 1;

constexpr auto FF_QUIT_EVENT = (SDL_USEREVENT + 2);    //自定义的退出事件

extern unsigned sws_flags;    //图像转换默认算法

extern AVPacket flush_pkt;  //刷新用的packet

extern SDL_Window *win;
extern SDL_Renderer *renderer;
extern SDL_RendererInfo renderer_info;
extern SDL_AudioDeviceID audio_dev;

/* 实际上是用户可指定的选项 */
extern const char *input_filename;
extern const char *window_title;
extern int default_width;
extern int default_height;
extern int screen_width;
extern int screen_height;
extern int loop;
extern int autoexit;
extern int framedrop;
extern int show_status;
extern double rdftspeed;
extern int64_t start_time;
extern int64_t duration;
extern int64_t cursor_last_shown;
extern int cursor_hidden;
extern int exit_on_keydown;
extern int exit_on_mousedown;
extern int is_full_screen;
extern int seek_by_bytes;
extern double seek_interval;
extern int64_t audio_callback_time; //暂时作为全局变量
extern int play_finished;


/* 通用Frame结构，包含了解码的视音频和字幕数据 */
struct Frame
{
	AVFrame *frame;       //解码的视频或音频数据
	AVSubtitle sub;       //解码的字幕数据
	int serial;           //序列号 
	double pts;           //显示时间戳
	double duration;      //估计的一帧持续的时间
	int64_t pos;          //该frame在输入文件中的字节偏移位置
	int width;            //字幕的宽高？
	int height;
	int format;
	AVRational sar;		  //宽高比？
	int uploaded;
	int flip_v;           //翻转视频？
};

/* 音频参数，用于复制SDL中与FFmpeg兼容的参数并加上符合FFmpeg的参数 */
struct AudioParams
{
	int freq;
	int channels;
	int64_t channel_layout;
	enum AVSampleFormat fmt;
	int frame_size;
	int bytes_per_sec;
};

enum ShowMode
{
	SHOW_MODE_NONE = -1,
	SHOW_MODE_VIDEO = 0,
	SHOW_MODE_WAVES,
	SHOW_MODE_RDFT,
	SHOW_MODE_NB
};

#endif // !GLOBAL_H_

