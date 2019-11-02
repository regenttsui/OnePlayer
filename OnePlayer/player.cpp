#include "player.h"

Player::Player()
{
	stopped = true;
	seeked = got_dur = already_played = false;
}


void Player::play(MediaState *ms)
{
	if (already_played && stopped)
		m_lock.unlock();

	if (!already_played)
		already_played = true;

	cur_ms = ms;
	stopped = seeked = got_dur = false;
	this->start();
}


Player::~Player()
{
	requestInterruption();//整个程序退出时Player对象析构，在此之前先调用该函数停止run函数里面的工作
	quit();
	wait();
}

bool Player::isStop()
{
	return stopped;
}

void Player::stop()
{
	stopped = true;
	m_lock.lock();
}

void Player::seek(double incr)
{
	seeked = true;
	progress_incr = incr;
}

void Player::run()
{
	double pos, frac, remaining_time = 0.0;
	while (!isInterruptionRequested())
	{
		//获取时长
		if (!got_dur && cur_ms->ic)
		{
			got_dur = true;
			emit gotDuration();
		}
		if (play_finished)
		{
			emit finished();
			play_finished = 0;//跨线程操作的变量，需要线程同步
			continue;
		}
		m_lock.lock();//mutex实现停止播放时休眠，但是这样每次播放都要加锁，很耗费资源，待修改
		//if (stopped)
		//{
		//	continue; //并非真正停止线程
		//}
		if (seeked)
		{
			if (seek_by_bytes)
			{
				pos = -1;
				if (pos < 0 && cur_ms->video_stream >= 0)
					pos = cur_ms->pict_fq.frame_queue_last_pos();
				if (pos < 0 && cur_ms->audio_stream >= 0)
					pos = cur_ms->pict_fq.frame_queue_last_pos();
				if (pos < 0)
					pos = avio_tell(cur_ms->ic->pb);
				if (cur_ms->ic->bit_rate)
					progress_incr *= cur_ms->ic->bit_rate / 8.0;
				else
					progress_incr *= 180000.0;
				pos += progress_incr;
				stream_seek(cur_ms, pos, progress_incr, 1);
			}
			else
			{
				pos = cur_ms->get_master_clock();
				if (isnan(pos))
					pos = (double)cur_ms->seek_pos / AV_TIME_BASE; //先转为秒
				pos += progress_incr; //更新主时钟到快进快退后的时间
				if (cur_ms->ic->start_time != AV_NOPTS_VALUE && pos < cur_ms->ic->start_time / (double)AV_TIME_BASE)
					pos = cur_ms->ic->start_time / (double)AV_TIME_BASE;
				stream_seek(cur_ms, (int64_t)(pos * AV_TIME_BASE), (int64_t)(progress_incr * AV_TIME_BASE), 0);
			}
			seeked = 0;
		}

		if (remaining_time > 0.0)
			av_usleep((int64_t)(remaining_time * 1000000.0));
		remaining_time = REFRESH_RATE;
		if (cur_ms->show_mode != SHOW_MODE_NONE && (!cur_ms->paused || cur_ms->force_refresh))
			video_refresh(cur_ms, &remaining_time); //核心

		m_lock.unlock();
	}
}

void Player::video_refresh(void * opaque, double * remaining_time)

