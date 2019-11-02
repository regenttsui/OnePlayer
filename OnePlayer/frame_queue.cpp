#include "frame_queue.h"

/* 初始化Frame队列 */
FrameQueue::FrameQueue(PacketQueue *pkt_q, int m_size, int keep_lst, bool suc) :success(suc)
{
	//清零，建立互斥量和条件变量
	if (!(mutex = SDL_CreateMutex()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		success = false;
	}
	if (!(cond = SDL_CreateCond()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		success = false;
	}

	//跟相应的packet队列关联
	pktq = pkt_q;

	//这里比较巧妙，根据FRAME_QUEUE_SIZE的定义可知这里其实最终得到的队列大小就是max_size，即视音频或字幕自身预定义的宏的值
	max_size = FFMIN(m_size, FRAME_QUEUE_SIZE);

	//表示是否在环形缓冲区的读写过程中保留最后一个读节点不被覆写。双感叹号旨在让int参数转换为以0/1表示的“bool值”
	keep_last = !!keep_lst;

	//给队列的每一个frame分配内存
	for (int i = 0; i < max_size; i++)
		if (!(fq[i].frame = av_frame_alloc()))
			success = false;
}

/* 销毁Frame队列 */
FrameQueue::~FrameQueue()
{
	for (int i = 0; i < max_size; i++)
	{
		Frame *vp = &fq[i];
		//queue元素的释放分两步
		frame_queue_unref_item(vp);
		av_frame_free(&vp->frame);
	}
	SDL_DestroyMutex(mutex);
	SDL_DestroyCond(cond);
}

//FrameQueue::FrameQueue(const FrameQueue &fq)
//{
//	pktq = new PacketQueue();
//	mutex = new SDL_mutex;
//	cond = new SDL_cond;
//	for (int i = 0; i < max_size; i++)
//		if (!(fq[i].frame = av_frame_alloc()))
//			success = false;
//	memcpy(name, s.name, strlen(s.name));
//}

FrameQueue & FrameQueue::operator=(FrameQueue && frame_q) noexcept
{
	if (this != &frame_q)
	{
		//TODO:先释放已有资源

		mutex = frame_q.mutex;
		cond = frame_q.cond;
		pktq = frame_q.pktq;
		max_size = frame_q.max_size;
		keep_last = frame_q.keep_last;

		//给队列的每一个frame分配内存
		for (int i = 0; i < max_size; i++)
			fq[i] = frame_q.fq[i];

		frame_q.mutex = nullptr;
		frame_q.cond = nullptr;
		frame_q.pktq = nullptr;
		//TODO:下面的释放有bug
		for (int i = 0; i < max_size; i++)
		{
			Frame *tmp_ptr = &frame_q.fq[i];
			tmp_ptr = nullptr;
		}
	}

	return *this;
}

/* 发信号，供外部程序调用 */
void FrameQueue::frame_queue_signal()
{
	SDL_LockMutex(mutex);
	SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
}

/* 检测是否可读，若是则返回当前节点 */
Frame * FrameQueue::frame_queue_peek_readable()
{
	/* 加锁情况下，持续等待有可读的节点(因为要读的节点不能超过已写入的节点)，要多于一个节点可读才行？*/
	SDL_LockMutex(mutex);
	while (size - rindex_shown <= 0 && !pktq->is_abort())
	{
		SDL_CondWait(cond, mutex);
	}
	SDL_UnlockMutex(mutex);

	//如果有退出请求，则返回NULL
	if (pktq->is_abort())
		return NULL;

	//读取当前可读节点(rindex+rindex_shown刚好是将要读的节点)，因为rindex加1后可能超过max_size，所以这里取余
	return &fq[(rindex + rindex_shown) % max_size];
}

/* 读当前节点，与frame_queue_peek_readable等效，但没有检查是否有可读节点 */
Frame * FrameQueue::frame_queue_peek()
{
	return &fq[(rindex + rindex_shown) % max_size];
}

/* 读下一个节点 */
Frame * FrameQueue::frame_queue_peek_next()
{
	return &fq[(rindex + rindex_shown + 1) % max_size];
}

/* 读上一个节点，注意不需要求余了 */
Frame * FrameQueue::frame_queue_peek_last()
{
	return &fq[rindex];
}

/* 检测队列是否可以进行写入，若是返回相应的Frame以供后续对其写入相关值 */
Frame * FrameQueue::frame_queue_peek_writable()
{
	/* 加锁情况下，等待直到队列有空余空间可写(已经读过的frame可以被重写) */
	SDL_LockMutex(mutex);
	while (size >= max_size && !pktq->is_abort())
	{
		SDL_CondWait(cond, mutex);
	}
	SDL_UnlockMutex(mutex);

	//如果有退出请求，则返回NULL
	if (pktq->is_abort())
		return NULL;

	//返回windex位置的元素（windex指向当前应写位置）
	return &fq[windex];
}

/* 将写好相关变量值的Frame存入队列 */
void FrameQueue::frame_queue_push()
{
	//windex加1，如果超过max_size，则重置为0
	if (++windex == max_size)
		windex = 0;
	//加锁情况下size加1
	SDL_LockMutex(mutex);
	size++;
	SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
}

/* 在读完一个节点后调用，用于标记一个节点已经被读过 */
void FrameQueue::frame_queue_next()
{
	//如果支持keep_last，且rindex_shown为0，则rindex_shown赋1，直接返回，这里最多在一开始读的时候执行一次
	if (keep_last && !rindex_shown)
	{
		rindex_shown = 1;
		return;
	}

	//否则，移动rindex指针，并减小size，表示队列增加了一个可写的空间
	frame_queue_unref_item(&fq[rindex]);
	if (++rindex == max_size)
		rindex = 0;
	SDL_LockMutex(mutex);
	size--;
	SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
}


/* 与析构函数作用相同，仅用于外部调用 */
void FrameQueue::frame_queue_destroy()
{
	for (int i = 0; i < max_size; i++)
	{
		Frame *vp = &fq[i];
		//queue元素的释放分两步
		frame_queue_unref_item(vp);
		av_frame_free(&vp->frame);
	}
	SDL_DestroyMutex(mutex);
	SDL_DestroyCond(cond);
}

/* 返回未读的帧数(不包括当前节点) */
int FrameQueue::frame_queue_nb_remaining()
{
	return size - rindex_shown;
}

void FrameQueue::frame_queue_init(PacketQueue * pkt_q, int m_size, int keep_lst)
{
	//清零，建立互斥量和条件变量
	if (!(mutex = SDL_CreateMutex()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		success = false;
	}
	if (!(cond = SDL_CreateCond()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		success = false;
	}

	//跟相应的packet队列关联
	pktq = pkt_q;

	//这里比较巧妙，根据FRAME_QUEUE_SIZE的定义可知这里其实最终得到的队列大小就是max_size，即视音频或字幕自身预定义的宏的值
	max_size = FFMIN(m_size, FRAME_QUEUE_SIZE);

	//表示是否在环形缓冲区的读写过程中保留最后一个读节点不被覆写。双感叹号旨在让int参数转换为以0/1表示的“bool值”
	keep_last = !!keep_lst;

	//给队列的每一个frame分配内存
	for (int i = 0; i < max_size; i++)
		fq[i].frame = av_frame_alloc();

}

/* 返回上一个播放的帧的字节位置 */
int64_t FrameQueue::frame_queue_last_pos()
{
	Frame *fp = &fq[rindex];
	if (rindex_shown && fp->serial == pktq->get_serial())
		return fp->pos;
	else
		return -1;
}

/* 释放AVFrame关联的内存 */
void FrameQueue::frame_queue_unref_item(Frame * vp)
{

	av_frame_unref(vp->frame);//视音频frame数据计数减1
	avsubtitle_free(&vp->sub);//字幕数据关联的内存释放
}

bool FrameQueue::is_construct()
{
	return success;
}

SDL_mutex * FrameQueue::get_mutex()
{
	return mutex;
}

int FrameQueue::get_rindex_shown()
{
	return rindex_shown;
}
