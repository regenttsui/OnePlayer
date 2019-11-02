#include "packet_queue.h"

/* 构造各个字段的初始值，并创建mutex和cond */
//PacketQueue::PacketQueue(queue<pair<AVPacket, int>> q, int nb_pkts, int byte_size,
//	int64_t dur, bool abort_req, int seri, bool suc) : pq(q), nb_packets(nb_pkts), 
//	size(byte_size), duration(dur), abort_request(abort_req), serial(seri), success(suc)
//{
//	mutex = SDL_CreateMutex();
//	if (!mutex) 
//	{
//		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
//		success = false;
//	}
//	cond = SDL_CreateCond();
//	if (!cond) 
//	{
//		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
//		success = false;
//	}
//}

PacketQueue::PacketQueue(int nb_pkts, int byte_size,
	int64_t dur, bool abort_req, int seri, bool suc) : nb_packets(nb_pkts),
	size(byte_size), duration(dur), abort_request(abort_req), serial(seri), success(suc)
{
	mutex = SDL_CreateMutex();
	if (!mutex)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		success = false;
	}
	cond = SDL_CreateCond();
	if (!cond)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		success = false;
	}
}

/* 根据原则应该同时定义拷贝构造函数和拷贝赋值运算符，但程序中pq一般只有一个，故暂不定义 */
PacketQueue::~PacketQueue()
{
	packet_queue_flush();
	SDL_DestroyMutex(mutex);
	SDL_DestroyCond(cond);
}

void PacketQueue::packet_queue_init()
{
	mutex = SDL_CreateMutex();
	if (!mutex) 
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
	}
	cond = SDL_CreateCond();
	if (!cond) 
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
	}
	abort_request = 1; //并未启用队列
	success = true;
}

/* 构造函数是否成功执行 */
bool PacketQueue::is_construct()
{
	return success;
}

bool PacketQueue::is_abort()
{
	return abort_request;
}

int PacketQueue::get_serial()
{
	return serial;
}

int PacketQueue::get_nb_packets()
{
	return nb_packets;
}

int PacketQueue::get_size()
{
	return size;
}

int64_t PacketQueue::get_duration()
{
	return duration;
}

/* 真正开始启用队列 */
void PacketQueue::packet_queue_start()
{
	SDL_LockMutex(mutex);
	abort_request = false; //这里很重要
	packet_queue_put_private(&flush_pkt); //放入了一个flush_pkt，主要用来作为非连续的两端数据的“分界”标记
	SDL_UnlockMutex(mutex);
}

/* 中止队列，实际上只是将终止请求为置1并发出信号 */
void PacketQueue::packet_queue_abort()
{
	SDL_LockMutex(mutex);
	abort_request = 1;
	SDL_CondSignal(cond);  //发出信号
	SDL_UnlockMutex(mutex);
}

/* 清除PacketQueue的所有节点以及将相关变量的值清零 */
//void PacketQueue::packet_queue_flush()
//{
//	SDL_LockMutex(mutex);
//	//not sure
//	while (!pq.empty())
//	{
//		av_packet_unref(&pq.back().first);
//		pq.pop();
//	}
//	nb_packets = 0;
//	size = 0;
//	duration = 0;
//	SDL_UnlockMutex(mutex);
//}

void PacketQueue::packet_queue_flush()
{
	MyAVPacketList *pkt, *pkt1;

	SDL_LockMutex(mutex);
	for (pkt = first_pkt; pkt; pkt = pkt1) 
	{
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		av_freep(&pkt);
	}
	last_pkt = NULL;
	first_pkt = NULL;
	nb_packets = 0;
	size = 0;
	duration = 0;
	SDL_UnlockMutex(mutex);
}

/* 与析构函数作用一致，仅用于外部调用 */
void PacketQueue::packet_queue_destroy()
{
	packet_queue_flush();
	SDL_DestroyMutex(mutex);
	SDL_DestroyCond(cond);
}

