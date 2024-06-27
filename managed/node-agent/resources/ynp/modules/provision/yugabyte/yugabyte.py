from ...base_module import BaseYnpModule


class CreateYugabyteUser(BaseYnpModule):

    run_template = "yugabyte_run.j2"
    precheck_template = "yugabyte_precheck.j2"
