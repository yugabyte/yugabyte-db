/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { makeStyles } from '@material-ui/core';

import { YBAHost } from '../../../redesign/helpers/constants';
import { HostInfo, Universe } from '../../../redesign/helpers/dtos';
import { NTPSetupType, ProviderCode, CloudVendorProviders } from './constants';
import { UniverseItem } from './providerView/providerDetails/UniverseTable';
import { YBProvider, YBProviderMutation } from './types';

export const getNtpSetupType = (providerConfig: YBProvider): NTPSetupType => {
  if (
    providerConfig.code === ProviderCode.KUBERNETES ||
    providerConfig.details.setUpChrony === false
  ) {
    return NTPSetupType.NO_NTP;
  }
  if (providerConfig.details.ntpServers.length) {
    return NTPSetupType.SPECIFIED;
  }
  return NTPSetupType.CLOUD_VENDOR;
};

// The public cloud providers (AWS, GCP, AZU) each provide their own NTP server which users may opt use
export const hasDefaultNTPServers = (providerCode: ProviderCode) =>
  (CloudVendorProviders as readonly ProviderCode[]).includes(providerCode);

// TODO: API should return the YBA host as part of the hostInfo response.
export const getYBAHost = (hostInfo: HostInfo) => {
  if (!(typeof hostInfo.gcp === 'string' || hostInfo.gcp instanceof String)) {
    return YBAHost.GCP;
  }
  if (!(typeof hostInfo.aws === 'string' || hostInfo.aws instanceof String)) {
    return YBAHost.AWS;
  }
  return YBAHost.SELF_HOSTED;
};

export const getInfraProviderTab = (providerConfig: YBProvider | YBProviderMutation) =>
  providerConfig.code === ProviderCode.KUBERNETES
    ? providerConfig.details.cloudInfo.kubernetes.kubernetesProvider
    : providerConfig.code;

export const getLinkedUniverses = (providerUUID: string, universes: Universe[]) =>
  universes.reduce((linkedUniverses: UniverseItem[], universe) => {
    const linkedClusters = universe.universeDetails.clusters.filter(
      (cluster) => cluster.userIntent.provider === providerUUID
    );
    if (linkedClusters.length) {
      linkedUniverses.push({
        ...universe,
        linkedClusters: linkedClusters
      });
    }
    return linkedUniverses;
  }, []);

export const usePillStyles = makeStyles((theme) => ({
  pill: {
    height: 'fit-content',
    padding: '4px 6px',
    fontSize: '10px',
    borderRadius: '6px',
    backgroundColor: theme.palette.grey[200]
  }
}));
