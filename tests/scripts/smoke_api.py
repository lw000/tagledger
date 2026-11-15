#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""API 冒烟测试（跨平台，仅标准库）。

用法：python tests/scripts/smoke_api.py [--base-url http://localhost:8080]
前置：tagledger.exe 已在运行（或使用 run_tests.py 自动拉起）。
"""
import argparse
import json
import random
import sys
import urllib.error
import urllib.request

PASSED = 0
FAILED = 0


def check(name, cond, detail=""):
    global PASSED, FAILED
    if cond:
        PASSED += 1
        print("PASS  " + name)
    else:
        FAILED += 1
        print("FAIL  {}  {}".format(name, detail))


def invoke(method, url, body=None):
    """返回 (status, body)。JSON 响应解析为 dict，其余（如 HTML）返回原始字符串。"""
    data = None
    headers = {"Content-Type": "application/json; charset=utf-8"}
    if body is not None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            raw = resp.read().decode("utf-8")
            try:
                return resp.status, json.loads(raw)
            except ValueError:
                return resp.status, raw
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, raw
    except urllib.error.URLError as e:
        return 0, {"code": -1, "message": str(e.reason)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base-url", default="http://localhost:8080")
    args = ap.parse_args()
    base = args.base_url.rstrip("/")
    file = "smoke_py_" + str(random.randint(10**5, 10**9))

    # ---- 健康检查 ----
    st, b = invoke("GET", base + "/api/health")
    check("health = 200", st == 200 and b and b["code"] == 0, str(b))

    # ---- 文件管理 ----
    st, b = invoke("POST", base + "/api/v1/files", {"name": file})
    check("files/create = 200",
          st == 200 and b["data"]["name"] == file + ".csv", str(b))
    st, b = invoke("POST", base + "/api/v1/files", {"name": file})
    check("files/create duplicate = 409/40901",
          st == 409 and b and b["code"] == 40901, str(b))
    st, b = invoke("POST", base + "/api/v1/files", {"name": "../evil"})
    check("files/create traversal = 400/40002",
          st == 400 and b and b["code"] == 40002, str(b))

    st, b = invoke("GET", base + "/api/v1/files")
    names = [f["name"] for f in b["data"]["files"]] if b else []
    check("files/list contains new file", st == 200 and (file + ".csv") in names, str(names))

    # ---- 单写 / 单读 ----
    pt = {"name": "temp1", "io_address": "40001", "address_type": "io",
          "point_type": "float", "rw": "rw", "desc": "1#炉温"}
    st, b = invoke("POST", base + "/api/v1/points", {"file": file, "point": pt})
    check("points/single-write append = 200 index=0",
          st == 200 and b["data"]["index"] == 0 and b["data"]["op"] == "append", str(b))

    st, b = invoke("GET", base + "/api/v1/points?file={}&index=0".format(file))
    check("points/single-read = 200", st == 200 and b["data"]["point"]["name"] == "temp1", str(b))
    st, b = invoke("GET", base + "/api/v1/points?file={}&index=99".format(file))
    check("points/single-read out-of-range = 404/40402",
          st == 404 and b and b["code"] == 40402, str(b))

    pt["desc"] = "已修改"
    st, b = invoke("POST", base + "/api/v1/points",
                   {"file": file, "index": 0, "point": pt})
    check("points/single-write update = 200",
          st == 200 and b["data"]["op"] == "update", str(b))
    st, b = invoke("GET", base + "/api/v1/points?file={}&index=0".format(file))
    check("points/read-back desc", b["data"]["point"]["desc"] == "已修改", str(b))

    st, b = invoke("POST", base + "/api/v1/points",
                   {"file": file, "point": {"name": "temp1", "io_address": "40002",
                                            "address_type": "io", "point_type": "int",
                                            "rw": "ro", "desc": ""}})
    check("points/single-write duplicate = 409/40901",
          st == 409 and b and b["code"] == 40901, str(b))

    st, b = invoke("POST", base + "/api/v1/points",
                   {"file": file, "point": {"name": "x", "io_address": "1",
                                            "address_type": "bad", "point_type": "int",
                                            "rw": "ro", "desc": ""}})
    check("points/single-write bad-enum = 400/40002",
          st == 400 and b and b["code"] == 40002, str(b))

    # ---- 批量写 ----
    batch = {"file": file, "items": [
        {"index": 0, "point": dict(pt, rw="ro", desc="批量改")},
        {"point": {"name": "press1", "io_address": "40002", "address_type": "io",
                   "point_type": "int", "rw": "rw", "desc": ""}},
        {"point": {"name": "const1", "io_address": "100", "address_type": "memory",
                   "point_type": "bool", "rw": "ro", "desc": ""}},
    ]}
    st, b = invoke("POST", base + "/api/v1/points/batch", batch)
    check("points/batch-write = 200 applied=3",
          st == 200 and b["data"]["applied"] == 3, str(b))

    bad_batch = {"file": file, "items": [
        {"point": {"name": "press2", "io_address": "40003", "address_type": "io",
                   "point_type": "int", "rw": "rw", "desc": ""}},
        {"point": {"name": "press1", "io_address": "40004", "address_type": "io",
                   "point_type": "int", "rw": "rw", "desc": ""}},
    ]}
    st, b = invoke("POST", base + "/api/v1/points/batch", bad_batch)
    check("points/batch-write atomic-reject = 409/40901",
          st == 409 and b and b["code"] == 40901, str(b))

    # ---- 批量读 ----
    st, b = invoke("GET", base + "/api/v1/points?file={}&start=0&count=2".format(file))
    check("points/batch-read page = 200 total=3",
          st == 200 and len(b["data"]["points"]) == 2 and b["data"]["total"] == 3, str(b))
    st, b = invoke("GET", base + "/api/v1/points?file={}".format(file))
    check("points/batch-read all = 200",
          st == 200 and len(b["data"]["points"]) == 3, str(b))
    st, b = invoke("GET", base + "/api/v1/points?file=no_such_file_xyz")
    check("points/batch-read missing-file = 404/40401",
          st == 404 and b and b["code"] == 40401, str(b))

    # ---- 空请求体 ----
    st, b = invoke("POST", base + "/api/v1/files")
    check("files/create empty-body = 400/40001",
          st == 400 and b and b["code"] == 40001, str(b))

    # ---- 静态页面 ----
    st, b = invoke("GET", base + "/")
    check("static index page", st == 200 and isinstance(b, str)
          and "fileList" in b, st)
    check("static index page is fresh build (contains reloadPoints)",
          isinstance(b, str) and "reloadPoints" in b, st)

    # ---- 响应头：Cache-Control: no-store（API 与静态页均禁缓存，防陈旧数据） ----
    with urllib.request.urlopen(base + "/api/health", timeout=10) as resp:
        cc_api = resp.headers.get("Cache-Control", "")
    check("api response Cache-Control = no-store",
          "no-store" in cc_api.lower(), cc_api)
    with urllib.request.urlopen(base + "/", timeout=10) as resp:
        cc_page = resp.headers.get("Cache-Control", "")
    check("static page Cache-Control = no-store",
          "no-store" in cc_page.lower(), cc_page)

    print()
    if FAILED:
        print("{} FAILED".format(FAILED))
        sys.exit(1)
    print("ALL SMOKE TESTS PASSED ({} checks)".format(PASSED))


if __name__ == "__main__":
    main()
