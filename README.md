# timebox
## Run a program for at most M + N seconds

[Timeboxing](https://en.wikipedia.org/wiki/Timeboxing) is a time management technique. Some humans [find it beneficial](https://spin.atomicobject.com/2014/05/03/timeboxing-mitigate-risk/) to start with a fixed time period and size their work to fit. Can machines practice timeboxing too? Sure! That's kind of what an [RTOS does](https://en.wikipedia.org/wiki/Real-time_operating_system). If you want to timebox a regular old unix program though, you'll need something else...

## Silly Example

See how many files can be counted under your home directory in 5-6 seconds:

```sh
time timebox 5.0 1.0 find ~ | wc -l
```

The two numbers `5.0` and `1.0` are the run period and grace period, respectively. If the program doesn't exit before 5 seconds is up, `timebox` sends it `SIGTERM`. If it still hasn't exited after 6 seconds, `timebox` sends it `SIGKILL`. This is not unlike what `init` does with running processes at shutdown.

## Other Applications

- Problems that have "best effort" solutions, like search. Exhaustive searches can take a long time. Perhaps you always want to respond with _something_ within a second. In your search program, print the best result you've found so far when you receive `SIGTERM`. You can then run it under `timebox`.

- Suppose you're a cost-constrained cloud user being charged for cpu time. (I mean, who isn't these days?) You want to ~crack a hash~ solve a problem, but can't spend more than $X, even if that means you might not get a solution at all. Timebox it!

- College programming assignments where you a submit source code solution. A grading program compiles, runs, and checks the ouput of your program. But what if your program is naughty and never exits? Or what if you submit a small C++ program which [generates GBs of compiler errors](https://tgceec.tumblr.com/post/74534916370/results-of-the-grand-c-error-explosion?is_related_post=1) that take forever to format and print? Timebox it!

- You want to expose a server, but only for a limited, short period of time. Forgetting and leaving it open indefinitely would be risky. Timebox it!

## Okay But Why

This was inspired by ["Unix System Call Timeouts"](https://eklitzke.org/unix-system-call-timeouts). Specifically, the ["waitpid equivalent with timeout?"](http://stackoverflow.com/questions/282176/waitpid-equivalent-with-timeout/290025) question on Stack Overflow. There are all kinds of ways to do this...

### SIGALRM

Set an `alarm` and wait for `SIGALRM`. This is hard to get right, and you need to watch out for [race conditions](http://docstore.mik.ua/orelly/perl4/cook/ch16_22.htm).

### signalfd

Turn `SIGCHLD` into a selectable event via `signalfd`. Only works on Linux.

### self-pipe

Use [djb's self-pipe trick](https://cr.yp.to/docs/selfpipe.html) to turn `SIGCHLD` into a selectable event. This is sort of the poor man's `signalfd`, and is probably the most reliable and broadly portable way to do it. Although I wonder if the "can't safely mix select with signals" reason still applies on modern unixes.

### sigtimedwait

Use `sigtimedwait` with a signal set of `SIGCHLD`. This is a nice, simple approach, but wouldn't mix well with an event loop doing other things. Not that we're doing that here, but it might be relevant in _your_ program.

### EOF on child-to-parent pipe

`timebox` uses yet another way: wait for `EOF` on a child-to-parent pipe, which happens when the pipe's write side is closed at child exit.

