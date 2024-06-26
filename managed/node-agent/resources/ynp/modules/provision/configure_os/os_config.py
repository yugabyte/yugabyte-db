from ...base_module import BaseYnpModule


class ConfigureOs(BaseYnpModule):

    run_template = "run.j2"
    precheck_template = "precheck.j2"
