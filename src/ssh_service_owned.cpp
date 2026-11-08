// PS5Term owned native SSH selection. GPL-3.0-or-later.
#if defined(PS5TERM_OWNED_SSH)
#include "ssh_service.hpp"
#include "i18n.hpp"
#include "../backend/owned_ssh/service.hpp"
namespace term
{
SshService::SshService(const char *directory, unsigned primary, unsigned, unsigned) noexcept
    : directory_(directory), port_(primary), service_(std::make_unique<ssh::Service>())
{
}
SshService::~SshService() = default;
void SshService::tick(std::uint64_t now) noexcept
{
    if (!attempted_)
    {
        attempted_ = true;
        failed_ = !service_->start(directory_, "0.0.0.0", port_);
    }
    if (!failed_)
        service_->tick(now);
}
void SshService::retry() noexcept
{
    service_->stop();
    attempted_ = failed_ = false;
}
void SshService::cancel() noexcept
{
    retry();
}
bool SshService::set_password(std::string_view value) noexcept
{
    if (service_->listening())
        return service_->set_password(value);
    // Configuration while disabled creates no listener and never enables SSH.
    ssh::Identity identity;
    return identity.load_or_create(directory_) && identity.update_password(value);
}
void SshService::set_guest(ssh::GuestAccess *guest) noexcept
{
    service_->set_guest(guest);
}
bool SshService::listening() const noexcept
{
    return service_->listening();
}
unsigned SshService::port() const noexcept
{
    return listening() ? service_->port() : port_;
}
// The owned service's status texts (backend/owned_ssh/service.cpp), listed here so
// tools/generate-i18n.py collects them; the app translates status() where it draws it.
[[maybe_unused]] constexpr const char *owned_status_texts[] = {
    N_("Invalid SSH address or port"),
    N_("Invalid SSH bind address"),
    N_("SSH crypto initialization failed"),
    N_("SSH socket creation failed"),
    N_("Cannot bind SSH port (busy or unavailable)"),
    N_("Cannot read SSH listener address"),
    N_("PS5Term SSH server is listening"),
};
std::string_view SshService::status() const noexcept
{
    return service_->status();
}
} // namespace term
#endif
