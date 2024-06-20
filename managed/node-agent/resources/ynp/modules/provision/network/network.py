from ...base_module import BaseYnpModule
import jinja2
from jinja2 import Environment, FileSystemLoader
from utils.filters import split_servers
import logging


class ConfigureNetwork(BaseYnpModule):

    run_template = "run.j2"
    precheck_template = "precheck.j2"
