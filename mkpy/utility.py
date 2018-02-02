import sys, subprocess, os, ast

import importlib.util, inspect, pathlib, filecmp
def get_functions():
    """
    Returns a list of functions defined in the __main__ module, it's a list of
    tuples like [(f_name, f), ...].

    NOTE: Functions imported like 'from <module> import <function>' are
    included too.
    """
    return inspect.getmembers(sys.modules['__main__'], inspect.isfunction).copy()

def get_user_functions():
    """
    Like get_functions() removing the ones defined in this file.
    """
    keys = globals().copy().keys()
    return [(m,v) for m,v in get_functions() if m not in keys]

def call_user_function(name):
    fun = None
    for f_name, f in get_user_functions():
        if f_name == name:
            fun = f
            break

    fun() if fun else print ('No function: '+name)
    return

def check_completions ():
    comp_path = pathlib.Path('/usr/share/bash-completion/completions/pymk.py')
    if not comp_path.exists():
        warn ('Tab completions not installed:')
        print ('Use "sudo ./pymk.py --install_completions" to install them\n')
        return
    if comp_path.is_file():
        if not filecmp.cmp ('mkpy/pymk.py', str(comp_path)):
            err ('Tab completions outdated:')
            print ('Update with "sudo ./pymk.py --install_completions"\n')
    else:
        err ('Something funky is going on.')

def default_opt (s):
    opt_lst = s.split(',')
    res = None
    for s in opt_lst: 
        if s.startswith('--'):
            res = s
    if res == None:
        res = opt_lst[0]
    return res

cli_completions = {}
def cli_completion_options ():
    """
    Handles command line options used by tab completion.
    The option --get_completions exits the script after printing completions.
    """
    if get_cli_option('--install_completions'):
        ex ("cp mkpy/pymk.py /usr/share/bash-completion/completions/")

    data_str = get_cli_option('--get_completions', unique_option=True, has_argument=True)
    if data_str != None:
        data_lst = data_str.split(' ')
        curs_pos = int(data_lst[0])
        line = data_lst[1:]
        if len(line) == 1: line.append('')

        match_found = False
        for opt,vals in cli_completions.items():
            if line[-2] in opt.split(',') and vals != None:
                match_found = True
                print (' '.join(vals))

        if not match_found:
            f_names = [s for s,f in get_user_functions()]
            print (' '.join(f_names))
            if line[-1] == '-':
                def_opts = [default_opt(s) for s in cli_completions.keys()]
                print (' '.join(def_opts))
            elif line[-1] != '':
                def_opts = []
                for s in cli_completions.keys(): def_opts += s.split(',')
                print (' '.join(def_opts))
        exit ()

def get_cli_option (opts, values=None, has_argument=False, unique_option=False):
    """
    Parses sys.argv looking for option _opt_.

    If _opt_ is not found, returns None.
    If _opt_ does not have arguments, returns True if the option is found.
    If _opt_ has an argument, returns the value of the argument. In this case
    additional error checking is available if _values_ is set to a list of the
    possible values the argument could take.

    When unique_option is True then _opt_ must be the only option used.

    """
    res = None
    i = 1
    if values != None: has_argument = True
    cli_completions[opts] = values

    while i<len(sys.argv):
        if sys.argv[i] in opts.split(','):
            if has_argument:
                if i+1 >= len(sys.argv):
                    print ('Missing argument for option '+opt+'.');
                    if values != None:
                        print ('Possible values are [ '+' | '.join(values)+' ].')
                    return
                else:
                    res = sys.argv[i+1]
                    break
            else:
                res = True
        i = i+1

    if unique_option and res != None:
        if (has_argument and len(sys.argv) != 3) or  (not has_argument and len(sys.argv) != 2):
            print ('Option '+opt+' receives no other option.')
            return

    if values != None and res != None:
        if res not in values:
            print ('Argument '+res+' is not valid for option '+opt+',')
            print ('Possible values are: [ '+' | '.join(values)+' ].')
            return
    return res

def get_cli_rest ():
    """
    Returns an array of all argv values that are after options starting with '-'.
    """
    i = 1;
    while i<len(sys.argv):
        if sys.argv[i].startswith ('-'):
            if len(sys.argv) > i+1 and not sys.argv[i+1].startswith('-'):
                i = i+1
        else:
            return sys.argv[i:]
        i = i+1
    return None

def err (string):
    print ('\033[1m\033[91m{}\033[0m'.format(string))

def ok (string):
    print ('\033[1m\033[92m{}\033[0m'.format(string))

def get_user_str_vars ():
    """
    Returns a dictionary with global strings in module __main__.
    """
    var_list = inspect.getmembers(sys.modules['__main__'])
    var_dict = {}
    for v_name, v in var_list:
        if type(v) == type(""):
            var_dict[v_name] = v
    return var_dict

def get_deps_pkgs (flags):
    """
    Prints dpkg packages from where -l* options in _flags_ are comming from.
    """
    # TODO: Is there a better way to find out this information?
    libs = [f for f in flags.split(" ") if f.startswith("-l")]
    strs = ex ('ld --verbose '+" ".join(libs)+' | grep succeeded | grep -Po "/\K(/.*.so)" | xargs dpkg-query --search', ret_stdout=True, echo=False)
    ex ('rm a.out', echo=False) # ld always creates a.out
    res = ex ('echo "' +str(strs)+ '" | grep -Po "^.*?(?=:)" | sort | uniq | xargs echo', ret_stdout=True, echo=False)
    print (res[:-1])

def ex (cmd, no_stdout=False, ret_stdout=False, echo=True):
    resolved_cmd = cmd.format(**get_user_str_vars())
    if echo: print (resolved_cmd)
    if not ret_stdout:
        redirect = open(os.devnull, 'wb') if no_stdout else None
        return subprocess.call(resolved_cmd, shell=True, stdout=redirect)
    else:
        return subprocess.check_output(resolved_cmd, shell=True, stderr=open(os.devnull, 'wb')).decode()

def pers (name, default=None, value=None):
    """
    Makes persistent some value across runs of the script storing it in a
    dctionary on "mkpy/cache".  Stores _name_:_value_ pair in cache unless
    value==None.  Returns the value of _name_ in the cache.

    If default is used, when _value_==None and _name_ is not in the cache the
    pair _name_:_default is stored.
    """

    cache_dict = {}
    if os.path.exists('mkpy/cache'):
        cache = open ('mkpy/cache', 'r')
        cache_dict = ast.literal_eval (cache.readline())
        cache.close ()

    if value == None:
        if name in cache_dict.keys ():
            return cache_dict[name]
        else:
            if default != None:
                cache_dict[name] = default
            else:
                print ('Key '+name+' is not in cache.')
                return
    else:
        cache_dict[name] = value

    cache = open ('mkpy/cache', 'w')
    cache.write (str(cache_dict)+'\n')
    return cache_dict.get (name)

def pymk_default ():
    if len(sys.argv) == 1:
        check_completions ()
        call_user_function ('default')
        return
    cli_completion_options()
    if get_cli_rest():
        f_names = [s for s,f in get_user_functions()]
        targets = set(get_cli_rest()).intersection(f_names)
        for t in targets:
            check_completions ()
            call_user_function (t)
            pers ('last_target', value=t)

