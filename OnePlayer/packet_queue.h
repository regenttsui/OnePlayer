#ifndef PACKET_QUEUE_H_
#define PACKET_QUEUE_H_
#include <iostream>
#include <queue>
#include <utility>

#include "global.h"

using std::cout;
using std::endl;
using std::queue;
using std::pair;
using std::make_pair;

//PacketQueue的一个节点
typedef struct MyAVPacketList
{
	AVPacket pkt;
	struct MyAVPacketList *next;    //下一个节点
	int serial;                     //序列号，用于标记当前节点的序列号，区分是否连续数据，在seek的时候发挥作用
} MyAVPacketList;

class PacketQueue
{
public:
	PacketQueue() = default;
	/*PacketQueue(queue<pair<AVPacket, int>> q, int nb_pkts = 0, int byte_size = 0,
		int64_t dur = 0, bool abort_req = true, int seri = 0, bool suc = true);*/
	PacketQueue(int nb_pkts = 0, int byte_size = 0,
		int64_t dur = 0, bool abort_req = true, int seri = 0, bool suc = true);
	PacketQueue(PacketQueue&&) = default;
	PacketQueue&operator=(PacketQueue&&) = default;
	~PacketQueue();
	void packet_queue_init();
	void packet_queue_start();
	void packet_queue_abort();
	void packet_queue_flush();
	void packet_queue_destroy();
	int packet_queue_put_nullpacket(int stream_index);
	int packet_queue_put(AVPacket *pkt);
	int packet_queue_get(AVPacket *pkt, bool block, int *serial);
	bool is_construct();
	bool is_abort();
	int get_serial();
	int get_nb_packets();
	int get_size();
	int64_t get_duration();


private:
	//queue<pair<AVPacket, int>> pq;	//pair模拟MyAVPacketList，包含packet和serial
	MyAVPacketList *first_pkt, *last_pkt;   //队首，队尾
	int nb_packets;                 //节点数
	int size;                       //队列所有节点字节总数，用于计算cache大小
	int64_t duration;               //队列所有节点的合计时长
	bool abort_request;             //是否要中止队列操作，用于安全快速退出播放
	int serial;                     //序列号，和MyAVPacketList的serial作用相同，但明显的区别是一个PacketQueue只有一个serial，但组成它的多个packet分别有自己的serial，该区别即体现了利用serial来区分packet的作用
	SDL_mutex *mutex;               //用于维持PacketQueue的多线程安全
	SDL_cond *cond;                 //用于读、写线程通信
	int success;					//构造是否成功的标志

	int packet_queue_put_private(AVPacket *pkt);


};



#endif // !PACKET_QUEUE_H_
