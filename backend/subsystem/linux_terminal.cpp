// PS5Term - GPL-3.0-or-later.
#include "linux_terminal.hpp"
#include <algorithm>
#include <new>
namespace term {
LinuxTerminal::LinuxTerminal(int rows,int columns,std::size_t limit) noexcept
 :session_(ps5_linux_session_create(limit)),model_(new(std::nothrow) TerminalModel(rows,columns)),rows_(rows),columns_(columns) {}
LinuxTerminal::~LinuxTerminal() { ps5_linux_session_destroy(session_); }
bool LinuxTerminal::healthy() const noexcept
{ return session_ && model_ && model_->healthy() && !failed_; }
Ps5LinuxSessionStatus LinuxTerminal::status() const noexcept
{ return ps5_linux_session_status(session_); }
bool LinuxTerminal::start(const Ps5LinuxImages &images) noexcept
{
    if(!healthy() || status().state!=PS5_SESSION_STOPPED) return false;
    std::unique_ptr<TerminalModel> clean(new(std::nothrow) TerminalModel(rows_,columns_));
    if(!clean || !clean->healthy()) return false;
    if(!ps5_linux_session_resize(session_,columns_,rows_) || !ps5_linux_session_start(session_,&images)) return false;
    model_=std::move(clean);input_size_=input_sent_=output_size_=output_fed_=0;return true;
}
bool LinuxTerminal::stop() noexcept
{
    bool stopped=ps5_linux_session_stop(session_);
    if(stopped) input_size_=input_sent_=output_size_=output_fed_=0;
    return stopped;
}
bool LinuxTerminal::resize(int rows,int columns) noexcept
{
    if(!healthy() || rows<1 || rows>TerminalModel::max_rows || columns<1 || columns>TerminalModel::max_columns) return false;
    if(!model_->resize(rows,columns)) { failed_=true;return false; }
    rows_=rows;columns_=columns;
    return ps5_linux_session_resize(session_,columns,rows);
}
void LinuxTerminal::flush_input(std::size_t &budget) noexcept
{
    while(budget) {
        if(input_sent_==input_size_) {
            input_sent_=0;input_size_=model_->take_output(input_);
            if(!input_size_) break;
        }
        auto size=std::min(budget,input_size_-input_sent_);
        auto sent=ps5_linux_session_write(session_,input_.data()+input_sent_,size);
        input_sent_+=sent;budget-=sent;
        if(sent<size) break;
    }
}
void LinuxTerminal::pump() noexcept
{
    if(!healthy()) return;
    std::size_t input_budget=pump_budget,output_budget=pump_budget;
    flush_input(input_budget);
    while(output_budget) {
        if(output_fed_==output_size_) {
            output_fed_=0;
            output_size_=ps5_linux_session_read(session_,output_.data(),std::min(output_.size(),output_budget));
            if(!output_size_) break;
        }
        auto size=std::min(output_budget,output_size_-output_fed_);
        auto fed=model_->feed({output_.data()+output_fed_,size});
        output_fed_+=fed;output_budget-=fed;
        flush_input(input_budget); // Includes VT replies produced by feed.
        if(fed<size) break;
    }
    if(!model_->healthy()) failed_=true;
}
}
