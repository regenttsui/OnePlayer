#ifndef PLAYER_H_
#define PLAYER_H_


#include <QThread>
#include <QImage>
#include <QTime>
#include <QMutex>
#include <QDebug>
#include "read_stream.h"

class Player : public QThread
{
	Q_OBJECT

public:

	Player();
	~Player();

	bool isStop();

public slots:
	void play(MediaState *ms);
	void stop();
	void seek(double incr);

protected:
	void run() override;

private:
	MediaState *cur_ms;
	bool stopped;
	bool seeked;
	bool already_played;
	QImage image;
	double progress_incr;
	int64_t slider_sec;
	bool got_dur;
	QMutex m_lock;

	void video_refresh(void *opaque, double *remaining_time);

signals:
	void displayOneFrame();
	void gotDuration();
	void finished();
};

#endif // !PLAYER_H_