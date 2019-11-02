#include "read_stream.h"

double vp_duration(MediaState *ms, Frame * vp, Frame * nextvp)
{
	if (vp->serial == nextvp->serial)
	{
		double duration = nextvp->pts - vp->pts;
		if (isnan(duration) || duration <= 0 || duration > ms->max_frame_duration)
			return vp->duration;
		else
			return duration;
	}
	else
	{
		return 0.0;
	}
}


double compute_target_delay(double delay, MediaState *ms)
{
	double sync_threshold, diff = 0;


	if (ms->get_master_sync_type() != AV_SYNC_VIDEO_MASTER)
	{

		diff = ms->vidclk.get_clock() - ms->get_master_clock();


		sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
		if (!isnan(diff) && fabs(diff) < ms->max_frame_duration)
		{

			if (diff <= -sync_threshold)
				delay = FFMAX(0, delay + diff);
			else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
				delay = delay + diff;
			else if (diff >= sync_threshold)
				delay = 2 * delay;
		}
	}

	av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n", delay, -diff);

	return delay;
}

void update_video_pts(MediaState * ms, double pts, int64_t pos, int serial)
{
	ms->vidclk.set_clock(pts, serial);
	ms->sync_clock_to_slave(&ms->extclk, &ms->vidclk);
}


void stream_seek(MediaState * ms, int64_t pos, int64_t rel, int seek_by_bytes)
{

	if (!ms->seek_req)
	{
		ms->seek_pos = pos; //设置seek到的位置的绝对时间，微秒
		ms->seek_rel = rel; //设置seek的相对时间(即快进或快退的时间)
		ms->seek_flags &= ~AVSEEK_FLAG_BYTE; //设置seek方法的标志
		if (seek_by_bytes)
			ms->seek_flags |= AVSEEK_FLAG_BYTE;
		ms->seek_req = 1; //设置seek的请求标志
		SDL_CondSignal(ms->continue_read_thread);
	}
}

void stream_toggle_pause(MediaState * ms)
{
	if (ms->paused)
	{
		ms->frame_timer += av_gettime_relative() / 1000000.0 - ms->vidclk.get_last_updated();
		if (ms->read_pause_return != AVERROR(ENOSYS))
		{
			ms->vidclk.set_paused(0); //重启视频时钟
		}
		ms->vidclk.set_clock(ms->vidclk.get_clock(), *ms->vidclk.get_serial());
	}
	ms->extclk.set_clock(ms->extclk.get_clock(), *ms->extclk.get_serial());
	ms->paused = !ms->paused;
	ms->audclk.set_paused(!ms->paused);
	ms->vidclk.set_paused(!ms->paused);
	ms->extclk.set_paused(!ms->paused);
}


void toggle_pause(MediaState * ms)
{
	stream_toggle_pause(ms);
	ms->step = 0; //纯暂停没有跳转到某一帧
}


void toggle_mute(MediaState * ms)
{
	ms->muted = !ms->muted;
}


