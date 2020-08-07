from ybops.cloud.common.method import ListInstancesMethod, CreateInstancesMethod, \
    ProvisionInstancesMethod, DestroyInstancesMethod, AbstractMethod, \
    AbstractAccessMethod, AbstractNetworkMethod, AbstractInstancesMethod, \
    DestroyInstancesMethod, AbstractInstancesMethod
import logging
import json

DEFAULT_AZURE_CENTOS_IMAGE = '7.8.2020051900'


class AzureNetworkBootstrapMethod(AbstractNetworkMethod):
    def __init__(self, base_command):
        super(AzureNetworkBootstrapMethod, self).__init__(base_command, "bootstrap")

    def add_extra_args(self):
        super(AzureNetworkBootstrapMethod, self).add_extra_args()
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data")

    def callback(self, args):
        self.cloud.network_bootstrap(args)


class AzureCreateInstancesMethod(CreateInstancesMethod):
    def __init__(self, base_command):
        super(AzureCreateInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(AzureCreateInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--volume_type", choices=["Premium_LRS"], default="Premium_LRS",
                                 help="Volume type for volumes on EBS-backed instances.")

    def preprocess_args(self, args):
        super(AzureCreateInstancesMethod, self).preprocess_args(args)

    def callback(self, args):
        super(AzureCreateInstancesMethod, self).callback(args)

    def run_ansible_create(self, args):
        self.update_ansible_vars(args)
        self.cloud.create_instance(args, self.extra_vars["ssh_user"])


class AzureProvisionInstancesMethod(ProvisionInstancesMethod):
    def __init__(self, base_command):
        super(AzureProvisionInstancesMethod, self).__init__(base_command)

    def setup_create_method(self):
        self.create_method = AzureCreateInstancesMethod(self.base_command)

    def add_extra_args(self):
        super(AzureProvisionInstancesMethod, self).add_extra_args()

    def callback(self, args):
        super(AzureProvisionInstancesMethod, self).callback(args)

    def update_ansible_vars_with_args(self, args):
        super(AzureProvisionInstancesMethod, self).update_ansible_vars_with_args(args)
        self.extra_vars["device_names"] = self.cloud.get_device_names(args)
        self.extra_vars["mount_points"] = self.cloud.get_mount_points_csv(args)


class AzureDestroyInstancesMethod(DestroyInstancesMethod):
    def __init__(self, base_command):
        super(AzureDestroyInstancesMethod, self).__init__(base_command)

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            logging.error("Host {} does not exists.".format(args.search_pattern))
            return

        self.cloud.destroy_instance(args)


class AzureAccessAddKeyMethod(AbstractAccessMethod):
    def __init__(self, base_command):
        super(AzureAccessAddKeyMethod, self).__init__(base_command, "add-key")

    def callback(self, args):
        (private_key_file, public_key_file) = self.validate_key_files(args)
        print(json.dumps({"private_key": private_key_file, "public_key": public_key_file}))


class AzureQueryVPCMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AzureQueryVPCMethod, self).__init__(base_command, "vpc")

    def callback(self, args):
        print(json.dumps({"westus2": {"default_image": DEFAULT_AZURE_CENTOS_IMAGE}}))


class AzureQueryRegionsMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AzureQueryRegionsMethod, self).__init__(base_command, "region")

    def callback(self, args):
        logging.debug("Querying Region!")


class AzureQueryInstanceTypesMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AzureQueryInstanceTypesMethod, self).__init__(base_command, "instance_types")

    def add_extra_args(self):
        super(AzureQueryInstanceTypesMethod, self).add_extra_args()
        self.parser.add_argument("--regions", nargs='+')
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        print(json.dumps(self.cloud.get_instance_types(args)))


class AzureQueryZonesMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AzureQueryZonesMethod, self).__init__(base_command, "zones")

    def add_extra_args(self):
        super(AzureQueryZonesMethod, self).add_extra_args()
        self.parser.add_argument(
            "--dest_vpc_id", default=None,
            help="Custom VPC to get zone and subnet info for.")
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        print(json.dumps({"westus2": {"zones": ["1", "2", "3"],
                          "subnetworks": ["default"]}}))
