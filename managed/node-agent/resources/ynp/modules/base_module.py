import inspect


class BaseYnpModule:
    registry = {}

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        cls.registry[cls.__name__] = (cls, inspect.getfile(cls))

    def common_method(self):
        pass
