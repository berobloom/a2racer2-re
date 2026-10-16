#!/bin/sh
# Serve the web build at http://localhost:8080/a2racer2.html (or the port
# given).  Any static web server does; this one needs only Python.
cd "$(dirname "$0")/out" || { echo "build it first: port/web/build.sh" >&2; exit 1; }
PORT=${1:-8080}
echo "A2 Racer II: http://localhost:$PORT/a2racer2.html"
exec python3 -m http.server "$PORT" --bind 127.0.0.1