void update_volume(MediaState * ms, int sign, double step)
{
	double volume_level = ms->audio_volume ? (20 * log(ms->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
	int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
	ms->audio_volume = av_clip(ms->audio_volume == new_volume ? (ms->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}


void step_to_next_frame(MediaState * ms)
{
	if (ms->paused)
		stream_toggle_pause(ms);
	ms->step = 1;
}


void toggle_full_screen(MediaState * ms)
{
	is_full_screen = !is_full_screen;
	SDL_SetWindowFullscreen(win, is_full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}


void toggle_audio_display(MediaState * ms)
{

	int next = ms->show_mode;

	do
	{
		next = (next + 1) % SHOW_MODE_NB;
	} while (next != ms->show_mode && (next == SHOW_MODE_VIDEO && !ms->video_st || next != SHOW_MODE_VIDEO && !ms->audio_st));

	if (ms->show_mode != next)
	{
		ms->force_refresh = 1;
		ms->show_mode = next;
	}
}



int decode_interrupt_cb(void * ctx)
{
	MediaState *ms = (MediaState*)ctx;
	return ms->abort_request;
}


int is_realtime(AVFormatContext * ctx)
{
	if (!strcmp(ctx->iformat->name, "rtp")
		|| !strcmp(ctx->iformat->name, "rtsp")
		|| !strcmp(ctx->iformat->name, "sdp"))
		return 1;

	if (ctx->pb && (!strncmp(ctx->url, "rtp:", 4)
		|| !strncmp(ctx->url, "udp:", 4)))
		return 1;
	return 0;
}

int stream_has_enough_packets(AVStream * st, int stream_id, PacketQueue * queue)
{
	return stream_id < 0 ||
		queue->is_abort() ||
		(st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
		queue->get_nb_packets() > MIN_FRAMES && (!queue->get_duration() || av_q2d(st->time_base) * queue->get_duration() > 1.0);
}


int is_pkt_in_play_range(AVFormatContext * ic, AVPacket * pkt)
{
	if (duration == AV_NOPTS_VALUE)
		return 1;

	int64_t stream_ts = get_pkt_ts(pkt) - get_stream_start_time(ic, pkt->stream_index);
	double stream_ts_s = ts_as_second(stream_ts, ic, pkt->stream_index);

	double ic_ts = stream_ts_s - get_ic_start_time(ic);

	return ic_ts <= ((double)duration / 1000000);
}


int stream_component_open(MediaState *ms, int stream_index)
{
	AVFormatContext *fmt_ctx = ms->ic;
	AVCodecContext *avctx;
	AVCodec *codec;
	int sample_rate, nb_channels;
	int64_t channel_layout = 0;
	int ret = 0;

	if (stream_index < 0 || stream_index >= fmt_ctx->nb_streams)
		return -1;


	avctx = avcodec_alloc_context3(NULL);
	if (!avctx)
		return AVERROR(ENOMEM);

	ret = avcodec_parameters_to_context(avctx, fmt_ctx->streams[stream_index]->codecpar);
	if (ret < 0)
	{
		avcodec_free_context(&avctx);
		return ret;
	}

	avctx->pkt_timebase = fmt_ctx->streams[stream_index]->time_base;

	codec = avcodec_find_decoder(avctx->codec_id);

	switch (avctx->codec_type)
	{
	case AVMEDIA_TYPE_AUDIO:
		ms->last_audio_stream = stream_index;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		ms->last_subtitle_stream = stream_index;
		break;
	case AVMEDIA_TYPE_VIDEO:
		ms->last_video_stream = stream_index;
		break;
	}

	if (!codec)
	{

		av_log(NULL, AV_LOG_WARNING, "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
		ret = AVERROR(EINVAL);
		avcodec_free_context(&avctx);
		return ret;
	}

	if ((ret = avcodec_open2(avctx, codec, NULL)) < 0)
	{
		avcodec_free_context(&avctx);
		return ret;
	}

	ms->eof = 0;
	fmt_ctx->streams[stream_index]->discard = AVDISCARD_DEFAULT;


	switch (avctx->codec_type)
	{
	case AVMEDIA_TYPE_AUDIO:

		sample_rate = avctx->sample_rate;
		nb_channels = avctx->channels;


		if ((ret = audio_open(ms, channel_layout, nb_channels, sample_rate, &ms->audio_tgt)) < 0)
		{
			avcodec_free_context(&avctx);
			return ret;
		}
		ms->audio_hw_buf_size = ret;
		ms->audio_src = ms->audio_tgt;

		ms->audio_buf_size = 0;
		ms->audio_buf_index = 0;

		ms->audio_diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
		ms->audio_diff_avg_count = 0;
		ms->audio_diff_threshold = (double)(ms->audio_hw_buf_size) / ms->audio_tgt.bytes_per_sec;

		ms->audio_stream = stream_index;
		ms->audio_st = fmt_ctx->streams[stream_index];

		//初始化音频的Decoder
		//ms->auddec = Decoder(avctx, &ms->audio_pq, ms->continue_read_thread);
		ms->auddec.decoder_init(avctx, &ms->audio_pq, ms->continue_read_thread);
		if ((ms->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) &&
			!ms->ic->iformat->read_seek)
		{
			ms->auddec.set_start_pts(ms->audio_st->start_time);
			ms->auddec.set_start_pts_tb(ms->audio_st->time_base);
		}
		//启动了PacketQueue，并创建了音频解码线程
		if ((ret = ms->auddec.decoder_start(audio_thread, ms)) < 0)
			return ret;
		SDL_PauseAudioDevice(audio_dev, 0);
		break;
	case AVMEDIA_TYPE_VIDEO:
		ms->video_stream = stream_index;
		ms->video_st = fmt_ctx->streams[stream_index];

		//初始化视频的Decoder，并创建了视频解码线程
		//ms->viddec = Decoder(avctx, &ms->video_pq, ms->continue_read_thread);
		ms->viddec.decoder_init(avctx, &ms->video_pq, ms->continue_read_thread);
		if ((ret = ms->viddec.decoder_start(video_thread, ms)) < 0)
			return ret;
		ms->queue_attachments_req = 1;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		ms->subtitle_stream = stream_index;
		ms->subtitle_st = fmt_ctx->streams[stream_index];

		//初始化字幕的Decoder，并创建了字幕解码线程
		//ms->subdec = Decoder(avctx, &ms->subtitle_pq, ms->continue_read_thread);
		ms->subdec.decoder_init(avctx, &ms->subtitle_pq, ms->continue_read_thread);
		if ((ret = ms->subdec.decoder_start(subtitle_thread, ms)) < 0)
			return ret;
		break;
	default:
		break;
	}

	//avcodec_free_context(&avctx);
	return ret;
}

void stream_component_close(MediaState * ms, int stream_index)
{
	AVFormatContext *ic = ms->ic;
	AVCodecParameters *codecpar;

	if (stream_index < 0 || stream_index >= ic->nb_streams)
		return;
	codecpar = ic->streams[stream_index]->codecpar;

	switch (codecpar->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		ms->auddec.decoder_abort(&ms->samp_fq);
		SDL_CloseAudioDevice(audio_dev);
		ms->auddec.decoder_destroy();
		swr_free(&ms->swr_ctx);
		av_freep(&ms->audio_buf1);
		ms->audio_buf1_size = 0;
		ms->audio_buf = NULL;
		break;
	case AVMEDIA_TYPE_VIDEO:
		ms->viddec.decoder_abort(&ms->pict_fq);
		ms->viddec.decoder_destroy();
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		ms->subdec.decoder_abort(&ms->sub_fq);
		ms->subdec.decoder_destroy();
		break;
	default:
		break;
	}

	ic->streams[stream_index]->discard = AVDISCARD_ALL;
	switch (codecpar->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		ms->audio_st = NULL;
		ms->audio_stream = -1;
		break;
	case AVMEDIA_TYPE_VIDEO:
		ms->video_st = NULL;
		ms->video_stream = -1;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		ms->subtitle_st = NULL;
		ms->subtitle_stream = -1;
		break;
	default:
		break;
	}
}

int read_thread(void * arg)
{
	MediaState *ms = (MediaState*)arg;
	AVFormatContext *fmt_ctx = NULL;
	int err, i, ret;
	int st_index[AVMEDIA_TYPE_NB];			   //储存流的索引的数组
	AVPacket pkt1, *pkt = &pkt1;
	int pkt_in_play_range = 0;
	SDL_mutex *wait_mutex = SDL_CreateMutex(); //创建互斥量
	int infinite_buffer = -1;
	SDL_Event event;						   //失败时使用的事件

	if (!wait_mutex)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		event.type = FF_QUIT_EVENT;
		event.user.data1 = ms;
		SDL_PushEvent(&event);
		return -1;
	}

	//设置MediaState的一些初始值
	memset(st_index, -1, sizeof(st_index));
	ms->last_video_stream = ms->video_stream = -1;
	ms->last_audio_stream = ms->audio_stream = -1;
	ms->last_subtitle_stream = ms->subtitle_stream = -1;
	ms->eof = 0;

	//创建输入上下文
	fmt_ctx = avformat_alloc_context();
	if (!fmt_ctx)
	{
		av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
		event.type = FF_QUIT_EVENT;
		event.user.data1 = ms;
		SDL_PushEvent(&event);
		SDL_DestroyMutex(wait_mutex);
		return -1;
	}

	fmt_ctx->interrupt_callback.callback = decode_interrupt_cb;
	fmt_ctx->interrupt_callback.opaque = ms;

	err = avformat_open_input(&fmt_ctx, ms->filename, NULL, NULL);
	if (err < 0)
	{
		if (fmt_ctx && !ms->ic)
			avformat_close_input(&fmt_ctx);

		event.type = FF_QUIT_EVENT;
		event.user.data1 = ms;
		SDL_PushEvent(&event);
		SDL_DestroyMutex(wait_mutex);
		return -1;
	}

	ms->ic = fmt_ctx;

	err = avformat_find_stream_info(fmt_ctx, NULL);
	if (err < 0)
	{
		av_log(NULL, AV_LOG_WARNING,
			"%s: could not find codec parameters\n", ms->filename);
		if (fmt_ctx && !ms->ic)
			avformat_close_input(&fmt_ctx);

		event.type = FF_QUIT_EVENT;
		event.user.data1 = ms;
		SDL_PushEvent(&event);
		SDL_DestroyMutex(wait_mutex);
		return -1;
	}

	if (fmt_ctx->pb)
		fmt_ctx->pb->eof_reached = 0;

	if (seek_by_bytes < 0)
		seek_by_bytes = !!(fmt_ctx->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", fmt_ctx->iformat->name);

	ms->max_frame_duration = (fmt_ctx->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

	ms->realtime = is_realtime(fmt_ctx);

	if (show_status)
		av_dump_format(fmt_ctx, 0, ms->filename, 0);

	st_index[AVMEDIA_TYPE_VIDEO] = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
	st_index[AVMEDIA_TYPE_AUDIO] = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, st_index[AVMEDIA_TYPE_AUDIO],
		st_index[AVMEDIA_TYPE_VIDEO], NULL, 0);
	st_index[AVMEDIA_TYPE_SUBTITLE] = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_SUBTITLE, st_index[AVMEDIA_TYPE_SUBTITLE],
		(st_index[AVMEDIA_TYPE_AUDIO] >= 0 ? st_index[AVMEDIA_TYPE_AUDIO] : st_index[AVMEDIA_TYPE_VIDEO]), NULL, 0);

	ms->show_mode = SHOW_MODE_NONE;

	if (st_index[AVMEDIA_TYPE_VIDEO] >= 0)
	{
		AVStream *st = fmt_ctx->streams[st_index[AVMEDIA_TYPE_VIDEO]];
		AVCodecParameters *codecpar = st->codecpar;
		AVRational sar = av_guess_sample_aspect_ratio(fmt_ctx, st, NULL);
		if (codecpar->width)
			set_default_window_size(codecpar->width, codecpar->height, sar);
	}

	if (st_index[AVMEDIA_TYPE_AUDIO] >= 0)
		stream_component_open(ms, st_index[AVMEDIA_TYPE_AUDIO]);

	ret = -1;
	if (st_index[AVMEDIA_TYPE_VIDEO] >= 0)
		ret = stream_component_open(ms, st_index[AVMEDIA_TYPE_VIDEO]);

	if (ms->show_mode == SHOW_MODE_NONE)
		ms->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;

	if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0)
		stream_component_open(ms, st_index[AVMEDIA_TYPE_SUBTITLE]);

	if (ms->video_stream < 0 && ms->audio_stream < 0)
	{
		av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
			ms->filename);
		if (fmt_ctx && !ms->ic)
			avformat_close_input(&fmt_ctx);

		event.type = FF_QUIT_EVENT;
		event.user.data1 = ms;
		SDL_PushEvent(&event);
		SDL_DestroyMutex(wait_mutex);
		return -1;
	}

	if (ms->realtime)
		infinite_buffer = 1;

	while (true)
	{
		if (ms->abort_request)
			break;

		if (ms->paused != ms->last_paused)
		{
			ms->last_paused = ms->paused;
			if (ms->paused)
				ms->read_pause_return = av_read_pause(fmt_ctx);
			else
				av_read_play(fmt_ctx);
		}

		if (ms->seek_req)
		{
			int64_t seek_target = ms->seek_pos;
			int64_t seek_min = ms->seek_rel > 0 ? seek_target - ms->seek_rel + 2 : INT64_MIN;
			int64_t seek_max = ms->seek_rel < 0 ? seek_target - ms->seek_rel - 2 : INT64_MAX;

			ret = avformat_seek_file(ms->ic, -1, seek_min, seek_target, seek_max, ms->seek_flags);

			if (ret < 0)
			{
				av_log(NULL, AV_LOG_ERROR, "%s: error while seeking\n", ms->ic->url);
			}
			else
			{
				if (ms->audio_stream >= 0)
				{
					ms->audio_pq.packet_queue_flush();
					ms->audio_pq.packet_queue_put(&flush_pkt);
				}
				if (ms->subtitle_stream >= 0)
				{
					ms->subtitle_pq.packet_queue_flush();
					ms->subtitle_pq.packet_queue_put(&flush_pkt);
				}
				if (ms->video_stream >= 0)
				{
					ms->video_pq.packet_queue_flush();
					ms->video_pq.packet_queue_put(&flush_pkt);
				}
				if (ms->seek_flags & AVSEEK_FLAG_BYTE)
				{
					ms->extclk.set_clock(NAN, 0);
				}
				else
				{
					ms->extclk.set_clock(seek_target / (double)AV_TIME_BASE, 0);
				}
			}
			ms->seek_req = 0;
			ms->queue_attachments_req = 1;
			ms->eof = 0;

			if (ms->paused)
				step_to_next_frame(ms);
		}

		if (ms->queue_attachments_req)
		{
			if (ms->video_st && ms->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)
			{
				AVPacket copy = { 0 };
				if ((ret = av_packet_ref(&copy, &ms->video_st->attached_pic)) < 0)
				{
					if (fmt_ctx && !ms->ic)
						avformat_close_input(&fmt_ctx);

					if (ret != 0)
					{
						SDL_Event event;

						event.type = FF_QUIT_EVENT;
						event.user.data1 = ms;
						SDL_PushEvent(&event);
					}
					SDL_DestroyMutex(wait_mutex);
					return -1;
				}
				ms->video_pq.packet_queue_put(&copy);
				ms->video_pq.packet_queue_put_nullpacket(ms->video_stream);
			}
			ms->queue_attachments_req = 0;
		}

		if (infinite_buffer < 1 &&
			(ms->audio_pq.get_size() + ms->video_pq.get_size() + ms->subtitle_pq.get_size() > MAX_QUEUE_SIZE
				|| (stream_has_enough_packets(ms->audio_st, ms->audio_stream, &ms->audio_pq) &&
					stream_has_enough_packets(ms->video_st, ms->video_stream, &ms->video_pq) &&
					stream_has_enough_packets(ms->subtitle_st, ms->subtitle_stream, &ms->subtitle_pq))))
		{
			SDL_LockMutex(wait_mutex);
			SDL_CondWaitTimeout(ms->continue_read_thread, wait_mutex, 10);
			SDL_UnlockMutex(wait_mutex);
			continue;
		}

		if (!ms->paused &&
			(!ms->audio_st || (ms->auddec.is_finished() == ms->audio_pq.get_serial() && ms->samp_fq.frame_queue_nb_remaining() == 0)) &&
			(!ms->video_st || (ms->viddec.is_finished() == ms->video_pq.get_serial() && ms->pict_fq.frame_queue_nb_remaining() == 0)))
		{
			play_finished = 1;
			return -1;
			//if (allow_loop())
			//{
			//	stream_seek(ms, start_time != AV_NOPTS_VALUE ? start_time : 0, 0, 0);
			//}
			//else if (autoexit)
			//{
			//	ret = AVERROR_EOF;
			//	if (fmt_ctx && !ms->ic)
			//		avformat_close_input(&fmt_ctx);

			//	if (ret != 0)
			//	{
			//		SDL_Event event;

			//		event.type = FF_QUIT_EVENT;
			//		event.user.data1 = ms;
			//		SDL_PushEvent(&event);
			//	}
			//	SDL_DestroyMutex(wait_mutex);
			//	return -1;
			//}
		}

		ret = av_read_frame(fmt_ctx, pkt);

		if (ret < 0)
		{
			if ((ret == AVERROR_EOF || avio_feof(fmt_ctx->pb)) && !ms->eof)
			{
				if (ms->video_stream >= 0)
					ms->video_pq.packet_queue_put_nullpacket(ms->video_stream);
				if (ms->audio_stream >= 0)
					ms->audio_pq.packet_queue_put_nullpacket(ms->audio_stream);
				if (ms->subtitle_stream >= 0)
					ms->subtitle_pq.packet_queue_put_nullpacket(ms->subtitle_stream);
				ms->eof = 1;
			}
			if (fmt_ctx->pb && fmt_ctx->pb->error)
				break;
			SDL_LockMutex(wait_mutex);
			SDL_CondWaitTimeout(ms->continue_read_thread, wait_mutex, 10);
			SDL_UnlockMutex(wait_mutex);
			continue;
		}
		else
		{
			ms->eof = 0;
		}

		pkt_in_play_range = is_pkt_in_play_range(fmt_ctx, pkt);

		if (pkt->stream_index == ms->audio_stream && pkt_in_play_range)
		{
			ms->audio_pq.packet_queue_put(pkt);
		}
		else if (pkt->stream_index == ms->video_stream && pkt_in_play_range
			&& !(ms->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC))
		{
			ms->video_pq.packet_queue_put(pkt);
		}
		else if (pkt->stream_index == ms->subtitle_stream && pkt_in_play_range)
		{
			ms->subtitle_pq.packet_queue_put(pkt);
		}
		else
		{
			av_packet_unref(pkt);
		}
	}

	ret = 0;
	if (fmt_ctx && !ms->ic)
		avformat_close_input(&fmt_ctx);

	if (ret != 0)
	{
		SDL_Event event;

		event.type = FF_QUIT_EVENT;
		event.user.data1 = ms;
		SDL_PushEvent(&event);
	}
	SDL_DestroyMutex(wait_mutex);
	return 0;
}

void stream_close(MediaState * ms)
{
	ms->abort_request = 1;
	SDL_WaitThread(ms->read_tid, NULL);

	if (ms->audio_stream >= 0)
		stream_component_close(ms, ms->audio_stream);
	if (ms->video_stream >= 0)
		stream_component_close(ms, ms->video_stream);
	if (ms->subtitle_stream >= 0)
		stream_component_close(ms, ms->subtitle_stream);

	avformat_close_input(&ms->ic);

	ms->video_pq.packet_queue_destroy();
	ms->audio_pq.packet_queue_destroy();
	ms->subtitle_pq.packet_queue_destroy();
	ms->pict_fq.frame_queue_destroy();
	ms->samp_fq.frame_queue_destroy();
	ms->sub_fq.frame_queue_destroy();

	SDL_DestroyCond(ms->continue_read_thread);
	sws_freeContext(ms->img_convert_ctx);
	sws_freeContext(ms->sub_convert_ctx);
	av_free(ms->filename);
	if (ms->vis_texture)
		SDL_DestroyTexture(ms->vis_texture);
	if (ms->vid_texture)
		SDL_DestroyTexture(ms->vid_texture);
	if (ms->sub_texture)
		SDL_DestroyTexture(ms->sub_texture);
	av_free(ms);
}


MediaState *stream_open(const char * filename)
{
	MediaState *ms;
	ms = (MediaState *)av_mallocz(sizeof(MediaState));
	if (!ms)
		return NULL;

	ms->filename = av_strdup(filename); //拷贝文件名字符串
	if (!ms->filename)
	{
		stream_close(ms);
		return NULL;
	}

	ms->video_pq.packet_queue_init();
	ms->subtitle_pq.packet_queue_init();
	ms->audio_pq.packet_queue_init();
	if (!ms->video_pq.is_construct() || !ms->subtitle_pq.is_construct() || !ms->audio_pq.is_construct())
	{
		stream_close(ms);
		return NULL;
	}

	//ms->pict_fq = FrameQueue(&ms->video_pq, VIDEO_PICTURE_QUEUE_SIZE, 1);
	//ms->sub_fq = FrameQueue(&ms->subtitle_pq, SUBPICTURE_QUEUE_SIZE, 0);
	//ms->samp_fq = FrameQueue(&ms->audio_pq, SAMPLE_QUEUE_SIZE, 1);
	ms->pict_fq.frame_queue_init(&ms->video_pq, VIDEO_PICTURE_QUEUE_SIZE, 1);
	ms->sub_fq.frame_queue_init(&ms->subtitle_pq, SUBPICTURE_QUEUE_SIZE, 0);
	ms->samp_fq.frame_queue_init(&ms->audio_pq, SAMPLE_QUEUE_SIZE, 1);

	int video_serial = ms->video_pq.get_serial(), audio_serial = ms->audio_pq.get_serial();
	ms->vidclk = Clock(&video_serial);
	ms->audclk = Clock(&audio_serial);
	ms->extclk = Clock(ms->extclk.get_serial());
	ms->audio_clock_serial = -1;

	int startup_volume = STARTUP_VOLUME;
	startup_volume = av_clip(startup_volume, 0, 100);
	startup_volume = av_clip(SDL_MIX_MAXVOLUME * startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
	ms->audio_volume = startup_volume;		  //设置初始音量,后期应该和上次的一致
	ms->muted = 0;							  //不静音
	ms->av_sync_type = AV_SYNC_AUDIO_MASTER;  //默认视频同步于音频

	if (!(ms->continue_read_thread = SDL_CreateCond()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		stream_close(ms);
		return NULL;
	}

	ms->read_tid = SDL_CreateThread(read_thread, "read_thread", ms);
	if (!ms->read_tid)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
		stream_close(ms);
		return NULL;
	}

	return ms;
}

void do_exit()
{
	//if (ms)
	//{
	//	stream_close(ms);
	//}
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (win)
		SDL_DestroyWindow(win);

	avformat_network_deinit();
	if (show_status)
		printf("\n");
	SDL_Quit();
	av_log(NULL, AV_LOG_QUIET, "%s", "");
	exit(0);
}