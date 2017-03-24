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
  // Process arguments.

  if (argc < 4) die(1, "usage: timebox TIMEOUT GRACE program arg1 arg2 ...");

  char *argv0 = *argv++; argc--;
  double timeout_argument = atof(*argv++); argc--;
  double grace_argument = atof(*argv++); argc--;

  if (timeout_argument <= 0) die(1, "invalid TIMEOUT");
  if (grace_argument < 0) die(1, "invalid GRACE");

  // We work with timevals throughout, convert from doubles.

  struct timeval timeout;
  struct timeval grace;

  timeout.tv_sec  = (time_t)timeout_argument;
  timeout.tv_usec = (suseconds_t)(1e6 * (timeout_argument - timeout.tv_sec));
  grace.tv_sec    = (time_t)grace_argument;
  grace.tv_usec   = (suseconds_t)(1e6 * (grace_argument - grace.tv_sec));

  // Create the child-to-parent pipe.

  int pipefds[2];
  if (pipe(pipefds) < 0) sysdie("pipe error");

  // SIGINT: initiate shutdown without any more delays.
  // SIGCHLD: child has exited, no grace period delay needed.

  sig_catch(SIGINT, sigint);
  sig_catch(SIGCHLD, sigchld);

  // Fork and run the child program.

  pid_t pid;
  switch ((pid = fork())) {
    case -1:
      sysdie("fork error"); break;
    case  0: // Child, close pipe read side and run tail program.
      sig_uncatch(SIGINT);
      sig_uncatch(SIGCHLD);
      if (close(pipefds[0]) < 0) sysdie("close error");
      if (execvp(*argv, argv) < 0) sysdie("exec error");
      // Never reached.
      break;
    default: // Parent, continue with this program.
      break;
  }

  // Close pipe write side.

  if (close(pipefds[1]) < 0) sysdie("close error");

  // Calculate absolute deadlines for running and grace periods.

  struct timeval started;
  struct timeval deadline_running;
  struct timeval deadline_gracing;

  if (!gettimeofday(&started, 0) < 0) sysdie("gettimeofday error");
  timeradd(&started,          &timeout, &deadline_running);
  timeradd(&deadline_running, &grace,   &deadline_gracing);

  // Running period. Wait until one of these occurs:
  // 1. The deadline has passed.
  // 2. The deadline timeout elapses.
  // 3. The child exits, triggering a readable zero byte at eof.

  while (!done_running)
  {
    // Recalculate remaining time.

    struct timeval now;
    struct timeval remaining;
    if (!gettimeofday(&now, 0) < 0) sysdie("gettimeofday error");
    timersub(&deadline_running, &now, &remaining);

    // Done if deadline has passed (1).

    if (remaining.tv_sec < 0) {
      done_running = 1;
      break;
    }

    // Wait for (2) or (3).

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(pipefds[0], &readable);
    int rc = select(pipefds[0]+1, &readable, 0, 0, &remaining);
    // Done if timeout elapses (2).
    if (rc == 0) done_running = 1;
    if (done_running) break;
    if (rc < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (rc < 0) sysdie("select error");

    // The child-to-parent pipe is readable, so try to read a byte.

    char c;
    ssize_t bytes = read(pipefds[0], &c, 1);
    // Prevent child keeping parent busy by writing to pipe.
    if (bytes > 0) die(101, "child wrote to pipe");
    // Done if we read zero bytes at eof (3).
    if (!bytes) done_running = 1;
    if (done_running) break;
    if (bytes < 0) sysdie("read error");
  }

  // Grace period. Send TERM to child and wait for deadline to elapse.

  kill(pid, SIGTERM);
  kill(pid, SIGCONT);

  while (!done_gracing)
  {
    // Recalculate remaining time.

    struct timeval now;
    struct timeval remaining;
    if (!gettimeofday(&now, 0) < 0) sysdie("gettimeofday error");
    timersub(&deadline_gracing, &now, &remaining);

    // Done if deadline has passed.

    if (remaining.tv_sec < 0) {
      done_gracing = 1;
      break;
    }

    // Sleep until deadline.

    int rc = select(0, 0, 0, 0, &remaining);
    if (rc == 0) done_gracing = 1;
    if (done_gracing) break;
    if (rc < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (rc < 0) sysdie("select error");
  }

  // Finally, force kill child in case it's not responding.

  kill(pid, SIGKILL);

  // Collect child status and bubble up its exit code.

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

