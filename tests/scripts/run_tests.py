#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""统一测试入口（跨平台，仅标准库）。

按 docs/05-测试与安全退出规范.md 的顺序执行：
  1. [unit]      单元测试（Catch2）
  2. [shutdown]  安全退出控制器单元测试（Catch2）
  3. [perf]      性能基准（Catch2）
  4. 集成：API 冒烟 / 并发写一致性 / 安全退出（Python，自动管理服务进程生命周期）

用法：
  python tests/scripts/run_tests.py                 # 全部
  python tests/scripts/run_tests.py --skip-perf    # 跳过性能基准
  python tests/scripts/run_tests.py --only unit    # 仅一组
"""
import argparse
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def find_exe(name):
    # 2026-11-15 更名 TagLedger：子目录目标（tagledger，定义于 apps/tagledger/）
    # 输出到 target 所在目录 build/apps/tagledger/；顶层目标（tagledger_tests）
    # 输出到 build/ 根。按新布局优先检索，build/ 根仅作兼容回退。
    rel_dirs = [os.path.join("build", "apps", "tagledger"), "build"]
    for rel in rel_dirs:
        cand = os.path.join(ROOT, rel, name)
        if os.name == "nt":
            cand += ".exe"
        if os.path.exists(cand):
            return cand
    return None


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def run_step(title, cmd, cwd=None, timeout=600):
    print("=" * 60)
    print("STEP  " + title)
    print("$ " + " ".join(cmd))
    print("-" * 60)
    try:
        rc = subprocess.call(cmd, cwd=cwd or ROOT, timeout=timeout)
    except subprocess.TimeoutExpired:
        print("TIMEOUT")
        return False
    print("--> exit code: {}".format(rc))
    return rc == 0


def start_server(exe, port):
    data_dir = os.path.join("build", "it_data")
    cfg_dir = os.path.join("build", "it_config")
    os.makedirs(data_dir, exist_ok=True)
    os.makedirs(cfg_dir, exist_ok=True)
    with open(os.path.join(cfg_dir, "whitelist.conf"), "w", encoding="utf-8") as f:
        f.write("127.0.0.1\n::1\n")
    proc = subprocess.Popen([exe, "--port={}".format(port),
                             "--data-dir=" + data_dir,
                             "--web-dir=" + os.path.join(ROOT, "web"),
                             "--config-dir=" + cfg_dir],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # 等待就绪
    import urllib.request
    deadline = time.time() + 10
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(
                    "http://localhost:{}/api/health".format(port), timeout=1) as r:
                if r.status == 200:
                    return proc, port
        except Exception:
            time.sleep(0.1)
    proc.kill()
    return None, port


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-perf", action="store_true")
    ap.add_argument("--only", choices=["unit", "shutdown", "perf", "integration"])
    args = ap.parse_args()

    tests_exe = find_exe("tagledger_tests")
    tagledger_exe = find_exe("tagledger")
    if not tests_exe or not tagledger_exe:
        print("ERROR: build first (cmake --build build): missing "
              "tagledger_tests / tagledger")
        sys.exit(1)

    def wanted(step):
        return args.only is None or args.only == step

    failed = []

    if wanted("unit") and not run_step("unit tests [unit]",
                                       [tests_exe, "[unit]"]):
        failed.append("unit")
    if wanted("shutdown") and not run_step("shutdown controller tests [shutdown]",
                                           [tests_exe, "[shutdown]"]):
        failed.append("shutdown")
    if wanted("perf") and not args.skip_perf and \
            not run_step("performance benchmarks [perf]", [tests_exe, "[perf]"]):
        failed.append("perf")

    if wanted("integration"):
        # graceful_shutdown_test 自管服务生命周期，不接收 --base-url
        scripts = [
            ("API smoke", os.path.join(HERE, "smoke_api.py"), True),
            ("concurrency", os.path.join(HERE, "concurrency_test.py"), True),
            ("graceful shutdown", os.path.join(HERE, "graceful_shutdown_test.py"), False),
        ]
        port = free_port()
        proc, port = start_server(tagledger_exe, port)
        if proc is None:
            print("ERROR: server failed to start on port {}".format(port))
            failed.append("integration")
        else:
            try:
                for title, script, needs_base in scripts:
                    cmd = [sys.executable, script]
                    if needs_base:
                        cmd += ["--base-url", "http://localhost:{}".format(port)]
                    if not run_step(title, cmd):
                        failed.append("integration/" + title)
            finally:
                proc.kill()
                proc.wait()

    print("=" * 60)
    if failed:
        print("FAILED STEPS: " + ", ".join(failed))
        sys.exit(1)
    print("ALL TEST STEPS PASSED")


if __name__ == "__main__":
    main()
