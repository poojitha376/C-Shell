#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  int initial = getreadcount();
  printf("Initial read count: %d\n", initial);

  // Read 100 bytes from a file
  int fd = open("README", O_RDONLY);
  if(fd < 0){
    printf("Failed to open README\n");
    exit(1);
  }

  char buf[100];
  read(fd, buf, 100);
  close(fd);

  int final = getreadcount();
  printf("Final read count: %d\n", final);
  printf("Bytes read in test: %d\n", final - initial);

  exit(0);
}
