#!/usr/bin/env python3
# bake_live2d.py - render a Live2D Cubism model to PNG frame
# sequences (the input of frames_to_bdm.py) on the host.
#
# The device never runs Live2D: motions are baked here, once,
# into plain sprite frames, exactly like a film render. The
# renderer is live2d-py (MIT wrapper around the Cubism Native
# SDK 5-r.5; the Cubism Core is Live2D Inc.'s proprietary
# library, used host-side only, under Live2D's SDK terms) with
# Mesa software GL under Xvfb - no GPU needed.
#
# Usage (inside a venv with live2d-py, glfw, PyOpenGL, Pillow):
#   xvfb-run -a -s "-screen 0 1600x1400x24" python3 \
#       bake_live2d.py MODEL.model3.json -o FRAMES_DIR \
#       [--fps 30] [--height 1200] \
#       [--clips Stand:procedural:4.0,Idle1:Idle:0,Idle2:Idle:2,Tap:TapBody:0]
#
# Clip spec: Name:Kind:Arg
#   procedural - no motion file; auto breath + auto blink only
#                (Arg = seconds to record)
#   Group      - motion group name (Arg = motion index in it)
#
# Output: FRAMES_DIR/<ClipName>/f0000.png ... plus manifest.json
# (anchor from the settled idle alpha bbox: bottom-centre).
# Local validation only: rendered frames embed the model art;
# do not redistribute them.

import argparse
import json
import os
import sys

import glfw
from OpenGL.GL import GL_RGBA, GL_UNSIGNED_BYTE, glReadPixels
from PIL import Image

import live2d


def capture(w, h):
    data = glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE)
    return Image.frombytes('RGBA', (w, h), data).transpose(
        Image.FLIP_TOP_BOTTOM)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model')
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('--height', type=int, default=1200)
    ap.add_argument('--name')
    ap.add_argument('--clips', default='Stand:procedural:4.0,'
                                       'Idle1:Idle:0,Idle2:Idle:2,'
                                       'Idle3:Idle:6,Tap:TapBody:0')
    ap.add_argument('--aspect', help='canvas W:H (internal: '
                                     'measured by the probe pass)')
    ap.add_argument('--probe', action='store_true',
                    help='only print the model canvas aspect')
    args = ap.parse_args()

    if args.probe:
        if not glfw.init():
            return 1
        glfw.window_hint(glfw.VISIBLE, glfw.FALSE)
        win = glfw.create_window(64, 64, 'probe', None, None)
        glfw.make_context_current(win)
        live2d.init()
        live2d.glInit()
        m = live2d.Model()
        m.LoadModelJson(args.model)
        cw, ch = m.GetCanvasSize()
        print('ASPECT %r:%r' % (cw, ch))
        return 0

    specs = []
    for part in args.clips.split(','):
        name, kind, arg = part.split(':')
        specs.append((name, kind, arg))

    if not glfw.init():
        print('glfw init failed', file=sys.stderr)
        return 1
    # The window must be created at its final size: the
    # framework keys its render targets off the framebuffer it
    # first sees, and a later resize leaves the model drawn
    # into a corner. The canvas aspect is deterministic from
    # the model file, so a probe pass in a *separate process*
    # (own GL context) measures it first.
    if not args.aspect:
        import subprocess
        out = subprocess.run(
            [sys.executable, os.path.abspath(__file__),
             args.model, '-o', args.out, '--probe'],
            capture_output=True, text=True)
        for line in out.stdout.splitlines():
            if line.startswith('ASPECT '):
                args.aspect = line.split()[1]
                break
        if not args.aspect:
            print('aspect probe failed:\n%s\n%s' %
                  (out.stdout, out.stderr), file=sys.stderr)
            return 1
    cw, ch = (float(v) for v in args.aspect.split(':'))
    h = args.height
    w = int(round(h * cw / ch))
    glfw.window_hint(glfw.VISIBLE, glfw.FALSE)
    win = glfw.create_window(w, h, 'bake', None, None)
    if not win:
        print('window creation failed', file=sys.stderr)
        return 1
    glfw.make_context_current(win)
    live2d.init()
    live2d.glInit()
    m = live2d.Model()
    m.LoadModelJson(args.model)
    m.Resize(w, h)
    m.SetAutoBreath(True)
    m.SetAutoBlink(True)

    dt = 1.0 / args.fps
    os.makedirs(args.out, exist_ok=True)

    def settle(steps):
        for _ in range(steps):
            m.Update(dt)

    def record(clip_dir, max_frames, stop_when_finished=False):
        os.makedirs(clip_dir, exist_ok=True)
        n = 0
        while n < max_frames:
            m.Update(dt)
            live2d.clearBuffer(0, 0, 0, 0)
            m.Draw()
            capture(w, h).save(os.path.join(
                clip_dir, 'f%04d.png' % n))
            n += 1
            if stop_when_finished and n > 10 and \
                    m.IsMotionFinished():
                break
        return n

    # Settle physics (hair) and take the anchor from the alpha
    # bbox of a settled idle frame: bottom-centre = the feet.
    settle(90)
    live2d.clearBuffer(0, 0, 0, 0)
    m.Draw()
    bb = capture(w, h).getchannel('A').getbbox()
    anchor = [(bb[0] + bb[2]) // 2, bb[3] - 1]
    print('canvas %dx%d anchor %s bbox %s' % (w, h, anchor, bb))

    clips_meta = []
    for name, kind, arg in specs:
        if kind == 'procedural':
            frames = record(os.path.join(args.out, name),
                            int(float(arg) * args.fps))
            ctype, loop = 'Stay', True
        else:
            m.StopAllMotions()
            settle(int(args.fps))  # return to neutral, fade out
            m.StartMotion(kind, int(arg), 3)
            frames = record(os.path.join(args.out, name),
                            int(20 * args.fps), True)
            ctype, loop = 'Animate', False
        clips_meta.append({'name': name, 'type': ctype,
                           'loop': loop, 'frames': frames,
                           'dir': name})
        print('clip %-8s %-8s %d frames' % (name, ctype, frames))

    manifest = {
        'name': args.name or os.path.basename(
            os.path.dirname(os.path.abspath(args.model))),
        'fps': args.fps,
        'anchor': anchor,
        'clips': clips_meta,
        # The runtime's floor pool and specials, by clip name.
        # Tap is in the pool with a small weight (variety); the
        # runtime's tap reaction picks a random Animate from the
        # pool, Tap included.
        'pool': [['Stand', 50]] +
                [[c['name'], 100] for c in clips_meta
                 if c['type'] == 'Animate' and c['name'] != 'Tap'] +
                [['Tap', 5]],
        'specials': {'fall': 'Stand', 'drag': 'Stand',
                     'thrown': 'Stand', 'stand': 'Stand'},
    }
    with open(os.path.join(args.out, 'manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=1)
    print('manifest written to', args.out)
    live2d.dispose()
    glfw.terminate()
    return 0


if __name__ == '__main__':
    sys.exit(main())
