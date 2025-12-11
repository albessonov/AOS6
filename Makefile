all:
	gcc server.c helpers.c -g -Wall -o server
	gcc client.c -g -Wextra -o vcs_client
install:
	cp vcs_client /usr/local/bin/vcs_client
