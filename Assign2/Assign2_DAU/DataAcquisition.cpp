#include "DataAcquisition.h"

using namespace std;

DataAcquisition* DataAcquisition::instance = nullptr;

static void interruptHandler(int signum)
{
    switch(signum) {
        case SIGINT:
            if(DataAcquisition::instance != nullptr) {
                DataAcquisition::instance->shutdown();
            }
            break;
    }
}

static void *sharedMemoryReadThread(void *arg)
{
    DataAcquisition *dataAc = (DataAcquisition *)arg;
    dataAc->SharedMemoryReadLoop();
    pthread_exit(NULL);
}

DataAcquisition::DataAcquisition()
{
    is_running = false;
    sem_id1 = nullptr;
    ShmKey = 0;
    ShmID = -1;
    ShmPTR = nullptr;
    seismicDataIndex = 0;
    readTid = 0;
    pthread_mutex_init(&queueMutex, NULL);
    DataAcquisition::instance = this;
}

DataAcquisition::~DataAcquisition()
{
    pthread_mutex_destroy(&queueMutex);
}

int DataAcquisition::setupSharedMemory()
{
    ShmKey = ftok(MEMNAME, 65);
    if(ShmKey == -1) {
        cout << "DataAcquisition: ftok() error" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    ShmID = shmget(ShmKey, sizeof(struct SeismicMemory), 0666);
    if(ShmID < 0) {
        cout << "DataAcquisition: shmget() error" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    ShmPTR = (struct SeismicMemory *)shmat(ShmID, NULL, 0);
    if(ShmPTR == (void *)-1) {
        cout << "DataAcquisition: shmat() error" << endl;
        cout << strerror(errno) << endl;
        ShmPTR = nullptr;
        return -1;
    }

    sem_id1 = sem_open(SEMNAME, 0);
    if(sem_id1 == SEM_FAILED) {
        cout << "DataAcquisition: sem_open() error" << endl;
        cout << strerror(errno) << endl;
        sem_id1 = nullptr;
        shmdt((void *)ShmPTR);
        ShmPTR = nullptr;
        return -1;
    }

    return 0;
}

void DataAcquisition::cleanupSharedMemory()
{
    if(sem_id1 != nullptr) {
        sem_close(sem_id1);
        sem_id1 = nullptr;
    }

    if(ShmPTR != nullptr) {
        shmdt((void *)ShmPTR);
        ShmPTR = nullptr;
    }
}

void DataAcquisition::pushPacket(const DataPacket &packet)
{
    pthread_mutex_lock(&queueMutex);
    packetQueue.push(packet);
    pthread_mutex_unlock(&queueMutex);
}

bool DataAcquisition::popPacket(DataPacket &packet)
{
    bool hasPacket = false;

    pthread_mutex_lock(&queueMutex);
    if(!packetQueue.empty()) {
        packet = packetQueue.front();
        packetQueue.pop();
        hasPacket = true;
    }
    pthread_mutex_unlock(&queueMutex);

    return hasPacket;
}

int DataAcquisition::getQueueSize()
{
    int size = 0;
    pthread_mutex_lock(&queueMutex);
    size = (int)packetQueue.size();
    pthread_mutex_unlock(&queueMutex);
    return size;
}

bool DataAcquisition::copyPacketFromSharedMemory()
{
    if(ShmPTR == nullptr || sem_id1 == nullptr) {
        return false;
    }

    bool copied = false;

    sem_wait(sem_id1);

    if(ShmPTR->seismicData[seismicDataIndex].status == WRITTEN) {
        DataPacket packet;
        packet.packetNo = ShmPTR->packetNo;
        packet.packetLen = ShmPTR->seismicData[seismicDataIndex].packetLen;

        if(packet.packetLen > BUF_LEN) {
            packet.packetLen = BUF_LEN;
        }

        memset(packet.data, 0, BUF_LEN);
        memcpy(packet.data,
               ShmPTR->seismicData[seismicDataIndex].data,
               packet.packetLen);

        ShmPTR->seismicData[seismicDataIndex].status = READ;
        copied = true;

        pushPacket(packet);

#ifdef DEBUG
        cout << "DataAcquisition: copied packet " << packet.packetNo
             << " from shared memory index " << seismicDataIndex
             << ", queue size = " << getQueueSize() << endl;
#endif
    }

    sem_post(sem_id1);

    ++seismicDataIndex;
    if(seismicDataIndex >= NUM_DATA) {
        seismicDataIndex = 0;
    }

    return copied;
}

void DataAcquisition::SharedMemoryReadLoop()
{
    while(is_running) {
        copyPacketFromSharedMemory();
        sleep(1);
    }
}

void DataAcquisition::shutdown()
{
    cout << "DataAcquisition::shutdown:" << endl;
    is_running = false;
}

int DataAcquisition::run()
{
    action.sa_handler = interruptHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, NULL);

    if(setupSharedMemory() != 0) {
        return -1;
    }

    is_running = true;

    int ret = pthread_create(&readTid, NULL, sharedMemoryReadThread, this);
    if(ret != 0) {
        cout << "DataAcquisition: Cannot create shared memory read thread" << endl;
        cout << strerror(ret) << endl;
        is_running = false;
        cleanupSharedMemory();
        return -1;
    }

    while(is_running) {
        // This loop keeps the DAU process alive while the shared memory
        // thread keeps filling the internal queue.
        // Hung will later add the UDP/network threads here.
        sleep(1);
    }

    pthread_join(readTid, NULL);
    cleanupSharedMemory();

    cout << "DataAcquisition: DONE" << endl;
    return 0;
}
