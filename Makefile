CC=gcc -g

tcpProxy:tcp-proxy.c
	$(CC)  $^ -o $@


