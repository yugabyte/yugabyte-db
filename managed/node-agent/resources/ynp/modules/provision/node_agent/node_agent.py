from ...base_module import BaseYnpModule
import logging
import time
import json
import requests
import os


class InstallNodeAgent(BaseYnpModule):

    def _get_headers(self, token):
        return {
            'Accept': 'application/json',
            'X-AUTH-YW-API-TOKEN': f'{token}',
            'Content-Type': 'application/json'
        }

    def _get_provider_url(self, context):
        return (
            f'{context.get("url")}/api/v1/customers/{context.get("customer_uuid")}/providers'
            f'?name={context.get("provider_name")}'
        )

    def _get_instance_type(self, yba_url, customer_uuid, p_uuid, code):
        return (
            f'{yba_url}/api/v1/customers/{customer_uuid}/providers/'
            f'{p_uuid}/instance_types/{code}'
        )

    def _generate_provider_payload(self, context):
        # Generates the body for provider payload.
        time_stamp = int(time.time())
        provider_data = {
            "name": context.get("provider_name", f'onprem{time_stamp}'),
            "code": "onprem",
            "details": {
                "skipProvisioning": True,
                "cloudInfo": {
                    "onprem": {
                        "ybHomeDir": context.get("yb_home_dir", "/home/yugabyte")
                    }
                }
            },
            "regions": []
        }
        region = {
            "name": context.get("provider_region_name"),
            "code": context.get("provider_region_name"),
            "zones": [{
                "name": context.get("provider_region_zone_name"),
                "code": context.get("provider_region_zone_name")
            }]
        }
        provider_data["regions"].append(region)
        if context.get("provider_access_key_path") is not None:
            with open(context.get("provider_access_key_path"), 'r') as file:
                provider_access_key = file.read().strip()
            provider_data["allAccessKeys"] = [{
                "keyInfo": {
                    "keyPairName": f'onprem_key_{time_stamp}.pem',
                    "sshPrivateKeyContent": provider_access_key,
                    "skipKeyValidateAndUpload": False
                }
            }]

        return provider_data

    def _generate_provider_update_payload(self, context, provider):
        regions = provider.get('regions', [])
        region_exist = False
        for region in regions:
            if region['code'] == context.get('provider_region_name'):
                region_exist = True
                zones = region.get('zones', [])
                zone_exist = False
                for zone in zones:
                    if zone['code'] == context.get('provider_region_zone_name'):
                        zone_exist = True
                if not zone_exist:
                    # Append the zone in the region.
                    zones.append({
                        "name": context.get("provider_region_zone_name"),
                        "code": context.get("provider_region_zone_name")
                    })
                region['zones'] = zones
        if not region_exist:
            # Append the region in the provider.
            regions.append({
                "name": context.get("provider_region_name"),
                "code": context.get("provider_region_name"),
                "zones": [{
                    "name": context.get("provider_region_zone_name"),
                    "code": context.get("provider_region_zone_name")
                }]
            })
        provider['regions'] = regions
        return provider

    def _generate_instance_type_payload(self, context):
        time_stamp = int(time.time())
        instance_data = {
            'idKey': {
                'instanceTypeCode': context.get('instance_type_name')
            },
            'providerUuid': "$provider_id",
            'providerCode': 'onprem',
            'numCores': context.get('instance_type_cores'),
            'memSizeGB': context.get('instance_type_memory_size'),
            'instanceTypeDetails': {
                'volumeDetailsList': []
            }
        }
        mount_points = (context.get('instance_type_mount_points')
                        .strip("[]").replace("'", "").split(", "))
        for mp in mount_points:
            volume_detail = {
                'volumeSizeGB': context.get('instance_type_volume_size'),
                'volumeType': 'SSD',
                'mountPath': mp
            }
            instance_data['instanceTypeDetails']['volumeDetailsList'].append(volume_detail)

        return instance_data

    def _generate_add_node_payload(self, context):
        node_add_payload = {
            "nodes": [
                {
                    "instanceType": context.get('instance_type_name'),
                    "ip": context.get('node_ip'),
                    "region": context.get('provider_region_name'),
                    "zone": context.get('provider_region_zone_name'),
                    "nodeName": context.get("node_name"),
                    "instanceName": context.get('instance_type_name')
                }
            ]
        }

        return node_add_payload

    def _get_provider(self, context):
        provider_url = self._get_provider_url(context)
        yba_url = context.get('url')
        skip_tls_verify = not yba_url.lower().startswith('https')
        response = requests.get(provider_url,
                                headers=self._get_headers(context.get('api_key')),
                                verify=skip_tls_verify)
        return response

    def _create_instance_if_not_exists(self, context, provider):
        yba_url = context.get('url')
        skip_tls_verify = not yba_url.lower().startswith('https')
        get_instance_type_url = self._get_instance_type(context.get('url'),
                                                        context.get('customer_uuid'),
                                                        provider.get('uuid'),
                                                        context.get('instance_type_name'))

        try:
            response = requests.get(get_instance_type_url,
                                    headers=self._get_headers(context.get('api_key')),
                                    verify=skip_tls_verify)
            response.raise_for_status()
            data = response.json()
            if not data:  # If the instance type does not exist
                raise ValueError("Instance type does not exist")
        except requests.exceptions.HTTPError as http_err:
            if response.status_code == 400:
                logging.info("Instance type does not exist, creating it.")
                context['provider_id'] = provider['uuid']
                instance_data = self._generate_instance_type_payload(context)

                instance_payload_file = os.path.join(context.get('tmp_directory'),
                                                     'create_instance.json')
                with open(instance_payload_file, 'w') as f:
                    json.dump(instance_data, f, indent=4)
            else:
                logging.error(f"Request error: {http_err}")
        except requests.exceptions.RequestException as req_err:
            logging.error(f"Request error: {req_err}")
        except ValueError as json_err:
            logging.error(f"Error parsing JSON response: {json_err}")

    def _cleanup(self, context):
        files_to_remove = [
            "create_provider.json",
            "update_provider.json",
            "create_instance.json",
            "add_node_to_provider.json"
        ]

        for file_name in files_to_remove:
            file_path = os.path.join(context.get('tmp_directory'), file_name)
            if os.path.isfile(file_path):
                os.remove(file_path)

    def render_templates(self, context):
        node_agent_enabled = False
        yba_url = context.get('url')
        skip_tls_verify = not yba_url.lower().startswith('https')
        self._cleanup(context)

        try:
            # Make the GET request
            response = self._get_provider(context)
            response.raise_for_status()  # Raise an HTTPError for bad responses (4xx and 5xx)

            # Parse the JSON response
            try:
                provider_data = response.json()
                if isinstance(provider_data, list) and len(provider_data) > 0:
                    provider_data = provider_data[0]
                    provider_details = provider_data.get('details', {})
                    regions = provider_data.get('regions', [])
                    region_exists = False
                    for region in regions:
                        if region['code'] == context.get('provider_region_name'):
                            region_exists = True
                            zones = region.get('zones', [])
                            zone_exist = False
                            for zone in zones:
                                if zone['code'] == context.get('provider_region_zone_name'):
                                    zone_exist = True
                    if not region_exists or not zone_exist:
                        update_provider_data = self._generate_provider_update_payload(
                            context, provider_data)
                        update_provider_data_file = os.path.join(context.get('tmp_directory'),
                                                                 'update_provider.json')
                        with open(update_provider_data_file, 'w') as f:
                            json.dump(update_provider_data, f, indent=4)
                    self._create_instance_if_not_exists(context, provider_data)
                    context['provider_id'] = str(provider_data.get('uuid'))
                    node_agent_enabled = provider_details.get('enableNodeAgent', False)
                else:
                    logging.info("Generating provider create payload...")
                    provider_payload = self._generate_provider_payload(context)
                    provider_payload_file = os.path.join(context.get('tmp_directory'),
                                                         'create_provider.json')
                    with open(provider_payload_file, 'w') as f:
                        json.dump(provider_payload, f, indent=4)
                    instance_create_payload = self._generate_instance_type_payload(context)
                    instance_payload_file = os.path.join(context.get('tmp_directory'),
                                                         'create_instance.json')
                    with open(instance_payload_file, 'w') as f:
                        json.dump(instance_create_payload, f, indent=4)
                    node_agent_enabled = True

            except ValueError as json_err:
                logging.error(f"Error parsing JSON response: {json_err}")
        except requests.exceptions.RequestException as req_err:
            logging.error(f"Request error: {req_err}")

        add_node_payload = self._generate_add_node_payload(context)
        add_node_payload_file = os.path.join(context.get('tmp_directory'),
                                             'add_node_to_provider.json')
        with open(add_node_payload_file, 'w') as f:
            json.dump(add_node_payload, f, indent=4)

        if node_agent_enabled:
            return super().render_templates(context)
        return None
