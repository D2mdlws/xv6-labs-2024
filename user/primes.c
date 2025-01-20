#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"

void primes(int*) __attribute__((noreturn));

void
primes(int *p)
{
    int buf1, buf2;
    close(p[1]);
    if(read(p[0], (void *)&buf1, sizeof(buf1)) != sizeof(buf1)){
        fprintf(2, "Read fail!\n");
        exit(1);
    }
    printf("prime %d\n", buf1);
    if (read(p[0], (void*)&buf2, sizeof(buf2))) {
        int q[2];
        pipe(q);
        if (fork() == 0) {
            primes(q);
        } else {
            close(q[0]);
            do {
                if (buf2 % buf1 != 0) {
                    write(q[1], (void*)&buf2, sizeof(buf2));
                }
            } while(read(p[0], (void*)&buf2, sizeof(buf2)));

            // the former process close the pipe
            close(p[0]);
            close(q[1]);
            wait(0);
        }
    }
    exit(0);
}   

int
main(int argc, char *argv[])
{
    int p[2];
    pipe(p);
    if (fork() == 0) {
        primes(p);
    } else {
        close(p[0]);
        for (int i = 2; i <= 280; i++) {
            if (write(p[1], (void*)&i, sizeof(i)) != 4) {
                fprintf(2, "Write error\n");
                exit(1);
            }
        }
        close(p[1]);
        wait(0);
    }

    exit(0);
}

