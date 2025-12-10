all:
	gcc server.c helpers.c -g -Wall -o server
	gcc client.c -g -Wextra -o client
