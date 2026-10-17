// Orbit Store TV app - Starting Orbit when it isn't running.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/starter.hpp"
#include "orbit/i18n.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <utility>
#include <vector>

namespace orbit::starter
{

namespace
{

constexpr long long kMaxPayload = 64ll << 20;

// major.minor.patch, then 1 for a release or 0 for a beta, then the beta number.
bool version_parts(std::string_view text, std::array<unsigned, 5> *parts)
{
    if (!text.empty() && text.front() == 'v')
        text.remove_prefix(1);
    *parts = {0, 0, 0, 1, 0};
    for (int i = 0; i < 3; ++i)
    {
        unsigned value = 0;
        int digits = 0;
        while (!text.empty() && text.front() >= '0' && text.front() <= '9')
        {
            if (++digits > 6)
                return false;
            value = value * 10 + static_cast<unsigned>(text.front() - '0');
            text.remove_prefix(1);
        }
        if (digits == 0)
            return false;
        (*parts)[i] = value;
        if (i < 2)
        {
            if (text.empty() || text.front() != '.')
                return false;
            text.remove_prefix(1);
        }
    }
    if (text.empty())
        return true;
    constexpr std::string_view kBeta = "-beta.";
    if (text.substr(0, kBeta.size()) != kBeta)
        return false;
    text.remove_prefix(kBeta.size());
    (*parts)[3] = 0;
    unsigned value = 0;
    int digits = 0;
    while (!text.empty() && text.front() >= '0' && text.front() <= '9')
    {
        if (++digits > 6)
            return false;
        value = value * 10 + static_cast<unsigned>(text.front() - '0');
        text.remove_prefix(1);
    }
    (*parts)[4] = value;
    return digits > 0 && text.empty();
}

std::string read_small(const std::string &path)
{
    std::string text;
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return text;
    char buffer[64];
    const ssize_t n = read(fd, buffer, sizeof(buffer));
    close(fd);
    if (n > 0)
        text.assign(buffer, static_cast<std::size_t>(n));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

// A regular file that starts like Orbit: ELF64, little-endian, x86-64.
bool readable_elf(const std::string &path, long long *bytes, std::string *why = nullptr)
{
    std::string unused;
    if (why == nullptr)
        why = &unused;
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        const int error = errno;
        if (error == ENOENT)
        {
            *why = "missing";
            return false;
        }
        // Say what the console refused: the app's own view of the file.
        *why = std::string("unreadable: ") + std::strerror(error) + " (" + std::to_string(error) + ")";
        struct stat found
        {
        };
        if (stat(path.c_str(), &found) == 0)
        {
            char mode[48];
            std::snprintf(mode, sizeof(mode), ", mode %04o uid %d gid %d",
                          static_cast<unsigned>(found.st_mode & 07777), static_cast<int>(found.st_uid),
                          static_cast<int>(found.st_gid));
            *why += mode;
        }
        return false;
    }
    struct stat info
    {
    };
    unsigned char head[20] = {};
    const bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 64 &&
                    info.st_size <= kMaxPayload &&
                    read(fd, head, sizeof(head)) == static_cast<ssize_t>(sizeof(head)) &&
                    std::memcmp(head, "\177ELF\2\1", 6) == 0 && head[18] == 62 && head[19] == 0;
    close(fd);
    if (ok)
        *bytes = static_cast<long long>(info.st_size);
    else if (!S_ISREG(info.st_mode))
        *why = "not a regular file";
    else if (info.st_size < 64)
        *why = "too small";
    else if (info.st_size > kMaxPayload)
        *why = "too large";
    else
        *why = "not an x86-64 ELF";
    return ok;
}

Payload candidate(const std::string &elf, const std::string &version, const char *source,
                  std::string *notes = nullptr)
{
    Payload payload;
    std::string why;
    if (!readable_elf(elf, &payload.bytes, &why))
    {
        if (notes != nullptr)
            *notes += std::string(" ") + source + "=rejected(" + why + ")";
        return payload;
    }
    if (notes != nullptr)
        *notes += std::string(" ") + source + "=" + read_small(version) + "/" +
                  std::to_string(payload.bytes);
    payload.path = elf;
    payload.version = read_small(version);
    payload.source = source;
    return payload;
}

const char *outcome_name(Outcome outcome)
{
    switch (outcome)
    {
    case Outcome::sent:
        return "sent";
    case Outcome::no_loader:
        return "no loader";
    case Outcome::no_payload:
        return "no payload";
    case Outcome::failed:
        break;
    }
    return "failed";
}

} // namespace

int compare_versions(std::string_view a, std::string_view b)
{
    std::array<unsigned, 5> x{};
    std::array<unsigned, 5> y{};
    const bool valid_a = version_parts(a, &x);
    const bool valid_b = version_parts(b, &y);
    if (!valid_a || !valid_b)
        return valid_a == valid_b ? 0 : valid_a ? 1 : -1;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        if (x[i] != y[i])
            return x[i] > y[i] ? 1 : -1;
    }
    return 0;
}

