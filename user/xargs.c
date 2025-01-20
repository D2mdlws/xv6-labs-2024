#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int
main(int argc, char* argv[])
{
    char* arg[32];
    char buf[512];
    if (argc < 2) {
        fprintf(2, "xargs usage: xargs <command...>\n");
        exit(1);
    }

    for (int i = 0; i < argc - 1; i++) {
        arg[i] = argv[i + 1];
    }

    while (1) {
        int n = 0;
        int readcount;
        while ((readcount = read(0, buf + n, sizeof(char))) > 0) { 
            if (buf[n] == '\n') {
                break;
            }
            ++n;
        }
        if (readcount == 0) {
            break;
        }
        buf[n] = 0;
        arg[argc - 1] = buf;
        arg[argc] = 0; 
        if (fork() == 0) {
            exec(arg[0], arg);
            fprintf(2, "exec error!\n");
            exit(1);
        } else {
            wait(0);
        } 
    }
    exit(0);
}