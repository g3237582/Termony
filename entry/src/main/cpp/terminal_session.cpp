#include "terminal.h"
#include "terminal_log.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <string>
#include <unistd.h>
#include <vector>
#include <pty.h>
#include <sys/stat.h>

extern char **environ;

#ifdef STANDALONE
static const char *kBashCandidates[] = {"/bin/bash", "/bin/sh"};
#else
// HNP bash first; system sh last so a phone that can spawn anything still
// gets pwd/echo/ls without inventing binaries.
static const char *kBashCandidates[] = {
    "/data/app/base.org/base_1.0/bin/bash",
    "/data/service/hnp/bin/bash",
    "/data/service/hnp/base.org/base_1.0/bin/bash",
    "/system/bin/sh",
    "/bin/sh",
};
#endif

static const char *kHomeCandidates[] = {
    "/storage/Users/currentUser",
    "/data/storage/el2/base/files",
};

static const char *kPath =
    "/data/app/base.org/base_1.0/bin:/data/app/bin:"
    "/data/service/hnp/base.org/base_1.0/bin:/data/service/hnp/bin:/bin:"
    "/usr/local/bin:/usr/bin:/system/bin:/vendor/bin";

std::string FormatErrno(const char *op, int err) {
    std::string s(op);
    s += ": ";
    s += std::to_string(err);
    s += " (";
    const char *msg = strerror(err);
    s += msg ? msg : "unknown";
    s += ")";
    return s;
}

void terminal_context::AppendNotice(const std::string &line) {
    if (col > 0) {
        row += 1;
        DropFirstRowIfOverflow();
        col = 0;
    }
    for (unsigned char ch : line) {
        InsertUtf8(ch);
    }
    row += 1;
    DropFirstRowIfOverflow();
    col = 0;
}

static bool PathExists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static const char *PickHome() {
    for (const char *home : kHomeCandidates) {
        if (PathExists(home)) {
            return home;
        }
    }
    return kHomeCandidates[0];
}

static const char *PickBash() {
    for (const char *bash : kBashCandidates) {
        if (access(bash, X_OK) == 0) {
            return bash;
        }
    }
    return kBashCandidates[0];
}

// Child-only: after forkpty the child may change cwd and env. Never call this
// in the UI process (posix_spawn path copies env into envp instead).
static void ApplyShellEnv(const char *home) {
#ifndef STANDALONE
    setenv("PATH", kPath, 1);
    setenv("HOME", home, 1);
    setenv("PWD", home, 1);
    setenv("LD_LIBRARY_PATH", "/data/app/base.org/base_1.0/lib", 1);
    setenv("TMUX_TMPDIR", "/data/storage/el2/base/cache", 1);
#endif
    if (chdir(home) != 0) {
        LOG_WARN("chdir(%s) failed: %d %s", home, errno, strerror(errno));
    }
}

static void ExecShell(const char *home) {
    ApplyShellEnv(home);
#ifdef STANDALONE
    execl("/bin/bash", "/bin/bash", nullptr);
    execl("/bin/sh", "/bin/sh", nullptr);
#else
    for (const char *bash : kBashCandidates) {
        execl(bash, bash, nullptr);
    }
#endif
    LOG_ERROR("execl bash failed: %s", FormatErrno("execl", errno).c_str());
    _exit(127);
}

static std::string FailPty(terminal_context *ctx, const std::string &detail) {
    LOG_ERROR("shell start failed: %s", detail.c_str());
    ctx->fd = -1;
    ctx->AppendNotice("[termony] failed to start shell");
    ctx->AppendNotice(detail);
    ctx->AppendNotice("HarmonyOS NEXT phone apps typically cannot fork/pty.");
    ctx->AppendNotice("HNP tools stay installed; a working shell needs 2in1");
    ctx->AppendNotice("or a future Native ChildProcess path. See docs/phone-mate80.md.");
    return detail;
}

