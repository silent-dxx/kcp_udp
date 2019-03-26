#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __MINGW32__
#include <windows.h>
#include <ws2tcpip.h>
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <pthread.h>

#include "isleep.h"
#include "ikcp.c"

#define SERVER_PORT 8888
#define BUFF_LEN    512
#define SERVER_IP   "127.0.0.1"

int client_fd;
ikcpcb *kcp1;

void * udp_msg_sender(void *argp)
{
	int fd = (int) argp;
	socklen_t len;
	int hr;
	char buffer[BUFF_LEN];
	struct sockaddr_in clent_addr;

	while (1) {
		memset(buffer, 0, BUFF_LEN);
		hr = recvfrom(fd, buffer, BUFF_LEN, 0, (struct sockaddr*)&clent_addr, &len);

		if (hr == -1) {
			//printf("recieve data fail!\n");
			continue;
		}

		//printf("ikcp_input: hr: %d\n", hr);
		// ��� p1�յ�udp������Ϊ�²�Э�����뵽kcp1
		ikcp_input(kcp1, buffer, hr);
	}
}

void * handle_update(void *argp)
{
	while (1) {
		isleep(1);
		ikcp_update(kcp1, iclock());
	}
}

int tx1 = 0;

// ģ�����磺ģ�ⷢ��һ�� udp��
int udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
	//union { int id; void *ptr; } parameter;
	//parameter.ptr = user;

	struct sockaddr_in ser_addr;
	memset(&ser_addr, 0, sizeof(ser_addr));
	ser_addr.sin_family      = AF_INET;
	ser_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
	ser_addr.sin_port        = htons(8888);

	//printf("%s call udp sendto\n", __func__);printf("%s call udp sendto\n", __func__);
	sendto(client_fd, buf, len, 0, (struct sockaddr*)&ser_addr, sizeof(struct sockaddr_in));

	tx1++;

	return 0;
}

int main(int argc, char* argv[])
{
	int ret;
	struct sockaddr_in ser_addr;

#ifdef __MINGW32__
	WORD sockVersion = MAKEWORD(2,2);
	WSADATA data;

	if (WSAStartup(sockVersion, &data) != 0) {
		return -1;
	}
#endif

	client_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (client_fd < 0) {
		printf("create socket fail!\n");
		return -1;
	}

	memset(&ser_addr, 0, sizeof(ser_addr));
	ser_addr.sin_family      = AF_INET;
	ser_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	ser_addr.sin_port        = htons(8889);

	ret = bind(client_fd, (struct sockaddr*)&ser_addr, sizeof(ser_addr));
	if (ret < 0) {
		printf("socket bind fail!\n");
		return -1;
	}

	// ���������˵�� kcp���󣬵�һ������ conv�ǻỰ��ţ�ͬһ���Ự��Ҫ��ͬ
	// ���һ���� user�������������ݱ�ʶ
	kcp1 = ikcp_create(0x11223344, (void*)0);

	// ����kcp���²����������Ϊ udp_output��ģ��udp�����������
	//kcp1->output = udp_output;
	ikcp_setoutput(kcp1, udp_output);

	// ���ô��ڴ�С��ƽ���ӳ�200ms��ÿ20ms����һ������
	// �����ǵ������ط�����������շ�����Ϊ128
	ikcp_wndsize(kcp1, 2048, 2048);

	// ��������ģʽ
	// �ڶ������� nodelay-�����Ժ����ɳ�����ٽ�����
	// ���������� intervalΪ�ڲ�����ʱ�ӣ�Ĭ������Ϊ 10ms
	// ���ĸ����� resendΪ�����ش�ָ�꣬����Ϊ2
	// ��������� Ϊ�Ƿ���ó������أ������ֹ
	ikcp_nodelay(kcp1, 1, 10, 2, 1);
	kcp1->rx_minrto = 10;
	kcp1->fastresend = 1;

	//udp_msg_sender(client_fd);
	pthread_t recvdata_id;
	pthread_t update_id;
	pthread_create(&recvdata_id, NULL, udp_msg_sender, (void*)client_fd);
	pthread_create(&update_id, NULL, handle_update, NULL);

	int mode = 2;

	IUINT32 current = iclock();
	IUINT32 slap = current + 20;
	IUINT32 index = 0;
	IUINT32 next = 0;
	IINT64 sumrtt = 0;
	int count = 0;
	int maxrtt = 0;

	char buffer[2000];
	int hr;

	IUINT32 ts1 = iclock();

	while (1) {
#if 0
		isleep(10);
		current = iclock();
		((IUINT32*)buffer)[0] = index++;
		((IUINT32*)buffer)[1] = current;

		//printf("ikcp_send\n");
		ikcp_send(kcp1, buffer, 8);
#endif
#if 1
		isleep(1);
		current = iclock();

		// ÿ�� 20ms��kcp1��������
		for (; current >= slap; slap += 20) {
			((IUINT32*)buffer)[0] = index++;
			((IUINT32*)buffer)[1] = current;

			// �����ϲ�Э���
			ikcp_send(kcp1, buffer, 8);
		}

		// kcp1�յ�kcp2�Ļ�������
		while (1) {
			hr = ikcp_recv(kcp1, buffer, 10);
			// û���յ������˳�
			if (hr < 0) break;
			IUINT32 sn = *(IUINT32*)(buffer + 0);
			IUINT32 ts = *(IUINT32*)(buffer + 4);
			IUINT32 rtt = current - ts;
			
			if (sn != next) {
				// ����յ��İ�������
				printf("ERROR sn %d<->%d\n", (int)count, (int)next);
				return NULL;
			}

			next++;
			sumrtt += rtt;
			count++;
			if (rtt > (IUINT32)maxrtt) maxrtt = rtt;

			printf("[RECV] mode=%d sn=%d rtt=%d\n", mode, (int)sn, (int)rtt);
		}
		if (next > 1000) break;
#endif
	}

	ts1 = iclock() - ts1;

	ikcp_release(kcp1);

	const char *names[3] = { "default", "normal", "fast" };
	printf("%s mode result (%dms):\n", names[mode], (int)ts1);
	printf("avgrtt=%d maxrtt=%d tx=%d\n", (int)(sumrtt / count), (int)maxrtt, tx1);

	pthread_join(recvdata_id, NULL);
	pthread_join(update_id, NULL);

#ifdef __MINGW32__
	closesocket(client_fd);
	WSACleanup();
#else
	close(client_fd);
#endif

	return 0;
}
