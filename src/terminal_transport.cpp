// PS5Term - GPL-3.0-or-later.
#include "terminal_transport.hpp"
#include <algorithm>
#include <cstring>
namespace term
{
bool TerminalTransport::write(std::string_view bytes) noexcept
{
    if (failed_ || eof_ || bytes.size() > input_.size() - (input_size_ - input_sent_))
        return false;
    if (input_sent_)
    {
        std::memmove(input_.data(), input_.data() + input_sent_, input_size_ - input_sent_);
        input_size_ -= input_sent_;
        input_sent_ = 0;
    }
    if (!bytes.empty())
        std::memcpy(input_.data() + input_size_, bytes.data(), bytes.size());
    input_size_ += bytes.size();
    return true;
}
void TerminalTransport::reset() noexcept
{
    input_size_ = input_sent_ = output_size_ = output_fed_ = 0;
    eof_ = failed_ = false;
}
void TerminalTransport::flush(TerminalModel &model, Port port, std::size_t &budget) noexcept
{
    while (budget && !failed_)
    {
        if (input_sent_ == input_size_)
        {
            input_sent_ = 0;
            input_size_ = model.take_output(std::span<char>(input_.data(), 4096));
            if (!input_size_)
                break;
        }
        auto size = std::min({budget, input_size_ - input_sent_, std::size_t(4096)});
        auto result = port.write(port.context, input_.data() + input_sent_, size);
        if (result.count > size || (result.state != platform::IoState::ok && result.count))
        {
            failed_ = true;
            break;
        }
        input_sent_ += result.count;
        budget -= result.count;
        if (result.state == platform::IoState::failed || result.state == platform::IoState::eof)
        {
            failed_ = true;
            break;
        }
        if (result.count < size)
            break;
    }
}
void TerminalTransport::pump(TerminalModel &model, Port port) noexcept
{
    if (failed_ || eof_)
        return;
    if (!model.healthy() || !port.write || !port.read)
    {
        failed_ = true;
        return;
    }
    std::size_t in_budget = pump_budget, out_budget = pump_budget;
    flush(model, port, in_budget);
    while (out_budget && !failed_)
    {
        if (output_fed_ == output_size_)
        {
            auto result =
                port.read(port.context, output_.data(), std::min(output_.size(), out_budget));
            if (result.count > std::min(output_.size(), out_budget) ||
                (result.state != platform::IoState::ok && result.count))
            {
                failed_ = true;
                break;
            }
            output_fed_ = 0;
            output_size_ = result.count;
            if (result.state == platform::IoState::eof)
            {
                eof_ = true;
                break;
            }
            if (result.state == platform::IoState::failed)
            {
                failed_ = true;
                break;
            }
            if (!output_size_)
                break;
        }
        auto size = std::min(out_budget, output_size_ - output_fed_);
        auto fed = model.feed({output_.data() + output_fed_, size});
        output_fed_ += fed;
        out_budget -= fed;
        flush(model, port, in_budget); // Query replies use the same retained queue.
        if (!model.healthy())
        {
            failed_ = true;
            break;
        }
        if (fed < size)
            break;
    }
}
} // namespace term
