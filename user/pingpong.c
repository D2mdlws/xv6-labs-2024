#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"

int
main(int argc, char *argv[])
{
	char *buf1 = (char*)malloc(sizeof(char));
	char *buf2 = (char*)malloc(sizeof(char));
	int p[2]; // parent writes to child
	int q[2]; // child wirtes to parent
	pipe(p);
	pipe(q);
	if (fork() == 0) {
		read(p[0], buf1, sizeof(char));
		printf("%d: received ping\n", getpid());
		write(q[1], buf1, sizeof(char));
		exit(0);
	} else {
		write(p[1], "a", sizeof(char));
		read(q[0], buf2, sizeof(char));
		printf("%d: received pong\n", getpid());
		free(buf1);
		free(buf2);
		exit(0);
	}
}