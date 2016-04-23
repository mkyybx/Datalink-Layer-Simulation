#include "datalink.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PACKET_SIZE 256

//what if crc doesn't found error? should we do sth?

struct buffer {
	unsigned char data[MAX_PACKET_SIZE + 7];
	bool ackArrived;//for receiver, it means if a frame has arrived
	int length;
};

//arguments
unsigned short windowSize;//from 0 to n-1
unsigned int retimer;//for debuging, it should be large enough
unsigned int acktimer;//acktimer
int bufferSize;//buffer size
//init datalink layer
unsigned short senderLeft;//left edge of sender
unsigned short senderRight;//right edge of sender, which has data unfilled
unsigned short receiverLeft;//left edge of receiver
//unsigned short receiverRight = bufferSize - 1;//right edge of receiver
//init buffer
buffer* sender;
buffer* receiver;

void mySendFrame(unsigned char* databuff, int size) {//add crc and cooperate with physical layer. databuff should be ready to send. size shoule be without crc
	//append crc
	unsigned char *p = databuff + size;
	*(unsigned int*)p = crc32(databuff, size);
	size += 4;//add length

	if (databuff[0] == FRAME_DATA) {
		//add timer
		start_timer(databuff[1] % bufferSize, retimer);
		//piggyback ack
		short i = (receiverLeft - 1) % windowSize;//log the last ack arrived
		for (short j = receiverLeft, k = 0; k < bufferSize; k++) {
			receiver[j % bufferSize].ackArrived ? i = j % windowSize : 0;
			j++;
		}
		databuff[2] = i;
	}

	while (phl_sq_len() >= 62000) //physical layer is not ready
		_sleep(10);
	send_frame(databuff, size);

	//piggyback ack timer
	if (databuff[0] == FRAME_DATA) {//restart timer
		stop_ack_timer();
		start_ack_timer(acktimer);
	}

}

bool isInBuffer(short left, short right, short seq) {//whether a serial number is in buffer range,[left, right)
	if (right > left)
		return (seq >= left && seq < right);
	else return seq < right || seq >= left;
}

void main(int argc, char** argv) {
	//init
	protocol_init(argc, argv);
	//arguments
	windowSize = 8;//from 0 to n-1
	retimer = 800;//for debuging, it should be large enough
	acktimer = 100;
	bufferSize = windowSize / 2;//buffer size
	//init datalink layer
	senderLeft = 0;//left edge of sender
	senderRight = 0;//right edge of sender, which has data unfilled
	receiverLeft = 0;//left edge of receiver
	//receiverRight = bufferSize - 1;//right edge of receiver
	//init buffer
	sender = (buffer*)malloc(sizeof(buffer)* bufferSize);
	receiver = (buffer*)malloc(sizeof(buffer)* bufferSize);
	for (int i = 0; i < bufferSize; i++) {
		sender[i].ackArrived = false;
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
			if (((senderRight > senderLeft) && (senderRight - senderLeft == bufferSize - 1)) || (senderRight < senderLeft) && (windowSize - senderLeft + senderRight == bufferSize - 1)) {
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
					if (isInBuffer(senderLeft, senderRight, temp[1])) {//sequence is in buffer
						if (temp[0] == FRAME_ACK) {//if it's an ack frame
							sender[temp[1] % bufferSize].ackArrived = true;
							stop_timer(temp[1] % bufferSize);//stop timmer
							//else do noting
						}
						else if (temp[0] == FRAME_NAK) {//if it's a nak frame
							//retranmit
							temp[0] = 1;
							memcpy(temp + 3, sender[temp[1]].data, sender[temp[1]].length * sizeof(unsigned char));
							mySendFrame(temp, sender[temp[1]].length + 3);
						}
					}
					else if (isInBuffer(receiverLeft, (receiverLeft + bufferSize - 1) % windowSize, temp[1])){
						if (temp[0] == FRAME_DATA) {//if it's a data frame
							receiver[temp[1]].ackArrived = 1;
							memcpy(receiver[temp[1]].data, (void*)(temp[3]), (frameLength - 7) * sizeof(unsigned char));
							receiver[temp[1]].length = frameLength - 7;
						}
					}

				}

			}
		}

	}
}