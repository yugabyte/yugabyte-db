from ...base_module import BaseYnpModule
import jinja2
from jinja2 import Environment, FileSystemLoader
from utils.filters import split_servers


class ConfigureChrony(BaseYnpModule):

    run_template = "chrony_run.j2"
    precheck_template = "chrony_precheck.j2"

    def render_templates(self, context):
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
