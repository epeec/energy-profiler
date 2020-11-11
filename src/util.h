// util.h
#pragma once

#include <cstdint>
#include <cinttypes>
#include <signal.h>
#include <sys/ptrace.h>

namespace tep
{

void procmsg(const char* format, ...);

uintptr_t get_entrypoint_addr(pid_t pid);

constexpr bool is_clone_event(int wait_status)
{
    return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_CLONE << 8));
}

constexpr bool is_vfork_event(int wait_status)
{
    return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_VFORK << 8));
}

constexpr bool is_fork_event(int wait_status)
{
    return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_FORK << 8));
}

constexpr unsigned long lsb_mask()
{
#if __x86_64__
    return 0xFFFFFFFFFFFFFF00;
#elif __i386__
    return 0xFFFFFF00;
#else
    return 0x0;
#endif
}

}
