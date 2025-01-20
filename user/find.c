#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char*
get_fname(char *path)
{
    char* p;
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
    {

    }
    p++;
    return p;
}

void
find(char* fname, char* path)
{
    char *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
    case T_DEVICE:
    case T_FILE:
        if (strcmp(get_fname(path), fname) == 0) {
            printf("%s\n", path);
        }
        break;

    case T_DIR:
        if(strlen(path) + 1 + DIRSIZ + 1 > 512){
            printf("ls: path too long\n");
            break;
        }
        p = path+strlen(path);
        *p++ = '/'; // add '/' to the end of the path, then p points to the end of path.
        while(read(fd, &de, sizeof(de)) == sizeof(de)){ // each time read a struct d_entry
            if(de.inum == 0)
                continue;
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if(stat(path, &st) < 0){
                printf("ls: cannot stat %s\n", path);
                continue;
            }
            find(fname, path);
            // printf("%s %d %d %d\n", fmtname(buf), st.type, st.ino, (int) st.size);
        }
        break;
    }
    close(fd);
}

int
main(int argc, char* argv[])
{
    char buf[512];
    if (argc != 3) {
        fprintf(2, "usage for find: find <path> <filename>\n");
        exit(1);
    }
    strcpy(buf, argv[1]);
    find(argv[2], buf);
    exit(0);

}