


#include <stdio.h>
#include <liburing.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>


#define EVENT_ACCEPT   	0
#define EVENT_READ		1
#define EVENT_WRITE		2

struct conn_info {   // sqe->user_data
	int fd;
	int event;
};


int init_server(unsigned short port) {	 // create a listener sockfd

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);	
	struct sockaddr_in serveraddr;	
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));	
	serveraddr.sin_family = AF_INET;	
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);	
	serveraddr.sin_port = htons(port);	

	if (-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {		
		perror("bind");		
		return -1;	
	}	

	listen(sockfd, 10);
	
	return sockfd;
}



#define ENTRIES_LENGTH		1024
#define BUFFER_LENGTH		1024

int set_event_recv(struct io_uring *ring, int sockfd,
				      void *buf, size_t len, int flags) {

	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

	struct conn_info accept_info = {
		.fd = sockfd,
		.event = EVENT_READ,
	};
	
	io_uring_prep_recv(sqe, sockfd, buf, len, flags);
	memcpy(&sqe->user_data, &accept_info, sizeof(struct conn_info));

}


int set_event_send(struct io_uring *ring, int sockfd,
				      void *buf, size_t len, int flags) {

	struct io_uring_sqe *sqe = io_uring_get_sqe(ring); // get the current sqe of ring

	struct conn_info accept_info = {
		.fd = sockfd,
		.event = EVENT_WRITE,
	};
	
	io_uring_prep_send(sqe, sockfd, buf, len, flags);  // add an send event to sqe 
 	memcpy(&sqe->user_data, &accept_info, sizeof(struct conn_info)); // set user_data

}



int set_event_accept(struct io_uring *ring, int sockfd, struct sockaddr *addr,
					socklen_t *addrlen, int flags) {

	struct io_uring_sqe *sqe = io_uring_get_sqe(ring); // get the sqe of ring

	struct conn_info accept_info = {  // sqe->user_data
		.fd = sockfd,
		.event = EVENT_ACCEPT,
	};
	// 将事件注册到sqe
	io_uring_prep_accept(sqe, sockfd, (struct sockaddr*)addr, addrlen, flags); // add an accept event to sqe
	memcpy(&sqe->user_data, &accept_info, sizeof(struct conn_info)); // set 'fd' + 'event'

}




int main(int argc, char *argv[]) {

	unsigned short port = 9999;
	int sockfd = init_server(port);

	struct io_uring_params params; // used to init ring
	memset(&params, 0, sizeof(params));

	struct io_uring ring; 
	io_uring_queue_init_params(ENTRIES_LENGTH, &ring, &params);

	
#if 0
	struct sockaddr_in clientaddr;	
	socklen_t len = sizeof(clientaddr);
	accept(sockfd, (struct sockaddr*)&clientaddr, &len);
#else

	struct sockaddr_in clientaddr;	
	socklen_t len = sizeof(clientaddr);
	set_event_accept(&ring, sockfd, (struct sockaddr*)&clientaddr, &len, 0);
	
#endif

	char buffer[BUFFER_LENGTH] = {0};

	while (1) {

		io_uring_submit(&ring); // push task to worker


		struct io_uring_cqe *cqe;
		io_uring_wait_cqe(&ring, &cqe); // 阻塞等待至有事件完成

		struct io_uring_cqe *cqes[128];
		int nready = io_uring_peek_batch_cqe(&ring, cqes, 128);  // 获取已完成的事件

		int i = 0;
		for (i = 0;i < nready;i ++) {

			struct io_uring_cqe *entries = cqes[i]; // result
			struct conn_info result; 
			//  get the data of result 
			memcpy(&result, &entries->user_data, sizeof(struct conn_info));

			if (result.event == EVENT_ACCEPT) { // acception event finished

				set_event_accept(&ring, sockfd, (struct sockaddr*)&clientaddr, &len, 0); // reset an accept event
				//printf("set_event_accept\n"); //

				int connfd = entries->res; 

				set_event_recv(&ring, connfd, buffer, BUFFER_LENGTH, 0); // reset an recv event

				
			} else if (result.event == EVENT_READ) {  // read task finished

				int ret = entries->res;
				//printf("set_event_recv ret: %d, %s\n", ret, buffer); //

				if (ret == 0) {
					close(result.fd);
				} else if (ret > 0) {
					set_event_send(&ring, result.fd, buffer, ret, 0); // reset a send event
				}
			}  else if (result.event == EVENT_WRITE) { // write success
  //

				int ret = entries->res;
				//printf("set_event_send ret: %d, %s\n", ret, buffer);

				set_event_recv(&ring, result.fd, buffer, BUFFER_LENGTH, 0);
				
			}
			
		}

		io_uring_cq_advance(&ring, nready);
	}

}


