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

/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128
#define INPUT_ROWS 2

#define RECEIVE_START 2
#define RECEIVE_END 20
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
void print_receive(const char *msg);

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

    char message[MESSAGE_SIZE] = "";
    int message_idx = 0;
    int keycode = 0;
    char key = ' ';
    int shift = 0;

    char keystate[12];

    for (;;) {
        libusb_interrupt_transfer(keyboard, endpoint_address,
                        (unsigned char *) &packet, sizeof(packet),
                        &transferred, 0);
        if (transferred == sizeof(packet)) {
            sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0], packet.keycode[1]);
            printf("%s\n", keystate);

            fbdraw_cursor(fbmaxrows() - 1 - INPUT_ROWS + (message_idx / fbmaxcols()), message_idx % fbmaxcols());

            if (packet.keycode[0] == 40 || packet.keycode[1] == 40){ // Enter pressed
                if (message_idx != 0) {
                    // Send message
                    message[message_idx] = '\n';
                    write(sockfd, message, message_idx+1);

                    // Clear buffer
                    memset(message, 0, sizeof(message));

                    // Clear Screen
                    for (int i = 0; i < INPUT_ROWS; i++) {
                        for (int j = 0; j < fbmaxcols(); j++) {
                            fbputchar(' ', fbmaxrows() - 1 - INPUT_ROWS + i, j);
                        }
                    }

                    // Reset message index
                    message_idx = 0;
                }
            } else if (packet.keycode[0] == 0x00 || packet.keycode[1] == 0x00) { // Single character being pressed
                if (message_idx < MESSAGE_SIZE - 1) {
                    if (packet.keycode[0] == 0x00) {
                        keycode = packet.keycode[1];
                    } else {
                        keycode = packet.keycode[0];
                    }

                    if (keycode != 0) {
                        shift = packet.modifiers & (0x02 | 0x20);
    
                        if (keycode >= 4 && keycode <= 29) {
                            if (shift == 0) {
                                key = 'a' + (keycode - 4);
                            } else {
                                key = 'A' + (keycode - 4);
                            }
                        } else if (keycode == 44) {
                            key = ' ';
                        } else {
                            key = '?';
                        }
                        message[message_idx] = key;
    
                        fbputchar(key, fbmaxrows() - 1 - INPUT_ROWS + (message_idx / fbmaxcols()), message_idx % fbmaxcols());
    
                        message_idx++;
                    }
                }
            }
        }
    }

    /* Terminate the network thread */
    pthread_cancel(network_thread);

    /* Wait for the network thread to finish */
    pthread_join(network_thread, NULL);

    return 0;
}

void *network_thread_f(void *ignored){
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
                receive_row = RECEIVE_START;
            }

            fbputs(line, receive_row, 0);
            receive_row++;

            pos += chunk;
        }
    }

    return NULL;
}

