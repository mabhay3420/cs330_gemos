#include<ulib.h>


int main (u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    
    u32 pid;
    int fd[2];

    if (ppipe(fd) < 0) {
        printf("Ppipe creation failed!\n");
        return 0;
    }

    pid = fork();
    if (pid == 0) {
        sleep(5);
        char buf1[4096], buf2[4096];

        printf("C read: %d\n", read(fd[0], buf2, 1500));
        sleep(10);

        printf("C flush: %d\n", flush_ppipe(fd));
        printf("C read: %d\n", read(fd[0], buf2, 4096));
        printf("C flush: %d\n", flush_ppipe(fd));

        exit(0);
    }
    else {
        char buf1[4096], buf2[4096];
        for (int i = 0; i < 3000; i++) {
            buf1[i] = '0';
        }

        printf("P write: %d\n", write(fd[1], buf1, 3000));
        printf("P read: %d\n", read(fd[0], buf2, 2000));

        sleep(10);
        printf("P write: %d\n", write(fd[1], buf1, 2000));
        
        printf("P flush: %d\n", flush_ppipe(fd));
        printf("P write: %d\n", write(fd[1], buf1, 2000));
        printf("P read: %d\n", read(fd[0], buf2, 4096));

    }

    return 0;
}

