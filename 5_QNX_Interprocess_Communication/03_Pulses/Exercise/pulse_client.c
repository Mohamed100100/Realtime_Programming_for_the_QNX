// pulse_client.c
// QNX Pulse Exercise - Client that sends both a pulse and a message
// Compile: qcc -o pulse_client pulse_client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <errno.h>

#define BUFFER_SIZE 512

// ============================================================
// Message Type Definition (must match server)
// ============================================================
#define MSG_TYPE_CHECKSUM   0x0200   // User-defined message type

struct checksum_msg {
    uint16_t type;
    char text[BUFFER_SIZE];
};

// ============================================================
// Main Client
// ============================================================
int main(int argc, char *argv[]) {
    int coid;
    int server_pid;
    int server_chid;
    struct checksum_msg msg;
    unsigned int reply_checksum;
    int status;

    // Check command line arguments
    // Usage: ./pulse_client <server_pid> <server_chid> <message>
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_pid> <server_chid> <message>\n", argv[0]);
        fprintf(stderr, "Example: %s 12345 1 \"Hello World\"\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    server_pid = atoi(argv[1]);
    server_chid = atoi(argv[2]);

    // ========================================================
    // Step 1: Connect to the server's channel
    // ========================================================
    coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach failed");
        exit(EXIT_FAILURE);
    }

    printf("========================================\n");
    printf("  Pulse Client\n");
    printf("  Connected to Server PID: %d, Channel: %d\n", server_pid, server_chid);
    printf("  Connection ID (coid): %d\n", coid);
    printf("========================================\n\n");

    // ========================================================
    // Step 2: Send a PULSE first (non-blocking, no reply)
    // ========================================================
    printf("Sending PULSE to server...\n");

    // MsgSendPulse(coid, priority, code, value)
    //   priority = -1  -> use caller's thread priority
    //   code     = 3   -> user-defined pulse code (0-127)
    //   value    = 0xdeadc0de -> fun 32-bit value
    status = MsgSendPulse(coid, -1, 3, 0xdeadc0de);
    if (status == -1) {
        perror("MsgSendPulse failed");
        ConnectDetach(coid);
        exit(EXIT_FAILURE);
    }
    printf("  Pulse sent successfully!\n");
    printf("    Code: 3\n");
    printf("    Value: 0xdeadc0de\n\n");

    // ========================================================
    // Step 3: Prepare and send the CHECKSUM MESSAGE
    // ========================================================
    // Fill in the message structure
    msg.type = MSG_TYPE_CHECKSUM;
    strncpy(msg.text, argv[3], BUFFER_SIZE - 1);
    msg.text[BUFFER_SIZE - 1] = '\0';

    printf("Sending CHECKSUM MESSAGE to server...\n");
    printf("  Message: "%s"\n\n", msg.text);

    // Send message and wait for reply (synchronous, blocking)
    status = MsgSend(coid, &msg, sizeof(msg.type) + strlen(msg.text) + 1,
                     &reply_checksum, sizeof(reply_checksum));

    if (status == -1) {
        perror("MsgSend failed");
        ConnectDetach(coid);
        exit(EXIT_FAILURE);
    }

    // ========================================================
    // Step 4: Process the reply
    // ========================================================
    printf("MsgSend() returned status: %d\n", status);
    printf("Server replied with checksum: %u\n\n", reply_checksum);

    // ========================================================
    // Step 5: Detach the connection
    // ========================================================
    ConnectDetach(coid);

    printf("Client finished successfully!\n");
    return 0;
}
