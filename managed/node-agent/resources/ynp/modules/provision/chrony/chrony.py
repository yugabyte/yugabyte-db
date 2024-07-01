from ...base_module import BaseYnpModule


class ConfigureChrony(BaseYnpModule):

    run_template = "chrony_run.j2"
    precheck_template = "chrony_precheck.j2"
