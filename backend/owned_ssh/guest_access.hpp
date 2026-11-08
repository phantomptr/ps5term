// PS5Term - what an SSH session needs from the Linux guest. GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <string_view>
namespace term::ssh
{
inline constexpr int guest_link_failed=-2;
// Keeps the SSH server independent of the emulator. All calls happen on the
// service's thread. Handles are positive and never reused.
class GuestAccess
{
  public:
    virtual ~GuestAccess()=default;
    // A positive handle, 0 to try again shortly (the guest is booting or busy),
    // guest_link_failed if Linux runs but its session link broke (needs a restart),
    // or another negative value if Linux cannot be provided.
    // A command runs on a terminal when `terminal` (the client sent pty-req), else on
    // exact bytes with a real end of input.
    virtual int open(unsigned columns,unsigned rows,std::string_view term,std::string_view command,
                     bool terminal) noexcept=0;
    virtual std::size_t write(int handle,std::string_view bytes) noexcept=0;
    virtual std::size_t read(int handle,char *bytes,std::size_t capacity) noexcept=0;
    virtual void resize(int handle,unsigned columns,unsigned rows) noexcept=0;
    virtual void finish_input(int handle) noexcept=0; // The client's stdin ended.
    virtual void close(int handle) noexcept=0;
    // 0 while running, 1 when exited with *status set and all output read, -1 when gone.
    virtual int poll(int handle,int *status) noexcept=0;
};
}
