#!/usr/bin/python3
from mkpy.utility import *
assert sys.version_info >= (3,2)

DEP_FLAGS = '-lGL ' \
            '-lcairo ' \
            '-lX11-xcb ' \
            '-lX11 ' \
            '-lxcb ' \
            '-lxcb-sync ' \
            '-lxcb-randr ' \
            '-lm '

# NOTE: This is too much just to depend on Pango, maybe move to HarfBuzz?
PANGO_FLAGS = '-lgobject-2.0 ' \
              '-lpango-1.0 ' \
              '-lpangocairo-1.0 ' \
              '-I/usr/include/pango-1.0 ' \
              '-I/usr/include/cairo ' \
              '-I/usr/include/glib-2.0 ' \
              '-I/usr/lib/x86_64-linux-gnu/glib-2.0/include '

modes = {
        'debug': '-g -Wall',
        'release': '-O3 -DNDEBUG -Wall',
        'profile_release': '-O3 -pg -Wall'
        }
cli_mode = get_cli_option('-M,--mode', modes.keys())
FLAGS = modes[pers('mode', 'debug', cli_mode)]

def default():
    target = pers ('last_target', 'closet_maker')
    call_user_function(target)

def closet_maker ():
    os.makedirs ("bin", exist_ok=True)
    ex ('gcc {FLAGS} -o bin/closet_maker x11_platform.c {DEP_FLAGS} {PANGO_FLAGS}')
    return

if __name__ == "__main__":
    if get_cli_option ('--get_deps_pkgs'):
        get_deps_pkgs (DEP_FLAGS)
        exit()
    pymk_default()