Payload choose(const Locations &where, std::string *notes)
{
    Payload saved = candidate(where.saved_elf, where.saved_version, "saved", notes);
    Payload bundled = candidate(where.bundled_elf, where.bundled_version, "bundled", notes);
    if (saved.path.empty())
        return bundled;
    if (bundled.path.empty())
        return saved;
    // The saved copy is what payload managers start and what Orbit's updater
    // refreshes, so it wins unless this package carries a newer release.
    return compare_versions(bundled.version, saved.version) > 0 ? bundled : saved;
}

namespace
{

// Reads up to `size` bytes into `buffer`; returns what it read, 0 at the end, -1 on failure.
using Reader = std::function<ssize_t(char *buffer, std::size_t size)>;

Outcome stream_to_loader(const Reader &read_some, const std::string &what, std::uint16_t port,
                         int timeout_ms, std::string *error)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        *error = tr("Could not open a connection on this console.");
        return Outcome::failed;
    }
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<const sockaddr *>(&peer), sizeof(peer)) != 0)
    {
        const bool refused = errno == ECONNREFUSED;
        close(fd);
        *error = refused ? tr("No ELF loader is listening on port {port}.",
                              {{"port", std::to_string(port)}})
                         : tr("Could not reach the ELF loader on port {port}.",
                              {{"port", std::to_string(port)}});
        return refused ? Outcome::no_loader : Outcome::failed;
    }

    std::vector<char> buffer(64u << 10);
    Outcome outcome = Outcome::sent;
    for (;;)
    {
        const ssize_t got = read_some(buffer.data(), buffer.size());
        if (got < 0)
        {
            *error = tr("Could not read {path}.", {{"path", what}});
            outcome = Outcome::failed;
            break;
        }
        if (got == 0)
            break;
        std::size_t sent = 0;
        while (sent < static_cast<std::size_t>(got))
        {
#ifdef MSG_NOSIGNAL
            const ssize_t n = ::send(fd, buffer.data() + sent, got - sent, MSG_NOSIGNAL);
#else
            const ssize_t n = ::send(fd, buffer.data() + sent, got - sent, 0);
#endif
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
            {
                *error = tr("The ELF loader stopped taking Orbit part way.");
                outcome = Outcome::failed;
                break;
            }
            sent += static_cast<std::size_t>(n);
        }
        if (outcome != Outcome::sent)
            break;
    }
    if (outcome == Outcome::sent)
    {
        // The loader starts the payload once it reads the end of the file.
        // Wait for it to close its side, so nothing is cut short.
        shutdown(fd, SHUT_WR);
        char ignored[256];
        while (recv(fd, ignored, sizeof(ignored), 0) > 0)
        {
        }
    }
    close(fd);
    return outcome;
}

} // namespace

Outcome send(const std::string &path, std::uint16_t port, int timeout_ms, std::string *error)
{
    const int file = open(path.c_str(), O_RDONLY);
    if (file < 0)
    {
        *error = tr("Could not read {path}.", {{"path", path}});
        return Outcome::no_payload;
    }
    const Reader reader = [file](char *buffer, std::size_t size)
    {
        for (;;)
        {
            const ssize_t got = read(file, buffer, size);
            if (got < 0 && errno == EINTR)
                continue;
            return got;
        }
    };
    const Outcome outcome = stream_to_loader(reader, path, port, timeout_ms, error);
    close(file);
    return outcome;
}

