#include "datalink.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#define MAX_PACKET_SIZE 256

//what if crc doesn't found error? should we do sth?

struct buffer {
	unsigned int timer;
	unsigned char data[MAX_PACKET_SIZE + 7];
	bool ackArrived;
	int length;
};

void mySendFrame(unsigned char* databuff, int size) {//add crc and cooperate with physical layer. databuff should be ready to send. size shoule be without crc
	//append crc
	unsigned char *p = databuff + size;
	*(unsigned int*)p = crc32(databuff, size);
	size += 4;//add length

	while (phl_sq_len() >= 62000) //physical layer is not ready
		_sleep(10);
	send_frame(databuff, size);
}

bool 

void main(int argc, char** argv) {
	//init
	protocol_init(argc, argv);
	//arguments
	unsigned short windowSize = 8;//from 0 to n-1
	unsigned int acktimer = 800;//for debuging, it should be large enough
	const int bufferSize = windowSize / 2;//buffer size
	//init datalink layer
	unsigned short senderLeft = 0;//left edge of sender
	unsigned short senderRight = 0;//right edge of sender, which has data unfilled
	unsigned short recieverLeft = 0;//left edge of receiver
	//unsigned short recieverRight = bufferSize - 1;//right edge of receiver
	//init buffer
	buffer* sender = (buffer*)malloc(sizeof(buffer)* bufferSize);
	buffer* receiver = (buffer*)malloc(sizeof(buffer)* bufferSize);
	for (int i = 0; i < bufferSize; i++) {
		sender[i].timer = -1;
		sender[i].ackArrived = false;
		receiver[i].timer = -1;
		receiver[i].ackArrived = false;
	}
	//init interfcace
	enable_network_layer();
	bool isNetworkEnabled = true;
	//init event args
	int eventArgs = -1;
	int eventKind = -1;
	//has nak sent
	bool isNakSend = false;
	//main loop
	while (true) {
		eventKind = wait_for_event(&eventArgs);//get event
		/*we discard PHYSICAL_LAYER_READY events*/
		switch (eventKind) {
		case NETWORK_LAYER_READY:
			//if buffer nearly full
			if (senderRight - senderLeft == bufferSize - 1) {
				disable_network_layer();
				isNetworkEnabled = false;
			}
			//store frame in buffer
			sender[senderRight % bufferSize].length = get_packet(sender[senderRight % bufferSize].data);
			break;
		case FRAME_RECEIVED:
			//init temp vars
			static unsigned char temp[MAX_PACKET_SIZE + 11];
			static int frameLength = recv_frame(temp, MAX_PACKET_SIZE + 7);
			if (frameLength > MAX_PACKET_SIZE + 7)
				;//frame is too large, discard it
			else {
				//check crc
				if (!crc32(temp, frameLength + 4)) {//crc faild
					//send nak
					temp[0] = FRAME_NAK;//if the 2nd byte is error, it may sends false nak, but it doesn't matter
					mySendFrame(temp, 2);
				}
				else {
					if (temp[0] == FRAME_ACK) {//if it's an ack frame

					}
				}
			}
		}

	}
}