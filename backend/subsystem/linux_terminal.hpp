// PS5Term - GPL-3.0-or-later. Experimental; excluded from product build.
#pragma once
#include "linux_session.h"
#include "terminal_model.hpp"
#include <array>
#include <memory>
namespace term {
// One UI thread owns this adapter and model. Only its worker touches the engine.
// Images remain immutable/alive through stop. stop/destruction join synchronously;
// frontend teardown must account for that rather than running it in a frame pump.
class LinuxTerminal {
public:
    static constexpr std::size_t pump_budget=16*1024;
    LinuxTerminal(int rows,int columns,std::size_t heap_limit=160*1024*1024) noexcept;
    ~LinuxTerminal();
    LinuxTerminal(const LinuxTerminal &)=delete;
    LinuxTerminal &operator=(const LinuxTerminal &)=delete;
    bool healthy() const noexcept;
    bool start(const Ps5LinuxImages &images) noexcept;
    bool stop() noexcept;
    void pump() noexcept;
    bool resize(int rows,int columns) noexcept;
    Ps5LinuxSessionStatus status() const noexcept;
    // References expire on successful start (fresh model). Input generation and
    // model queries are UI-thread-only. False input calls mean backpressure.
    TerminalModel &model() noexcept { return *model_; }
    const TerminalModel &model() const noexcept { return *model_; }
private:
    Ps5LinuxSession *session_=nullptr;
    std::unique_ptr<TerminalModel> model_;
    int rows_,columns_;
    bool failed_=false;
    std::array<char,4096> input_{},output_{};
    std::size_t input_size_=0,input_sent_=0,output_size_=0,output_fed_=0;
    void flush_input(std::size_t &budget) noexcept;
};
}
