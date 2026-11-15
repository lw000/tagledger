#include "http_server/whitelist.h"

#include <cctype>
#include <fstream>
#include <sstream>

#include "common/logger.h"

namespace collector {

bool Whitelist::parse_ipv4(const std::string& s, unsigned int* out) {
    unsigned int v = 0;
    int octets = 0;
    size_t i = 0;
    while (i < s.size() && octets < 4) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        int octet = 0, digits = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            octet = octet * 10 + (s[i] - '0');
            if (octet > 255) return false;
            ++i;
            if (++digits > 3) return false;
        }
        v = (v << 8) | static_cast<unsigned int>(octet);
        ++octets;
        if (octets < 4) {
            if (i >= s.size() || s[i] != '.') return false;
            ++i;
        }
    }
    if (octets != 4 || i != s.size()) return false;
    if (out) *out = v;
    return true;
}

bool Whitelist::parse_rule(const std::string& line, WhitelistRule* out) {
    if (line.rfind("origin:", 0) == 0) {
        std::string v = line.substr(7);
        if (v.empty()) return false;
        out->kind = WhitelistRule::Kind::kOrigin;
        out->value = v;
        return true;
    }
    auto slash = line.find('/');
    if (slash != std::string::npos) {
        std::string ip = line.substr(0, slash);
        std::string prefix_s = line.substr(slash + 1);
        unsigned int net;
        int prefix;
        if (!parse_ipv4(ip, &net)) return false;
        if (prefix_s.empty() || prefix_s.size() > 2) return false;
        prefix = 0;
        for (char c : prefix_s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            prefix = prefix * 10 + (c - '0');
        }
        if (prefix > 32) return false;
        out->kind = WhitelistRule::Kind::kCidr;
        out->value = ip;
        out->prefix = prefix;
        return true;
    }
    // 单 IP：IPv4 或 IPv6（IPv6 仅做字符串归一化比较）
    unsigned int dummy;
    if (!parse_ipv4(line, &dummy)) {
        // 粗校验 IPv6：仅允许 hex 与 ':'，且至少含一个 ':'
        if (line.find(':') == std::string::npos) return false;
        for (char c : line)
            if (!std::isxdigit(static_cast<unsigned char>(c)) && c != ':' && c != '.')
                return false;
        out->kind = WhitelistRule::Kind::kIp;
        out->value = line;
        return true;
    }
    out->kind = WhitelistRule::Kind::kIp;
    out->value = line;
    return true;
}

Whitelist Whitelist::load(const std::string& conf_path) {
    Whitelist w;
    w.conf_path_ = conf_path;
    std::ifstream f(conf_path);
    if (!f) {
        w.allow_all_ = true;
        log_warning(
                 "whitelist config not found: " + conf_path +
                     " -> ALL SOURCES ALLOWED (open mode)");
        return w;
    }
    std::string line;
    while (std::getline(f, line)) {
        // 去注释与首尾空白
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto not_space = line.find_first_not_of(" \t\r\n");
        if (not_space == std::string::npos) continue;
        line = line.substr(not_space, line.find_last_not_of(" \t\r\n") - not_space + 1);
        WhitelistRule r;
        r.raw = line;
        if (!parse_rule(line, &r)) {
            log_warning("whitelist: skip invalid rule: " + line);
            continue;
        }
        w.rules_.push_back(r);
    }
    log_info(
             "whitelist loaded from " + conf_path + " (" +
                 std::to_string(w.rules_.size()) + " rules)");
    return w;
}

bool Whitelist::match_ip(const WhitelistRule& r, const std::string& ip) {
    if (ip.empty()) return false;
    if (r.kind == WhitelistRule::Kind::kCidr) {
        unsigned int net, client;
        if (!parse_ipv4(r.value, &net) || !parse_ipv4(ip, &client)) return false;
        unsigned int mask = r.prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - r.prefix));
        return (client & mask) == (net & mask);
    }
    // 精确 IP（IPv6 归一化为小写比较）
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(r.value) == lower(ip);
}

bool Whitelist::match_origin(const WhitelistRule& r, const std::string& origin) {
    if (origin.empty() || r.kind != WhitelistRule::Kind::kOrigin) return false;
    if (!r.value.empty() && r.value.back() == '*') {
        // 前缀通配（仅末尾端口位置）
        return origin.rfind(r.value.substr(0, r.value.size() - 1), 0) == 0;
    }
    return origin.rfind(r.value, 0) == 0;
}

bool Whitelist::allowed(const std::string& ip, const std::string& origin) const {
    if (allow_all_) return true;
    for (const auto& r : rules_) {
        if (r.kind == WhitelistRule::Kind::kOrigin) {
            if (match_origin(r, origin)) return true;
        } else {
            if (match_ip(r, ip)) return true;
        }
    }
    return false;
}

}  // namespace collector
