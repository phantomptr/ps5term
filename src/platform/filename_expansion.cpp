// PS5Term - GPL-3.0-or-later.
#include "filename_expansion.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
namespace term::platform
{
namespace
{
bool magic(const char *word, const char *base, const unsigned char *mask) noexcept
{
    if (!word)
        return false;
    for (std::size_t i = 0; word[i]; ++i)
        if (!mask[word - base + i] && std::strchr("*?[]", word[i]))
            return true;
    return false;
}
} // namespace
bool FilenameExpansion::needed(const Ps5termPipeline &p, const char *base,
                               const unsigned char *mask) noexcept
{
    for (int s = 0; s < p.count; ++s)
        for (int a = 0; a < p.stages[s].argc; ++a)
            if (magic(p.stages[s].argv[a], base, mask))
                return true;
    return magic(p.redir_in, base, mask) || magic(p.redir_out, base, mask) ||
           magic(p.redir_err, base, mask);
}
FilenameExpansion::FilenameExpansion(Ps5termPipeline &p, const char *base,
                                     const unsigned char *mask, const char *cwd) noexcept
    : pipeline_(p), line_(base), cwd_(cwd), protection_(mask)
{
    // Filename expansion is deliberately absent for redirects, rather than
    // creating a file whose unquoted pattern looks as though it was expanded.
    if (magic(p.redir_in, base, mask) || magic(p.redir_out, base, mask) ||
        magic(p.redir_err, base, mask))
        fail(ENOTSUP);
}
bool FilenameExpansion::active(const char *word, std::size_t i) const noexcept
{
    return !protection_[word - line_ + i];
}
void FilenameExpansion::fail(int e) noexcept
{
    error_ = e;
    done_ = true;
    pattern_ = nullptr;
    directory_.close();
}
bool FilenameExpansion::put(std::string_view first, std::string_view second) noexcept
{
    auto &st = stages_[stage_];
    if (st.argc >= PS5TERM_PIPE_ARGV - 1 ||
        first.size() + second.size() + 1 > storage_.size() - used_)
    {
        fail(E2BIG);
        return false;
    }
    st.argv[st.argc++] = storage_.data() + used_;
    std::memcpy(storage_.data() + used_, first.data(), first.size());
    used_ += first.size();
    if (!second.empty())
        std::memcpy(storage_.data() + used_, second.data(), second.size());
    used_ += second.size();
    storage_[used_++] = 0;
    return true;
}
bool FilenameExpansion::matches(std::string_view name) const noexcept
{
    const char *p = pattern_ + prefix_;
    if (!name.empty() && name[0] == '.' && p[0] != '.')
        return false;
    std::size_t i = 0, j = 0, star = std::string_view::npos, retry = 0;
    while (j < name.size())
    {
        if (p[i] && p[i] == '*' && active(p, i))
        {
            star = i++;
            retry = j;
        }
        else if (p[i] && ((p[i] == '?' && active(p, i)) || p[i] == name[j]))
        {
            ++i;
            ++j;
        }
        else if (star != std::string_view::npos)
        {
            i = star + 1;
            j = ++retry;
        }
        else
            return false;
    }
    while (p[i] == '*' && active(p, i))
        ++i;
    return p[i] == 0;
}
void FilenameExpansion::finish_pattern() noexcept
{
    directory_.close();
    auto &st = stages_[stage_];
    if (st.argc == match_start_)
    {
        if (!put(pattern_))
            return;
    }
    else
        std::sort(st.argv + match_start_, st.argv + st.argc,
                  [](const char *a, const char *b) { return std::strcmp(a, b) < 0; });
    pattern_ = nullptr;
}
void FilenameExpansion::tick() noexcept
{
    if (done_)
        return;
    for (unsigned work = 0; work < 32 && !done_; ++work)
    {
        if (pattern_)
        {
            std::string_view name;
            const auto step = directory_.next(name);
            if (step == DirectoryStep::failed)
            {
                fail(errno);
                return;
            }
            if (step == DirectoryStep::end)
            {
                finish_pattern();
                continue;
            }
            if (++visited_ > entry_limit)
            {
                fail(E2BIG);
                return;
            }
            if (name == "." || name == ".." || !matches(name))
                continue;
            if (!put({pattern_, prefix_}, name))
                return;
            continue;
        }
        if (stage_ == pipeline_.count)
        {
            for (int s = 0; s < pipeline_.count; ++s)
                pipeline_.stages[s] = stages_[s];
            done_ = true;
            return;
        }
        const auto &source = pipeline_.stages[stage_];
        if (arg_ == source.argc)
        {
            ++stage_;
            arg_ = 0;
            continue;
        }
        const char *word = source.argv[arg_++];
        if (!magic(word, line_, protection_))
        {
            if (!put(word))
                return;
            continue;
        }
        const char *slash = std::strrchr(word, '/');
        prefix_ = slash ? static_cast<std::size_t>(slash + 1 - word) : 0;
        for (std::size_t i = 0; word[i]; ++i)
            if (active(word, i) &&
                (std::strchr("[]", word[i]) || (i < prefix_ && std::strchr("*?", word[i]))))
            {
                fail(ENOTSUP);
                return;
            }
        std::array<char, 1024> directory{};
        const int n = word[0] == '/' ? std::snprintf(directory.data(), directory.size(), "%.*s",
                                                     static_cast<int>(prefix_), word)
                                     : std::snprintf(directory.data(), directory.size(), "%s/%.*s",
                                                     cwd_, static_cast<int>(prefix_), word);
        if (n < 0 || static_cast<std::size_t>(n) >= directory.size())
        {
            fail(ENAMETOOLONG);
            return;
        }
        if (!directory_.open(directory.data()))
        {
            const int e = errno;
            if (e == ENOENT || e == ENOTDIR)
            {
                if (!put(word))
                    return;
                continue;
            }
            fail(e);
            return;
        }
        pattern_ = word;
        match_start_ = stages_[stage_].argc;
    }
}
} // namespace term::platform