/* 放入空的packet。放入空packet意味着流的结束，一般在视频读取完成的时候放入空packet，用于刷新拿到解码器中缓存的最后几帧 */
int PacketQueue::packet_queue_put_nullpacket(int stream_index)
{
	//先创建一个空的packet，然后调用packet_queue_put
	AVPacket pkt1, *pkt = &pkt1;
	av_init_packet(pkt);
	pkt->data = NULL;
	pkt->size = 0;
	pkt->stream_index = stream_index;
	return packet_queue_put(pkt);
}

/* 将packet写入PacketQueue */
int PacketQueue::packet_queue_put(AVPacket * pkt)
{
	int ret;

	SDL_LockMutex(mutex);
	ret = packet_queue_put_private(pkt); //核心
	SDL_UnlockMutex(mutex);

	//写入失败则packet引用减1。由于flush_pkt是全局静态变量，一直会使用，故不能释放
	if (pkt != &flush_pkt && ret < 0)
		av_packet_unref(pkt);

	return ret;
}

/* 从PacketQueue中获取packet。
   block: 调用者是否需要在没节点可取的情况下阻塞等待；
   返回值：中止则<0，无packet则=0，有packet则> 0；
   AVPacket: 输出参数，即MyAVPacketList.pkt
   serial: 输出参数，即MyAVPacketList.serial */
//int PacketQueue::packet_queue_get(AVPacket * pkt, bool block, int * serial)
//{
//	int ret;
//
//	SDL_LockMutex(mutex);
//
//	while (true)
//	{
//		//中止时跳出循环
//		if (abort_request)
//		{
//			ret = -1;
//			break;
//		}
//
//		//队列中有数据
//		if (!pq.empty())
//		{
//			//取出packet并进行浅拷贝
//			*pkt = pq.front().first;
//			//如果需要输出serial，把serial输出
//			if (serial)
//				*serial = pq.front().second;
//			pq.pop();	//弹出原来的packet
//
//			//各种统计数据相应减少
//			nb_packets--;
//			size -= pkt->size + sizeof(AVPacket) + sizeof(int);
//			duration -= pkt->duration;
//
//			ret = 1;
//			break;
//		}
//		else if (!block) //队列中没有数据，且非阻塞调用，则跳出循环
//		{
//			ret = 0;
//			break;
//		}
//		else //队列中没有数据，且阻塞调用
//		{
//			//这里没有break。死循环的另一个作用是在获得条件变量后(即队列填充了数据)重复上述代码取出节点
//			SDL_CondWait(cond, mutex);
//		}
//	}
//	SDL_UnlockMutex(mutex);
//
//	return ret;
//}

int PacketQueue::packet_queue_get(AVPacket * pkt, bool block, int * serial)
{
	MyAVPacketList *pkt1;
	int ret;

	SDL_LockMutex(mutex);

	while (true) 
	{
		//中止时跳出循环
		if (abort_request) 
		{
			ret = -1;
			break;
		}

		pkt1 = first_pkt;
		//队列中有数据
		if (pkt1) 
		{
			//第二个节点变为队头，如果第二个节点为空，则队尾为空
			first_pkt = pkt1->next;
			if (!first_pkt)
				last_pkt = NULL;

			//各种统计数据相应减少
			nb_packets--;
			size -= pkt1->pkt.size + sizeof(*pkt1);
			duration -= pkt1->pkt.duration;

			//返回AVPacket，这里发生一次AVPacket结构体拷贝，AVPacket的data只拷贝了指针，因为AVPacket是分配在栈内存上的，而且使用等于号进行浅拷贝，所以不能释放从队列中取出的这个packet
			*pkt = pkt1->pkt;

			//如果需要输出serial，把serial输出
			if (serial)
				*serial = pkt1->serial;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) 
		{//队列中没有数据，且非阻塞调用，则跳出循环
			ret = 0;
			break;
		}
		else 
		{//队列中没有数据，且阻塞调用
			SDL_CondWait(cond, mutex);//这里没有break。for循环的另一个作用是在获得条件变量后(即队列填充了数据)重复上述代码取出节点
		}
	}
	SDL_UnlockMutex(mutex);
	return ret;
}

