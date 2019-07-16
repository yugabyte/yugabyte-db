# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import os
import requests
import json

from ybops.common.exceptions import YBOpsRuntimeError


class Replicated(object):
    REPLICATED_VENDOR_API = 'https://api.replicated.com/vendor/v1/app/'

    """Replicated class is used to fetch existing release information in replicated and to promote
    new release."""

    def __init__(self):
        api_token = os.environ.get('REPLICATED_API_TOKEN')
        self.app_id = os.environ.get('REPLICATED_APP_ID')
        assert api_token is not None, 'Environment Variable REPLICATED_API_TOKEN not set.'
        assert self.app_id is not None, 'Environment Variable REPLICATED_APP_ID not set.'
        self.auth_header = {'authorization': api_token, 'content-type': 'application/json'}
        self.releases = self._get_releases()
        self.current_release_sequence = None

    def _get_request_endpoint(self, request_type):
        base_url = os.path.join(self.REPLICATED_VENDOR_API, self.app_id)
        if request_type == 'LIST':
            return os.path.join(base_url, 'releases', 'paged')
        elif request_type == 'CREATE':
            return os.path.join(base_url, 'release')
        elif request_type == 'UPDATE':
            return os.path.join(base_url, str(self.current_release_sequence), 'raw')
        elif request_type == 'PROMOTE':
            return os.path.join(base_url, str(self.current_release_sequence), 'promote')
        else:
            raise TypeError('Invalid request type')

    def _get_releases(self):
        params = {'start': 0, 'count': 1}
        # Fetch the current releases and channel information
        response = requests.get(self._get_request_endpoint('LIST'),
                                headers=self.auth_header, json=params)
        response.raise_for_status()
        return json.loads(response.text)['releases']

    def _get_active_channel(self, channel_name='Alpha'):
        active_channel = None
        for release in self.releases:
            active_channel = next(iter([channel for channel in release['ActiveChannels']
                                        if channel['Name'] == channel_name]), None)
            if active_channel:
                break
        return active_channel

    def _get_tagged_channels(self, tag):
        tagged_channels = []
        for release in self.releases:
            tagged_channels = [channel for channel in release['ActiveChannels']
                               if channel['ReleaseLabel'] == tag]
            if tagged_channels:
                break
        return tagged_channels

    def _get_or_create_release(self, tag):
        draft_release = self._get_editable_release()
        # If we already have a release version created and in Editable state, just use that.
        if draft_release:
            return draft_release

        # Create a new release in replicated
        params = {'name': tag, 'source': 'latest', 'sourcedata': 0}
        response = requests.post(self._get_request_endpoint('CREATE'),
                                 headers=self.auth_header, json=params)
        response.raise_for_status()
        return json.loads(response.text)

    def _get_editable_release(self):
        return next(iter([release for release in self.releases
                          if release['Editable']]), None)

    def publish_release(self, tag, raw_data):
        release = self._get_or_create_release(tag)
        self.current_release_sequence = release['Sequence']
        # In case of update release we publish the yaml in the body so we need to make the put
        # request with content-type text/plain
        auth_header = self.auth_header.copy()
        auth_header['content-type'] = 'text/plain'
        response = requests.put(self._get_request_endpoint('UPDATE'),
                                headers=auth_header, data=raw_data)
        response.raise_for_status()

    def promote_release(self, tag, channel_name='Alpha', release_notes=[]):
        tagged_channels = self._get_tagged_channels(tag)
        active_channel = self._get_active_channel(channel_name)
        if any(tagged_channels):
            is_promoted = any([channel for channel in tagged_channels
                               if channel['Name'] == channel_name])
            self.current_release_sequence = tagged_channels[0]['ReleaseSequence']
            if is_promoted:
                raise YBOpsRuntimeError(
                    'Release {} already promoted for channel {}'.format(tag, channel_name))
        elif self.current_release_sequence is None:
            current_release = self._get_editable_release()
            self.current_release_sequence = current_release['Sequence']

        active_channel_id = active_channel['Id']
        params = {'channels': [active_channel_id], 'label': tag,
                  'release_notes': '\n'.join(release_notes), 'required': False}
        # Promote the release for the provided channel
        response = requests.post(self._get_request_endpoint('PROMOTE'),
                                 headers=self.auth_header, json=params)
        response.raise_for_status()
