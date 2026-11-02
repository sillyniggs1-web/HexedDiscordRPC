// HexedDiscordRPC - Discord Rich Presence for SA-MP (Nezuko) via the Discord Social SDK.
// HEXED Studio. The Lua side supplies server/nick data; this library only talks to Discord.
#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#define HEXED_API extern "C" __attribute__((visibility("default")))

namespace {

enum Status { ST_ERROR = -1, ST_STOPPED = 0, ST_IDLE = 1, ST_AUTH = 2, ST_CONNECTING = 3, ST_READY = 4 };

struct Shared {
    std::mutex m;
    std::string tokenPath;
    std::string hostname, nick, ip, largeKey, largeText;
    int port = 0;
    bool inServer = false;
    bool presenceDirty = false;
    bool loginRequested = false;
    uint64_t startMs = 0;
} S;

std::atomic<int> g_status{ST_STOPPED};
std::atomic<bool> g_run{false};
std::thread g_thread;
uint64_t g_appId = 0;

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "HexedDiscordRPC", "%s", buf);
#else
    fprintf(stderr, "[HexedDiscordRPC] %s\n", buf);
#endif
}

// Refresh token is stored as plain text in the data dir. Keep that dir private to the app if you can.
std::string LoadRefreshToken() {
    std::string path;
    { std::lock_guard<std::mutex> l(S.m); path = S.tokenPath; }
    std::ifstream f(path);
    std::string t;
    if (f) std::getline(f, t);
    return t;
}

void SaveRefreshToken(const std::string& t) {
    std::string path;
    { std::lock_guard<std::mutex> l(S.m); path = S.tokenPath; }
    std::ofstream f(path, std::ios::trunc);
    if (f) f << t;
}

// Cut to maxBytes without splitting a UTF-8 sequence (Discord limits these fields to 128 bytes).
std::string Trunc(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n);
}

void MarkPresenceDirty() {
    std::lock_guard<std::mutex> l(S.m);
    S.presenceDirty = true;
}

void ConnectWithToken(discordpp::Client* c, discordpp::AuthorizationTokenType type, const std::string& access) {
    c->UpdateToken(type, access, [c](discordpp::ClientResult r) {
        if (r.Successful()) {
            g_status = ST_CONNECTING;
            c->Connect();
        } else {
            Log("UpdateToken failed");
            g_status = ST_IDLE;
        }
    });
}

bool RefreshStored(discordpp::Client* c) {
    std::string rt = LoadRefreshToken();
    if (rt.empty()) return false;
    g_status = ST_CONNECTING;
    c->RefreshToken(g_appId, rt,
        [c](discordpp::ClientResult r, std::string access, std::string refresh,
            discordpp::AuthorizationTokenType type, int32_t, std::string) {
            if (!r.Successful()) {
                Log("Token refresh failed - login required (/hxdrpc)");
                g_status = ST_IDLE;
                return;
            }
            SaveRefreshToken(refresh);
            ConnectWithToken(c, type, access);
        });
    return true;
}

void DoLogin(discordpp::Client* c) {
    g_status = ST_AUTH;
    auto verifier = c->CreateAuthorizationCodeVerifier();
    discordpp::AuthorizationArgs args{};
    args.SetClientId(g_appId);
    args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
    args.SetCodeChallenge(verifier.Challenge());
    c->Authorize(args, [c, verifier](discordpp::ClientResult result, std::string code, std::string redirectUri) {
        if (!result.Successful()) {
            Log("Authorize failed: %s", result.Error().c_str());
            g_status = ST_IDLE;
            return;
        }
        c->GetToken(g_appId, code, verifier.Verifier(), redirectUri,
            [c](discordpp::ClientResult r, std::string access, std::string refresh,
                discordpp::AuthorizationTokenType type, int32_t, std::string) {
                if (!r.Successful()) {
                    Log("GetToken failed: %s", r.Error().c_str());
                    g_status = ST_IDLE;
                    return;
                }
                SaveRefreshToken(refresh);
                ConnectWithToken(c, type, access);
            });
    });
}

