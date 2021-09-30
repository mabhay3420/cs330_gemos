#include<ulib.h>


int main (u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    
    int fd[2];

    if (ppipe(fd) < 0) {
        printf("Ppipe creation failed!\n");
        return 0;
    }

    char* buf1 = "Hi!";
    char buf2[10];

    printf("W: %d\n", write(fd[1], buf1, 4));
    printf("R: %d\n", read(fd[0], buf2, 4));
    close(fd[0]);
    printf("Flush: %d\n", flush_ppipe(fd));
    
    return 0;
}

