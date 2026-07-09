from importlib import import_module
from typing import Any

_LAZY_EXPORTS = {
    "PDFProcessor": ("pdf_processing.process_pdf", "PDFProcessor"),
}

__all__ = list(_LAZY_EXPORTS.keys())


def __getattr__(name: str) -> Any:
    target = _LAZY_EXPORTS.get(name)
    if target is None:
        raise AttributeError(
            f"module 'pdf_processing' has no attribute {name!r}"
        )
    module_name, attr_name = target
    module = import_module(module_name)
    value = getattr(module, attr_name)
    globals()[name] = value
    return value


def __dir__() -> list:
    return sorted(list(globals().keys()) + list(_LAZY_EXPORTS.keys()))
