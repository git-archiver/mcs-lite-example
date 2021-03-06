#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/sockets.h"
#include "ethernetif.h"
#include "lwip/sockets.h"
#include "netif/etharp.h"
#include "timers.h"
#include "os.h"
#include "mcs.h"
#include "hal_sys.h"

//#define TCP_ip "192.168.1.241"

/* tcp config */
//#define SOCK_TCP_SRV_PORT 8000

#define MAX_STRING_SIZE 200
TimerHandle_t heartbeat_timer;


int32_t mcs_tcp_init(void (*mcs_tcp_callback)(char *),char *websocket_server, uint16_t websocket_port, char *device_id, char *device_key)
{
    int s;
    int c;
    int ret;
    struct sockaddr_in addr;
    int count = 0;
    int rcv_len, rlen;
    int errorStatus = 0;
    int successes = 0, failures = 0;
    int32_t mcs_ret = MCS_TCP_DISCONNECT;
    //uint16_t daria_port = 8000;

    /* command buffer */
    char cmd [300]= {0};

mcs_tcp_connect:
    errorStatus = 0;
    os_memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(websocket_port);
    addr.sin_addr.s_addr =inet_addr(websocket_server);

    /* create the socket */
    s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        mcs_ret = MCS_TCP_SOCKET_INIT_ERROR;
        printf("tcp client create fail 0\n");
        goto idle;
    }

    ret = lwip_connect(s, (struct sockaddr *)&addr, sizeof(addr));

    if (ret < 0) {
        lwip_close(s);
        printf("tcp client connect fail 1\n");
        goto mcs_tcp_connect;
    }

    sprintf(cmd, "GET /deviceId/%s/deviceKey/%s/viewer HTTP/1.1\r\n", device_id, device_key);
    ret = write(s, cmd, strlen(cmd));
    if (ret < 0) goto again;

    sprintf(cmd, "Upgrade: websocket\r\n");
    ret = write(s, cmd, strlen(cmd));
    if (ret < 0) goto again;

    sprintf(cmd, "Connection: Upgrade\r\n");
    ret = write(s, cmd, strlen(cmd));
    if (ret < 0) goto again;

    sprintf(cmd, "Sec-WebSocket-Version: 13\r\n");
    ret = write(s, cmd, strlen(cmd));
    if (ret < 0) goto again;

    sprintf(cmd, "Sec-WebSocket-Key: L159VM0TWUzyDxwJEIEzjw==\r\n");
    ret = write(s, cmd, strlen(cmd));
    if (ret < 0) goto again;

    //sprintf(cmd, "Host: %s\r\n", TCP_ip);
    sprintf(cmd, "Host: %s\r\n", websocket_server);
    ret = write(s, cmd, strlen(cmd));
    if (ret < 0) goto again;

    sprintf(cmd, "Origin: null\r\n\r\n");
    ret = write(s, cmd, strlen(cmd));
    if (ret < 0) goto again;

    char recv_buf[512];
    char *token;

    do {
        bzero(recv_buf, 512);
        ret = read(s, recv_buf, 511);
        if(ret > 0) {
            printf("[LEN: %d] %s", ret, recv_buf);
        }
        token = strtok(recv_buf, "\r\n\r\n");
        if (token != NULL)
            break;
    } while(ret > 0);

    /* handshak timer */
    if (successes == 0) {
        void tcpTimerCallback( TimerHandle_t pxTimer ) {
            unsigned char frame;
            frame = 0x01; // FIN
            frame = (0x01 << 4); // WebSocketOpcode.TEXT_FRAME
            if (errorStatus == 0) {
                ret = write(s, &frame, 1);
                printf("[RET: %d] 0x%04x\n, ", ret, frame);
            }
        }

        heartbeat_timer = xTimerCreate("TimerMain", (10*1000 / portTICK_RATE_MS), pdTRUE, (void *)0, tcpTimerCallback);
        xTimerStart( heartbeat_timer, 0 );
    }

    do {
        char rcv_buf1[MAX_STRING_SIZE] = {0};

        rcv_len = 0;
        rlen = lwip_recv(s, &rcv_buf1[rcv_len], sizeof(rcv_buf1) - 1 - rcv_len, 0);
        rcv_len += rlen;

        if ( 0 == rcv_len ) {
            errorStatus = 1;
            goto mcs_tcp_connect;
        }
        successes ++;

        int buf_len = strlen(rcv_buf1);

        for (int i = 2; i < buf_len; ++i) {
            rcv_buf1[i-2] = rcv_buf1[i];
            if (i == buf_len-1) {
                rcv_buf1[i-1] = '\0';
            }
        }

        printf("rcv_buf: %s\n", rcv_buf1);
        tcp_callback(rcv_buf1);
    } while(errorStatus < 1);

again:
    printf("... socket send failed\r\n");
    close(s);
    vTaskDelay(5000 / portTICK_RATE_MS);
    failures++;

idle:
    LOG_I(common, "MCS tcp-client end");
    return MCS_TCP_DISCONNECT;
}