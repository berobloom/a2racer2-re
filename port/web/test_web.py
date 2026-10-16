#!/usr/bin/env python3
"""Drive the web build in headless Chromium: serve port/web/out, give the
page the game folder through its folder picker, press Play, then follow a
script of waits, clicks and keys, screenshotting the canvas as it goes and
printing the game's console output.

Usage: test_web.py <game data dir> <out dir> [step ...]
  steps:  wait:<s>  until:<text> (a console line, up to 10 min)  shot:<name>
          click:<x>,<y> (640x480 menu coordinates)
          key:<Key> (Playwright key name)  hold:<Key>:<s>  eval:<js>
A2_VIEW=<w>x<h> sets the browser window (default 1320x1000).
Needs: pip install playwright (any cached Chromium; set A2_CHROME to pick one)
"""
import functools
import glob
import http.server
import os
import sys
import threading
import time

from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
data, out = sys.argv[1], sys.argv[2]
steps = sys.argv[3:]
os.makedirs(out, exist_ok=True)

handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                            directory=os.path.join(HERE, 'out'))
http.server.SimpleHTTPRequestHandler.log_message = lambda *a: None
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
url = f'http://127.0.0.1:{srv.server_address[1]}/a2racer2.html'

chrome = os.environ.get('A2_CHROME') or sorted(glob.glob(os.path.expanduser(
    '~/.cache/ms-playwright/chromium-*/chrome-linux64/chrome')))[-1]
logf = open(os.path.join(out, 'console.log'), 'w')

with sync_playwright() as p:
    b = p.chromium.launch(executable_path=chrome, args=[
        '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
        '--autoplay-policy=no-user-gesture-required', '--ignore-gpu-blocklist'])
    vw, vh = (int(v) for v in os.environ.get('A2_VIEW', '1320x1000').split('x'))
    page = b.new_page(viewport={'width': vw, 'height': vh})
    seen = []
    def note(t):
        seen.append(t)
        logf.write(f'{time.time() - t0:7.1f} {t}\n')
        logf.flush()
    t0 = time.time()
    page.on('console', lambda m: note(m.text))
    page.on('pageerror', lambda e: note('PAGEERROR ' + str(e)))
    page.goto(url)
    page.wait_for_selector('#pick:not(.hidden), #ready:not(.hidden)', timeout=60000)
    if page.is_visible('#pick'):
        page.set_input_files('#folder', data)
        page.wait_for_selector('#ready:not(.hidden)', timeout=600000)
    page.screenshot(path=os.path.join(out, 'page.png'))
    page.click('#play')
    canvas = page.locator('#canvas')
    for s in steps:
        kind, _, arg = s.partition(':')
        if kind == 'wait':
            time.sleep(float(arg))
        elif kind == 'until':
            mark, end = len(seen), time.time() + 600
            while time.time() < end and not any(arg in t for t in seen[mark:]):
                page.wait_for_timeout(250)
        elif kind == 'shot':
            note('shot ' + arg)
            canvas.screenshot(path=os.path.join(out, arg + '.png'), timeout=180000)
        elif kind == 'click':
            x, y = (float(v) for v in arg.split(','))
            box = canvas.bounding_box()
            # the menu's 4:3 picture, letterboxed in the canvas
            bw = min(box['width'], box['height'] * 4 / 3)
            bh = bw * 3 / 4
            bx = box['x'] + (box['width'] - bw) / 2
            by = box['y'] + (box['height'] - bh) / 2
            page.mouse.click(bx + x * bw / 640, by + y * bh / 480)
        elif kind == 'eval':
            page.evaluate(arg)
        elif kind == 'key':
            page.keyboard.press(arg)
        elif kind == 'hold':
            key, _, secs = arg.partition(':')
            page.keyboard.down(key)
            time.sleep(float(secs))
            page.keyboard.up(key)
    b.close()
srv.shutdown()
