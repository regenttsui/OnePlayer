#include "audio.h"


/* 音频解码线程函数，不考虑filter部分，与video_thread几乎一致 */
int audio_thread(void * arg)
{
	MediaState *ms = (MediaState *)arg;
	AVFrame *frame = av_frame_alloc();
	Frame *af;
	int got_frame = 0;
	AVRational tb;
	int ret = 0;

	if (!frame)
		return AVERROR(ENOMEM);

	do {
		if ((got_frame = ms->auddec.decoder_decode_frame(frame, NULL)) < 0)
		{
			av_frame_free(&frame);
			return ret;
		}
			

		if (got_frame) 
		{
			tb = { 1, frame->sample_rate }; //计算时基

			if (!(af = ms->samp_fq.frame_queue_peek_writable()))
			{
				av_frame_free(&frame);
				return ret;
			}				

			af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
			af->pos = frame->pkt_pos;
			af->serial = ms->auddec.get_pkt_serial();
			af->duration = av_q2d({ frame->nb_samples, frame->sample_rate });

			av_frame_move_ref(af->frame, frame);
			ms->samp_fq.frame_queue_push();
		}
	} while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
	
	av_frame_free(&frame);
	return ret;
}

/* 注意，此函数实际上不具备解码的功能，它处理的是已解码的一帧数据，根据需要可能会重采样，最后将其存入ms->audio_buf */
int audio_decode_frame(MediaState * ms)
{
	int data_size, resampled_data_size;
	int64_t dec_channel_layout;
	av_unused double audio_clock0;
	int wanted_nb_samples;
	Frame *af;

	//暂停状态，不用再取数据，返回-1，sdl_audio_callback会处理为输出静音
	if (ms->paused)
		return -1;

	do {
#if defined(_WIN32)
		while (ms->samp_fq.frame_queue_nb_remaining() == 0) 
		{
			if ((av_gettime_relative() - audio_callback_time) > 1000000LL * ms->audio_hw_buf_size / ms->audio_tgt.bytes_per_sec / 2)
				return -1;
			av_usleep(1000);
		}
#endif
		//1. 从sampq取一帧，必要时丢帧，如发生了seek，此时serial会不连续，就需要丢帧处理
		if (!(af = ms->samp_fq.frame_queue_peek_readable()))
			return -1;
		ms->samp_fq.frame_queue_next();
	} while (af->serial != ms->audio_pq.get_serial());

	//2. 计算这一帧的字节数
	data_size = av_samples_get_buffer_size(NULL, af->frame->channels, af->frame->nb_samples, (AVSampleFormat)af->frame->format, 1);

	//计算dec_channel_layout，用于确认是否需要重新初始化重采样（难道af->channel_layout不可靠？不理解）
	dec_channel_layout =
		(af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
		af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
	//wanted_nb_samples = synchronize_audio(ms, af->frame->nb_samples);
	wanted_nb_samples = af->frame->nb_samples; //由于默认同步于音频，所以直接赋值即可

	//判断是否需要重新初始化重采样
	if (af->frame->format != ms->audio_src.fmt ||
		dec_channel_layout != ms->audio_src.channel_layout ||
		af->frame->sample_rate != ms->audio_src.freq ||
		(wanted_nb_samples != af->frame->nb_samples && !ms->swr_ctx)) 
	{
		swr_free(&ms->swr_ctx);
		ms->swr_ctx = swr_alloc_set_opts(NULL,
			ms->audio_tgt.channel_layout, ms->audio_tgt.fmt, ms->audio_tgt.freq,
			dec_channel_layout, (AVSampleFormat)af->frame->format, af->frame->sample_rate,
			0, NULL);
		if (!ms->swr_ctx || swr_init(ms->swr_ctx) < 0) 
		{
			av_log(NULL, AV_LOG_ERROR,
				"Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
				af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->channels,
				ms->audio_tgt.freq, av_get_sample_fmt_name(ms->audio_tgt.fmt), ms->audio_tgt.channels);
			swr_free(&ms->swr_ctx);
			return -1;
		}
		ms->audio_src.channel_layout = dec_channel_layout;
		ms->audio_src.channels = af->frame->channels;
		ms->audio_src.freq = af->frame->sample_rate;
		ms->audio_src.fmt = (AVSampleFormat)af->frame->format;
	}

	//3. 获取这一帧的数据，如果初始化了重采样，则先对这一帧数据进行重采样再输出
	if (ms->swr_ctx) 
	{
		const uint8_t **in = (const uint8_t **)af->frame->extended_data;
		uint8_t **out = &ms->audio_buf1;
		int out_count = (int64_t)wanted_nb_samples * ms->audio_tgt.freq / af->frame->sample_rate + 256;
		int out_size = av_samples_get_buffer_size(NULL, ms->audio_tgt.channels, out_count, ms->audio_tgt.fmt, 0);
		int len2;
		if (out_size < 0) 
		{
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
			return -1;
		}
		if (wanted_nb_samples != af->frame->nb_samples) 
		{
			if (swr_set_compensation(ms->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * ms->audio_tgt.freq / af->frame->sample_rate,
				wanted_nb_samples * ms->audio_tgt.freq / af->frame->sample_rate) < 0) 
			{
				av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
				return -1;
			}
		}
		av_fast_malloc(&ms->audio_buf1, &ms->audio_buf1_size, out_size);
		if (!ms->audio_buf1)
			return AVERROR(ENOMEM);
		len2 = swr_convert(ms->swr_ctx, out, out_count, in, af->frame->nb_samples);
		if (len2 < 0) 
		{
			av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
			return -1;
		}
		if (len2 == out_count) 
		{
			av_log(NULL, AV_LOG_WARNING, "audio buffer ms probably too small\n");
			if (swr_init(ms->swr_ctx) < 0)
				swr_free(&ms->swr_ctx);
		}
		ms->audio_buf = ms->audio_buf1;
		resampled_data_size = len2 * ms->audio_tgt.channels * av_get_bytes_per_sample(ms->audio_tgt.fmt);
	}
	else 
	{
		ms->audio_buf = af->frame->data[0];
		resampled_data_size = data_size;
	}

	//audio_clock0用于打印调试信息
	audio_clock0 = ms->audio_clock;

	//4. 更新audio_clock，audio_clock_serial
	if (!isnan(af->pts))
		ms->audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
	else
		ms->audio_clock = NAN;
	ms->audio_clock_serial = af->serial;
//#ifdef DEBUG
//	{
//		static double last_clock;
//		printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
//			ms->audio_clock - last_clock,
//			ms->audio_clock, audio_clock0);
//		last_clock = ms->audio_clock;
//	}
//#endif
	return resampled_data_size;
}

void update_sample_display(MediaState * ms, short * samples, int samples_size)
{
	int size, len;

	size = samples_size / sizeof(short);
	while (size > 0)
	{
		len = SAMPLE_ARRAY_SIZE - ms->sample_array_index;
		if (len > size)
			len = size;
		memcpy(ms->sample_array + ms->sample_array_index, samples, len * sizeof(short));
		samples += len;
		ms->sample_array_index += len;
		if (ms->sample_array_index >= SAMPLE_ARRAY_SIZE)
			ms->sample_array_index = 0;
		size -= len;
	}
}

void sdl_audio_callback(void * opaque, Uint8 * stream, int len)
{
	MediaState *ms = (MediaState *)opaque; //拿回用户数据
	int audio_size, len1;

	audio_callback_time = av_gettime_relative();//获取当前系统时间

	/*len是每次SDL回调时需要音频的数据量，此处给SDL循环发送需要的数据，直到发够所需数据长度len。
	  stream就是buffer A或者buffer B，这两个buffer都是SDL内部的，而非ms->audio_buf*/
	while (len > 0) 
	{
		//如果audio_buf消耗完了，就调用audio_decode_frame重新填充audio_buf
		if (ms->audio_buf_index >= ms->audio_buf_size) 
		{
			audio_size = audio_decode_frame(ms);
			if (audio_size < 0) 
			{
				/* 出错则静音 */
				ms->audio_buf = NULL;
				ms->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / ms->audio_tgt.frame_size * ms->audio_tgt.frame_size;
			}
			else 
			{
				if (ms->show_mode != SHOW_MODE_VIDEO)
					update_sample_display(ms, (int16_t *)ms->audio_buf, audio_size);
				ms->audio_buf_size = audio_size; //重新设置缓冲区大小
			}
			ms->audio_buf_index = 0; //重置
		}

		//根据缓冲区剩余大小进行处理len1，若剩余的数据多余SDL仍需要的数据，则只需要给SDL发其需要的数据量即可
		len1 = ms->audio_buf_size - ms->audio_buf_index;
		if (len1 > len)
			len1 = len;

		/* 根据audio_volume决定如何输出audio_buf给SDL的数据缓冲区stream。如果audio_volume为最大音量且不静音，
		  则只需复制len1长度数据给stream即可。否则，可以利用SDL_MixAudioFormat进行音量调整和混音再输出给stream */
		if (!ms->muted && ms->audio_buf && ms->audio_volume == SDL_MIX_MAXVOLUME)
			memcpy(stream, (uint8_t *)ms->audio_buf + ms->audio_buf_index, len1);
		else 
		{
			memset(stream, 0, len1);
			if (!ms->muted && ms->audio_buf)
				SDL_MixAudioFormat(stream, (uint8_t *)ms->audio_buf + ms->audio_buf_index, AUDIO_S16SYS, len1, ms->audio_volume);
		}

		//调整各buffer
		len -= len1;                    //仍需发送给SDL的数据减少
		stream += len1;                 //SDL将要输出数据增加
		ms->audio_buf_index += len1;    //已读的缓冲区标记移动
	}
	ms->audio_write_buf_size = ms->audio_buf_size - ms->audio_buf_index;

	/*更新audclk:
	  ms->audio_clock是当前拿到的最新的audio sample的时间戳，在上面的audio_decode_frame函数中计算的。
	  此时，未播放的音频数据包括buffer A 和 buffer B中的数据加上ms->audio_buf中的剩余数据，
	  所以当前播放的时间戳相对于ms->audio_clock要落后(2 * ms->audio_hw_buf_size + ms->audio_write_buf_size) / ms->audio_tgt.bytes_per_sec。
	*/
	if (!isnan(ms->audio_clock)) 
	{
		ms->audclk.set_clock_at(ms->audio_clock - (double)(2 * ms->audio_hw_buf_size + ms->audio_write_buf_size) / ms->audio_tgt.bytes_per_sec, ms->audio_clock_serial, audio_callback_time / 1000000.0);
		ms->sync_clock_to_slave(&ms->extclk, &ms->audclk);
	}
}

/* 打开音频设备，优先尝试请求参数能否打开输出设备，尝试失败后会自动查找最佳的参数重新尝试 */
int audio_open(void * opaque, int64_t wanted_channel_layout, int wanted_nb_channels,
	int wanted_sample_rate, AudioParams * audio_hw_params)
{
	SDL_AudioSpec wanted_spec, spec;
	const char *env;
	static const int next_nb_channels[] = { 0, 0, 1, 6, 2, 6, 4, 6 };
	static const int next_sample_rates[] = { 0, 44100, 48000, 96000, 192000 };
	int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1; //sample_rate数组的最大下标

	//设置各种参数
	env = SDL_getenv("SDL_AUDIO_CHANNELS");
	if (env) 
	{
		wanted_nb_channels = atoi(env);
		wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
	}
	if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) 
	{
		wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
		wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
	}
	wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
	wanted_spec.channels = wanted_nb_channels;
	wanted_spec.freq = wanted_sample_rate;
	if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) 
	{
		av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
		return -1;
	}
	while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
		next_sample_rate_idx--;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.silence = 0;
	//采样数，取512和源音频中1/30秒内包含的samples数中的较大者，后一个式子是取对数再取指数，为了凑成2的整数幂。该值基本上确定了SDL调用callback的频率
	wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
	wanted_spec.callback = sdl_audio_callback;
	wanted_spec.userdata = opaque;  //用户指定参数，此处是前一个函数传入的VideoState

	//打开音频设备，注意这里用的是新的API，失败后会自动查找最佳的参数重新尝试，直到把所有预设的组合遍历完
	while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) 
	{
		av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
			wanted_spec.channels, wanted_spec.freq, SDL_GetError());
		wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
		if (!wanted_spec.channels) 
		{
			wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
			wanted_spec.channels = wanted_nb_channels;
			if (!wanted_spec.freq) 
			{
				av_log(NULL, AV_LOG_ERROR,
					"No more combinations to try, audio open failed\n");
				return -1;
			}
		}
		wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
	}
	if (spec.format != AUDIO_S16SYS) 
	{
		av_log(NULL, AV_LOG_ERROR,
			"SDL advised audio format %d is not supported!\n", spec.format);
		return -1;
	}
	if (spec.channels != wanted_spec.channels) 
	{
		wanted_channel_layout = av_get_default_channel_layout(spec.channels);
		if (!wanted_channel_layout) 
		{
			av_log(NULL, AV_LOG_ERROR,
				"SDL advised channel count %d is not supported!\n", spec.channels);
			return -1;
		}
	}

	//设置最终的实际音频参数
	audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
	audio_hw_params->freq = spec.freq;
	audio_hw_params->channel_layout = wanted_channel_layout;
	audio_hw_params->channels = spec.channels;
	audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
	audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
	if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) 
	{
		av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
		return -1;
	}
	return spec.size;   // 返回的是buffer A和B的Size，赋值给了VideoState->audio_hw_buf_size
}
