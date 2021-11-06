import os
from distutils.core import setup, Extension
from distutils.command.build_ext import build_ext
import pathlib
import subprocess
import shutil

root_dir = pathlib.Path(__file__).parent
src_dir = root_dir / 'src'
evermizer_dir = src_dir / 'evermizer'

sources = [src_dir / '_evermizer.c']
scripts = list(evermizer_dir.glob('patches/*.txt'))
ips = list(evermizer_dir.glob('ips/*.txt'))
data = [evermizer_dir / 'gourds.csv']
tools = [evermizer_dir / 'gourds2h.py', evermizer_dir / 'everscript2h.py', evermizer_dir / 'ips2h.py']
includes = list(root_dir.glob('*.h')) + list(evermizer_dir.glob('*.h')) + [evermizer_dir / 'main.c']
generated = [evermizer_dir / 'gourds.h', evermizer_dir / 'gen.h']
depends = [f for f in scripts + ips + data + includes + tools if f not in generated]

long_description = (root_dir / 'README.md').read_text(encoding='utf-8')
debug_c_args = {
    'unix': ['-Og', '-g'],
    'gcc': ['-Og', '-g'],
}
release_c_args = {
    'unix': ['-Os', '-s', '-ffunction-sections'],
    'gcc': ['-Os', '-s', '-ffunction-sections'],
    'msvc': ['/Os'],
    'mingw32': ['-Os', '-s'],
}
debug_l_args = {
}
release_l_args = {
    'unix': ['-s', '-Wl,--gc-sections'],
    'gcc': ['-s', '-Wl,--gc-sections']
}
c_args = release_c_args
l_args = release_l_args

evermizer_module = Extension('pyevermizer._evermizer',
    sources = list(map(str, sources)),
    depends = list(map(str, depends)),
    define_macros = [('NO_ASSERT',1), ('NDEBUG',1)])

class EvermizerPreBuild():
    """Custom build mixin to run pre-build steps for evermizer"""
    def prebuild(self):
        print('Running pre-build steps...')
        py = 'py' if shutil.which('py') else 'python'
        for script, args in (
            ['gourds2h.py', [evermizer_dir / 'gourds.h', evermizer_dir / 'gourds.csv']],
            ['everscript2h.py', [evermizer_dir / 'gen.h', *scripts]],
            ['ips2h.py', ['-a', evermizer_dir / 'gen.h', *ips]]
        ):
            try:
                res = subprocess.run([py, str(evermizer_dir / script), *map(str,args)],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
                print(res.stdout.decode('utf-8'), end='')
            except Exception as ex:
                print('\n' + ex.output.decode('utf-8'))
                raise(ex)

class EvermizerExtBuilder(EvermizerPreBuild, build_ext):
    def run(self):
        self.prebuild()
        return build_ext.run(self)
    def build_extensions(self):
        c = self.compiler.compiler_type
        if c not in c_args and c not in l_args:
            print('using unknown compiler: ' + c)
        if c in c_args:
            for e in self.extensions:
                e.extra_compile_args = c_args[c]
        if c in l_args:
            for e in self.extensions:
                e.extra_link_args = l_args[c]
        return build_ext.build_extensions(self)

setup (name = 'pyevermizer',
       author = 'black-sliver',
       version = '0.39',
       description = 'Python wrapper for Evermizer',
       long_description = long_description,
       long_description_content_type='text/markdown',
       license = 'LGPLv3',
       url = 'https://github.com/black-sliver/pyevermizer',
       python_requires='>=3', # TODO: test this
       packages = ['pyevermizer'],
       package_dir = {'pyevermizer': str(src_dir)},
       ext_modules = [evermizer_module],
       cmdclass={'build_ext': EvermizerExtBuilder})

