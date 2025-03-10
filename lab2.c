/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI:
 *      Matthew Modi (mem2382)
 *      Kamil Zajkowski (kmz2123)
 *      Rahul Pulidini (rp3254)
 */
#include "fbputchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "usbkeyboard.h"
#include <pthread.h>
#include <time.h>

/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128
#define INPUT_ROWS 5

#define REPEAT_INTERVAL 0.2 // seconds
#define REPEAT_DELAY 1 // seconds

#define RECEIVE_START 2
int receive_row = RECEIVE_START;

/*
 * References:
 *
 * https://web.archive.org/web/20130307100215/http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 *
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);

int main(){
    int err, col;

    struct sockaddr_in serv_addr;

    struct usb_keyboard_packet packet;
    int transferred;

    if ((err = fbopen()) != 0) {
        fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
        exit(1);
    }

    /* Clear the entire screen and setup the display layout
     * We clear the entire screen, then draw a horizontal separator
     * at rows 21 so that rows 21-23 can serve as the input region
     * Finally, we display a cursor in the input region
    */
    fbclear();                // Clear the entire screen
    fbgradient();             // Draw a gradient background

    /* Draw rows of asterisks across the top and bottom of the screen */
    for (col = 0 ; col < fbmaxcols() ; col++) {
        fbputchar('*', 1, col);
        fbputchar('-', fbmaxrows()-2-INPUT_ROWS, col); // Horizontal separator
        fbputchar('*', fbmaxrows()-1, col);
    }

    fbputs("CSEE 4840 Chat", 0, fbmaxcols() >= 14 ? (fbmaxcols()/2)-7 : 0);

    /* Open the keyboard */
    if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
        fprintf(stderr, "Did not find a keyboard\n");
        exit(1);
    }
        
    /* Create a TCP communications socket */
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        fprintf(stderr, "Error: Could not create socket\n");
        exit(1);
    }

    /* Get the server address */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
        exit(1);
    }

    /* Connect the socket to the server */
    if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Error: connect() failed.    Is the server running?\n");
        exit(1);
    }

    /* Start the network thread */
    pthread_create(&network_thread, NULL, network_thread_f, NULL);

    /* Look for and handle keypresses */
    int MESSAGE_SIZE = INPUT_ROWS * fbmaxcols();
    char* message = (char*) malloc(MESSAGE_SIZE * sizeof(char));
    if (!message) {
        fprintf(stderr, "Error: Could not allocate memory for message buffer\n");
        exit(1);
    }
    int message_len = 0;
    int cursor_idx = 0;
    int keycode = 0;
    char key = ' ';
    int shift = 0;
    int keys[2];

    char keystate[12];

    for (;;) {
        libusb_interrupt_transfer(keyboard, endpoint_address,
                        (unsigned char *) &packet, sizeof(packet),
                        &transferred, 0);
        if (transferred == sizeof(packet)) {
            sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0], packet.keycode[1]);
            printf("%s\n", keystate);
            
            // erase all cursor space 
            for (int i = 0; i < MESSAGE_SIZE; i++) {
                fberase_cursor(fbmaxrows() - 1 - INPUT_ROWS + (i / fbmaxcols()), i % fbmaxcols());
            }
            fbdraw_cursor(fbmaxrows() - 1 - INPUT_ROWS + (cursor_idx / fbmaxcols()), cursor_idx % fbmaxcols());

            if (packet.keycode[0] == 40 || packet.keycode[1] == 40){ // Enter pressed
                if (message_len != 0) {
                    // Send message
                    message[message_len] = '\n';
                    write(sockfd, message, message_len+1);

                    // Clear buffer
                    memset(message, 0, MESSAGE_SIZE);

                    // Clear Screen
                    for (int i = 0; i < INPUT_ROWS; i++) {
                        for (int j = 0; j < fbmaxcols(); j++) {
                            fbputchar(' ', fbmaxrows() - 1 - INPUT_ROWS + i, j);
                        }
                    }

                    // Reset message index
                    message_len = 0;
                    cursor_idx = 0;
                }
            } else { // Single character being pressed
                if (message_len < MESSAGE_SIZE - 1) { // Still room in the text box
                    // From packet.keycode[0] and packet.keycode[1], determine the next keycode which was not already being pressed in the last set and store it in keycode. keys[0] and keys[1] store the last set of keycodes.
                    if (packet.keycode[0] != keys[0] && packet.keycode[0] != keys[1]) {
                        keycode = packet.keycode[0];
                    } else if (packet.keycode[1] != keys[0] && packet.keycode[1] != keys[1]) {
                        keycode = packet.keycode[1];
                    } else {
                        keycode = 0;
                    }
                    keys[0] = packet.keycode[0];
                    keys[1] = packet.keycode[1];
                    

                    if (keycode == 42) { // Backspace
                        if (cursor_idx > 0) {
                            cursor_idx--;
                            for (int i = cursor_idx; i < message_len - 1; i++) {
                                message[i] = message[i + 1];
                                fbputchar(message[i], fbmaxrows() - 1 - INPUT_ROWS + (i / fbmaxcols()), i % fbmaxcols());
                            }
                            message[message_len-1] = ' ';
                            fbputchar(' ', fbmaxrows() - 1 - INPUT_ROWS + ((message_len-1) / fbmaxcols()), message_len-1 % fbmaxcols());
                            message_len--;
                        }
                    } else if (keycode == 76) { // Forward delete
                        if (cursor_idx < message_len) {
                            for (int i = cursor_idx; i < message_len - 1; i++) {
                                message[i] = message[i + 1];
                                fbputchar(message[i], fbmaxrows() - 1 - INPUT_ROWS + (i / fbmaxcols()), i % fbmaxcols());
                            }
                            fbputchar(' ', fbmaxrows() - 1 - INPUT_ROWS + ((message_len-1) / fbmaxcols()), message_len-1 % fbmaxcols());
                            message_len--;
                        }
                    } else if (keycode == 80) { // Left
                        if (cursor_idx > 0) {
                            cursor_idx--;
                        }
                    } else if (keycode == 79) { // Right
                        if (cursor_idx < message_len-1) {
                            cursor_idx++;
                        }
                    } else if (keycode == 82 // Up
                            || keycode == 81 // Down
                            || keycode == 43 // Tab
                            || keycode == 41 // Escape
                            || keycode == 83 // Num Lock
                            ) {
                        // Do nothing
                    } else if (keycode != 0) { // Type character or question mark
                        shift = packet.modifiers & (0x02 | 0x20);
    
                        if (keycode >= 4 && keycode <= 29) {
                            if (shift == 0) {
                                key = 'a' + (keycode - 4);
                            } else {
                                key = 'A' + (keycode - 4);
                            }
                        } else if (keycode >= 30 && keycode <= 39) {
                            if (shift == 0) {
                                key = '0' + (keycode - 30);
                            } else {
                                key = ")!@#$%^&*("[keycode - 30];
                            }
                        } else if (keycode >= 45 && keycode <= 56) { // Special Characters
                            if (shift == 0) {
                                key = "-=[]\\ ;'`,./"[keycode - 45];
                            } else {
                                key = "_+{}| :\"~<>?"[keycode - 45];
                            }
                        } else if (keycode == 44) {
                            key = ' ';
                        } else {
                            key = '?';
                        }

                        // Type character while shifting everything else over and inserting key into first index
                        for (int i = message_len; i > cursor_idx; i--) {
                            message[i] = message[i-1];
                            fbputchar(message[i], fbmaxrows() - 1 - INPUT_ROWS + (i / fbmaxcols()), i % fbmaxcols());
                        }
                        message[cursor_idx] = key;
                        fbputchar(key, fbmaxrows() - 1 - INPUT_ROWS + (cursor_idx / fbmaxcols()), cursor_idx % fbmaxcols());
    
                        message_len++;
                        cursor_idx++;
                    }
                }
            }
        }
    }
    free(message);

    /* Terminate the network thread */
    pthread_cancel(network_thread);

    /* Wait for the network thread to finish */
    pthread_join(network_thread, NULL);

    return 0;
}

void *network_thread_f(void *ignored){
    int RECEIVE_END = fbmaxrows() - INPUT_ROWS - 2;
    char recvBuf[BUFFER_SIZE];
    int n;
    /* Receive data */
    while ( (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0 ) {
        recvBuf[n] = '\0';
        printf("%s", recvBuf);
        int len = strlen(recvBuf);
        int pos = 0;
        char line[256];

        while (pos < len) {
            int chunk = (len - pos > fbmaxcols() ? fbmaxcols() : len - pos);
            strncpy(line, recvBuf + pos, chunk);
            line[chunk] = '\0';

            if (receive_row >= RECEIVE_END) {
                // Clear the entire receive region
                for (int r = RECEIVE_START; r < RECEIVE_END; r++) {
                    for (int c = 0; c < fbmaxcols(); c++) {
                        fbputchar(' ', r, c);
                    }
                }
                receive_row = RECEIVE_START;
            }

            fbputs(line, receive_row, 0);
            receive_row++;

            pos += chunk;
        }
    }

    return NULL;
}