/* 真正的实现写入PacketQueue的函数 */
//int PacketQueue::packet_queue_put_private(AVPacket * pkt)
//{
//	AVPacket pkt1;
//	int pkt_serial;
//
//	//如果已中止，则放入失败
//	if (abort_request)
//		return -1;
//
//	/*拷贝AVPacket(浅拷贝，AVPacket.data等内存并没有拷贝，即pkt1与pkt的data指向同一块内存，要记住AVPacket结构只是容器，
//	  本身是不包括data的内存空间的，但是这样做并没有增加引用计数，不过根据观察其内存并不会因为av_read_frame被释放，不懂原因)*/
//	pkt1 = *pkt;
//
//	//如果放入的是flush_pkt，需要增加队列的序列号，以区分不连续的两段数据
//	if (pkt == &flush_pkt)
//		serial++;
//	//用队列序列号标记节点
//	pkt_serial = serial;
//
//	/* 将packet入队 */
//	pq.push(make_pair(pkt1, pkt_serial));
//
//	/* 队列属性操作：增加节点数、cache大小、cache总时长。
//	   注意pkt1.size是指向的实际数据的大小，所以还要加上AVPacket本身大小 */
//	nb_packets++;
//	size += pkt1.size + sizeof(AVPacket) + sizeof(int);
//	duration += pkt1.duration;
//
//	//发出信号，表明当前队列中有数据了，通知等待中的读线程可以取数据了
//	SDL_CondSignal(cond);
//	
//	return 0;
//}

int PacketQueue::packet_queue_put_private(AVPacket * pkt)
{
	MyAVPacketList *pkt1;

	//如果已中止，则放入失败
	if (abort_request)
		return -1;

	//分配节点内存
	pkt1 = (MyAVPacketList*)av_malloc(sizeof(MyAVPacketList));
	//分配内存失败
	if (!pkt1)
		return -1;
	/*拷贝AVPacket。这是浅拷贝，AVPacket.data等内存并没有拷贝，即pkt1->pkt与pkt的data指向同一块内存，要记住AVPacket结构只是容器，
	  本身是不包括data的内存空间的，但是这样做并没有增加引用计数，不过根据观察其内存并不会因为av_read_frame被释放，
	  因为av_read_frame没有调用av_packet_ref，而是调用av_init_packet将引用计数的指针指向NULL。同时由于很多时候AVPacket是
	  直接在栈上分配内存的，所以不需要手工释放*/
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	//如果放入的是flush_pkt，需要增加队列的序列号，以区分不连续的两段数据
	if (pkt == &flush_pkt)
		serial++;
	//用队列序列号标记节点
	pkt1->serial = serial;

	/*队列操作：如果last_pkt为空，说明队列是空的，新增节点为队头；否则，队列有数据，则让原队尾的next为新增节点。最后将队尾指向新增节点
	  因为q->first_pkt或q->last_pkt和pkt1指向同一块内存，所以最后不能释放pkt1指向的内存！*/
	if (!last_pkt)
		first_pkt = pkt1;
	else
		last_pkt->next = pkt1;
	last_pkt = pkt1; //只有一个pkt时头尾节点是相同的

	//队列属性操作：增加节点数、cache大小、cache总时长
	nb_packets++;
	size += pkt1->pkt.size + sizeof(*pkt1);
	duration += pkt1->pkt.duration;
	/* XXX: should duplicate packet data in DV case */
	//发出信号，表明当前队列中有数据了，通知等待中的读线程可以取数据了
	SDL_CondSignal(cond);
	return 0;
}

