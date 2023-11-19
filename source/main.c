#include "main.h"

// https://github.com/switchbrew/nx-hbmenu/blob/master/nx_main/nx_netstatus.c
bool check_internet_status()
{
    NifmInternetConnectionType type;
    u32 wifiStrength;
    NifmInternetConnectionStatus status;

    if (R_FAILED(nifmGetInternetConnectionStatus(&type, &wifiStrength, &status)))
        return false;

    if (type == NifmInternetConnectionType_Ethernet)
    {
        if (status != NifmInternetConnectionStatus_Connected)
            return false;

        return true;
    }

    if (wifiStrength == 0)
        return false;

    return true;
}

const char *request_command_type_to_string(ECommandType type)
{
    switch (type)
    {
    case CMD_PING:
        return "CMD_PING";
    case CMD_GET_CERT_FOR_TITLE:
        return "CMD_GET_CERT_FOR_TITLE";
    case CMD_COMPLETE_CHALLENGE:
        return "CMD_COMPLETE_CHALLENGE";
    case CMD_RESPONSE:
    case CMD_RESPONSE_FAILED:
    default:
        return "CMD_UNKNOWN";
    }
}

int main(int argc, char *argv[])
{
    consoleInit(NULL);

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    RETURN_ON_FAIL(nsInitialize(), "nsInitialize failed!");
    RETURN_ON_FAIL(socketInitializeDefault(), "socketInitializeDefault failed!");
    RETURN_ON_FAIL(nifmInitialize(NifmServiceType_Admin), "nifmInitialize failed!");

    u64 tid;
    s32 out;
    bool cond = R_FAILED(nsListApplicationIdOnGameCard(&tid, 1, &out)) || out == 0;
    RETURN_ON_COND(cond, "Failed to get gamecard title id!\n");

    printf("Welcome to nx-netauth-link! (Press + to exit at anytime)\n");

    RETURN_ON_FAIL(fsInitialize(), "fsInitialize failed!");

    FsDeviceOperator deviceOperator;
    RETURN_ON_FAIL(fsOpenDeviceOperator(&deviceOperator), "fsOpenDeviceOperator failed!");

    FsGameCardHandle gcHandle = {-1};
    RETURN_ON_FAIL(fsDeviceOperatorGetGameCardHandle(&deviceOperator, &gcHandle), "fsDeviceOperatorGetGameCardHandle failed!");

    printf("Waiting for internet connection ...\n");
    while (!check_internet_status())
    {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus)
            goto exit;

        consoleUpdate(NULL);
    }

    printf("Connected to internet!\n\n");

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    RETURN_ON_COND(fd < 0, "Failed to create a socket!\n");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7789);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    RETURN_ON_COND(bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0, "Failed to bind address to socket!\n");

    int flags = fcntl(fd, F_GETFL, 0);
    RETURN_ON_COND(flags == -1, "Failed to get socket flags\n");
    RETURN_ON_COND(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1, "Failed setting socket to non-blocking mode\n");

    u32 ip;
    nifmGetCurrentIpAddress(&ip);
    printf("Nintendo Switch Local IP: %s\n\n", inet_ntoa((struct in_addr){ip}));

    printf("Gamecard title id: %016lx (check it matches, PLEASE)\n", tid);
    printf("Waiting for commands ...\n\n");

    // Main loop
    while (appletMainLoop())
    {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus)
            break;

        consoleUpdate(NULL);

        u8 buffer[256];
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        ssize_t len = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (len == -1)
        {
            if (errno == EWOULDBLOCK)
                continue;

            RETURN_ON_COND(true, "Unknown socket error!\n");
        }
        else if (len > 0)
        {
            u8 command = buffer[0];
            printf("Received command: %s -> ", request_command_type_to_string((ECommandType)command));

            if (command == CMD_PING)
            {
                SPingResponse response = {CMD_RESPONSE};
                sendto(fd, &response, sizeof(response), 0, (struct sockaddr *)&client_addr, client_addr_len);
                printf(CONSOLE_GREEN "Success!" CONSOLE_RESET "\n");
            }
            else if (command == CMD_GET_CERT_FOR_TITLE)
            {

                SGetCertForTitleRequest *request = (SGetCertForTitleRequest *)buffer;
                SGetCertForTitleResponse response;

                if (__builtin_bswap64(request->title_id) != tid)
                {
                    response.type = CMD_RESPONSE_FAILED;
                    sendto(fd, &response, sizeof(response), 0, (struct sockaddr *)&client_addr, client_addr_len);
                    printf(CONSOLE_RED "Failed!" CONSOLE_RESET " (title id mismatch)\n");
                }
                else
                {

                    memset(response.cert, 0, sizeof(response.cert));
                    Result rc = fsDeviceOperatorGetGameCardDeviceCertificate(&deviceOperator, &gcHandle, response.cert, sizeof(response.cert), sizeof(response.cert));

                    if (R_FAILED(rc))
                    {
                        response.type = CMD_RESPONSE_FAILED;
                        sendto(fd, &response, sizeof(response), 0, (struct sockaddr *)&client_addr, client_addr_len);
                        printf(CONSOLE_RED "Failed!" CONSOLE_RESET " (with code %0x08x, you may need to restart application and replug gamecard)\n", rc);
                    }
                    else
                    {
                        response.type = CMD_RESPONSE;
                        sendto(fd, &response, sizeof(response), 0, (struct sockaddr *)&client_addr, client_addr_len);
                        printf(CONSOLE_GREEN "Success!" CONSOLE_RESET "\n");
                    }
                }
            }
            else if (command == CMD_COMPLETE_CHALLENGE)
            {
                SCompleteChallengeRequest *request = (SCompleteChallengeRequest *)buffer;
                SCompleteChallengeResponse response;

                Result rc = fsDeviceOperatorChallengeCardExistence(&deviceOperator, &gcHandle,
                                                                   response.challenge, sizeof(response.challenge),
                                                                   request->seed, sizeof(request->seed),
                                                                   request->value, sizeof(request->value));

                if (R_FAILED(rc))
                {
                    response.type = CMD_RESPONSE_FAILED;
                    sendto(fd, &response, sizeof(response), 0, (struct sockaddr *)&client_addr, client_addr_len);
                    printf(CONSOLE_RED "Failed!" CONSOLE_RESET " (with code %0x08x, you may need to restart application and replug gamecard)\n", rc);
                }
                else
                {
                    response.type = CMD_RESPONSE;
                    sendto(fd, &response, sizeof(response), 0, (struct sockaddr *)&client_addr, client_addr_len);
                    printf(CONSOLE_GREEN "Success!" CONSOLE_RESET "\n");
                }
            }
            else
            {
                printf("Unknown command!\n");
            }
        }
    }

exit:

    fsDeviceOperatorClose(&deviceOperator);
    fsExit();

    nifmExit();
    socketExit();
    nsEnsureGameCardAccess();
    nsExit();

    consoleExit(NULL);
    return 0;
}
