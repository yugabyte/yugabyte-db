from ...base_module import BaseYnpModule


class ConfigureNodeExporter(BaseYnpModule):

    run_template = "node_exporter_run.j2"
    precheck_template = "node_exporter_precheck.j2"
