#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

void sig_catch(int sig, void (*f)());
void sig_uncatch(int sig);
void die(int code, const char *message);
void sysdie(const char *message);

int done_running = 0;
int done_gracing = 0;

void sigint()  { done_gracing = done_running = 1; }
void sigchld() { done_gracing = 1; }

int main(int argc, char **argv)
{
  if (argc < 4) die(1, "usage: timebox TIMEOUT GRACE program arg1 arg2 ...");

  char *argv0 = *argv++; argc--;
  double timeout_argument = atof(*argv++); argc--;
  double grace_argument = atof(*argv++); argc--;

  if (timeout_argument <= 0) die(1, "invalid TIMEOUT");
  if (grace_argument < 0) die(1, "invalid GRACE");

  struct timeval timeout;
  struct timeval grace;

  timeout.tv_sec  = (time_t)timeout_argument;
  timeout.tv_usec = (suseconds_t)((long)(timeout_argument * 1000000) % 1000000);
  grace.tv_sec    = (time_t)grace_argument;
  grace.tv_usec   = (suseconds_t)((long)(grace_argument * 1000000) % 1000000);

  sig_catch(SIGINT, sigint);

  int pipefds[2];
  pid_t pid;

  if (pipe(pipefds) < 0) sysdie("pipe error");
  if ((pid = fork()) < 0) sysdie("fork error");
  if (!pid) {
    // child
    sig_uncatch(SIGINT);
    if (close(pipefds[0]) < 0) sysdie("close error");
    if (execvp(*argv, argv) < 0) sysdie("exec error");
  }

  // parent
  sig_catch(SIGCHLD, sigchld);
  if (close(pipefds[1]) < 0) sysdie("close error");

  // calculate absolute deadlines here
  struct timeval started;
  struct timeval deadline_running;
  struct timeval deadline_gracing;

  if (!gettimeofday(&started, 0) < 0) sysdie("gettimeofday error");
  timeradd(&started,          &timeout, &deadline_running);
  timeradd(&deadline_running, &grace,   &deadline_gracing);

  while (!done_running) {
    struct timeval now;
    struct timeval remaining;
    if (!gettimeofday(&now, 0) < 0) sysdie("gettimeofday error");
    timersub(&deadline_running, &now, &remaining);
    if (remaining.tv_sec < 0) {
      done_running = 1;
      break;
    }
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(pipefds[0], &readable);
    int rc = select(pipefds[0]+1, &readable, 0, 0, &remaining);
    if (rc == 0) done_running = 1;
    if (done_running) break;
    if (rc < 0) sysdie("select error");
    char c;
    ssize_t bytes = read(pipefds[0], &c, 1);
    if (!bytes) done_running = 1;
    if (done_running) break;
    if (bytes < 0) sysdie("read error");
  }

  kill(pid, SIGTERM);

  while (!done_gracing) {
    struct timeval now;
    struct timeval remaining;
    if (!gettimeofday(&now, 0) < 0) sysdie("gettimeofday error");
    timersub(&deadline_gracing, &now, &remaining);
    if (remaining.tv_sec < 0) {
      done_gracing = 1;
      break;
    }
    int rc = select(0, 0, 0, 0, &remaining);
    if (rc == 0) done_gracing = 1;
    if (done_gracing) break;
    if (rc < 0) sysdie("select error");
  }

  kill(pid, SIGKILL);

  int status;
  if (waitpid(pid, &status, 0) < 0) sysdie("waitpid error");
  return WEXITSTATUS(status);
}

void sig_catch(int sig, void (*f)())
{
  struct sigaction sa;
  sa.sa_handler = f;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(sig,&sa,(struct sigaction *) 0);
}

void sig_uncatch(int sig)
{
  struct sigaction sa;
  sa.sa_handler = SIG_IGN;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(sig,&sa,(struct sigaction *) 0);
}

void die(int code, const char *message)
{
  fprintf(stderr, "%s\n", message);
  _exit(100);
}

void sysdie(const char *message)
{
  fprintf(stderr, "%s: %s\n", message, strerror(errno));
  _exit(errno & 0x7f);
}

