#!/usr/bin/python3
from mkpy import utility as cfg
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
    ex ('gcc {FLAGS} -o bin/closet_maker x11_platform.c {DEP_FLAGS}')
    return

cfg.builtin_completions = ['--get_run_deps', '--get_build_deps']
if __name__ == "__main__":
    # Everything above this line will be executed for each TAB press.
    # If --get_completions is set, handle_tab_complete() calls exit().
    handle_tab_complete ()

    pymk_default()

