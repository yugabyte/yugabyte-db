from ...base_module import BaseYnpModule
import jinja2
from jinja2 import Environment, FileSystemLoader
from utils.filters import split_servers


class ConfigureSystemd(BaseYnpModule):

    run_template = "systemd_run.j2"
    precheck_template = "systemd_precheck.j2"