{
	MediaState *ms = (MediaState*)opaque;
	double time;

	Frame *sp, *sp2;

	//if (!ms->paused && ms->get_master_sync_type() == AV_SYNC_EXTERNAL_CLOCK && ms->realtime)
	//	ms->check_external_clock_speed();

	if (ms->show_mode != SHOW_MODE_VIDEO && ms->audio_st)
	{
		time = av_gettime_relative() / 1000000.0;
		if (ms->force_refresh || ms->last_vis_time + rdftspeed < time)
		{
			//video_display(ms);
			emit displayOneFrame();
			ms->last_vis_time = time;
		}
		*remaining_time = FFMIN(*remaining_time, ms->last_vis_time + rdftspeed - time);
	}

	if (ms->video_st)
	{
		do {
			if (ms->pict_fq.frame_queue_nb_remaining() == 0)
			{
				// nothing to do, no picture to display in the queue
			}
			else
			{
				double last_duration, duration, delay;
				Frame *vp, *lastvp;

				lastvp = ms->pict_fq.frame_queue_peek_last();
				vp = ms->pict_fq.frame_queue_peek();

				ms->curr_frame = *lastvp->frame;//保存当前显示的帧

				if (vp->serial != ms->video_pq.get_serial())
				{
					ms->pict_fq.frame_queue_next();
					continue;
				}

				if (lastvp->serial != vp->serial)
					ms->frame_timer = av_gettime_relative() / 1000000.0;

				do {
					if (ms->paused)
						break;

					last_duration = vp_duration(ms, lastvp, vp);
					delay = compute_target_delay(last_duration, ms); //同步的关键

					time = av_gettime_relative() / 1000000.0;
					if (time < ms->frame_timer + delay)
					{
						*remaining_time = FFMIN(ms->frame_timer + delay - time, *remaining_time); //计算还要等待多久才到下一帧
						continue;
					}

					ms->frame_timer += delay;
					if (delay > 0 && time - ms->frame_timer > AV_SYNC_THRESHOLD_MAX)
						ms->frame_timer = time;

					SDL_LockMutex(ms->pict_fq.get_mutex());
					if (!isnan(vp->pts))
						update_video_pts(ms, vp->pts, vp->pos, vp->serial); //更新video clock，同时顺带更新外部时钟，视频同步音频时更新同步时钟其实没作用
					SDL_UnlockMutex(ms->pict_fq.get_mutex());

					if (ms->pict_fq.frame_queue_nb_remaining() > 1)
					{
						Frame *nextvp = ms->pict_fq.frame_queue_peek_next();
						duration = vp_duration(ms, vp, nextvp);
						if (!ms->step && (framedrop > 0 || (framedrop && ms->get_master_sync_type() != AV_SYNC_VIDEO_MASTER)) &&
							time > ms->frame_timer + duration)
						{
							ms->frame_drops_late++;
							ms->pict_fq.frame_queue_next(); //直接标记该帧已读，然后跳回retry
							continue;
						}
					}

					if (ms->subtitle_st)
					{
						while (ms->sub_fq.frame_queue_nb_remaining() > 0)
						{
							sp = ms->sub_fq.frame_queue_peek();

							if (ms->sub_fq.frame_queue_nb_remaining() > 1)
								sp2 = ms->sub_fq.frame_queue_peek_next();
							else
								sp2 = NULL;

							if (sp->serial != ms->subtitle_pq.get_serial()
								|| (ms->vidclk.get_pts() > (sp->pts + ((float)sp->sub.end_display_time / 1000)))
								|| (sp2 && ms->vidclk.get_pts() > (sp2->pts + ((float)sp2->sub.start_display_time / 1000))))
							{
								if (sp->uploaded)
								{
									int i;
									for (i = 0; i < sp->sub.num_rects; i++)
									{
										AVSubtitleRect *sub_rect = sp->sub.rects[i]; //拿到字幕矩形区域
										uint8_t *pixels;
										int pitch, j;

										if (!SDL_LockTexture(ms->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch))
										{
											for (j = 0; j < sub_rect->h; j++, pixels += pitch)
												memset(pixels, 0, sub_rect->w << 2);
											SDL_UnlockTexture(ms->sub_texture);
										}
									}
								}
								ms->sub_fq.frame_queue_next(); //通过直接拿到下一帧实现丢帧
							}
							else
							{
								break;
							}
						}
					}

					ms->pict_fq.frame_queue_next();
					ms->force_refresh = 1;

					if (ms->step && !ms->paused)
						stream_toggle_pause(ms);
				} while (0);
			}
			if (ms->force_refresh && ms->show_mode == SHOW_MODE_VIDEO && ms->pict_fq.get_rindex_shown())
			{
				//video_display(ms);
				emit displayOneFrame();
			}
		} while (0);
	}
	ms->force_refresh = 0;

	if (show_status)
	{
		static int64_t last_time;
		int64_t cur_time;
		int aqsize, vqsize, sqsize;
		double av_diff;

		cur_time = av_gettime_relative();
		if (!last_time || (cur_time - last_time) >= 30000)
		{
			aqsize = 0;
			vqsize = 0;
			sqsize = 0;
			if (ms->audio_st)
				aqsize = ms->audio_pq.get_size();
			if (ms->video_st)
				vqsize = ms->video_pq.get_size();
			if (ms->subtitle_st)
				sqsize = ms->subtitle_pq.get_size();
			av_diff = 0;
			if (ms->audio_st && ms->video_st)
				av_diff = ms->audclk.get_clock() - ms->vidclk.get_clock();
			else if (ms->video_st)
				av_diff = ms->get_master_clock() - ms->vidclk.get_clock();
			else if (ms->audio_st)
				av_diff = ms->get_master_clock() - ms->audclk.get_clock();
			av_log(NULL, AV_LOG_INFO,
				"%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%" PRId64 "/%" PRId64 "   \r",
				ms->get_master_clock(),
				(ms->audio_st && ms->video_st) ? "A-V" : (ms->video_st ? "M-V" : (ms->audio_st ? "M-A" : "   ")),
				av_diff,
				ms->frame_drops_early + ms->frame_drops_late,
				aqsize / 1024,
				vqsize / 1024,
				sqsize,
				ms->video_st ? ms->viddec.get_avctx()->pts_correction_num_faulty_dts : 0,
				ms->video_st ? ms->viddec.get_avctx()->pts_correction_num_faulty_pts : 0);
			fflush(stdout);
			last_time = cur_time;
		}
	}
}
