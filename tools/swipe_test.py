"""Click-vs-swipe regression test for the panel (test firmware).

Swipes start on tiles/buttons; the firmware logs 'Touch ...' per touch and
'Activate ...' / 'Command ...' / 'Relay ...' when a control fires, so a
swipe that clicks anything is a failure.
  python tools/swipe_test.py --port COM14 --out shots
"""
import argparse
import os
import time

from panel_test import Panel, back, CARDS


def drain(p, secs):
    out = []
    end = time.time() + secs
    while time.time() < end:
        try:
            line = p._line(min(end, time.time() + 0.2))
        except TimeoutError:
            continue
        if line and not line.startswith("State "):
            out.append(line)
    return out


def act(p, text, settle=1.2):
    p.cmd(text)
    return drain(p, settle)


def fired(lines):
    return [l for l in lines if l.startswith(("Activate", "Command", "Relay"))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM14")
    ap.add_argument("--out", default="shots")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    p = Panel(a.port)
    shot = lambda name: p.shot(os.path.join(a.out, name + ".png"))
    results = []
    ok_all = True
    for name, cx, cy in CARDS:
        log = act(p, f"TAP {cx} {cy}", 2.0)
        print(f"[{name}] open:", log)
        for ms in (150, 400, 800):
            # Next page: start on the right-hand tile/button, drag left.
            l1 = act(p, f"SWIPE 353 166 93 166 {ms}")
            # Previous page: start on the left-hand tile/button, drag right.
            l2 = act(p, f"SWIPE 127 166 387 166 {ms}")
            for d, l in (("left", l1), ("right", l2)):
                touch = [x for x in l if x.startswith("Touch")]
                bad = fired(l)
                ok = not bad and touch and "gesture" in touch[0]
                ok_all &= bool(ok)
                results.append((name, d, ms, touch[0] if touch else "-", bad))
                print(f"  {d:5} {ms:3}ms {'OK ' if ok else 'BAD'} {touch[0] if touch else '-'} {bad}")
        if name in ("licht", "storen"):
            l = act(p, "SWIPE 353 166 93 166 400")
            shot(f"swipe_{name}_p2")
            shot(f"handle_{name}_p2")  # left handle should be visible
        if name == "storen":
            for _ in range(8):
                act(p, "SWIPE 353 320 93 320 300")
            shot("handle_storen_last")
            for _ in range(8):
                act(p, "SWIPE 127 320 387 320 300")
            shot("handle_storen_first")
        if name == "sonstiges":
            shot("swipe_sonstiges")
            # Tap the Relais tile: on, then off again.
            l_on = act(p, "TAP 127 166", 1.5)
            shot("swipe_relais_on")
            l_off = act(p, "TAP 127 166", 1.5)
            print("  relais tap on:", fired(l_on), " off:", fired(l_off))
            results.append(("relais", "tap", 0, str(fired(l_on)), fired(l_off)))
            # Slow drag that ends on the Relais tile again: must not toggle.
            l = act(p, "SWIPE 127 166 200 166 800")
            l += act(p, "SWIPE 127 166 140 170 600")
            print("  relais short drags:", [x for x in l if x.startswith(("Touch", "Activate", "Relay"))])
        lb = act(p, "SWIPE 476 240 260 240 300")
        print("  back:", [x for x in lb if x.startswith("Touch")])
        results.append((name, "back", 300, str([x for x in lb if x.startswith("Touch")]), fired(lb)))
    shot("swipe_home_after")
    print("ALL SWIPES CLEAN" if ok_all else "SOME SWIPES FIRED A CONTROL")


if __name__ == "__main__":
    main()
