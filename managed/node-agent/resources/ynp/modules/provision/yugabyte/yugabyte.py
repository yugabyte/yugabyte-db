from ...base_module import BaseYnpModule
import jinja2
from jinja2 import Environment, FileSystemLoader


class CreateYugabyteUser(BaseYnpModule):

    run_template = "yugabyte_run.j2"
    precheck_template = "yugabyte_precheck.j2"
