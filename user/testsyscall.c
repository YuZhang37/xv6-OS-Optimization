#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;
  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

int main() {
    //start the share scheduler
    startcfs();
    //create 10 child processes of nice=10 (note: the parent process is of nice=0 by default)
    int ret=0;
    for(int i=0; i<10; i++){
        ret=fork1(); 
        if(ret==0){
            nice(10);
            break; 
        }
    }
    //all processes run the same code as follows
    printf("process (pid=%d) has nice = %d\n", getpid(),nice(-30)); int t=0;
    while(t++<2){
        double x=987654321.9;
        for(int i=0; i<100000000; i++){
            x /= 12345.6789; 
        }
    }
    //parent process waits for child processes to terminate and then stop share scheduler
    if (ret > 0) {
        wait(0);
        stopcfs(); 
    }
    return 0; 
}


