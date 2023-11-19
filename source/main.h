#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define RETURN_ON_FAIL(rc, text)                         \
    {                                                    \
        Result abortlocalerrorcode = rc;                 \
        if (R_FAILED(abortlocalerrorcode))               \
        {                                                \
            printf(text);                                \
            printf(" -> 0x%08x\n", abortlocalerrorcode); \
            printf("Exiting in 3 seconds ...\n");        \
            consoleUpdate(NULL);                         \
            svcSleepThread(3ULL * 1000 * 1000 * 1000);   \
            return -1;                                   \
        }                                                \
    }

#define RETURN_ON_COND(cond, text)                     \
    {                                                  \
        if (cond)                                      \
        {                                              \
            printf(text);                              \
            printf("Exiting in 3 seconds ...\n");      \
            consoleUpdate(NULL);                       \
            svcSleepThread(3ULL * 1000 * 1000 * 1000); \
            return -1;                                 \
        }                                              \
    }

typedef enum __attribute__((__packed__))
{
    CMD_RESPONSE = 0,
    CMD_RESPONSE_FAILED = 1,
    CMD_PING,
    CMD_GET_CERT_FOR_TITLE,
    CMD_COMPLETE_CHALLENGE,
} ECommandType;

static_assert(sizeof(ECommandType) == 1, "ECommandType is not 1 byte long");

///====================================
/// Ping command structures
///====================================

typedef struct __attribute__((__packed__))
{
    ECommandType type;
} SPingRequest;

typedef struct __attribute__((__packed__))
{
    ECommandType type;
    bool success;
} SPingResponse;

///====================================
/// Get cert command structures
///====================================

typedef struct __attribute__((__packed__))
{
    ECommandType type;
    u64 title_id;
} SGetCertForTitleRequest;

typedef struct __attribute__((__packed__))
{
    ECommandType type;
    u8 cert[0x200];
} SGetCertForTitleResponse;

///====================================
/// Complete challenge command structures
///====================================

typedef struct __attribute__((__packed__))
{
    ECommandType type;
    u8 value[16];
    u8 seed[15];
} SCompleteChallengeRequest;

typedef struct __attribute__((__packed__))
{
    ECommandType type;
    u8 challenge[0x58];
} SCompleteChallengeResponse;