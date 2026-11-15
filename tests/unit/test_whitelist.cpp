// 白名单单元测试。命名规则："<模块>/<被测行为>"，分组标签 [unit][whitelist]。
#include "http_server/whitelist.h"

#include <catch2/catch_test_macros.hpp>

using collector::Whitelist;
using WR = collector::WhitelistRule;

TEST_CASE("whitelist/ipv4 exact match", "[unit][whitelist]") {
    WR r1; r1.kind = WR::Kind::kIp; r1.value = "192.168.1.10";
    REQUIRE(Whitelist::match_ip(r1, "192.168.1.10"));
    REQUIRE_FALSE(Whitelist::match_ip(r1, "192.168.1.11"));
    REQUIRE_FALSE(Whitelist::match_ip(r1, ""));
}

TEST_CASE("whitelist/cidr match", "[unit][whitelist]") {
    WR r2; r2.kind = WR::Kind::kCidr; r2.value = "10.0.0.0"; r2.prefix = 8;
    REQUIRE(Whitelist::match_ip(r2, "10.255.255.254"));
    REQUIRE_FALSE(Whitelist::match_ip(r2, "11.0.0.1"));

    WR r2b; r2b.kind = WR::Kind::kCidr; r2b.value = "192.168.0.0"; r2b.prefix = 16;
    REQUIRE(Whitelist::match_ip(r2b, "192.168.3.4"));
    REQUIRE_FALSE(Whitelist::match_ip(r2b, "192.169.0.1"));

    WR r2c; r2c.kind = WR::Kind::kCidr; r2c.value = "0.0.0.0"; r2c.prefix = 0;
    REQUIRE(Whitelist::match_ip(r2c, "8.8.8.8"));  // /0 匹配全部 IPv4
}

TEST_CASE("whitelist/ipv6 normalized exact match", "[unit][whitelist]") {
    WR r3; r3.kind = WR::Kind::kIp; r3.value = "::1";
    REQUIRE(Whitelist::match_ip(r3, "::1"));
    REQUIRE_FALSE(Whitelist::match_ip(r3, "::2"));
}

TEST_CASE("whitelist/origin prefix wildcard", "[unit][whitelist]") {
    WR r4; r4.kind = WR::Kind::kOrigin; r4.value = "http://localhost:*";
    REQUIRE(Whitelist::match_origin(r4, "http://localhost:8080"));
    REQUIRE(Whitelist::match_origin(r4, "http://localhost:3000"));
    REQUIRE_FALSE(Whitelist::match_origin(r4, "http://evil.com:8080"));
    REQUIRE_FALSE(Whitelist::match_origin(r4, ""));  // 无 Origin 头不适用该维度
}

TEST_CASE("whitelist/rule parsing", "[unit][whitelist]") {
    WR p;
    REQUIRE(Whitelist::parse_rule("192.168.1.10", &p));
    REQUIRE(p.kind == WR::Kind::kIp);

    REQUIRE(Whitelist::parse_rule("10.0.0.0/8", &p));
    REQUIRE(p.kind == WR::Kind::kCidr);
    REQUIRE(p.prefix == 8);

    REQUIRE(Whitelist::parse_rule("origin:http://localhost:*", &p));
    REQUIRE(p.kind == WR::Kind::kOrigin);

    // 非法规则必须被拒绝（含目录穿越形态）
    REQUIRE_FALSE(Whitelist::parse_rule("999.1.1.1", &p));
    REQUIRE_FALSE(Whitelist::parse_rule("../etc/passwd", &p));
    REQUIRE_FALSE(Whitelist::parse_rule("10.0.0.0/33", &p));
    REQUIRE_FALSE(Whitelist::parse_rule("origin:", &p));
}

TEST_CASE("whitelist/allowed combines ip and origin dimensions", "[unit][whitelist]") {
    Whitelist w;
    WR ip; ip.kind = WR::Kind::kIp; ip.value = "192.168.1.10";
    WR og; og.kind = WR::Kind::kOrigin; og.value = "http://localhost:*";
    // 直接注入规则（绕过文件加载，避免对临时文件的依赖）
    w.add_rule(ip);
    w.add_rule(og);

    REQUIRE(w.allowed("192.168.1.10", ""));                  // IP 命中
    REQUIRE(w.allowed("10.1.2.3", "http://localhost:8080")); // Origin 命中
    REQUIRE_FALSE(w.allowed("10.1.2.3", ""));                // 双维度均未命中
}
