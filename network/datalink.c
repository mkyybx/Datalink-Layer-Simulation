//
//  main.c
//  test
//
//  Created by 刘欣 on 16/4/30.
//  Copyright © 2016年 liu. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"
#include "datalink.h"

#define MAX_SEQ 7//帧的最大序号
#define NR_BUFS ((MAX_SEQ + 1)/2) //窗口大小
#define DATA_TIMER 2500//暂定为2000
#define ACK_TIMER 1000

typedef struct{//帧结构，包含帧的类型，seq，ack，实际数据
    unsigned char kind;
    unsigned char ack;
    unsigned char seq;
    unsigned char info[PKT_LEN];
    unsigned int padding;//不加长度报错
} FRAME;


static int phl_ready=0;
static unsigned int no_nak = 1;//前面序列都没有出错
static unsigned int oldest_frame = MAX_SEQ+1;



static unsigned char inc(unsigned char k)//下一帧序号
{
    return (k+1)%(MAX_SEQ+1);
}


static int between(unsigned char a,unsigned char b,unsigned char c)//如果b在[a,c)之间返回1，否则返回0
{
    return ( ( (a<=b)&&(b<c) ) || ( (c<a)&&(a<=b) ) || ( (b<c)&&(c<a) ) );
}

static void put_frame(unsigned char *frame, int length)
{
    *(unsigned int*)(frame+length)=crc32(frame,length);
    send_frame(frame,length+4);
    phl_ready=0;
}

static void send_data(unsigned char fk, unsigned char frame_num, unsigned char frame_expected, FRAME buffer[])
{
    FRAME s;
    s.kind=fk;
    s.ack=(frame_expected+MAX_SEQ)%(MAX_SEQ+1); //ack为待收帧的前一个
    s.seq=frame_num;
    
    if(fk==FRAME_ACK)
        {
            dbg_frame("Send ACK %d\n",s.ack);
            put_frame((unsigned char *)&s, 2);
            
        }
    
    if(fk==FRAME_DATA)
    {
        memcpy(s.info,buffer[frame_num % NR_BUFS].info,PKT_LEN);//从源buffer所指的内存地址的起始位置开始拷贝length个字节到目标info所指的内存地址的起始位置中
        dbg_frame("Send DATA %d %d, ID %d\n",s.seq,s.ack,*(short*)s.info);
        put_frame((unsigned char *)&s, 3 + PKT_LEN);
        start_timer(frame_num % NR_BUFS,DATA_TIMER); //单位为毫秒
    }

    if (fk == FRAME_NAK)
    {
        no_nak=0;
        dbg_frame("Send NAK %d\n",s.ack);
        put_frame((unsigned char *)&s, 2);
    }
    stop_ack_timer();
    
}

int main(int argc, char **argv)
{
    static unsigned char next_frame_to_send=0;//发送方上界＋1
    static unsigned char ack_expected=0;//发送方下界
    static unsigned char frame_expected=0;//接收方下界
    static unsigned char too_far=NR_BUFS;//接收方上界+1
    static FRAME out_buffer[NR_BUFS];//发送缓冲区，只有数据
    static FRAME in_buffer[NR_BUFS];//接收缓冲区,整个帧
    static unsigned char arrived[NR_BUFS]; //判定是否接受
    static unsigned char nbuffered=0;//发送的帧数
    int event;//事件
    FRAME f;//收到的帧
    int i;//计数用
    int arg;//产生超时事件的定时器编号
    int length=0; //帧长
    
    for(i = 0; i < NR_BUFS; i++)
        arrived[i]=0;
    
    protocol_init(argc,argv);
    enable_network_layer();
    
    for(;;)
    {
        event=wait_for_event(&arg);
        
        switch(event)
        {
            case NETWORK_LAYER_READY://网络层有待发送的分组
                
                get_packet(out_buffer[next_frame_to_send % NR_BUFS].info);
                nbuffered++;
                send_data(FRAME_DATA,next_frame_to_send,frame_expected,out_buffer);
                next_frame_to_send=inc(next_frame_to_send);
                break;
             
            case PHYSICAL_LAYER_READY://物理层
                
                phl_ready=1;
                break;
                
            case FRAME_RECEIVED://物理层收到了一整帧
                
                length=recv_frame((unsigned char*)&f,sizeof f);//从物理层接收
                
                if(length < 5 || crc32((unsigned char *)&f, length) != 0)//crc四位    //校验出错
                {
                    dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                    if(no_nak)
                        send_data(FRAME_NAK, 0, frame_expected+1, out_buffer);
                    break;
                }
                
                if ((f.kind == FRAME_NAK) && between(ack_expected, f.ack, next_frame_to_send)) //收到NAK
                {
                    dbg_frame("Recv NAK  %d\n", f.ack);
                    send_data(FRAME_DATA, f.ack, frame_expected, out_buffer);
                }

                if (f.kind == FRAME_ACK)    //收到ACK
                {
                    dbg_frame("Recv ACK  %d\n", f.ack);
                
                }
                
                if (f.kind == FRAME_DATA)  //收到数据帧
                {
                    dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.info);
                    
                    if( (f.seq !=frame_expected) && no_nak) //前面有帧丢失
                        send_data(FRAME_NAK,0,frame_expected,out_buffer);
                    else
                        start_ack_timer(ACK_TIMER); //发送ack
                    
                    if(between(frame_expected, f.seq, too_far) && arrived[f.seq % NR_BUFS]==0) //在接受窗口内且未重复接收
                    {
                        arrived[f.seq % NR_BUFS]=1;
                        in_buffer[f.seq % NR_BUFS]=f;
                    } 
                    
                    while(arrived[frame_expected % NR_BUFS]) //将接受窗口内的帧数据按顺序传给网络层
                    {
						printf("put=%d\n", *(short*)(in_buffer[frame_expected % NR_BUFS].info));
                        put_packet(in_buffer[frame_expected % NR_BUFS].info, length - 7);
						
						stop_timer(frame_expected % NR_BUFS);
                        no_nak=1;
                        arrived[frame_expected % NR_BUFS]=0;
                        frame_expected=inc(frame_expected);
                        too_far=inc(too_far);
                        start_ack_timer(ACK_TIMER); //发送ack
                    }
                    
                    
                }

                while (between(ack_expected, f.ack, next_frame_to_send)) //收到的ack在发送窗口中,可以累积ack
                {
                    nbuffered--;
                    stop_timer(ack_expected % NR_BUFS);
                    ack_expected=inc(ack_expected);
                }
                
                
                break;
                
            case DATA_TIMEOUT://定时器超时
                
                dbg_event("---- DATA %d timeout\n", arg);
                
                send_data(FRAME_DATA, arg, frame_expected, out_buffer);   
                
                break;
            case ACK_TIMEOUT:
                
                dbg_event("---- ACK %d timeout\n", frame_expected);
                send_data(FRAME_ACK, 0, frame_expected, out_buffer);
                
                break;
        }
        
        if((nbuffered<NR_BUFS)&&(phl_ready))//发送方窗口已有帧数小于可发送帧最大个数
            enable_network_layer();
        else
            disable_network_layer();
    }
    
    return 0;
}
