Import("env")

import os

include_dir = os.path.join(env.subst("$PROJECT_DIR"), "include")
if not os.path.isdir(include_dir):
    raise SystemExit("LVGL config directory is missing: %s" % include_dir)

# SCons passes this path to both the project and the LVGL library build.
env.Prepend(CPPPATH=[include_dir])
