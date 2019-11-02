#ifndef MEDIA_STATE_H_
#define MEDIA_STATE_H_

#include "decoder.h"
#include "clock.h"
#include "sdl_utility.h"

constexpr auto SAMPLE_ARRAY_SIZE = (8 * 65536);

//总控数据结构，把其他核心数据结构整合在一起，起一个中转的作用，便于在各个子结构之间跳转。
struct MediaState
{
	//MediaState() = default;
	//~MediaState() = default;
	int queue_picture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);
	int get_master_sync_type();
	double get_master_clock();
	void sync_clock_to_slave(Clock *c, Clock *slave);

	SDL_Thread *read_tid;   //解复用、读媒体文件流的线程，得到AVPacket，并对packet入栈
	int abort_request;      //异常退出请求标记
	int force_refresh;      //强制刷新
	int paused;             //当前控制视频暂停或播放标志位
	int last_paused;        //上一次paused标志位的值
	int queue_attachments_req;  // 队列附件请求？
	int seek_req;           // seek的请求标志，seek其实包括了快进快退和直接点击/拖动进度条两者方式
	int seek_flags;         // seek的方法
	int64_t seek_pos;       // seek位置，应该是seek后位置的绝对时间，微秒
	int64_t seek_rel;       // seek的相对时间(即快进或快退的时间)
	int read_pause_return;
	AVFormatContext *ic;    //输入文件格式上下文指针
	int realtime;           // 是否实时码流，应该指的是网络上的流媒体

	Clock audclk;           //音频时钟
	Clock vidclk;           //视频时钟
	Clock extclk;           //外部时钟

	FrameQueue pict_fq;       //视频frame队列
	FrameQueue sub_fq;       //字幕frame队列
	FrameQueue samp_fq;       //音频frame队列

	Decoder auddec;         //音频解码器
	Decoder viddec;         //视频解码器
	Decoder subdec;         //字幕解码器

	int av_sync_type;       // 视音频同步类型

	double audio_clock;     //音频时钟值
	int audio_clock_serial; // 音频时钟序列号？
	double audio_diff_cum; /* used for AV difference average computation 用于音频差分计算*/
	double audio_diff_avg_coef;
	double audio_diff_threshold;
	int audio_diff_avg_count;
	int audio_stream;       //音频流索引
	AVStream *audio_st;     //音频流指针
	PacketQueue audio_pq;     //音频packet队列
	int audio_hw_buf_size;   // 硬件缓冲区(即声卡取数据播放的地方)大小
	uint8_t *audio_buf;     //从要输出的AVFrame中取出的音频数据（PCM），如果有必要，则对该数据重采样
	uint8_t *audio_buf1;    //另一个缓冲区？
	unsigned int audio_buf_size; /*audio_buf的总大小 in bytes*/
	unsigned int audio_buf1_size;
	int audio_buf_index;    /*下一次可读的audio_buf的index位置 in bytes */
	int audio_write_buf_size;   // audio_buf剩余未输出的数据大小，即audio_buf_size - audio_buf_index
	int audio_volume;       //音频音量
	int muted;              // 是否静音
	struct AudioParams audio_src;   // 源音频参数
	struct AudioParams audio_tgt;   // 目标音频参数
	struct SwrContext *swr_ctx;     // 音频转换上下文
	int frame_drops_early;
	int frame_drops_late;

	//显示类型
	int show_mode;
	int16_t sample_array[SAMPLE_ARRAY_SIZE];    // 采样数组
	int sample_array_index; // 采样索引
	int last_i_start;
	//RDFTContext *rdft;      // 自适应滤波器上下文
	//int rdft_bits;
	//FFTSample *rdft_data;   // 快速傅里叶采样
	int xpos;
	double last_vis_time;
	SDL_Texture *vis_texture;   // 音频Texture
	SDL_Texture *sub_texture;   // 字幕Texture
	SDL_Texture *vid_texture;   // 视频频Texture

	int subtitle_stream;        //字幕流索引
	AVStream *subtitle_st;      //字幕流指针
	PacketQueue subtitle_pq;      //字幕packet队列

	double frame_timer;         // 帧计时器，可以理解为帧显示的时刻
	double frame_last_returned_time;    // 上一次返回时间
	double frame_last_filter_delay;     // 上一个过滤器延时
	int video_stream;       //视频流索引
	AVStream *video_st;     //视频流指针
	PacketQueue video_pq;     //视频packet队列
	double max_frame_duration;      // 一帧最大的显示时间
	struct SwsContext *img_convert_ctx; // 视频转换上下文
	struct SwsContext *sub_convert_ctx; // 字幕转换上下文
	int eof;             // 结束标志

	char *filename;     //媒体文件名
	int width, height, xleft, ytop;     //视频宽高和左上角位置
	int step;           // 步进

	// 上一个视频码流/音频码流/字幕码流的索引
	int last_video_stream, last_audio_stream, last_subtitle_stream;

	SDL_cond *continue_read_thread; // 读线程条件变量

	AVFrame curr_frame; //保存当前显示的帧
};


#endif // !MEDIA_STATE_H_

