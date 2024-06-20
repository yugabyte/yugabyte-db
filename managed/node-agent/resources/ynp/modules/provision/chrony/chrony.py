from ...base_module import BaseYnpModule
import jinja2
from jinja2 import Environment, FileSystemLoader
from utils.filters import split_servers


class ConfigureChrony(BaseYnpModule):

    run_template = "chrony_run.j2"
    precheck_template = "chrony_precheck.j2"
