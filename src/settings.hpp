// PS5Term - persistent SSH preference. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace term
{
class Settings
{
  public:
    explicit Settings(const char *directory = "/data/ps5term",
                      const char *flag = "/data/ps5term/ssh.enabled") noexcept
        : directory_(directory), flag_(flag)
    {
    }
    unsigned text_style() const noexcept;
    bool set_text_style(unsigned) noexcept;
    bool ssh_enabled() const noexcept;
    bool set_ssh_enabled(bool enabled) noexcept;
    static bool read_ssh_password(char *output, std::size_t capacity,
                                  const char *path =
#if defined(PS5TERM_OWNED_SSH)
                                      "/data/ps5term/ssh/password"
#else
                                      "/data/ps5sshd/password"
#endif
                                  ) noexcept;

  private:
    const char *directory_, *flag_;
};
} // namespace term
