from ...base_module import BaseYnpModule
import logging
import time
import json
import urllib.request
import urllib.error
import urllib.parse
import ssl
import os
import sys


class InstallNodeAgent(BaseYnpModule):

    def _get_headers(self, token):
        return {
            'Accept': 'application/json',
            'X-AUTH-YW-API-TOKEN': f'{token}',
            'Content-Type': 'application/json'
        }

    def _get_provider_url(self, context):
        url = context.get("url")
        customer_uuid = context.get("customer_uuid")
        provider_name = context.get("provider_name")
        return (f'{url}/api/v1/customers/{customer_uuid}'
                f'/providers?name={provider_name}')

    def _get_instance_type(self, yba_url, customer_uuid, p_uuid, code):
        return (f'{yba_url}/api/v1/customers/{customer_uuid}/providers/'
                f'{p_uuid}/instance_types/{code}')

    def _make_request(self, url, headers=None, method='GET', data=None, verify_ssl=True):
        """Make HTTP request using urllib"""
        if headers is None:
            headers = {}

        # Create request
        req = urllib.request.Request(url, headers=headers, method=method)

        # Add data for POST requests
        if data and method in ['POST', 'PUT']:
            req.data = json.dumps(data).encode('utf-8')

        # Create SSL context
        ssl_context = ssl.create_default_context()
        if not verify_ssl:
            ssl_context.check_hostname = False
            ssl_context.verify_mode = ssl.CERT_NONE

        try:
            with urllib.request.urlopen(req, context=ssl_context) as response:
                response_data = response.read()
                if 200 <= response.status < 300:
                    return {
                        'status_code': response.status,
                        'json': lambda: json.loads(response_data.decode('utf-8'))
                    }
                else:
                    raise urllib.error.HTTPError(url, response.status, response.reason,
                                                 response.headers, None)
        except urllib.error.HTTPError as e:
            raise e
        except urllib.error.URLError as e:
            raise Exception(f"Request error: {e.reason}")

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
                        "ybHomeDir": context.get("yb_home_dir", "/home/yugabyte"),
                        "useClockbound": context.get("configure_clockbound", "false")
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
        mount_points = context.get('instance_type_mount_points')
        if not mount_points:
            raise ValueError(
                "instance_type_mount_points is required but not provided in configuration")

        mount_points = mount_points.strip("[]").replace("'", "").split(", ")
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
                    "ip": context.get('node_external_fqdn'),
                    "region": context.get('provider_region_name'),
                    "zone": context.get('provider_region_zone_name'),
                    "instanceName": context.get('node_name')
                }
            ]
        }

        return node_add_payload

    def _get_provider(self, context):
        provider_url = self._get_provider_url(context)
        yba_url = context.get('url')
        skip_tls_verify = not yba_url.lower().startswith('https')
        response = self._make_request(provider_url,
                                      headers=self._get_headers(context.get('api_key')),
                                      verify_ssl=skip_tls_verify)
        return response

    def _create_instance_if_not_exists(self, context, provider):
        yba_url = context.get('url')
        skip_tls_verify = not yba_url.lower().startswith('https')
        get_instance_type_url = self._get_instance_type(context.get('url'), context.get(
            'customer_uuid'), provider.get('uuid'), context.get('instance_type_name'))

        try:
            response = self._make_request(get_instance_type_url,
                                          headers=self._get_headers(context.get('api_key')),
                                          verify_ssl=skip_tls_verify)
            data = response['json']()
            if not data:  # If the instance type does not exist
                raise ValueError("Instance type does not exist")
        except urllib.error.HTTPError as http_err:
            if http_err.code == 400:
                logging.info("Instance type does not exist, creating it.")
                instance_data = self._generate_instance_type_payload(context)

                instance_payload_file = os.path.join(
                    context.get('tmp_directory'), 'create_instance.json')
                with open(instance_payload_file, 'w') as f:
                    json.dump(instance_data, f, indent=4)
            else:
                logging.error(f"Request error: {http_err}")
                sys.exit(1)
        except Exception as req_err:
            logging.error(f"Request error: {req_err}")
            sys.exit(1)
        except ValueError as json_err:
            logging.error(f"Error parsing JSON response: {json_err}")
            sys.exit(1)

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
        if context.get('is_cloud'):
            return super().render_templates(context)

        node_agent_enabled = False
        self._cleanup(context)

        try:
            # Make the GET request
            response = self._get_provider(context)

            # Parse the JSON response
            try:
                provider_data = response['json']()
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
                        update_provider_data_file = os.path.join(
                            context.get('tmp_directory'), 'update_provider.json')
                        with open(update_provider_data_file, 'w') as f:
                            json.dump(update_provider_data, f, indent=4)
                    self._create_instance_if_not_exists(context, provider_data)
                    context['provider_id'] = str(provider_data.get('uuid'))
                    node_agent_enabled = provider_details.get('enableNodeAgent', False)
                else:
                    logging.info("Generating provider create payload...")
                    provider_payload = self._generate_provider_payload(context)
                    provider_payload_file = os.path.join(
                        context.get('tmp_directory'), 'create_provider.json')
                    with open(provider_payload_file, 'w') as f:
                        json.dump(provider_payload, f, indent=4)
                    instance_create_payload = self._generate_instance_type_payload(context)
                    instance_payload_file = os.path.join(
                        context.get('tmp_directory'), 'create_instance.json')
                    with open(instance_payload_file, 'w') as f:
                        json.dump(instance_create_payload, f, indent=4)
                    node_agent_enabled = True

            except ValueError as json_err:
                logging.error(f"Error parsing JSON response: {json_err}")
                sys.exit(1)
        except Exception as req_err:
            logging.error(f"Request error: {req_err}")
            sys.exit(1)

        add_node_payload = self._generate_add_node_payload(context)
        add_node_payload_file = os.path.join(context.get(
            'tmp_directory'), 'add_node_to_provider.json')
        with open(add_node_payload_file, 'w') as f:
            json.dump(add_node_payload, f, indent=4)

        if node_agent_enabled:
            return super().render_templates(context)
        return None
