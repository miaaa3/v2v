#include "map/TileLoader.h"
#include "utils/Log.h"

#include <curl/curl.h>
#include <stb_image.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <utility>

namespace v2v::map {

namespace {

// Reference-count global curl init across instances.
std::atomic<int> g_curlRefs{0};

void ensureCurlGlobalInit()
{
    if (g_curlRefs.fetch_add(1) == 0) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
}

void releaseCurlGlobal()
{
    if (g_curlRefs.fetch_sub(1) == 1) {
        curl_global_cleanup();
    }
}

std::size_t writeToVector(char* ptr, std::size_t size, std::size_t nmemb, void* userdata)
{
    auto& buf = *static_cast<std::vector<std::uint8_t>*>(userdata);
    const std::size_t bytes = size * nmemb;
    const auto begin = reinterpret_cast<const std::uint8_t*>(ptr);
    buf.insert(buf.end(), begin, begin + bytes);
    return bytes;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    if (from.empty()) return;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

} // namespace

TileLoader::TileLoader(int numWorkers, std::string urlTemplate, std::string userAgent)
    : m_urlTemplate(std::move(urlTemplate))
    , m_userAgent  (std::move(userAgent))
{
    ensureCurlGlobalInit();

    numWorkers = std::clamp(numWorkers, 1, 16);
    m_workers.reserve(static_cast<std::size_t>(numWorkers));
    for (int i = 0; i < numWorkers; ++i) {
        m_workers.emplace_back([this] { workerLoop(); });
    }
}

TileLoader::~TileLoader()
{
    m_stop.store(true);
    m_reqCv.notify_all();
    for (auto& t : m_workers) {
        if (t.joinable()) t.join();
    }
    releaseCurlGlobal();
}

void TileLoader::request(TileKey key)
{
    {
        std::lock_guard<std::mutex> lk(m_reqMutex);
        if (m_inFlight.find(key) != m_inFlight.end()) return;
        m_inFlight.insert(key);
        m_requests.push_back(key);
    }
    m_reqCv.notify_one();
}

bool TileLoader::tryPop(LoadedTile& out)
{
    std::lock_guard<std::mutex> lk(m_resMutex);
    if (m_results.empty()) return false;
    out = std::move(m_results.front());
    m_results.pop_front();
    return true;
}

std::size_t TileLoader::pendingCount() const
{
    std::lock_guard<std::mutex> lk(m_reqMutex);
    return m_requests.size() + m_inFlight.size();
}

std::size_t TileLoader::completedCount() const
{
    std::lock_guard<std::mutex> lk(m_resMutex);
    return m_results.size();
}

std::string TileLoader::formatUrl(TileKey key) const
{
    std::string url = m_urlTemplate;
    replaceAll(url, "{z}", std::to_string(key.z));
    replaceAll(url, "{x}", std::to_string(key.x));
    replaceAll(url, "{y}", std::to_string(key.y));
    return url;
}

void TileLoader::workerLoop()
{
    while (!m_stop.load()) {
        TileKey key{};
        {
            std::unique_lock<std::mutex> lk(m_reqMutex);
            m_reqCv.wait(lk, [this] { return m_stop.load() || !m_requests.empty(); });
            if (m_stop.load()) break;
            key = m_requests.front();
            m_requests.pop_front();
        }

        LoadedTile loaded;
        loaded.key = key;
        const bool ok = fetchAndDecode(key, loaded);
        loaded.success = ok;
        if (!ok) {
            V2V_LOG_WARN("Failed to load tile z=%d x=%d y=%d", key.z, key.x, key.y);
        }

        {
            std::lock_guard<std::mutex> lk(m_resMutex);
            m_results.push_back(std::move(loaded));
        }
        {
            std::lock_guard<std::mutex> lk(m_reqMutex);
            m_inFlight.erase(key);
        }
    }

    // fetchAndDecode creates / destroys its own CURL handle per request, which
    // keeps the worker shutdown path simple and stateless.
}

bool TileLoader::fetchAndDecode(TileKey key, LoadedTile& out) const
{
    const std::string url = formatUrl(key);

    std::vector<std::uint8_t> body;
    body.reserve(64 * 1024);

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      m_userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  &writeToVector);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR,    1L); // ≥400 → error
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // any supported encoding

    const CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || body.empty()) {
        return false;
    }

    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        body.data(), static_cast<int>(body.size()), &w, &h, &comp, 4);

    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    out.width  = w;
    out.height = h;
    out.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return true;
}

} // namespace v2v::map
