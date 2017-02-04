# !/usr/bin/env python
import os
from os.path import abspath, dirname, exists
import subprocess
import sys

from setuptools import (
    Extension,
    find_packages,
    setup,
)
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    def __init__(self, name, cmake_lists_dir='.', sources=None, **kwargs):
        Extension.__init__(self, name, sources=sources or [], **kwargs)
        self.cmake_lists_dir = abspath(cmake_lists_dir)


class CMakeBuild(build_ext):

    def build_extensions(self):
        try:
            subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError('Cannot find CMake executable')

        for ext in self.extensions:
            extdir = abspath(dirname(self.get_ext_fullpath(ext.name)))
            cfg = os.environ.get('QB_BUILD_TYPE', 'Release')

            cmake_args = [
                '-DCMAKE_BUILD_TYPE=' + cfg,
                '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}'.format(
                    cfg.upper(), extdir,
                ),
                '-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_{}={}'.format(
                    cfg.upper(), self.build_temp,
                ),
                '-DPYTHON_EXECUTABLE={}'.format(sys.executable),
                '-DBUILD_TESTING=OFF',
            ]

            if not exists(self.build_temp):
                os.makedirs(self.build_temp)

            # Config and build the extension
            subprocess.check_call(
                ['cmake', ext.cmake_lists_dir, '-G', 'Ninja'] + cmake_args,
                cwd=self.build_temp,
            )
            subprocess.check_call(
                ['cmake', '--build', '.', '--config', cfg],
                cwd=self.build_temp,
            )


ext_modules = [
    CMakeExtension('quelling_blade.arena_allocatable'),
]

setup(
    name='quelling-blade',
    version='0.0.1',
    description='Library for creating objects which can be arena allocated.',
    author='Joe Jevnik',
    author_email='joejev@gmail.com',
    packages=find_packages(),
    cmdclass={'build_ext': CMakeBuild},
    ext_modules=ext_modules,
)
