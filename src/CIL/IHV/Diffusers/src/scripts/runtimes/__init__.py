"""Concrete Runtime classes, registered via @register_runtime.

Imported eagerly so the @register_runtime decorators fire.
"""
from runtimes import nvidia  # noqa: F401
from runtimes import apple  # noqa: F401
from runtimes import amd  # noqa: F401
