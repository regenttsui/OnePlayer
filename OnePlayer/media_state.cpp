#include "media_state.h"

/* 检测FrameQueue是否可读，然后对Frame的相关变量进行赋值并写入队列 */
int MediaState::queue_picture(AVFrame * src_frame, double pts, double duration, int64_t pos, int serial)
{
	Frame *vp;

//#if defined(DEBUG_SYNC)
//	printf("frame_type=%c pts=%0.3f\n",
//		av_get_picture_type_char(src_frame->pict_type), pts);
//#endif

	//取FrameQueue的当前可写的节点赋给vp
	if (!(vp = pict_fq.frame_queue_peek_writable()))
		return -1;

	//拷贝解码好的帧的相关数据给节点保存
	vp->sar = src_frame->sample_aspect_ratio;
	vp->uploaded = 0;

	vp->width = src_frame->width;
	vp->height = src_frame->height;
	vp->format = src_frame->format;

	vp->pts = pts;
	vp->duration = duration;
	vp->pos = pos;
	vp->serial = serial;

	set_default_window_size(vp->width, vp->height, vp->sar);

	//AVFrame的拷贝是通过av_frame_move_ref实现的，所以拷贝后src_frame就是无效的了
	av_frame_move_ref(vp->frame, src_frame);

	//push写好的节点到队列
	pict_fq.frame_queue_push();
	return 0;

}

/* 获取主时钟的类型 */
int MediaState::get_master_sync_type()
{
	if (av_sync_type == AV_SYNC_VIDEO_MASTER) 
	{
		if (video_st)
			return AV_SYNC_VIDEO_MASTER;
		else
			return AV_SYNC_AUDIO_MASTER;
	}
	else if (av_sync_type == AV_SYNC_AUDIO_MASTER) 
	{
		if (audio_st)
			return AV_SYNC_AUDIO_MASTER;
		else
			return AV_SYNC_EXTERNAL_CLOCK;
	}
	else 
	{
		return AV_SYNC_EXTERNAL_CLOCK;
	}
}

/* 获取主时钟的当前值 */
double MediaState::get_master_clock()
{
	double val;
	switch (get_master_sync_type()) 
	{
	case AV_SYNC_VIDEO_MASTER:
		val = vidclk.get_clock();
		break;
	case AV_SYNC_AUDIO_MASTER:
		val = audclk.get_clock();
		break;
	default:
		val = extclk.get_clock();
		break;
	}
	return val;
}

/* 将c时钟与slave时钟同步(why?) */
void MediaState::sync_clock_to_slave(Clock * c, Clock * slave)
{
	double clock = c->get_clock();
	double slave_clock = slave->get_clock();
	if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
		c->set_clock(slave_clock, *slave->get_serial());
}

