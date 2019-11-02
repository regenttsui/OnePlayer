#include "clock.h"

Clock::Clock(int * q_serial, int spd, int pause) : queue_serial(q_serial), speed(spd), paused(pause)
{
	set_clock(NAN, -1);
}

/* 获取一次音/视频的时钟 */
double Clock::get_clock()
{
	if (*queue_serial != serial)
		return NAN;
	if (paused)
	{
		return pts; //如果时钟暂停了，那么直接返回之前的pts不用更新
	}
	else
	{
		double time = av_gettime_relative() / 1000000.0; //获取当前时间
		//由于系统时钟和某时钟速度可能不一致，所以要减去这部分时间差(假设系统时钟速度为1)
		return pts_drift + time - (time - last_updated) * (1.0 - speed);
	}
}

/* 设置一次音/视频时钟 */
void Clock::set_clock_at(double pts, int serial, double time)
{
	this->pts = pts;
	last_updated = time;	//上一次更新时间设为当前时间
	pts_drift = pts - time; //某时钟pts和当前时间的差值，用于下一次校准时钟的pts
	this->serial = serial;
}

/* 使用系统当前时间调用set_clock_at来设置一次时钟，多数设置时钟使用的都是该函数 */
void Clock::set_clock(double pts, int serial)
{
	double time = av_gettime_relative() / 1000000.0;
	set_clock_at(pts, serial, time);
}

int* Clock::get_serial()
{
	return &serial;
}

double Clock::get_pts()
{
	return pts;
}

void Clock::set_paused(int pause)
{
	paused = pause;
}


double Clock::get_last_updated()
{
	return last_updated;
}

/* 设置主时钟速度 */
void Clock::set_clock_speed(double speed)
{
	set_clock(get_clock(), serial);
	this->speed = speed;
}