Outcome send_image(const std::string &image, std::uint16_t port, int timeout_ms, std::string *error)
{
    std::size_t at = 0;
    const Reader reader = [&image, &at](char *buffer, std::size_t size) -> ssize_t
    {
        const std::size_t count = std::min(size, image.size() - at);
        std::memcpy(buffer, image.data() + at, count);
        at += count;
        return static_cast<ssize_t>(count);
    };
    return stream_to_loader(reader, "Orbit", port, timeout_ms, error);
}

LoaderStarter::LoaderStarter(Locations where, std::uint16_t port,
                             std::function<void(const std::string &)> log)
    : where_(std::move(where)), port_(port), log_(std::move(log))
{
}

void LoaderStarter::announce(std::string version, std::string image)
{
    // Logged so a console log shows whether Orbit handed its copy over before a restart.
    if (log_ && (!version.empty() || !image.empty()))
        log_("announce: service copy " + (image.empty() ? std::string("not received") : std::to_string(image.size()) + " bytes") +
             (version.empty() ? "" : ", version " + version));
    std::lock_guard<std::mutex> lock(mutex_);
    announced_version_ = std::move(version);
    announced_image_ = image.empty() ? nullptr : std::make_shared<const std::string>(std::move(image));
}

Result LoaderStarter::start()
{
    Result result;
    std::string notes;
    Payload payload = choose(where_, &notes);
    std::string announced;
    std::shared_ptr<const std::string> image;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        announced = announced_version_;
        image = announced_image_;
    }
    // The saved file is out of the app's reach, so the service hands over its own copy. It wins
    // over the one bundled here unless the bundle is a newer release, like a saved file would.
    const bool from_service = image != nullptr &&
                              (payload.path.empty() || payload.source[0] != 's') &&
                              (payload.path.empty() || announced.empty() ||
                               compare_versions(announced, payload.version) >= 0);
    if (image != nullptr)
        notes += std::string(" service=") + (announced.empty() ? "?" : announced) + "/" +
                 std::to_string(image->size()) + (from_service ? " used" : " not used");
    // Orbit's updater announces the release it saved (the service says it before it stops, or
    // saved-version.txt does where the file can be read). If the copy to send is older, the
    // saved file was refused: say so instead of starting the old release and reporting a
    // version mismatch afterwards.
    if (announced.empty())
        announced = read_small(where_.saved_version);
    if (!from_service && !payload.path.empty() && !announced.empty() && payload.source[0] == 'b' &&
        compare_versions(announced, payload.version) > 0)
    {
        result.outcome = Outcome::failed;
        result.detail = tr("The saved Orbit {version} can\xE2\x80\x99t be used. Start {path} from your payload manager.",
                           {{"version", announced}, {"path", where_.saved_elf}});
        if (log_)
            log_("start: refused, saved copy unusable;" + notes + " announced=" + announced);
        return result;
    }
    const char *source = payload.source;
    std::string version = payload.version;
    long long bytes = payload.bytes;
    if (from_service)
    {
        source = "service";
        version = announced;
        bytes = static_cast<long long>(image->size());
    }
    if (!from_service && payload.path.empty())
    {
        result.outcome = Outcome::no_payload;
        result.detail = tr("No copy of Orbit was found to start.");
    }
    else
    {
        result.version = version;
        std::string error;
        result.outcome = from_service ? send_image(*image, port_, 10000, &error)
                                      : send(payload.path, port_, 10000, &error);
        result.detail = result.outcome == Outcome::sent
                            ? tr("Sent Orbit {version} to the ELF loader.",
                                 {{"version", version.empty() ? std::string(tr("(version unknown)"))
                                                              : version}})
                            : error;
    }
    if (log_)
    {
        char line[640];
        std::snprintf(line, sizeof(line), "start: %s copy=%s version=%s bytes=%lld [%s ]: %s",
                      outcome_name(result.outcome), source, version.empty() ? "?" : version.c_str(),
                      bytes, notes.c_str(), result.detail.c_str());
        log_(line);
    }
    return result;
}

} // namespace orbit::starter
