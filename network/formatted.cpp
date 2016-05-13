#include "datalink.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define MAX_PACKET_SIZE 256

struct buffer {
	unsigned char data[MAX_PACKET_SIZE + 7];
	bool frameArrived;//for receiver, it means if a frame has arrived
	bool hasSent;//if the frame has sent in sender
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
short isPhysicalLayerReady;//-1 means not initiated, 0 means no, 1 means yes
short lastAck;//the last ack synced

//init buffer
buffer* sender;
buffer* receiver;

void mySendFrame(unsigned char* databuff, int size) {//add crc and cooperate with physical layer. databuff should be ready to send. size shoule be without crc

	if (databuff[0] != FRAME_NAK) {
		//piggyback ack
		short i = (receiverLeft - 1) % windowSize;//log the last ack arrived
		if (i < 0)
			i += windowSize;
		for (short j = 0; j < bufferSize; j++) {
			if (receiver[(receiverLeft + j) % bufferSize].frameArrived)
				i = (receiverLeft + j) % windowSize;
			else break;
		}
		databuff[0] == FRAME_DATA ? databuff[2] = i : databuff[1] = i;
	}

	//append crc
	*(unsigned int*)(databuff + size) = crc32(databuff, size);
	size += 4;//add length

	if (databuff[0] == FRAME_DATA)
		//add timer
		start_timer(databuff[1], retimer);

	//piggyback ack timer
	if (databuff[0] != FRAME_NAK) {
		stop_ack_timer();
		start_ack_timer(acktimer);
	}

	send_frame(databuff, size);

}

bool isInBuffer(short left, short right, short seq, bool isReceiver) {//whether a serial number is in buffer range,[left, right)
	if (right > left)
		return (seq >= left && seq < right);
	else if (left == right)
		return false;
	else return seq < right || seq >= left;
}

void main1(int argc, char** argv) {
	srand(time(0));
	//init
	protocol_init(argc, argv);
	//arguments
	windowSize = 22;
	retimer = 3300;//retransmit timer
	acktimer = 600;
	bufferSize = windowSize / 2;//buffer size
	//init datalink layer
	senderLeft = 0;//left edge of sender
	senderRight = 0;//right edge of sender, which has data unfilled
	receiverLeft = 0;//left edge of receiver
	//receiverRight = bufferSize - 1;//right edge of receiver
	isPhysicalLayerReady = -1;
	lastAck = windowSize - 1;
	//init buffer
	sender = (buffer*)malloc(sizeof(buffer)* bufferSize);
	receiver = (buffer*)malloc(sizeof(buffer)* bufferSize);
	for (int i = 0; i < bufferSize; i++) {
		sender[i].frameArrived = false;
		receiver[i].frameArrived = false;
		sender[i].hasSent = false;
		receiver[i].hasSent = false;
	}
	//init interfcace
	enable_network_layer();
	bool isNetworkEnabled = true;
	//init event args
	int eventArgs = -1;
	int eventKind = -1;
	//allocate temp space
	unsigned char temp[MAX_PACKET_SIZE + 11];
	//main loop
	while (true) {
		static int frameLength;
		eventKind = wait_for_event(&eventArgs);//get event
		switch (eventKind) {
		case PHYSICAL_LAYER_READY:
			isPhysicalLayerReady = 1;
			break;
		case NETWORK_LAYER_READY:
			//if buffer nearly full
			if (((senderRight > senderLeft) && (senderRight - senderLeft == bufferSize - 1)) || (senderRight < senderLeft) && (windowSize - senderLeft + senderRight == bufferSize - 1)) {
				disable_network_layer();
				isNetworkEnabled = false;
			}

			//store frame in buffer
			sender[senderRight % bufferSize].length = get_packet(sender[senderRight % bufferSize].data);

			//slide window
			senderRight = (senderRight + 1) % windowSize;
			break;
		case FRAME_RECEIVED:
			//init temperory variables
			frameLength = recv_frame(temp, MAX_PACKET_SIZE + 7);

			if (frameLength > MAX_PACKET_SIZE + 7)
				;//frame is too large, discard it
			else {
				//check crc
				if (crc32(temp, frameLength) != 0) {//crc faild
					if (isInBuffer(senderLeft, senderRight, temp[0] == FRAME_DATA ? temp[2] : temp[1], false)) {
						//send nak
						temp[0] = FRAME_NAK;//if the 2nd byte is error, it may sends false nak, but it doesn't matter
						mySendFrame(temp, 2);
					}
				}
				else {
					if (temp[0] == FRAME_ACK) {//if it's an ack frame
						if (isInBuffer(senderLeft, senderRight, temp[1], false)) {
							if (isInBuffer(lastAck, senderRight, temp[1], false))
								lastAck = temp[1];
							//else do noting
							break;
						}
					}
					else if (temp[0] == FRAME_NAK) {//if it's a nak frame
						if (isInBuffer(senderLeft, senderRight, temp[1], false)) {
							//retranmit
							temp[0] = 1;
							memcpy(temp + 3, sender[temp[1] % bufferSize].data, sender[temp[1] % bufferSize].length * sizeof(unsigned char));
							mySendFrame(temp, sender[temp[1] % bufferSize].length + 3);
							break;
						}
					}
					else if (temp[0] == FRAME_DATA) {//if it's a data frame
						if (isInBuffer(lastAck, senderRight, temp[2], false))
							lastAck = temp[2];
						if (isInBuffer(receiverLeft, (receiverLeft + bufferSize) % windowSize, temp[1], true)){
							if (!receiver[temp[1] % bufferSize].frameArrived) {
								receiver[temp[1] % bufferSize].frameArrived = true;
								receiver[temp[1] % bufferSize].length = frameLength - 7;
								for (int i = 0; i < frameLength - 7; i++) {
									receiver[temp[1] % bufferSize].data[i] = temp[3 + i];
								}
							}
						}
					}

				}

			}
			break;
		case DATA_TIMEOUT:
			//just retransmit the frame
			if (isInBuffer(lastAck, senderRight, eventArgs, false) && isInBuffer(senderLeft, senderRight, eventArgs, false)) {
				if (sender[eventArgs % bufferSize].hasSent) {//if it has been sent
					if (eventArgs == senderLeft || rand() % 10 > 10) {
						//build the frame
						temp[0] = FRAME_DATA;
						temp[1] = eventArgs;
						memcpy((void*)(temp + 3), sender[eventArgs % bufferSize].data, sender[eventArgs % bufferSize].length * sizeof(unsigned char));
						//transmit
						mySendFrame(temp, sender[eventArgs % bufferSize].length + 3);
					}
					else
						start_timer(eventArgs, retimer * (rand() % 10 / 10.0 + 1));
				}
			}
			break;
		case ACK_TIMEOUT:
			//just send an ack
			temp[0] = FRAME_ACK;
			mySendFrame(temp, 2);
			break;
		}


		//sliding the sender window
		//send
		{
			int i = senderLeft;
			while (isInBuffer(senderLeft, senderRight, i, false)) {
				if (sender[i % bufferSize].hasSent == false){
					if (isPhysicalLayerReady == 1 || isPhysicalLayerReady == -1 || phl_sq_len() < 1000) {
						//build the frame
						temp[0] = FRAME_DATA;
						temp[1] = i % windowSize;
						memcpy((void*)(temp + 3), sender[i % bufferSize].data, sender[i % bufferSize].length * sizeof(unsigned char));
						//transmit
						mySendFrame(temp, sender[i % bufferSize].length + 3);
						sender[i % bufferSize].hasSent = true;
						isPhysicalLayerReady = 0;
					}
					break;
				}
				i = (i + 1) % windowSize;
			}
		}
		//slide
		while (isInBuffer(senderLeft, senderRight, lastAck, false)) {//·â×°º¯Êý
			sender[senderLeft % bufferSize].hasSent = false;
			stop_timer(senderLeft);
			senderLeft = (senderLeft + 1) % windowSize;
		}
		//enable network layer
		if (!(((senderRight > senderLeft) && (senderRight - senderLeft == bufferSize)) || (senderRight < senderLeft) && (windowSize - senderLeft + senderRight == bufferSize)) && !isNetworkEnabled) {
			enable_network_layer();
			isNetworkEnabled = true;
		}


		//sliding the receiver window
		{
			int i = 0;
			for (i = 0; i < bufferSize; i++) {
				if (receiver[(receiverLeft + i) % bufferSize].frameArrived) {
					put_packet(receiver[(receiverLeft + i) % bufferSize].data, receiver[(receiverLeft + i) % bufferSize].length);
					receiver[(receiverLeft + i) % bufferSize].frameArrived = false;
				}
				else break;
			}
			receiverLeft = (receiverLeft + i) % windowSize;
		}
	}
}