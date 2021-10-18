#include<ulib.h>


int main (u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
    
    u32 pid1;
    int fd[2];

    if (ppipe(fd) < 0) {
        printf("Ppipe creation failed!\n");
        return 0;
    }

    pid1 = fork();
    if (pid1 == 0) {
        // process i
        sleep(20);
        printf("P_i close: %d\n", close(fd[0]));
        printf("P_i flush: %d\n", flush_ppipe(fd));
        exit(0);
    }
    else {
        u32 pid2 = fork();
        if (pid2 == 0) {
            // process j
            sleep(5);
            char buf1[4096], buf2[4096];

            printf("P_j read: %d\n", read(fd[0], buf2, 500));
            sleep(70);
            for (int i = 0; i < 2196; i++) {
                buf1[i] = '0';
            }
            printf("P_j write: %d\n", write(fd[1], buf1, 2196));
            sleep(120);
            printf("P_j write: %d\n", write(fd[1], buf1, 200));
        }
        else {
            // process k
            char buf1[4096], buf2[4096];
            
            for (int i = 0; i < 1000; i++) {
                buf1[i] = '0';
            }

            printf("P_k write: %d\n", write(fd[1], buf1, 1000));
            sleep(10);

            printf("P_k read: %d\n", read(fd[0], buf2, 200));
            printf("P_k write: %d\n", write(fd[1], buf1, 1000));
            printf("P_k flush: %d\n", flush_ppipe(fd));
            sleep(50);
            printf("P_k read: %d\n", read(fd[0], buf2, 3000));
            sleep(100);
            printf("P_k read: %d\n", read(fd[0], buf2, 2196));
        }

    }

    return 0;
}

