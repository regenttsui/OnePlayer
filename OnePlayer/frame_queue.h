#ifndef FRAME_QUEUE_H_
#define FRAME_QUEUE_H_
#include "packet_queue.h"

/*FrameQueue的预定义参数*/
constexpr auto VIDEO_PICTURE_QUEUE_SIZE = 3;
constexpr auto SUBPICTURE_QUEUE_SIZE = 16;
constexpr auto SAMPLE_QUEUE_SIZE = 9;
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

class FrameQueue
{
public:
	FrameQueue() = default;
	FrameQueue(PacketQueue *pkt_q, int m_size, int keep_lst, bool suc = true);
	~FrameQueue();
	//FrameQueue(FrameQueue&& frame_q);
	FrameQueue& operator=(FrameQueue&& frame_q) noexcept;
	void frame_queue_signal();
	Frame *frame_queue_peek_readable();
	Frame *frame_queue_peek();
	Frame *frame_queue_peek_next();
	Frame *frame_queue_peek_last();
	Frame *frame_queue_peek_writable();
	void frame_queue_push();
	void frame_queue_next();
	void frame_queue_destroy();
	int frame_queue_nb_remaining();
	void frame_queue_init(PacketQueue *pkt_q, int m_size, int keep_lst);
	int64_t frame_queue_last_pos();
	bool is_construct();
	SDL_mutex* get_mutex();
	int get_rindex_shown();

private:
	Frame fq[FRAME_QUEUE_SIZE];		//循环队列，用数组模拟
	int rindex;                     //读取的位置索引
	int windex;                     //写入的位置索引
	int size;                       //当前存储的节点个数(或者说，当前已写入的节点个数)
	int max_size;                   //最大允许存储的节点个数
	int keep_last;                  //是否要保留最后一个读节点
	int rindex_shown;               //当前节点是否已经被读/显示。rindex+rindex_shown刚好是将要读的节点
	SDL_mutex *mutex;
	SDL_cond *cond;
	PacketQueue *pktq;              //关联的PacketQueue
	int success;					//构造是否成功的标志

	void frame_queue_unref_item(Frame *vp);
};


#endif // FRAME_QUEUE_H_

