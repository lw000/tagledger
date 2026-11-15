#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""并发写一致性测试（跨平台，仅标准库）。

20 个线程并发向同一 CSV 追加点位，期望最终 20 行且无丢失、无重复。
用法：python tests/scripts/concurrency_test.py [--base-url http://localhost:8080]
"""
import argparse
import json
import random
import sys
import threading
import urllib.error
import urllib.request

ap = argparse.ArgumentParser()
ap.add_argument("--base-url", default="http://localhost:8080")
ap.add_argument("--concurrency", type=int, default=20)
args = ap.parse_args()
base = args.base_url.rstrip("/")
file = "cc_py_" + str(random.randint(10**5, 10**9))


def invoke(method, url, body=None):
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(url, data=data,
                                 headers={"Content-Type": "application/json"},
                                 method=method)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            raw = resp.read().decode("utf-8")
            return resp.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, None


st, _ = invoke("POST", base + "/api/v1/files", {"name": file})
if st != 200:
    print("create file failed: http {}".format(st))
    sys.exit(1)

results = []
lock = threading.Lock()


def worker(i):
    body = {"file": file, "point": {
        "name": "cc{}".format(i), "io_address": "40001",
        "address_type": "io", "point_type": "int", "rw": "rw", "desc": "c"}}
    st, _ = invoke("POST", base + "/api/v1/points", body)
    with lock:
        results.append(st)


threads = [threading.Thread(target=worker, args=(i,)) for i in range(args.concurrency)]
for t in threads:
    t.start()
for t in threads:
    t.join()

st, b = invoke("GET", base + "/api/v1/points?file=" + file)
data = b["data"]
names = [row["point"]["name"] for row in data["points"]]
total = data["total"]
unique = len(set(names))
ok_http = all(s == 200 for s in results)

print("http_ok={} total={} names={} unique={}".format(
    ok_http, total, len(names), unique))

if ok_http and total == args.concurrency and len(names) == unique:
    print("CONCURRENCY TEST PASSED")
else:
    print("CONCURRENCY TEST FAILED")
    sys.exit(1)