void ApplyPresence(discordpp::Client* c) {
    std::string host, nick, ip, lk, lt;
    int port;
    bool in;
    uint64_t start;
    {
        std::lock_guard<std::mutex> l(S.m);
        host = S.hostname; nick = S.nick; ip = S.ip; lk = S.largeKey; lt = S.largeText;
        port = S.port; in = S.inServer; start = S.startMs;
        S.presenceDirty = false;
    }
    if (!in) {
        c->ClearRichPresence();
        return;
    }
    discordpp::Activity a;
    a.SetType(discordpp::ActivityTypes::Playing);
    a.SetDetails(Trunc(host, 128));                                              // line 2: server name
    a.SetState(Trunc(nick + " \xE2\x80\xA2 " + ip + ":" + std::to_string(port), 128)); // line 3: nick • ip:port

    discordpp::ActivityTimestamps ts;
    ts.SetStart(start);                                                          // elapsed timer
    a.SetTimestamps(ts);

    if (!lk.empty()) {
        discordpp::ActivityAssets as;
        as.SetLargeImage(lk);
        if (!lt.empty()) as.SetLargeText(lt);
        a.SetAssets(as);
    }
    c->UpdateRichPresence(a, [](discordpp::ClientResult r) {
        if (!r.Successful()) Log("UpdateRichPresence failed");
    });
}

void Worker() {
    auto client = std::make_shared<discordpp::Client>();
    discordpp::Client* c = client.get();

    c->AddLogCallback([](std::string msg, discordpp::LoggingSeverity) { Log("sdk: %s", msg.c_str()); },
                      discordpp::LoggingSeverity::Warning);

    c->SetStatusChangedCallback([](discordpp::Client::Status status, discordpp::Client::Error error, int32_t detail) {
        if (status == discordpp::Client::Status::Ready) {
            g_status = ST_READY;
            MarkPresenceDirty();
        } else if (error != discordpp::Client::Error::None) {
            Log("Connection error %d (detail %d)", static_cast<int>(error), detail);
            g_status = ST_ERROR;
        } else if (status == discordpp::Client::Status::Connecting ||
                   status == discordpp::Client::Status::Connected) {
            g_status = ST_CONNECTING;
        }
    });

    c->SetTokenExpirationCallback([c]() { RefreshStored(c); });

    g_status = ST_IDLE;
    RefreshStored(c);  // silent reconnect if a token was saved earlier

    while (g_run.load()) {
        discordpp::RunCallbacks();

        bool login = false, dirty = false;
        {
            std::lock_guard<std::mutex> l(S.m);
            login = S.loginRequested;
            S.loginRequested = false;
            dirty = S.presenceDirty;
        }
        if (login) DoLogin(c);
        if (dirty && g_status.load() == ST_READY) ApplyPresence(c);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (g_status.load() == ST_READY) c->ClearRichPresence();
    discordpp::RunCallbacks();
    g_status = ST_STOPPED;
}

}  // namespace

// ---- C API (called from Lua through FFI) ----------------------------------------------------
// appId is passed as a string because Discord IDs do not fit in a Lua double.

HEXED_API int HexedRPC_Start(const char* appId, const char* dataDir) {
    if (g_run.load()) return 1;
    if (!appId || !dataDir) return 0;
    g_appId = strtoull(appId, nullptr, 10);
    if (g_appId == 0) return 0;
    {
        std::lock_guard<std::mutex> l(S.m);
        S.tokenPath = std::string(dataDir) + "/hexed_discord_token.txt";
    }
    if (g_thread.joinable()) g_thread.join();
    g_run = true;
    g_thread = std::thread(Worker);
    return 1;
}

HEXED_API void HexedRPC_Stop() {
    g_run = false;
    if (g_thread.joinable()) g_thread.join();
}

HEXED_API void HexedRPC_Login() {
    std::lock_guard<std::mutex> l(S.m);
    S.loginRequested = true;
}

// 0 stopped, 1 needs login, 2 waiting for Discord auth, 3 connecting, 4 ready, -1 error
HEXED_API int HexedRPC_Status() { return g_status.load(); }

HEXED_API void HexedRPC_SetAssets(const char* largeKey, const char* largeText) {
    std::lock_guard<std::mutex> l(S.m);
    S.largeKey = largeKey ? largeKey : "";
    S.largeText = largeText ? largeText : "";
    S.presenceDirty = true;
}

HEXED_API void HexedRPC_SetServer(const char* hostname, const char* nick, const char* ip, int port) {
    std::lock_guard<std::mutex> l(S.m);
    std::string newHost = hostname ? hostname : "", newNick = nick ? nick : "", newIp = ip ? ip : "";
    bool changedServer = !S.inServer || newIp != S.ip || port != S.port;
    if (!changedServer && newHost == S.hostname && newNick == S.nick) return;  // nothing new
    if (changedServer) S.startMs = static_cast<uint64_t>(time(nullptr)) * 1000ULL;  // restart timer
    S.hostname = newHost; S.nick = newNick; S.ip = newIp; S.port = port;
    S.inServer = true;
    S.presenceDirty = true;
}

HEXED_API void HexedRPC_ClearServer() {
    std::lock_guard<std::mutex> l(S.m);
    if (!S.inServer) return;
    S.inServer = false;
    S.presenceDirty = true;
}
