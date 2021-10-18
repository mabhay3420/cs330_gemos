#include<ulib.h>

/*
Test 6:
https://piazza.com/class/kromsvk16w01j4?cid=98_f31
Child reading after parent comes back to the same position and forks.
Also checking multiple forks and cases with closed ends.
*/

int main (u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {

    u32 pid;
    int fd[2];

    if (ppipe(fd) < 0) {
        printf("Create pipe failed!");
        return 0;
    }
    char dum[4100];
    char dum2[10000];
    for(int i = 0; i<4096; i++){
        dum[i] = i&3;
    }

    printf("O write: %d\n", write(fd[1], dum, 4096));
    printf("O read: %d\n", read(fd[0], dum2, 2000));
    printf("O flush: %d\n", flush_ppipe(fd));
    printf("O write: %d\n", write(fd[1], dum, 1000));
    printf("O read: %d\n", read(fd[0], dum2+2000, 2096));    

    pid = fork();
    if (pid == 0) {
        // Child 1
        char buf[4096];
        sleep(2);
        printf("C read: %d\n", read(fd[0], buf, 100)); // T = 2
        sleep(8);
        printf("C read: %d\n", read(fd[0], buf, 3000)); // T = 10
        sleep(10);
        printf("C write: %d\n", write(fd[1], dum, 3000)); // T = 20
        sleep(50);
        printf("C R close: %d\n", close(fd[0]));
        printf("C read: %d\n", read(fd[1], buf, 100)); // T = 70
        sleep(2);
        printf("C read: %d\n", read(fd[0], buf, 100)); // T = 72
        printf("C W close: %d\n", close(fd[1]));
        exit(0);
    }
    else {   
        // Parent 
	    char buf2[4096];
        printf("P read: %d\n", read(fd[0], buf2, 200)); // T = 0
        sleep(5);
        printf("P flush: %d\n", flush_ppipe(fd)); // T = 5
        sleep(10);
        printf("P write: %d\n", write(fd[1], dum, 500)); // T = 15
        sleep(50);
        printf("P W close: %d\n", close(fd[1]));
        printf("P write: %d\n", write(fd[0], dum, 500)); // T = 65
        sleep(2);
        printf("P write: %d\n", write(fd[1], dum, 500)); // T = 67
        sleep(8);
        printf("P flush: %d\n", flush_ppipe(fd)); // T = 75
        sleep(10);
        pid = fork();
        if(pid == 0){
            // Child 2
            sleep(5);
            printf("C write: %d\n", write(fd[1], dum, 500)); // T = 90
            sleep(10);
            printf("C read: %d\n", read(fd[0], buf2, 250)); // T = 100
            exit(0);
        }
        else{
            // Parent
            printf("P write: %d\n", write(fd[1], dum, 500)); // T = 85
            sleep(10);
            printf("P read: %d\n", read(fd[0], buf2, 300)); // T = 95
            sleep(10);
            printf("P flush: %d\n", flush_ppipe(fd)); // T = 105
            printf("P R close: %d\n", close(fd[0]));
            sleep(10);
            printf("P flush: %d\n", flush_ppipe(fd)); // T = 115
            printf("P R close: %d\n", close(fd[0])); // T = 115
            printf("P read:%d\n", read(fd[0], buf2, 10)); // T = 115
            printf("P write:%d\n", write(fd[1], dum, 10)); // T = 115
        }
        
    }

    return 0;
}


