#ifndef SERVER_H
#define SERVER_H

#include "types.h"

void run_zmq_server(LocationData* sharedData, ScrollingBuffer* sharedHistory);

#endif