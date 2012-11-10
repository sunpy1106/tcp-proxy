#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<unistd.h>

#include<arpa/inet.h>
#include<netinet/in.h>
#include<sys/epoll.h>
#include<sys/types.h>
#include<sys/socket.h>


#define BUF_SIZE 2024



int 
tcp_listen(const char* port){
    int listenfd,backlog;
    const int on = 1;
    struct sockaddr_in servaddr;
    char* ptr;

    if((listenfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
        perror("socket error");
        return -1;
    }

    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0){
        perror("setsockopt error");
        return -1;
    }

    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(port));
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(listenfd,(const struct  sockaddr*)&servaddr,sizeof(servaddr)) < 0){
        perror("bind error for port ");
        return -1;
    }


    /*can override 2nd argument with environment variable */
    if((ptr = getenv("LISTENQ")) != NULL){
        backlog = atoi(ptr);
    }else{
        backlog = 10;
    }

    if(listen(listenfd,backlog) < 0){
        perror("listen error");
        return -1;
    }

    return listenfd;
}


int 
tcp_connect(const char* host, const char* port){
    int sockfd;     
    const int on = 1;
    struct sockaddr_in servaddr;

    if((sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
        perror("socket error");
        return -1;
    }
    
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0){
        perror("setsockopt error");
        return -1;
    }

    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(port));
    if(inet_pton(AF_INET,host,&servaddr.sin_addr) <= 0){
        perror("inet_pton error");
        return -1;
    }
    if(connect(sockfd,(const struct sockaddr*)&servaddr,sizeof(servaddr)) < 0){
        sleep(15);
        if(connect(sockfd,(const struct sockaddr*)&servaddr,sizeof(servaddr)) < 0){
            sleep(25);
            if(connect(sockfd,(const struct sockaddr*)&servaddr,sizeof(servaddr)) < 0){
                perror("tcp connect error ");
                return -1;
            }
        }
    }
	//cout<<"connected ..."<<endl;
    return sockfd;
}

int
addNewClient(int epfd,int listenfd){
	struct epoll_event ev;
	struct sockaddr_in cliaddr;
	int len  = sizeof(cliaddr);
	int connfd =  accept(listenfd,(struct sockaddr *)&cliaddr,(socklen_t*)&len);
	if(connfd <0){
		perror("accept error!");
		return -1;
	}
	ev.data.fd = connfd;
	ev.events = EPOLLIN;
	epoll_ctl(epfd,EPOLL_CTL_ADD,connfd,&ev);
	return connfd;
}


int messageProcess(int connfd,const void* buf,int len){
	int nSend;
	
	int totalSend =0;
	//can change
	int proxyFd = tcp_connect("127.0.0.1","80");
	if(proxyFd<0){
		printf("create proxy fd error!\n");
		return 0;
	}
	while(len > 0){
		nSend = send(proxyFd,(void*)(buf + totalSend),len,0);
		if(nSend <0){
			perror("send message error");
			exit(1);
		}else{
			len -= nSend;
		}
		totalSend += nSend;
	}
	//recv the data from apache
	char packet[BUF_SIZE];
	len = recv(proxyFd,(void*)packet,BUF_SIZE,0);
	if(len ==0)
		return 0;
	//send the data to client
	totalSend = 0;
	while(len > 0){
		nSend = send(connfd,(void*)(packet+totalSend),len,0);
		if(nSend <0){
			perror("send message error");
			exit(1);
		}else{
			len -= nSend;
		}
		totalSend +=nSend;
	}
	return 1;
}

int 
upstreamPacket(int connfd){
	char buf[BUF_SIZE];
	int len = recv(connfd,(void*)buf,BUF_SIZE,0);
	if(len <0){
		perror("recv error");
		return -1;
	}
	else if( len ==0){
		return 0;
	}else{
		return 	messageProcess(connfd,buf,len);
	}
}



int main(int argc, char *argv[]){
	int listen_fd = tcp_listen("7788");
	int clientSize = 1000;
	int epfd,i;
	struct epoll_event listen_ev,events[clientSize];
	epfd  = epoll_create(10);
	listen_ev.events = EPOLLIN;
	listen_ev.data.fd  = listen_fd;
	epoll_ctl(epfd,EPOLL_CTL_ADD,listen_fd,&listen_ev);
	int nready;
	for(;;){
		nready = epoll_wait(epfd,events,clientSize,-1);
		if(nready < 0){
			if(errno == EINTR)
				continue;
			perror("epoll_wait error");
			exit(1);
		}
		for(i= 0;i<nready;i++){
			if(events[i].data.fd == listen_fd){
				addNewClient(epfd,listen_fd);
			}else if(events[i].events && EPOLLIN){
				int flag = upstreamPacket(events[i].data.fd);
				if((flag == 0)){
					close(events[i].data.fd);
					epoll_ctl(epfd,EPOLL_CTL_DEL,events[i].data.fd,events + i );
				}
			}
			else if(events[i].events && EPOLLERR){
				close(events[i].data.fd);
				epoll_ctl(epfd,EPOLL_CTL_DEL,events[i].data.fd,events+i);
			}
		}
	}
	return 0;		
}


