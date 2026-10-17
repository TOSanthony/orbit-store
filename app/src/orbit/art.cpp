// Orbit Store TV app - Game artwork: fetched through Orbit, decoded off the frame.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/art.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

namespace orbit
{

namespace
{

// The backend answers 202 while it fetches from the CDN; ask again shortly,
// for up to about half a minute. Each ask is cheap, so ask often.
constexpr double kPendingDelay = 0.3;
constexpr int kPendingAttempts = 100;
// More queued requests than this means the player scrolled past them.
constexpr std::size_t kMaxQueued = 48;

std::string make_key(const std::string &game_id, api::ArtKind kind)
{
    return game_id + (kind == api::ArtKind::hero ? "#hero" : "#cover");
}

bool is_hero(const std::string &key)
{
    return key.size() > 5 && key.compare(key.size() - 5, 5, "#hero") == 0;
}

} // namespace

ArtCache::ArtCache(http::Transport &transport, TextureSink &sink, ArtLimits limits)
    : transport_(transport), sink_(sink), limits_(limits)
{
}

ArtCache::~ArtCache()
{
    stop();
}

void ArtCache::start()
{
    thread_.start([this] { worker(); });
}

void ArtCache::stop()
{
    {
        Lock lock(mutex_);
        stopping_ = true;
    }
    signal_.notify();
    thread_.join();
}

void ArtCache::set_token(const std::string &token)
{
    has_token_ = !token.empty();
    Lock lock(mutex_);
    token_ = token;
}

void ArtCache::set_log(std::function<void(const std::string &)> log)
{
    log_ = std::move(log);
}

void ArtCache::note(const Decoded &d, double upload_ms, double total_ms)
{
    if (!log_)
        return;
    char line[240];
    std::snprintf(line, sizeof line,
                  "art %s %s: total=%.0fms wait=%.0f http=%.0f(x%d) decode=%.0f ambient=%.0f upload=%.1f %zuB%s",
                  d.hero ? "hero" : "cover", d.key.substr(0, d.key.find('#')).c_str(), total_ms,
                  d.wait_ms, d.http_ms, d.attempts + 1, d.decode_ms, d.ambient_ms, upload_ms,
                  d.bytes, d.ok ? "" : " FAILED");
    log_(line);
    Totals &t = totals_;
    ++t.count;
    t.total_ms += total_ms;
    t.max_ms = std::max(t.max_ms, total_ms);
    t.wait_ms += d.wait_ms;
    t.http_ms += d.http_ms;
    t.decode_ms += d.decode_ms + d.ambient_ms;
    t.upload_ms += upload_ms;
    if (t.count % 25 == 0)
    {
        std::snprintf(line, sizeof line,
                      "art summary (last 25): avg total=%.0fms max=%.0f | wait=%.0f http=%.0f "
                      "decode=%.0f upload=%.1f",
                      t.total_ms / t.count, t.max_ms, t.wait_ms / t.count, t.http_ms / t.count,
                      t.decode_ms / t.count, t.upload_ms / t.count);
        log_(line);
        t = Totals{};
    }
}

bool ArtCache::settled(const std::string &game_id, api::ArtKind kind) const
{
    const auto found = entries_.find(make_key(game_id, kind));
    return found != entries_.end() && found->second.state != State::loading;
}

const Artwork *ArtCache::get(const std::string &game_id, api::ArtKind kind)
{
    const std::string key = make_key(game_id, kind);
    const double now = now_seconds();
    // Artwork is served to the console's session only: wait until it is open.
    if (!has_token_)
        return nullptr;
    auto found = entries_.find(key);
    if (found != entries_.end())
    {
        Entry &entry = found->second;
        entry.last_used = now;
        if (entry.state == State::ready)
            return &entry.art;
        if (entry.state == State::loading || now < entry.retry_at)
            return nullptr;
    }
    Entry &entry = entries_[key];
    entry.state = State::loading;
    entry.last_used = now;
    entry.asked_at = now;
    {
        Lock lock(mutex_);
        Request request;
        request.asked_at = now;
        request.key = key;
        request.game_id = game_id;
        request.kind = kind;
        requests_.push_back(std::move(request));
        // Anything past the limit was asked for long ago and is likely gone
        // from the screen. It is marked failed briefly and asked again if it
        // is drawn again.
        while (requests_.size() > kMaxQueued)
        {
            const std::string dropped = requests_.front().key;
            requests_.pop_front();
            inbox_.post(
                [this, dropped]
                {
                    auto it = entries_.find(dropped);
                    if (it != entries_.end() && it->second.state == State::loading)
                        entries_.erase(it);
                });
        }
    }
    signal_.notify();
    return nullptr;
}

void ArtCache::tick()
{
    inbox_.drain();
    const double now = now_seconds();
    for (int n = 0; n < limits_.uploads_per_frame && !uploads_.empty(); ++n)
    {
        Decoded decoded = std::move(uploads_.front());
        uploads_.pop_front();
        auto found = entries_.find(decoded.key);
        if (found == entries_.end())
            continue; // dropped while it was decoding
        Entry &entry = found->second;
        if (!decoded.ok)
        {
            entry.state = State::failed;
            entry.retry_at = now + limits_.retry_failed;
            note(decoded, 0.0, (now_seconds() - decoded.asked_at) * 1000.0);
            continue;
        }
        const double upload_start = now_seconds();
        entry.art.texture = sink_.create(decoded.pixels);
        entry.art.width = decoded.pixels.width;
        entry.art.height = decoded.pixels.height;
        entry.art.ambient = decoded.ambient.empty() ? 0 : sink_.create(decoded.ambient);
        entry.art.ready_at = now;
        entry.state = entry.art.texture != 0 ? State::ready : State::failed;
        if (entry.state == State::failed)
            entry.retry_at = now + limits_.retry_failed;
        note(decoded, (now_seconds() - upload_start) * 1000.0, (now_seconds() - decoded.asked_at) * 1000.0);
    }
    evict(api::ArtKind::cover, limits_.max_covers);
    evict(api::ArtKind::hero, limits_.max_heroes);
}

void ArtCache::evict(api::ArtKind kind, int keep)
{
    std::vector<std::pair<double, std::string>> ready;
    for (const auto &[key, entry] : entries_)
    {
        if (entry.state == State::ready && is_hero(key) == (kind == api::ArtKind::hero))
            ready.emplace_back(entry.last_used, key);
    }
    if (static_cast<int>(ready.size()) <= keep)
        return;
    std::sort(ready.begin(), ready.end());
    const double now = now_seconds();
    const std::size_t extra = ready.size() - static_cast<std::size_t>(keep);
    for (std::size_t i = 0; i < extra; ++i)
    {
        // Never take away something drawn in the last frame or so.
        if (now - ready[i].first < 0.5)
            break;
        auto it = entries_.find(ready[i].second);
        sink_.destroy(it->second.art.texture);
        if (it->second.art.ambient != 0)
            sink_.destroy(it->second.art.ambient);
        entries_.erase(it);
    }
}

bool ArtCache::idle()
{
    inbox_.drain();
    Lock lock(mutex_);
    return requests_.empty() && in_flight_ == 0 && uploads_.empty();
}

void ArtCache::clear()
{
    for (auto &[key, entry] : entries_)
    {
        if (entry.state != State::ready)
            continue;
        sink_.destroy(entry.art.texture);
        if (entry.art.ambient != 0)
            sink_.destroy(entry.art.ambient);
    }
    entries_.clear();
    uploads_.clear();
}

bool ArtCache::work_once()
{
    Request request;
    std::string token;
    {
        Lock lock(mutex_);
        const double now = now_seconds();
        // Newest first: the player is looking at what was asked for last.
        auto pick = requests_.end();
        for (auto it = requests_.end(); it != requests_.begin();)
        {
            --it;
            if (it->not_before <= now)
            {
                pick = it;
                break;
            }
        }
        if (pick == requests_.end())
            return false;
        request = std::move(*pick);
        requests_.erase(pick);
        if (request.started_at == 0.0)
            request.started_at = now;
        token = token_;
        ++in_flight_;
    }

    api::Client client(transport_);
    client.set_token(token);
    const double http_start = now_seconds();
    api::Result<api::Art> art = client.art(request.game_id, request.kind);
    request.http_ms += (now_seconds() - http_start) * 1000.0;
    Decoded decoded;
    decoded.key = request.key;
    decoded.hero = request.kind == api::ArtKind::hero;
    decoded.asked_at = request.asked_at;
    decoded.wait_ms = (request.started_at - request.asked_at) * 1000.0;
    decoded.http_ms = request.http_ms;
    decoded.attempts = request.attempts;
    decoded.status = art.status;
    // Still being fetched by the backend, the session is not open yet (a
    // restarted backend), or the artwork workers are busy or still starting
    // (503, as when the app has just started Orbit): ask again shortly
    // rather than giving up.
    const bool again =
        (art.ok() && art.value.pending) || art.status == 401 || art.status == 503;
    if (again && request.attempts < kPendingAttempts)
    {
        Lock lock(mutex_);
        request.not_before = now_seconds() + kPendingDelay;
        ++request.attempts;
        requests_.push_front(std::move(request));
        --in_flight_;
        return true;
    }
    if (art.ok() && !art.value.pending)
    {
        const bool hero = request.kind == api::ArtKind::hero;
        const int width = hero ? limits_.hero_width : limits_.cover_size;
        const int height = hero ? limits_.hero_height : limits_.cover_size;
        decoded.bytes = art.value.bytes.size();
        const double decode_start = now_seconds();
        decoded.ok = image::decode(art.value.bytes, width, height, &decoded.pixels);
        decoded.decode_ms = (now_seconds() - decode_start) * 1000.0;
        if (decoded.ok && !hero)
        {
            const double ambient_start = now_seconds();
            decoded.ambient = image::ambient(decoded.pixels);
            decoded.ambient_ms = (now_seconds() - ambient_start) * 1000.0;
        }
    }
    inbox_.post([this, decoded = std::move(decoded)]() mutable
                { uploads_.push_back(std::move(decoded)); });
    {
        Lock lock(mutex_);
        --in_flight_;
    }
    return true;
}

void ArtCache::worker()
{
    for (;;)
    {
        {
            Lock lock(mutex_);
            if (stopping_)
                return;
        }
        if (work_once())
            continue;
        Lock lock(mutex_);
        if (stopping_)
            return;
        // Sleep until a request arrives or a pending one becomes due.
        double wait = 1.0;
        const double now = now_seconds();
        for (const Request &request : requests_)
            wait = std::min(wait, std::max(0.01, request.not_before - now));
        signal_.wait(mutex_, wait);
    }
}

} // namespace orbit
