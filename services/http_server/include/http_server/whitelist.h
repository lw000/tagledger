#pragma once
// 白名单访问控制：来源 IP（精确/CIDR）与 Origin（前缀通配）匹配。
#include <string>
#include <vector>

namespace collector {

struct WhitelistRule {
    enum class Kind { kIp, kCidr, kOrigin } kind = Kind::kIp;
    std::string raw;       // 原始规则文本（日志用）
    std::string value;     // IP / "a.b.c.d/prefix" / origin 前缀
    int prefix = 32;       // CIDR 前缀长度
};

class Whitelist {
public:
    // 加载配置文件。文件不存在 → allow_all=true（放行所有来源并告警）。
    static Whitelist load(const std::string& conf_path);

    bool allowed(const std::string& ip, const std::string& origin) const;
    bool allow_all() const { return allow_all_; }
    const std::vector<WhitelistRule>& rules() const { return rules_; }
    // 测试与扩展用：注入单条已解析规则
    void add_rule(const WhitelistRule& r) { rules_.push_back(r); }
    const std::string& conf_path() const { return conf_path_; }

    // 单元测试辅助：单条规则匹配
    static bool match_ip(const WhitelistRule& r, const std::string& ip);
    static bool match_origin(const WhitelistRule& r, const std::string& origin);
    static bool parse_rule(const std::string& line, WhitelistRule* out);

private:
    static bool parse_ipv4(const std::string& s, unsigned int* out);

    std::vector<WhitelistRule> rules_;
    bool allow_all_ = false;
    std::string conf_path_;
};

}  // namespace collector
