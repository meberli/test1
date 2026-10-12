"""Checks that a firmware image contains none of the local secret values.

  python tools/check_secrets.py build-release/loxone_panel.bin
Reads the values from the gitignored sdkconfig.defaults.local and
main/panel_config.h / include/panel_config.h and searches the image for them
(also the iCal URL's secret path part). Prints only the setting names, never
the values. Exit code 1 if any value is found.
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..")


def secrets():
    out = {}
    p = os.path.join(ROOT, "sdkconfig.defaults.local")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8"):
            m = re.match(r'(CONFIG_\w+)="(.*)"\s*$', line.strip())
            if m and m.group(2):
                out[m.group(1)] = m.group(2)
    for rel in ("main/panel_config.h", "include/panel_config.h"):
        p = os.path.join(ROOT, rel)
        if os.path.exists(p):
            for line in open(p, encoding="utf-8"):
                m = re.match(r'\s*#define\s+(WIFI_PASSWORD|WIFI_SSID|LOXONE_USER|LOXONE_PASSWORD|LOXONE_HOST)\s+"(.*)"', line)
                if m and m.group(2):
                    out[f"{rel}:{m.group(1)}"] = m.group(2)
    # Secret part of the Google iCal URL (".../ical/<id>/private-<token>/basic.ics")
    for k, v in list(out.items()):
        for part in re.findall(r"private-[0-9a-f]{8,}", v):
            out[k + " (token)"] = part
    return out


def main():
    data = open(sys.argv[1], "rb").read()
    found = 0
    s = secrets()
    if not s:
        sys.exit("no local secrets found to compare against")
    for name, value in s.items():
        b = value.encode("utf-8")
        if len(b) < 8:
            # Short values (user names) also occur inside unrelated strings
            # ("admin1"): only a whole C string counts.
            hit = b"\0" + b + b"\0" in data
        else:
            hit = b in data or value.encode("utf-16-le") in data
        print(f"{'FOUND ' if hit else 'absent'}  {name} ({len(value)} chars)")
        found += hit
    print(f"{len(data)} bytes, {found} secret value(s) found")
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
