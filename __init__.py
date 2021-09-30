import os
import pathlib
import distutils.core
import traceback

FORCE_FALLBACK = False #True

# you can directly import the repository as a module, which
# 1. tries to build native code in place, 2. tries to run it through cppyy
_cwd = os.getcwd()
try:
    if FORCE_FALLBACK: raise Exception('building disabled')
    _dir = pathlib.Path(__file__).parent.resolve()
    os.chdir(_dir)
    distutils.core.run_setup('setup.py').run_command('build_ext')
    from .src import *
except:
    traceback.print_exc()
    from ._fallback import *
finally:
    os.chdir(_cwd)

# clean up globals
del(globals()['traceback'])
del(globals()['distutils'])
del(globals()['pathlib'])
del(globals()['os'])
