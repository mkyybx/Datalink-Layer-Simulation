#include "datalink.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>

void main(int argc, char** argv) {
	//init
	protocol_init(argc, argv);
	//arguments
	short windowSize = 8;//from 0 to n-1
	int acktimer = 800;//for debuging, it should be large enough
	const int bufferSize = windowSize / 2;//buffer size
	//init datalink layer

}