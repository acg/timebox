# timebox
## Run a program for at most M + N seconds

[Timeboxing](https://en.wikipedia.org/wiki/Timeboxing) is a time management technique. Some humans [find it beneficial](https://spin.atomicobject.com/2014/05/03/timeboxing-mitigate-risk/) to start with a fixed time period and size their work to fit. Could machines practice timeboxing too? Maybe. This is a simple C program that can help us find out: it acts as a timebox referee for any program.

## Example

See how many files can be counted under your home directory in 5-6 seconds:

```sh
make
time ./timebox 5.0 1.0 find ~ | wc -l
```

The two numbers `5.0` and `1.0` are the run period and grace period, respectively. If the program doesn't exit before 5 seconds is up, `timebox` sends it `SIGTERM`. If it still hasn't exited after 6 seconds, `timebox` sends it `SIGKILL`.

## Okay But Why

This was inspired by ["Unix System Call Timeouts"](https://eklitzke.org/unix-system-call-timeouts). Specifically, the ["waitpid equivalent with timeout?"](http://stackoverflow.com/questions/282176/waitpid-equivalent-with-timeout/290025) question on Stack Overflow. There are all kinds of ways to do this:

- Set an `alarm` and wait for `SIGALRM`. This probably won't work right, though.
- Turn `SIGCHLD` into a selectable event via `signalfd`. Only works on Linux.
- Use [djb's self-pipe trick](https://cr.yp.to/docs/selfpipe.html) to turn `SIGCHLD` into a selectable event. This is sort of the poor man's `signalfd`, and is probably the most reliable and broadly portable way to do it. Although I wonder if the "can't safely mix select with signals" reason still applies on modern unixes.
- Use `sigtimedwait` with a signal set of `SIGCHLD`. This is a nice, simple approach, but wouldn't mix well with an event loop doing other things. Not that we're doing that here, but it might be relevant in _your_ program.

Instead, `timebox` presents yet another way: wait for `EOF` on a child-to-parent pipe, which happens when the pipe's write side is closed at child exit.

One benefit of this approach is that `SIGINT` sent to `timebox` will cause it to immediately begin shutdown proceedings, sending `SIGTERM` to the child process and waiting for it to exit.

