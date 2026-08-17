"""Embedded-import bootstrap. C++ calls _install({name: source})."""

import importlib.abc
import importlib.util
import sys


class _MemoryLoader(importlib.abc.Loader):
    def __init__(self, name, source):
        self.name = name
        self.source = source
        self._code = None

    def create_module(self, spec):
        return None

    def exec_module(self, module):
        module.__file__ = f"<embedded:{self.name}>"
        module.__loader__ = self
        if self._code is None:
            self._code = compile(self.source, module.__file__, "exec")
        exec(self._code, module.__dict__)


class _MemoryFinder(importlib.abc.MetaPathFinder):
    def __init__(self, sources):
        self.sources = sources

    def find_spec(self, fullname, path=None, target=None):
        src = self.sources.get(fullname)
        if src is not None:
            return importlib.util.spec_from_loader(
                fullname, _MemoryLoader(fullname, src), is_package=False)
        init_src = self.sources.get(fullname + ".__init__")
        if init_src is not None:
            return importlib.util.spec_from_loader(
                fullname, _MemoryLoader(fullname, init_src), is_package=True)
        return None


def _install(sources):
    sys.meta_path.insert(0, _MemoryFinder(sources))
