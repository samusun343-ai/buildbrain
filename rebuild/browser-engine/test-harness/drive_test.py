#!/usr/bin/env python3
"""Drive the framebuffer NetSurf binary on the libnsfb "brain" surface
against a fake fbdev (regular file) and a fake evdev (FIFO), then
inspect the rendered pixels.

Usage: drive_test.py <scenario> [options]
Scenarios:
  render    load page1.html, snapshot
  nav       load page1.html, tap the big link, snapshot; tap Back, snapshot
  images    load images.html with budget profile on, snapshot + log checks
  js        load js.html with JS on, snapshot
  jshog     load js-hog.html with JS heap cap, check process survives
  cache     serve the site over HTTP, load many pages, measure disc cache

Common env knobs: BRAIN_TEST_KEEP=1 keeps the temp dir.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.expanduser("~/workspace/buildbrain-work/rebuild/browser-engine")
NSFB = os.path.join(ROOT, "netsurf-all-3.11/netsurf/nsfb")
RES_SRC = os.path.join(ROOT, "netsurf-all-3.11/netsurf/frontends/framebuffer/res")
SITE = os.path.join(ROOT, "test-site")
W, H = 480, 272

EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
ABS_X, ABS_Y, BTN_TOUCH = 0, 1, 0x14A
SYN_REPORT = 0


def ev(fd, etype, code, value):
    fd.write(struct.pack("qqHHi", 0, 0, etype, code, value))
    fd.flush()


def tap(fd, x, y):
    if fd is None:
        print("tap skipped (no input)")
        return
    ev(fd, EV_ABS, ABS_X, x)
    ev(fd, EV_ABS, ABS_Y, y)
    ev(fd, EV_KEY, BTN_TOUCH, 1)
    ev(fd, EV_SYN, SYN_REPORT, 0)
    time.sleep(0.35)
    ev(fd, EV_KEY, BTN_TOUCH, 0)
    ev(fd, EV_SYN, SYN_REPORT, 0)


def read_fb(path, bpp=16):
    from PIL import Image
    data = open(path, "rb").read()
    img = Image.new("RGB", (W, H))
    px = img.load()
    if bpp == 16:
        import struct as st
        vals = st.unpack("<%dH" % (W * H), data[: W * H * 2])
        for i, v in enumerate(vals):
            r = ((v >> 11) & 0x1F) * 255 // 31
            g = ((v >> 5) & 0x3F) * 255 // 63
            b = (v & 0x1F) * 255 // 31
            px[i % W, i // W] = (r, g, b)
    return img


def color_near(img, target, tol=24):
    """count pixels close to target RGB"""
    tr, tg, tb = target
    cnt = 0
    for (r, g, b) in img.getdata():
        if abs(r - tr) <= tol and abs(g - tg) <= tol and abs(b - tb) <= tol:
            cnt += 1
    return cnt


def rss_kb(pid):
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        return -1
    return -1


class Session:
    def __init__(self, choices_extra=None, url=None):
        self.dir = tempfile.mkdtemp(prefix="brain-test-")
        self.fb = os.path.join(self.dir, "fb.bin")
        open(self.fb, "wb").close()  # fake device node must pre-exist
        self.fifo = os.path.join(self.dir, "input.fifo")
        os.mkfifo(self.fifo)
        resdir = os.path.join(self.dir, "res")
        shutil.copytree(RES_SRC, resdir)
        choices = {
            "fb_device": self.fb,
            "fb_input_devpath": self.fifo,
            "window_width": str(W),
            "window_height": str(H),
        }
        if choices_extra:
            choices.update(choices_extra)
        # the fb frontend searches $HOME/.netsurf/ first for Choices
        nsdir = os.path.join(self.dir, ".netsurf")
        os.makedirs(nsdir, exist_ok=True)
        with open(os.path.join(nsdir, "Choices"), "w") as f:
            for k, v in choices.items():
                f.write("%s:%s\n" % (k, v))
        self.log = open(os.path.join(self.dir, "nsfb.log"), "wb")
        env = dict(os.environ)
        env["NETSURF_RES"] = resdir
        env["NETSURFRES"] = resdir
        env["HOME"] = self.dir
        self.proc = subprocess.Popen(
            [NSFB, "-v", "-f", "brain", "-w", str(W), "-h", str(H), "-b", "16", url],
            stdout=self.log, stderr=subprocess.STDOUT, env=env)
        # keep the FIFO open for the whole session; O_NONBLOCK so a
        # dead browser cannot wedge the harness; retry while the
        # browser is still starting up (ENXIO = no reader yet)
        self.infd = None
        for _ in range(50):
            try:
                fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
                self.infd = os.fdopen(fd, "wb", buffering=0)
                break
            except OSError:
                if self.proc.poll() is not None:
                    break
                time.sleep(0.1)
        if self.infd is None:
            print("WARN: could not open input fifo")

    def snapshot(self, name):
        img = read_fb(self.fb)
        out = os.path.join(self.dir, name + ".png")
        img.save(out)
        return img, out

    def logtext(self):
        self.log.flush()
        with open(os.path.join(self.dir, "nsfb.log"), "rb") as f:
            return f.read().decode("utf-8", "replace")

    def close(self):
        try:
            self.infd.close()
        except Exception:
            pass
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.log.close()
        print("workdir:", self.dir)


def scenario_render():
    s = Session(url="file://" + os.path.join(SITE, "page1.html"))
    time.sleep(4)
    img, path = s.snapshot("page1")
    print("snapshot:", path)
    print("pixels bg #204060:", color_near(img, (0x20, 0x40, 0x60)))
    print("pixels h1 #c03028:", color_near(img, (0xC0, 0x30, 0x28)))
    print("pixels link #28a028:", color_near(img, (0x28, 0xA0, 0x28)))
    print("rss_kb:", rss_kb(s.proc.pid))
    s.close()


def scenario_nav():
    s = Session(url="file://" + os.path.join(SITE, "page1.html"))
    time.sleep(4)
    img1, _ = s.snapshot("nav-before")
    link = color_near(img1, (0x28, 0xA0, 0x28))
    print("link pixels before:", link)
    # find the link block centroid from the actual frame
    pts = [(x, y) for y in range(H) for x in range(W)
           if abs(img1.getpixel((x, y))[0] - 0x28) <= 24
           and abs(img1.getpixel((x, y))[1] - 0xA0) <= 24
           and abs(img1.getpixel((x, y))[2] - 0x28) <= 24]
    if not pts:
        print("FAIL: link block not found in frame")
        s.close()
        return
    cx = sum(p[0] for p in pts) // len(pts)
    cy = sum(p[1] for p in pts) // len(pts)
    print("tapping link at", cx, cy)
    tap(s.infd, cx, cy)
    time.sleep(3)
    img2, p2 = s.snapshot("nav-after")
    print("snapshot:", p2)
    print("page2 bg #f0e040 pixels:", color_near(img2, (0xF0, 0xE0, 0x40)))
    print("page1 link pixels now:", color_near(img2, (0x28, 0xA0, 0x28)))
    # Back button: fb toolbar, first button at the left of the toolbar.
    # Toolbar is the top strip of the window; back button centre is
    # around (15, 15) in the default fb layout.
    tap(s.infd, 15, 15)
    time.sleep(3)
    img3, p3 = s.snapshot("nav-back")
    print("snapshot:", p3)
    print("back on page1: link pixels:", color_near(img3, (0x28, 0xA0, 0x28)))
    print("page2 bg pixels now:", color_near(img3, (0xF0, 0xE0, 0x40)))
    s.close()


def scenario_images():
    s = Session(choices_extra={"brain_budget_mb": "56"},
                url="file://" + os.path.join(SITE, "images.html"))
    time.sleep(6)
    img, path = s.snapshot("images")
    print("snapshot:", path)
    print("teal small.png pixels:", color_near(img, (0, 160, 160)))
    print("magenta big.png pixels (want ~0):", color_near(img, (200, 40, 160)))
    log = s.logtext()
    for needle in ("Brain budget profile", "decoding at 1/",
                   "Refusing oversized bitmap"):
        for line in log.splitlines():
            if needle in line:
                print("LOG:", line.strip())
    print("rss_kb:", rss_kb(s.proc.pid))
    s.close()


def scenario_png():
    s = Session(choices_extra={"brain_budget_mb": "56"},
                url="file://" + os.path.join(SITE, "png.html"))
    time.sleep(5)
    img, path = s.snapshot("png")
    print("snapshot:", path)
    print("teal small.png pixels (want >1000):",
          color_near(img, (0, 160, 160)))
    print("magenta big.png pixels (want ~0):",
          color_near(img, (200, 40, 160)))
    log = s.logtext()
    for line in log.splitlines():
        if "Refusing oversized bitmap" in line:
            print("LOG:", line.strip())
    print("rss_kb:", rss_kb(s.proc.pid))
    s.close()


def scenario_js():
    s = Session(choices_extra={"enable_javascript": "1",
                               "brain_budget_mb": "56"},
                url="file://" + os.path.join(SITE, "js.html"))
    time.sleep(5)
    img, path = s.snapshot("js")
    print("snapshot:", path)
    s.close()


def scenario_jshog():
    s = Session(choices_extra={"enable_javascript": "1",
                               "brain_budget_mb": "56"},
                url="file://" + os.path.join(SITE, "js-hog.html"))
    for i in range(10):
        time.sleep(1)
        alive = s.proc.poll() is None
        print("t=%ds alive=%s rss_kb=%s" % (i + 1, alive, rss_kb(s.proc.pid)))
        if not alive:
            break
    img, path = s.snapshot("jshog")
    print("snapshot:", path)
    log = s.logtext()
    for line in log.splitlines():
        if "JS heap cap" in line or "jserrors" in line.lower():
            print("LOG:", line.strip())
    s.close()


def scenario_cache():
    # serve the site over HTTP with cache headers so the disc cache
    # is exercised.  many.html pulls 40 distinct JPEGs totalling
    # ~3.6MB; with disc_cache_size forced to 1MB the backing store
    # must evict down to about the cap instead of keeping
    # everything.  llcache only persists objects that are complete,
    # unused and have remaining lifetime, so after loading we
    # navigate away (releasing the images) and then wait.
    server = subprocess.Popen(
        [sys.executable, os.path.join(ROOT, "test-harness/cache_server.py")],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    try:
        # default store path (gui.c Brainux change): $HOME/.netsurf/cache
        s = Session(choices_extra={"brain_budget_mb": "56",
                                   "disc_cache_size": "1048576"},
                    url="http://127.0.0.1:8932/many.html")
        cachepath = os.path.join(s.dir, ".netsurf", "cache")
        time.sleep(8)
        img, _ = s.snapshot("cache-loaded")
        pts = [(x, y) for y in range(H) for x in range(W)
               if abs(img.getpixel((x, y))[0] - 0x28) <= 24
               and abs(img.getpixel((x, y))[1] - 0xA0) <= 24
               and abs(img.getpixel((x, y))[2] - 0x28) <= 24]
        if pts:
            cx = sum(p[0] for p in pts) // len(pts)
            cy = sum(p[1] for p in pts) // len(pts)
            print("navigating away via link at", cx, cy)
            tap(s.infd, cx, cy)
        else:
            print("WARN: leave-link not found")
        time.sleep(30)
        sizes = {}
        for root, dirs, files in os.walk(cachepath):
            for fn in files:
                p = os.path.join(root, fn)
                sizes[p] = os.path.getsize(p)
        total = sum(sizes.values())
        print("disc cache files:", len(sizes), "total bytes:", total)
        print("rss_kb:", rss_kb(s.proc.pid))
        s.close()
    finally:
        server.terminate()


if __name__ == "__main__":
    sc = sys.argv[1] if len(sys.argv) > 1 else "render"
    fn = {"render": scenario_render, "nav": scenario_nav,
          "images": scenario_images, "png": scenario_png,
          "js": scenario_js,
          "jshog": scenario_jshog, "cache": scenario_cache}[sc]
    fn()
