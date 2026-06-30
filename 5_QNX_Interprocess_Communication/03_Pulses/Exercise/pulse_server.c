// pulse_server.c
// QNX Pulse Exercise - Server that handles both messages and pulses
// Compile: qcc -o pulse_server pulse_server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <errno.h>

#define BUFFER_SIZE 512

// ============================================================
// Message Type Definition
// ============================================================
#define MSG_TYPE_CHECKSUM   0x0200   // User-defined message type (above _IO_MAX)

struct checksum_msg {
    uint16_t type;          // Must be first field — message type
    char text[BUFFER_SIZE]; // String to calculate checksum on
};

// ============================================================
// Receive Buffer Union
// ============================================================
// This is a SERVER IMPLEMENTATION DETAIL — not part of the client/server protocol.
// The union guarantees the buffer is at least as large as struct _pulse (24 bytes).
// We include a bare 'type' field so we can inspect the type before deciding
// which union member to access.

typedef union {
    struct _pulse       pulse;      // 24 bytes — ensures minimum buffer size
    uint16_t            type;       // Bare type field for quick inspection
    struct checksum_msg checksum;   // Checksum message structure
} recv_buf_t;

// ============================================================
// Checksum Calculation
// ============================================================
unsigned int calculate_checksum(const char *str) {
    unsigned int checksum = 0;
    while (*str) {
        checksum += (unsigned char)(*str);
        str++;
    }
    return checksum;
}

// ============================================================
// Main Server
// ============================================================
int main(int argc, char *argv[]) {
    int chid;
    int rcvid;
    recv_buf_t msg;
    struct _msg_info info;
    unsigned int checksum;

    // Create a channel for receiving messages and pulses
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate failed");
        exit(EXIT_FAILURE);
    }

    printf("========================================
");
    printf("  Pulse Server Started
");
    printf("  PID: %d
", getpid());
    printf("  Channel ID: %d
", chid);
    printf("========================================
");
    printf("Waiting for messages and pulses...

");

    // Main server loop
    for (;;) {
        // Block waiting for a message or pulse
        // sizeof(msg) is guaranteed >= sizeof(struct _pulse) because
        // the union includes struct _pulse
        rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);
        if (rcvid == -1) {
            perror("MsgReceive failed");
            continue;
        }

        // ========================================================
        // PULSE CASE: rcvid == 0
        // ========================================================
        if (rcvid == 0) {
            printf("[PULSE RECEIVED]
");
            printf("  Pulse code: %d
", msg.pulse.code);
            printf("  Pulse value: 0x%08lx (%lu)
",
                   (unsigned long)msg.pulse.value.sival_long,
                   (unsigned long)msg.pulse.value.sival_long);
            printf("  From scoid: %d
", msg.pulse.scoid);
            printf("----------------------------------------

");
            continue;  // Do NOT reply to a pulse!
        }

        // ========================================================
        // MESSAGE CASE: rcvid > 0
        // ========================================================
        // First, inspect the type field to determine message type
        switch (msg.type) {

            case MSG_TYPE_CHECKSUM:
                printf("[CHECKSUM MESSAGE RECEIVED]
");
                printf("  From PID %d, TID %d
", info.pid, info.tid);
                printf("  Message: "%s"
", msg.checksum.text);

                // Calculate checksum
                checksum = calculate_checksum(msg.checksum.text);
                printf("  Calculated checksum: %u
", checksum);

                // Reply to the client with the checksum
                if (MsgReply(rcvid, EOK, &checksum, sizeof(checksum)) == -1) {
                    perror("MsgReply failed");
                } else {
                    printf("  Reply sent successfully!
");
                }
                break;

            default:
                printf("[UNKNOWN MESSAGE] Type: 0x%04x
", msg.type);
                MsgError(rcvid, ENOSYS);
                break;
        }

        printf("----------------------------------------

");
    }

    // Cleanup (unreachable in this example)
    ChannelDestroy(chid);
    return 0;
}
