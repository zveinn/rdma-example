
#include <stdint.h>
#include <stdio.h>

const int ListenerBacklog = 20;

const int16_t ErrNone = 0;
const int16_t ErrInvalidServerIP = 1;
const int16_t ErrUnableToCreateEventChannel = 2;
const int16_t ErrUnableToCreateServerCMID = 3;
const int16_t ErrUnableToBindToAddress = 4;
const int16_t ErrUnableToListenOnAddress = 5;
const int16_t ErrUnableToResolveAddress = 6;
const int16_t ErrUnableToTooManyConnections = 7;
const int16_t ErrUnableToEstablishConnection = 8;
const int16_t ErrUnableToAcceptConnection = 9;
const int16_t ErrUnableToAllocatePD = 10;
const int16_t ErrUnableToCreateCompletionChannel = 11;
const int16_t ErrUnableToCreateCompletionQueue = 12;
const int16_t ErrUnableToRegisterCQNotifications = 13;
const int16_t ErrUnableToCreateQueuePairs = 14;
const int16_t ErrUnableToGetFromEventChannel = 15;
const int16_t ErrUnableToPollEventChannelFD = 16;

const int16_t ErrUnexpectedEventStatus = 77;
const int16_t ErrUnexpectedEventType = 78;

const int16_t ErrUnableToGetEventChannelFlags = 96;
const int16_t ErrUnableToSetEventChannelToNoneBlocking = 97;
const int16_t ErrUnableToAckEvent = 98;
const int16_t ErrUnableToCreateThread = 99;

const int16_t CodeOK = 100;

// uint32_t goError(int16_t funcCode, int16_t minioCode);
uint32_t makeError(int8_t funcCode, int8_t minioCode, int8_t extra1, int8_t extra2);
void printBits(uint32_t value);
uint64_t timestampDiff(struct timespec *t1, struct timespec *t2);
