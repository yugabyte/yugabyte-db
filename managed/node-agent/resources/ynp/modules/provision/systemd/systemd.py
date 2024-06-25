from ...base_module import BaseYnpModule


class ConfigureSystemd(BaseYnpModule):

    run_template = "systemd_run.j2"
    precheck_template = "systemd_precheck.j2"
