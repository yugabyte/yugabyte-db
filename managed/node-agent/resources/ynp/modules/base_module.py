from jinja2 import Environment, FileSystemLoader
from utils.filters import split_servers
import inspect


class BaseYnpModule:
    registry = {}
    run_template = "run.j2"
    precheck_template = "precheck.j2"

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        cls.registry[cls.__name__] = (cls, inspect.getfile(cls))

    def common_method(self):
        pass

    def render_templates(self, context):
        if self.run_template is None or self.precheck_template is None:
            raise ValueError("Subclasses must define 'run_template' and 'precheck_template'")

        template_dir = context.get("templatedir")
        templates = {
            "run": self.run_template,
            "precheck": self.precheck_template
        }
        env = Environment(loader=FileSystemLoader(template_dir))
        # Register the custom filter
        env.filters['split_servers'] = split_servers

        output = {}
        for template_name, template_path in templates.items():
            template = env.get_template(template_path)
            output[template_name] = template.render(context)

        return output
