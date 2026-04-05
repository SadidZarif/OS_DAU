#ifndef _DATAACQUISITION_H_
#define _DATAACQUISITION_H_

#include <errno.h>
#include <iostream>
#include <queue>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "SeismicData.h"

struct DataPacket {
    unsigned int packetNo;
    unsigned short packetLen;
    char data[BUF_LEN];
};

class DataAcquisition {
    bool is_running;
    sem_t *sem_id1;
    key_t ShmKey;
    int ShmID;
    struct SeismicMemory *ShmPTR;
    struct sigaction action;

    int seismicDataIndex;
    pthread_t readTid;
    pthread_mutex_t queueMutex;
    std::queue<DataPacket> packetQueue;

    int setupSharedMemory();
    void cleanupSharedMemory();
    bool copyPacketFromSharedMemory();
    void pushPacket(const DataPacket &packet);

public:
    DataAcquisition();
    ~DataAcquisition();
    int run();
    void shutdown();
    void SharedMemoryReadLoop();

    bool popPacket(DataPacket &packet);
    int getQueueSize();

    static DataAcquisition* instance;
};

#endif// _DATAACQUISITION_H_
