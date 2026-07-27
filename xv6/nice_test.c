#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Demonstrates nice-based prioritization under CFS/RL: scheduler_test's own
// workload never varies nice (all processes default to 0), so it can't show
// CFS or the learned RL policy behaving any differently from a nice-blind
// one - this program forks processes at different nice values instead.

#define BURST 250000000

void
cpu_intensive()
{
  for(volatile int i = 0; i < BURST; i++);
  exit(0);
}

int
main(int argc, char *argv[])
{
  int nices[] = {-5, 0, 0, 0, 5};
  int n = 5;
  int fork_time[5];
  int pids[5];
  int start_time = uptime();

  for(int i = 0; i < n; i++){
    fork_time[i] = uptime();
    int pid = fork();
    if(pid == 0){
      cpu_intensive();
    }
    setnice(pid, nices[i]);
    pids[i] = pid;
  }

  int turnaround[5];
  for(int done = 0; done < n; done++){
    int pid = wait(0);
    int end = uptime();
    for(int i = 0; i < n; i++){
      if(pids[i] == pid){
        turnaround[i] = end - fork_time[i];
        break;
      }
    }
  }

  int end_time = uptime();
  for(int i = 0; i < n; i++){
    printf("nice %d: turnaround %d ticks\n", nices[i], turnaround[i]);
  }
  printf("Total time: %d ticks\n", end_time - start_time);

  exit(0);
}
