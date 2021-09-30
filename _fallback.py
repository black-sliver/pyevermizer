import pathlib
import sys as _sys
import cppyy as _cppy

_cppy.include(pathlib.Path(__file__).parent.resolve() / 'src' / '_evermizer.c')
_sys.modules['pyevermizer.src._evermizer'] = _cppy.gbl.PyInit__evermizer()
from .src import *

print('Running evermizer from c source')

# clean up globals
del(globals()['pathlib'])
