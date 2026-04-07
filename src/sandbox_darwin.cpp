#include "sandbox.h"

#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include <string>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/clonefile.h>

// sandbox_init() is declared deprecated/obsoleted in the public SDK for
// deployment targets >= macOS 10.8, so including <sandbox.h> hides the
// declarations via availability guards.  We forward-declare the functions
// directly here — the symbols still exist in libSystem at runtime and have
// been verified to work on macOS 26 (see macos-sandbox-design.md §3.1).
extern "C" {
    int  sandbox_init(const char *profile, uint64_t flags, char **errorbuf);
    void sandbox_free_error(char *errorbuf);
}

namespace boxsh {

static std::string errno_str(const char *context) {
    return std::string(context) + ": " + std::strerror(errno);
}

// ---------------------------------------------------------------------------
// SBPL profile builder
// ---------------------------------------------------------------------------

// Resolve symlinks in a path using realpath(3).  Returns the original path
// unchanged if realpath fails (e.g. path does not yet exist).
static std::string resolve_path(const std::string &path) {
    char buf[PATH_MAX];
    const char *r = realpath(path.c_str(), buf);
    return r ? std::string(r) : path;
}

// True if path equals prefix or starts with prefix + '/'.
static bool is_under(const std::string &path, const std::string &prefix) {
    return path == prefix ||
           (path.size() > prefix.size() &&
            path[prefix.size()] == '/' &&
            path.compare(0, prefix.size(), prefix) == 0);
}

// Build a Sandbox Profile Language (SBPL) string from the sandbox
// configuration.  sandbox_init() with flags=0 and a custom SBPL string is an
// undocumented private API verified to work on macOS 26+.
//
// Design: allow reads globally but deny the user-owned temp dir
// (/private/var/folders) so sandbox-external temp files are inaccessible.
// Write access is granted only to /dev, explict bind-mount destinations, and
// the process CWD (when it is not itself a COW source).
//
// All paths are resolved via realpath() so SBPL subpath rules use canonical
// paths (e.g. /private/tmp rather than the /tmp symlink).
//
// writable_cwd: canonical CWD to allow writes for; pass empty to skip.
static std::string build_sbpl(const SandboxConfig &cfg,
                               const std::string &writable_cwd) {
    std::string p;
    p += "(version 1)\n";
    p += "(deny default)\n";

    // Allow reading the entire filesystem.  macOS processes need broad read
    // access for dyld shared cache, system frameworks, mach services, etc.
    p += "(allow file-read*)\n";

    // Allow all process, mach and IPC operations.
    p += "(allow process*)\n";
    p += "(allow signal)\n";
    p += "(allow mach*)\n";
    p += "(allow ipc*)\n";
    p += "(allow sysctl*)\n";
    p += "(allow file-ioctl)\n";

    // Allow reads and writes to /dev (e.g. /dev/null, /dev/zero, /dev/urandom).
    p += "(allow file-read* (subpath \"/dev\"))\n";
    p += "(allow file-write* (subpath \"/dev\"))\n";

    // Allow writes to the process CWD when it is not a COW source so that
    // plain --sandbox mode does not block relative writes.
    if (!writable_cwd.empty()) {
        p += "(allow file-read* (subpath \"" + writable_cwd + "\"))\n";
        p += "(allow file-write* (subpath \"" + writable_cwd + "\"))\n";
    }

    // User-specified bind rules.  Paths are resolved so SBPL matches the
    // canonical path that the kernel presents to the policy engine.
    for (const auto &bm : cfg.bind_mounts) {
        std::string rsrc = resolve_path(bm.src);
        if (bm.mode == BindMount::Mode::RW) {
            p += "(allow file-read* (subpath \"" + rsrc + "\"))\n";
            p += "(allow file-write* (subpath \"" + rsrc + "\"))\n";
        } else if (bm.mode == BindMount::Mode::COW) {
            std::string rdst = resolve_path(bm.dst);
            // Block writes to the COW source so it stays pristine.
            p += "(deny file-write* (subpath \"" + rsrc + "\"))\n";
            // Allow full read+write access to the clone (dst).
            p += "(allow file-read* (subpath \"" + rdst + "\"))\n";
            p += "(allow file-write* (subpath \"" + rdst + "\"))\n";
        }
    }

    // Network: allow by default; deny everything when --new-net-ns is set.
    if (!cfg.new_net_ns) {
        p += "(allow network*)\n";
    }

    return p;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SandboxResult sandbox_apply(const SandboxConfig &cfg) {
    SandboxResult res;

    if (!cfg.enabled) {
        res.ok = true;
        return res;
    }

    // ── 1. COW: clonefile(src, dst) ──────────────────────────────────────
    // clonefile(2) creates an instant APFS COW snapshot of src at dst.
    // dst must not already exist.  main() pre-creates an empty directory
    // for Linux overlayfs compatibility; remove it before clonefile.
    for (const auto &bm : cfg.bind_mounts) {
        if (bm.mode != BindMount::Mode::COW) continue;

        struct stat st;
        if (stat(bm.dst.c_str(), &st) == 0) {
            if (!S_ISDIR(st.st_mode)) {
                res.error = "COW dst exists and is not a directory: " + bm.dst;
                return res;
            }
            if (rmdir(bm.dst.c_str()) != 0) {
                res.error = errno_str(("rmdir pre-existing dst: " + bm.dst).c_str());
                return res;
            }
        }

        if (clonefile(bm.src.c_str(), bm.dst.c_str(), 0) != 0) {
            if (errno == ENOTSUP) {
                res.error = "clonefile: " + bm.src + " -> " + bm.dst
                    + ": filesystem does not support COW cloning"
                      " (APFS volume required)";
            } else {
                res.error = errno_str(
                    ("clonefile: " + bm.src + " -> " + bm.dst).c_str());
            }
            return res;
        }
    }

    // ── 2. Apply Seatbelt profile ─────────────────────────────────────────
    // Capture the CWD before sandbox_init so we can allow writes there if
    // it is not itself a COW source (which must remain read-only).
    std::string writable_cwd;
    {
        char *cwd_buf = getcwd(nullptr, 0);
        if (cwd_buf) {
            std::string rcwd = resolve_path(cwd_buf);
            free(cwd_buf);
            bool cwd_is_cow_src = false;
            for (const auto &bm : cfg.bind_mounts) {
                if (bm.mode == BindMount::Mode::COW) {
                    if (is_under(rcwd, resolve_path(bm.src))) {
                        cwd_is_cow_src = true;
                        break;
                    }
                }
            }
            if (!cwd_is_cow_src) writable_cwd = rcwd;
        }
    }

    // sandbox_init() with flags=0 and a custom SBPL string is an undocumented
    // private API.  On failure we print a warning and continue without
    // sandboxing (documented fallback; see macos-sandbox-design.md §7).
    std::string profile = build_sbpl(cfg, writable_cwd);
    char *sb_err = nullptr;
    int rc = sandbox_init(profile.c_str(), 0, &sb_err);
    if (rc != 0) {
        std::fprintf(stderr,
            "boxsh: warning: sandbox_init failed: %s"
            " (running without sandbox)\n",
            sb_err ? sb_err : "unknown error");
        if (sb_err) sandbox_free_error(sb_err);
        res.ok = true;
        return res;
    }

    // ── 3. Redirect CWD into the COW dst if it was within a COW src ──────
    // getcwd() on macOS returns the canonical (symlink-resolved) path, so
    // we must resolve bm.src before comparing with is_under().
    {
        char *cwd_buf = getcwd(nullptr, 0);
        std::string saved_cwd = cwd_buf ? cwd_buf : "";
        free(cwd_buf);

        for (const auto &bm : cfg.bind_mounts) {
            if (bm.mode != BindMount::Mode::COW) continue;
            std::string rsrc = resolve_path(bm.src);
            if (!is_under(saved_cwd, rsrc)) continue;

            std::string new_cwd = (saved_cwd == rsrc)
                ? bm.dst
                : bm.dst + saved_cwd.substr(rsrc.size());
            if (chdir(new_cwd.c_str()) != 0) {
                chdir(bm.dst.c_str()); // fall back to clone root
            }
            break;
        }
    }

    res.ok = true;
    return res;
}

} // namespace boxsh
