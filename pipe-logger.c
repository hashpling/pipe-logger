#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

FILE *login = NULL;
FILE *logout = NULL;
FILE *logerr = NULL;

static void copy_input(int fd_in, int fd_out, fd_set *fdbase, int *open_fds) {
    char buffer[1024*1024];
    ssize_t n = read(fd_in, buffer, sizeof buffer);
    if (n > 0) {
        FILE *out = fd_in == 0 ? login : fd_out == 1 ? logout : fd_out == 2 ? logerr : NULL;
        if (out)
            fwrite(buffer, 1, n, out);
        write(fd_out, buffer, n);
    }
    else {
        (*open_fds)--;
        FD_CLR(fd_in, fdbase);
        if (fd_out != 2)
            close(fd_out);
    }
}

static int parse_args(int argc, char *argv[]) {
    int i;
    FILE **p = NULL;
    size_t k;
    struct Info { const char *p; size_t n; FILE **pfp; } info[] = {
        { "--in", 4, &login },
        { "--out", 5, &logout },
        { "--err", 5, &logerr },
    };

    for (i = 1; i != argc; ++i) {
        if (!strcmp(argv[i], "--"))
            break;

        if (p) {
            *p = fopen(argv[i], "wb");
            p = NULL;
            continue;
        }

        for (k = 0; k != sizeof info / sizeof *info; ++k) {
            struct Info *pI = &info[k];
            if (!strncmp(argv[i], pI->p, pI->n)) {
                if (argv[i][pI->n] == '\0') {
                    p = pI->pfp;
                }
                else if (argv[i][pI->n] == '=') {
                    *pI->pfp = fopen(argv[i] + pI->n + 1, "wb");
                }
                break;
            }
        }
    }
    return i;
}

static void handle_child(int n) {
}

int main(int argc, char *argv[]) {
    int pipes[3][2];
    pipe(pipes[0]);
    pipe(pipes[1]);
    pipe(pipes[2]);

    argv += parse_args(argc, argv);

    pid_t pid = fork();
    if (pid < 0) {
        return 1;
    }
    if (!pid) {
        close(pipes[0][1]);
        close(pipes[1][0]);
        close(pipes[2][0]);

        dup2(pipes[0][0], 0);
        dup2(pipes[1][1], 1);
        dup2(pipes[2][1], 2);
        close(pipes[0][0]);
        close(pipes[1][1]);
        close(pipes[2][1]);

        execvp(argv[1], argv + 1);
        return -1;
    }
    else {
        int open_fds = 3, st;
        fd_set fdbase, fds;

        signal(SIGCHLD, handle_child);

        close(pipes[0][0]);
        close(pipes[1][1]);
        close(pipes[2][1]);

        FD_ZERO(&fdbase);
        FD_SET(0, &fdbase);
        FD_SET(pipes[1][0], &fdbase);
        FD_SET(pipes[2][0], &fdbase);

        while (open_fds) {
            FD_COPY(&fdbase, &fds);
            if (select(pipes[2][0] + 1, &fds, NULL, NULL, NULL) < 0) {
                if (waitpid(pid, &st, WNOHANG) == pid) {
                    if (WIFEXITED(st)) {
                        return WEXITSTATUS(st);
                    }
                    else {
                        return -1;
                    }
                }
                continue;
            }

            if (FD_ISSET(0, &fds)) {
                copy_input(0, pipes[0][1], &fdbase, &open_fds);
            }
            if (FD_ISSET(pipes[1][0], &fds)) {
                copy_input(pipes[1][0], 1, &fdbase, &open_fds);
            }
            if (FD_ISSET(pipes[2][0], &fds)) {
                copy_input(pipes[2][0], 2, &fdbase, &open_fds);
            }
        }
        if (waitpid(pid, &st, 0) != -1) {
            if (WIFEXITED(st)) {
                return WEXITSTATUS(st);
            }
        }
    }
    return -1;
}
