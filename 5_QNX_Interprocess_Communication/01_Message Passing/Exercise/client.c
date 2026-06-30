// client.c
// QNX Message Passing Exercise - Checksum Client
// Compile: qcc -o client client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <errno.h>

#define BUFFER_SIZE 512

int main(int argc, char *argv[]) {
    int coid;
    int server_pid;
    int server_chid;
    char send_buffer[BUFFER_SIZE];
    unsigned int reply_checksum;
    int status;

    // Check command line arguments
    // Usage: ./client <server_pid> <server_chid> <message>
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_pid> <server_chid> <message>
", argv[0]);
        fprintf(stderr, "Example: %s 12345 1 "Hello World"
", argv[0]);
        exit(EXIT_FAILURE);
    }

    server_pid = atoi(argv[1]);
    server_chid = atoi(argv[2]);

    // Copy the message to send
    strncpy(send_buffer, argv[3], BUFFER_SIZE - 1);
    send_buffer[BUFFER_SIZE - 1] = '\0';

    // TODO 1: Connect to the server's channel
    // Use ConnectAttach() with:
    //   nd = 0 (local node)
    //   pid = server_pid
    //   chid = server_chid
    //   index = _NTO_SIDE_CHANNEL (mandatory!)
    //   flags = 0
    coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach failed");
        exit(EXIT_FAILURE);
    }

    printf("========================================
");
    printf("  Checksum Client
");
    printf("  Connected to Server PID: %d, Channel: %d
", server_pid, server_chid);
    printf("  Connection ID (coid): %d
", coid);
    printf("========================================
");
    printf("Sending message: "%s"

", send_buffer);

    // TODO 2: Send the message to the server and wait for reply
    // Use MsgSend() with:
    //   coid = connection ID
    //   smsg = send_buffer (the string)
    //   sbytes = strlen(send_buffer) + 1 (include null terminator)
    //   rmsg = &reply_checksum (buffer for server's reply)
    //   rbytes = sizeof(reply_checksum)
    status = MsgSend(coid, send_buffer, strlen(send_buffer) + 1,
                      &reply_checksum, sizeof(reply_checksum));

    // TODO 3: Check the result
    // MsgSend() returns the status from MsgReply() on success
    // Returns -1 on error
    if (status == -1) {
        perror("MsgSend failed");
        ConnectDetach(coid);
        exit(EXIT_FAILURE);
    }

    printf("MsgSend() returned status: %d
", status);
    printf("Server replied with checksum: %u
", reply_checksum);

    // TODO 4: Detach the connection
    ConnectDetach(coid);

    printf("
Client finished successfully!
");
    return 0;
}
