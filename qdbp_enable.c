#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

enum state {
  start_up    , // program is starting
  wait_segv   , // offending instruction triggered
  single_step , // run until offending instruction is executed
  signal_trap , // send signal
  wait_trap   , // action completed, re-arm for next iteration
};

static void ptrace_traceme() {
  if (ptrace(PTRACE_TRACEME, 0, NULL, NULL)) {
    perror("ptrace traceme");
    exit(1);
  }
}

static void ptrace_cont(pid_t pid, int signo) {
  if (ptrace(PTRACE_CONT, pid, NULL, signo)) {
    perror("ptrace cont");
    kill(pid, SIGTERM);
    exit(1);
  }
}

static void ptrace_singlestep(pid_t pid, int signo) {
  if (ptrace(PTRACE_SINGLESTEP, pid, NULL, signo)) {
    perror("ptrace singlestep");
    kill(pid, SIGTERM);
    exit(1);
  }
}

static void ptrace_getregs(pid_t pid, struct user_regs_struct *regs) {
  if (ptrace(PTRACE_GETREGS, pid, NULL, regs)) {
    perror("ptrace getregs");
    kill(pid, SIGTERM);
    exit(1);
  }
}

static void expect_sigtrap(pid_t pid, int signo) {
  if (signo != SIGTRAP) {
    fprintf(stderr, "Unexpected signal %u\n", signo);
    kill(pid, SIGTERM);
    exit(1);
  }
}

static void send_signal(pid_t pid) {
  if (kill(pid, SIGUSR1)) {
    perror("kill");
    exit(1);
  }
}

int main(int argc, char **argv) {
  enum state state = start_up;
  unsigned long long int inst;
  pid_t child_pid;
  int wait_status;

  if (argc < 2) {
    fprintf(stderr, "Expected a program name as argument\n");
    return 1;
  }

  child_pid = fork();
  if (child_pid < 0) {
    perror("fork");
    return 1;
  }

  if (!child_pid) {
    ptrace_traceme();
    execvp(argv[1], &argv[1]);
    perror("execvp");
    return 1;
  }

  while (wait(&wait_status), WIFSTOPPED(wait_status)) {
    struct user_regs_struct regs;
    unsigned sig = WSTOPSIG(wait_status);
    switch (state) {
    case start_up:
      expect_sigtrap(child_pid, sig);
      ptrace_cont(child_pid, 0);
      state = wait_segv;
      break;
    case wait_segv:
      if (sig != SIGSEGV) {
        ptrace_cont(child_pid, sig);
        break;
      }
      ptrace_getregs(child_pid, &regs);
      ptrace_singlestep(child_pid, sig);
      inst = regs.rip;
      state = single_step;
      break;
    case single_step:
      expect_sigtrap(child_pid, sig);
      ptrace_getregs(child_pid, &regs);
      ptrace_singlestep(child_pid, 0);
      if (inst == regs.rip)
        state = signal_trap;
      break;
    case signal_trap:
      expect_sigtrap(child_pid, sig);
      send_signal(child_pid);
      ptrace_cont(child_pid, 0);
      state = wait_trap;
      break;
    case wait_trap:
      ptrace_cont(child_pid, sig);
      if (sig == SIGUSR1)
        state = wait_segv;
      break;
    }
  }

  if (WIFEXITED(wait_status))
    return WEXITSTATUS(wait_status);
  if (WIFSIGNALED(wait_status)) {
    printf("Target killed by %u\n", WTERMSIG(wait_status));
    return 1;
  }
  return 0;
}
