// Owned lifecycle fixture; binds only loopback. GPL-3.0-or-later.
#include "backend/owned_ssh/service.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <string>
#include <iostream>
#include <poll.h>
#include <thread>
#include <unistd.h>
// A stand-in guest for routing tests: a session echoes input in upper case, reports
// its window on request, and exits with the status named by an exec command.
class FakeGuest final : public term::ssh::GuestAccess
{
  public:
    int boot_polls = 3;
    int open(unsigned columns, unsigned rows, std::string_view term, std::string_view command,
             bool terminal) noexcept override
    {
        if (command == "linkfail")
            return term::ssh::guest_link_failed;
        if (boot_polls > 0)
        {
            --boot_polls;
            return 0;
        }
        auto &session = sessions_[++last_];
        session.output = "term=" + std::string(term) + " size=" + std::to_string(columns) + "x" +
                         std::to_string(rows) + "\n";
        session.exec = !command.empty();
        if (session.exec)
        {
            if (command.size() > 64) // Long commands: report the size and ending, not the text.
                session.output += "ran " + std::to_string(command.size()) + " bytes ending:" +
                                  std::string(command.substr(command.size() - 16)) + "\n";
            else
                session.output += "ran:" + std::string(command) + "\n";
            if (terminal) // A terminal has no end of input: the command just finishes.
            {
                session.output += "on a terminal\n";
                session.ended = true;
            }
            session.status = command == "fail" ? 7 : 0;
            // "vanish" streams 48000 bytes, then Linux "stops" once 32 KiB have been read.
            if (command == "vanish")
                session.output += std::string(48000, 'v');
            session.vanish = command == "vanish";
        }
        return last_;
    }
    std::size_t write(int handle, std::string_view bytes) noexcept override
    {
        auto &session = sessions_[handle];
        for (char ch : bytes)
            session.output.push_back(
                static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        return bytes.size();
    }
    std::size_t read(int handle, char *bytes, std::size_t capacity) noexcept override
    {
        auto &session = sessions_[handle];
        const auto count = std::min(capacity, session.output.size());
        std::copy_n(session.output.data(), count, bytes);
        session.output.erase(0, count);
        session.consumed += count;
        return count;
    }
    void resize(int handle, unsigned columns, unsigned rows) noexcept override
    {
        sessions_[handle].output +=
            "resized=" + std::to_string(columns) + "x" + std::to_string(rows) + "\n";
    }
    void finish_input(int handle) noexcept override
    {
        sessions_[handle].output += "stdin-eof\n";
        sessions_[handle].ended = true;
    }
    void close(int handle) noexcept override
    {
        sessions_.erase(handle);
    }
    int poll(int handle, int *status) noexcept override
    {
        auto it = sessions_.find(handle);
        if (it == sessions_.end())
            return -1;
        *status = it->second.status;
        if (it->second.vanish && it->second.consumed >= 32768)
            return -1;
        return it->second.exec && it->second.output.empty() && it->second.ended ? 1 : 0;
    }

  private:
    struct Session
    {
        std::string output;
        bool exec = false, ended = false, vanish = false;
        std::size_t consumed = 0;
        int status = 0;
    };
    std::map<int, Session> sessions_;
    int last_ = 0;
};
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    term::ssh::Service service;
    FakeGuest guest;
    service.set_guest(&guest);
    if (!service.start(argv[1], "127.0.0.1", 0))
        return 3;
    std::cout << service.port() << std::endl;
    using Clock = std::chrono::steady_clock;
    const auto end = Clock::now() + std::chrono::minutes(5);
    while (Clock::now() < end)
    {
        pollfd control{STDIN_FILENO, POLLIN, 0};
        if (poll(&control, 1, 0) > 0 && (control.revents & (POLLIN | POLLHUP)))
        {
            std::string command;
            if (!std::getline(std::cin, command) || command == "stop")
                break;
            if (command.starts_with("password "))
                std::cout << (service.set_password(std::string_view(command).substr(9)) ? "saved"
                                                                                        : "refused")
                          << std::endl;
            volatile char *wipe = command.data();
            for (std::size_t i = 0; i < command.size(); ++i)
                wipe[i] = 0;
        }
        service.tick(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch())
                .count());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    service.stop();
}
