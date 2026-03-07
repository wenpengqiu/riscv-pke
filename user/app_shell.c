/*
 * This app starts a very simple shell and executes some simple commands.
 * The commands are stored in the hostfs_root/shellrc
 * The shell loads the file and executes the command line by line.                 
 */
#include "user_lib.h"
#include "string.h"
#include "util/types.h"

int main(int argc, char *argv[]) {
  printu("\n======== Shell Start ========\n\n");

  int MAXBUF = 1024;
  char buf[MAXBUF];
  char delim[3] = " \n";

  int fd = open("/shellrc", O_RDONLY);
  if (fd < 0) {
    printu("open /shellrc failed!\n");
    exit(-1);
  }

  int n = read_u(fd, buf, MAXBUF - 1);
  close(fd);
  if (n < 0) {
    printu("read /shellrc failed!\n");
    exit(-1);
  }
  buf[n] = '\0';

  char *command = naive_malloc();
  char *para = naive_malloc();

  char *token = strtok(buf, delim);
  while (token != NULL) {
    strcpy(command, token);

    token = strtok(NULL, delim);
    if (token == NULL) {
      printu("bad command line in shellrc!\n");
      break;
    }
    strcpy(para, token);

    if (strcmp(command, "END") == 0 && strcmp(para, "END") == 0)
      break;

    printu("Next command: %s %s\n\n", command, para);
    printu("==========Command Start============\n\n");

    int pid = fork();
    if (pid == 0) {
      int ret = exec(command, para);
      if (ret == -1) printu("exec failed!\n");
      exit(-1);
    } else if (pid > 0) {
      wait(pid);
      printu("==========Command End============\n\n");
    } else {
      printu("fork failed!\n");
      break;
    }

    token = strtok(NULL, delim);
  }

  exit(0);
  return 0;
}
