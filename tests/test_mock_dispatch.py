#!/usr/bin/env python3
"""Tests for the transport-free dispatch() in tools/mock_server.py (stdlib only).

dispatch() answers a request without a socket, so the hosted demo can run the same mock under
Pyodide. These tests drive it directly; tests/test_web_build.py covers the routes in depth.

Run: python3 tests/test_mock_dispatch.py
"""

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Import the mock first: it imports http.server (and stdlib datetime) before it puts
# components/klink on the path, where a datetime.py would otherwise shadow the stdlib module.
sys.path.insert(0, str(ROOT / "tools"))
import mock_server  # noqa: E402

failures = []


def check(name, ok, detail=""):
    if ok:
        print("ok   %s" % name)
    else:
        failures.append(name)
        print("FAIL %s%s" % (name, ": " + detail if detail else ""))


def request(method, target):
    return mock_server.dispatch(method, target)


def json_body(body):
    return json.loads(body)


def main():
    status, content_type, body = request("GET", "/klink")
    check("GET /klink status", status == 200, str(status))
    check("GET /klink is HTML", content_type.startswith("text/html"), content_type)
    check("GET /klink has a page", "<html" in body)

    status, content_type, body = request("GET", "/klink/state")
    check("GET /klink/state status", status == 200, str(status))
    check("GET /klink/state is JSON", content_type.startswith("application/json"), content_type)
    state = json_body(body)
    check("state has groups", isinstance(state.get("groups"), list))
    check("state has bus", isinstance(state.get("bus"), dict))

    status, content_type, body = request("POST", "/klink/mode?gid=101&mode=bogus")
    check("POST /klink/mode bad parameter status", status == 400, str(status))
    check("POST /klink/mode bad parameter error", "error" in json_body(body), body)

    status, content_type, body = request("POST", "/klink/state")
    check("POST on a GET-only route is 405", status == 405, str(status))
    check("405 body has error", "error" in json_body(body), body)

    status, content_type, body = request("GET", "/klink/mode")
    check("GET on a POST-only route is 405", status == 405, str(status))

    status, content_type, body = request("GET", "/klink/nope")
    check("unknown path is 404", status == 404, str(status))
    check("404 body has error", "error" in json_body(body), body)

    status, content_type, body = request("POST", "/klink/writes?on=0")
    check("POST /klink/writes?on=0 status", status == 200, str(status))
    state = json_body(request("GET", "/klink/state")[2])
    check("writes off after on=0", state["bus"]["writes"] is False)

    status, content_type, body = request("POST", "/klink/writes?on=1")
    check("POST /klink/writes?on=1 status", status == 200, str(status))
    state = json_body(request("GET", "/klink/state")[2])
    check("writes on after on=1", state["bus"]["writes"] is True)

    if failures:
        print("%d failure(s)" % len(failures))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
