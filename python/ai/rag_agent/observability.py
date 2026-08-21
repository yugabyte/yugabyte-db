import os
from functools import wraps
from typing import Any, Callable
from langfuse import observe as _langfuse_observe

def _is_tracing_enabled() -> bool:
    return os.getenv("ENABLE_LANGFUSE_TRACING", "false") == "true"

def _build_decorator(*observe_args: Any, **observe_kwargs: Any) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    def decorator(func: Callable[..., Any]) -> Callable[..., Any]:
        wrapped_holder: dict[str, Callable[..., Any]] = {}
        @wraps(func)
        def lazy_wrapper(*args: Any, **kwargs: Any) -> Any:
            if not _is_tracing_enabled():
                return func(*args, **kwargs)
            cached = wrapped_holder.get("fn")
            if cached is None:
                cached = _langfuse_observe(*observe_args, **observe_kwargs)(func)
                wrapped_holder["fn"] = cached
            return cached(*args, **kwargs)
        return lazy_wrapper
    return decorator

def meko_observe(*args: Any, **kwargs: Any):
    if len(args) == 1 and not kwargs and callable(args[0]):
        func = args[0]
        return _build_decorator()(func)
    return _build_decorator(*args, **kwargs)
