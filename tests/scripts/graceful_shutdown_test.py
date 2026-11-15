#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""安全退出集成测试（跨平台，仅标准库）。

验证要点（对应 docs/05 第 5.4 节）：
  1. POST /api/admin/shutdown（白名单内来源）触发优雅退出，进程在时限内退出且退出码 0；
  2. 退出后数据目录无 .tmp 残留（原子替换无半成品）；
  3. 重启后数据完整（退出前的写入可读）；
  4. POSIX 下另验证真实信号路径（SIGTERM）；Windows 控制台信号无法由外部可靠发送，
     以管理端点路径覆盖（signal_entry 逻辑在单元测试 [unit][shutdown] 中覆盖）。

用法：python tests/scripts/graceful_shutdown_test.py [--exe build/tagledger.exe]
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))  # 仓库根
FAILURES = []


def check(name, cond, detail=""):
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else "  " + detail))
    if not cond:
        FAILURES.append(name)


def invoke(method, url, body=None, timeout=10):
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(url, data=data,
                                 headers={"Content-Type": "application/json"},
                                 method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8")
            return resp.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, None
    except urllib.error.URLError:
        return 0, None


def start_server(exe, data_dir, cfg_dir, port):
    env = dict(os.environ)
    web_dir = os.path.join(ROOT, "web")
    proc = subprocess.Popen(
        [exe, "--port={}".format(port), "--data-dir=" + data_dir,
         "--web-dir=" + web_dir, "--config-dir=" + cfg_dir],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    return proc


def wait_health(port, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        st, _ = invoke("GET", "http://localhost:{}/api/health".format(port), timeout=1)
        if st == 200:
            return True
        time.sleep(0.1)
    return False


def write_whitelist_conf(cfg_dir):
    os.makedirs(cfg_dir, exist_ok=True)
    with open(os.path.join(cfg_dir, "whitelist.conf"), "w", encoding="utf-8") as f:
        f.write("127.0.0.1\n::1\n")


def wait_exit(proc, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        rc = proc.poll()
        if rc is not None:
            return rc
        time.sleep(0.05)
    return None


def scenario(exe, port, use_signal):
    label = "signal({})".format("SIGTERM" if use_signal else "admin-endpoint")
    tmp = tempfile.mkdtemp(prefix="tagledger_shutdown_")
    data_dir = os.path.join(tmp, "data")
    cfg_dir = os.path.join(tmp, "config")
    write_whitelist_conf(cfg_dir)

    try:
        proc = start_server(exe, data_dir, cfg_dir, port)
        check("[{}] server ready".format(label), wait_health(port))

        # 退出前写入数据（随后验证持久化完整性）
        st, _ = invoke("POST", "http://localhost:{}/api/v1/files".format(port),
                       {"name": "persist"})
        check("[{}] create file".format(label), st == 200)
        st, _ = invoke("POST", "http://localhost:{}/api/v1/points".format(port),
                       {"file": "persist", "point": {
                           "name": "p1", "io_address": "40001",
                           "address_type": "io", "point_type": "int",
                           "rw": "rw", "desc": "written-before-shutdown"}})
        check("[{}] write point".format(label), st == 200)

        # 触发退出
        if use_signal:
            proc.send_signal(signal.SIGTERM)
        else:
            st, b = invoke("POST", "http://localhost:{}/api/admin/shutdown".format(port))
            check("[{}] admin shutdown endpoint = 200".format(label), st == 200, str(b))

        rc = wait_exit(proc, timeout=10.0)
        check("[{}] process exits within 10s, code=0".format(label),
              rc == 0, "exit_code={}".format(rc))

        # 无 .tmp 残留
        leftovers = [f for f in os.listdir(data_dir) if f.endswith(".tmp")] \
            if os.path.isdir(data_dir) else ["<missing>"]
        check("[{}] no .tmp leftovers".format(label), not leftovers, str(leftovers))

        # 重启后数据完整
        proc2 = start_server(exe, data_dir, cfg_dir, port)
        check("[{}] restart ok".format(label), wait_health(port))
        st, b = invoke("GET",
                       "http://localhost:{}/api/v1/points?file=persist&index=0".format(port))
        check("[{}] data persisted across shutdown".format(label),
              st == 200 and b["data"]["point"]["name"] == "p1", str(b))
        proc2.terminate()
        try:
            proc2.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc2.kill()
    finally:
        if proc.poll() is None:
            proc.kill()
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe",
                    default=os.path.join(ROOT, "build", "tagledger.exe"))
    ap.add_argument("--port", type=int, default=18099)
    args = ap.parse_args()

    if not os.path.exists(args.exe):
        print("tagledger executable not found: " + args.exe)
        sys.exit(1)

    # 场景 A：管理端点触发（全平台）
    scenario(args.exe, args.port, use_signal=False)
    # 场景 B：真实信号触发（仅 POSIX 可从外部发送 SIGTERM）
    if os.name != "nt":
        scenario(args.exe, args.port + 1, use_signal=True)
    else:
        print("SKIP  真实信号场景（Windows 外部无法可靠发送控制台信号；"
              "signal_entry 逻辑已由 [unit][shutdown] 覆盖）")

    print()
    if FAILURES:
        print("{} FAILED".format(len(FAILURES)))
        sys.exit(1)
    print("ALL GRACEFUL SHUTDOWN TESTS PASSED")


if __name__ == "__main__":
    main()
