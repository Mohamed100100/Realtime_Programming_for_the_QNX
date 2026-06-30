// server.c
// QNX Message Passing Exercise - Checksum Server
// Compile: qcc -o server server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <errno.h>

#define BUFFER_SIZE 512

// Simple checksum: sum of all bytes in the string
unsigned int calculate_checksum(const char *str) {
    unsigned int checksum = 0;
    while (*str) {
        checksum += (unsigned char)(*str);
        str++;
    }
    return checksum;
}

int main(int argc, char *argv[]) {
    int chid;
    int rcvid;
    char recv_buffer[BUFFER_SIZE];
    unsigned int checksum;
    struct _msg_info info;

    // TODO 1: Create a channel for receiving messages
    // Use ChannelCreate() with default flags (0)
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate failed");
        exit(EXIT_FAILURE);
    }

    printf("========================================
");
    printf("  Checksum Server Started
");
    printf("  PID: %d
", getpid());
    printf("  Channel ID: %d
", chid);
    printf("========================================
");
    printf("Waiting for client messages...

");

    // Main server loop
    for (;;) {
        // TODO 2: Receive a message from the client
        // Use MsgReceive() with the channel ID
        rcvid = MsgReceive(chid, recv_buffer, sizeof(recv_buffer), &info);
        if (rcvid == -1) {
            perror("MsgReceive failed");
            continue;
        }

        // Check if it's a pulse (not a regular message)
        if (rcvid == 0) {
            printf("Received a pulse (not a message)
");
            continue;
        }

        printf("Received message from PID %d, TID %d
", info.pid, info.tid);
        printf("Client message: "%s"
", recv_buffer);

        // Calculate checksum
        checksum = calculate_checksum(recv_buffer);
        printf("Calculated checksum: %u
", checksum);

        // TODO 3: Reply to the client with the checksum
        // Use MsgReply() with rcvid, status=EOK, and the checksum data
        if (MsgReply(rcvid, EOK, &checksum, sizeof(checksum)) == -1) {
            perror("MsgReply failed");
        } else {
            printf("Reply sent successfully!
");
        }

        printf("----------------------------------------

");
    }

    // Cleanup (unreachable in this example)
    ChannelDestroy(chid);
    return 0;
}
