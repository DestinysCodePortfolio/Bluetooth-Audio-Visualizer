#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    int count = 1;
    int pid1=0,pid2=0;
    pid1 = fork();
    if ( (pid1 != 0) ) {
        count=count + 3;
    }

    printf("%d %d, %d ", count, pid1, pid2);

    if(count == 1)
    {
        count=count + 1;
        pid2=fork();
    }

    printf("%d  %d, %d ", count+1, pid1, pid2);

    if(pid2) {
        waitpid(pid2, NULL, 0);
        count = count+4;
        printf("%d %d, %d ", count, pid1, pid2);
    } else {
        printf ("%d %d, %d ", count+2, pid1, pid2);
    }

    printf("done %d, %d\n", pid1, pid2);
}