static bool SetNonblock(int pty_fd) {
    int flags = fcntl(pty_fd, F_GETFL);
    if (flags < 0) {
        return false;
    }
    return fcntl(pty_fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void StopChild(pid_t child) {
    if (child > 0) {
        kill(child, SIGTERM);
    }
}

static std::string FinishParent(terminal_context *ctx, pid_t child, const char *how) {
    if (!SetNonblock(ctx->fd)) {
        int err = errno;
        close(ctx->fd);
        ctx->fd = -1;
        StopChild(child);
        return FailPty(ctx, FormatErrno("fcntl(F_SETFL O_NONBLOCK)", err));
    }
    pthread_t terminal_thread;
    int rc = pthread_create(&terminal_thread, nullptr, terminal_context::TerminalWorker, ctx);
    if (rc != 0) {
        close(ctx->fd);
        ctx->fd = -1;
        StopChild(child);
        return FailPty(ctx, FormatErrno("pthread_create", rc));
    }
    pthread_detach(terminal_thread);
    LOG_INFO("shell started via %s fd=%d pid=%d", how, ctx->fd, static_cast<int>(child));
    return {};
}

static bool EnvKeyEquals(const char *entry, const char *key) {
    size_t keylen = strlen(key);
    return strncmp(entry, key, keylen) == 0 && entry[keylen] == '=';
}

// Copy parent environ and overlay shell vars without setenv/chdir in the UI process.
static std::vector<std::string> BuildSpawnEnv(const char *home) {
    std::vector<std::string> env;
    if (environ) {
        for (char **e = environ; *e; ++e) {
            if (EnvKeyEquals(*e, "PATH") || EnvKeyEquals(*e, "HOME") || EnvKeyEquals(*e, "PWD") ||
                EnvKeyEquals(*e, "LD_LIBRARY_PATH") || EnvKeyEquals(*e, "TMUX_TMPDIR")) {
                continue;
            }
            env.emplace_back(*e);
        }
    }
#ifndef STANDALONE
    env.emplace_back(std::string("PATH=") + kPath);
    env.emplace_back(std::string("LD_LIBRARY_PATH=/data/app/base.org/base_1.0/lib"));
    env.emplace_back("TMUX_TMPDIR=/data/storage/el2/base/cache");
#endif
    env.emplace_back(std::string("HOME=") + home);
    env.emplace_back(std::string("PWD=") + home);
    return env;
}

// HarmonyOS NEXT phones reject ordinary-app fork/pty (typically EPERM). Try
// openpty + posix_spawn before giving up; both still create a process and
// often fail too. Does not mutate parent cwd or environ.
static std::string TryPosixSpawn(terminal_context *ctx, const struct winsize *ws,
                                 int forkpty_err) {
    int master = -1;
    int slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, ws) != 0) {
        int openpty_err = errno;
        LOG_ERROR("%s", FormatErrno("openpty", openpty_err).c_str());
        std::string detail = FormatErrno("forkpty", forkpty_err);
        detail += "; ";
        detail += FormatErrno("openpty", openpty_err);
        return FailPty(ctx, detail);
    }

    const char *bash = PickBash();
    const char *home = PickHome();
    std::string script = std::string("cd '") + home + "' 2>/dev/null; exec '" + bash + "'";
    char dash_c[] = "-c";
    char *argv[] = {const_cast<char *>(bash), dash_c, script.data(), nullptr};

    std::vector<std::string> env_hold = BuildSpawnEnv(home);
    std::vector<char *> envp;
    envp.reserve(env_hold.size() + 1);
    for (std::string &item : env_hold) {
        envp.push_back(item.data());
    }
    envp.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, slave, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, slave, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, slave, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, master);
    posix_spawn_file_actions_addclose(&actions, slave);

    pid_t child = 0;
    int rc = posix_spawn(&child, bash, &actions, nullptr, argv, envp.data());
    posix_spawn_file_actions_destroy(&actions);
    close(slave);
    if (rc != 0) {
        close(master);
        LOG_ERROR("%s", FormatErrno("posix_spawn", rc).c_str());
        std::string detail = FormatErrno("forkpty", forkpty_err);
        detail += "; ";
        detail += FormatErrno("posix_spawn", rc);
        return FailPty(ctx, detail);
    }
    ctx->fd = master;
    return FinishParent(ctx, child, "openpty+posix_spawn");
}

std::string terminal_context::Fork() {
    struct winsize ws = {};
    ws.ws_col = static_cast<unsigned short>(num_cols);
    ws.ws_row = static_cast<unsigned short>(num_rows);
    fd = -1;

    pid_t pid = forkpty(&fd, nullptr, nullptr, &ws);
    if (pid == 0) {
        ExecShell(PickHome());
    }
    if (pid > 0) {
        return FinishParent(this, pid, "forkpty");
    }

    int forkpty_err = errno;
    LOG_ERROR("%s", FormatErrno("forkpty", forkpty_err).c_str());
    if (fd >= 0) {
        close(fd);
    }
    fd = -1;
    return TryPosixSpawn(this, &ws, forkpty_err);
}
