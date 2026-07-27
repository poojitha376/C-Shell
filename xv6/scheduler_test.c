#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NPROCS 5
#define BURST  250000000

void
cpu_intensive()
{
  for(volatile int i = 0; i < BURST; i++);
  exit(0);
}

int
main(int argc, char *argv[])
{
  int fork_time[NPROCS];
  int pids[NPROCS];
  int start_time = uptime();

  for(int i = 0; i < NPROCS; i++){
    fork_time[i] = uptime();
    int pid = fork();
    if(pid == 0){
      cpu_intensive();
    }
    pids[i] = pid;
  }

  int turnaround[NPROCS];
  for(int done = 0; done < NPROCS; done++){
    int pid = wait(0);
    int end = uptime();
    for(int i = 0; i < NPROCS; i++){
      if(pids[i] == pid){
        turnaround[i] = end - fork_time[i];
        break;
      }
    }
  }

  int end_time = uptime();
  int sum = 0, min = turnaround[0], max = turnaround[0];
  for(int i = 0; i < NPROCS; i++){
    printf("PID slot %d: turnaround %d ticks\n", i, turnaround[i]);
    sum += turnaround[i];
    if(turnaround[i] < min) min = turnaround[i];
    if(turnaround[i] > max) max = turnaround[i];
  }

  printf("Total time: %d ticks\n", end_time - start_time);
  printf("Avg turnaround: %d ticks (min %d, max %d)\n", sum / NPROCS, min, max);

  exit(0);
}
