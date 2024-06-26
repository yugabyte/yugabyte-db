from ...base_module import BaseYnpModule
import requests


class InstallNodeAgent(BaseYnpModule):

    run_template = "node_agent_run.j2"
    precheck_template = "node_agent_precheck.j2"

    def _get_headers(self, token):
        return {
            'Accept': 'application/json',
            'X-AUTH-YW-API-TOKEN': f'{token}'
        }

    def _get_provider_url(self, yba_url, customer_uuid, name):
        return f'{yba_url}/api/v1/customers/{customer_uuid}/providers?name={name}'

    def render_templates(self, context):
        node_agent_enabled = False
        yba_url = context.get('url')
        skip_tls_verify = not yba_url.lower().startswith('https')
        provider_url = self._get_provider_url(yba_url,
                                              context.get('customer_uuid'),
                                              context.get('provider_name'))

        try:
            # Make the GET request
            response = requests.get(provider_url,
                                    headers=self._get_headers(context.get('api_key')),
                                    verify=skip_tls_verify)
            response.raise_for_status()  # Raise an HTTPError for bad responses (4xx and 5xx)

            # Parse the JSON response
            try:
                provider_data = response.json()
                if isinstance(provider_data, list) and len(provider_data) > 0:
                    provider_details = provider_data[0].get('details', {})
                    context['provider_id'] = str(provider_data[0].get('uuid'))
                    node_agent_enabled = provider_details.get('enableNodeAgent', False)
                else:
                    print("No provider data found.")
                    # Todo: Create provider in case it does not exist.
            except ValueError as json_err:
                print(f"Error parsing JSON response: {json_err}")
        except requests.exceptions.RequestException as req_err:
            print(f"Request error: {req_err}")

        if node_agent_enabled:
            return super().render_templates(context)
        return None
