/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useState } from 'react';
import { useQuery } from 'react-query';
import { Collapse, Divider, Typography } from '@material-ui/core';
import { Cancel, Check } from '@material-ui/icons';

import { NTPSetupType, NTPSetupTypeLabel, ProviderCode } from '../../constants';
import { YBButton } from '../../../../../redesign/components';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { api, hostInfoQueryKey } from '../../../../../redesign/helpers/api';
import { getLatestAccessKey, getNtpSetupType } from '../../utils';

import { YBProvider } from '../../types';

import styles from './AWSPropertiesOverview.module.scss';

interface AWSPropertiesOverviewProps {
  providerConfig: YBProvider;
  isProviderInUse: boolean;
}

export const AWSPropertiesOverview = ({
  providerConfig,
  isProviderInUse
}: AWSPropertiesOverviewProps) => {
  const [isOverviewExpanded, setIsOverviewExpanded] = useState<boolean>(false);

  const hostInfoQuery = useQuery(hostInfoQueryKey.ALL, () => api.fetchHostInfo());

  if (providerConfig.code !== ProviderCode.AWS) {
    return null;
  }

  if (hostInfoQuery.isLoading || hostInfoQuery.isIdle) {
    return (
      <div className={styles.providerPropertiesContainer}>
        <YBLoading />
      </div>
    );
  }

  if (hostInfoQuery.isError) {
    return (
      <div className={styles.providerPropertiesContainer}>
        <YBErrorIndicator customErrorMessage="Error fetching host info." />
      </div>
    );
  }

  const hostInfo = hostInfoQuery.data;
  const ntpSetupType = getNtpSetupType(providerConfig);
  const latestAccessKey = getLatestAccessKey(providerConfig.allAccessKeys);
  return (
    <div className={styles.providerPropertiesContainer}>
      <div className={styles.propertiesRow}>
        <div>
          <Typography variant="body1">Usage</Typography>
          <Typography variant="body2">{isProviderInUse ? 'In Use' : 'Not In Use'}</Typography>
        </div>
        <div>
          <Typography variant="body1">SSH User</Typography>
          <Typography variant="body2">{providerConfig.details.sshUser}</Typography>
        </div>
        <div>
          <Typography variant="body1">SSH Port</Typography>
          <Typography variant="body2">{providerConfig.details.sshPort}</Typography>
        </div>
        <div>
          <Typography variant="body1">SSH Key</Typography>
          <Typography variant="body2">{latestAccessKey?.keyInfo.keyPairName}</Typography>
        </div>
      </div>
      <div className={styles.propertiesRow}>
        <div>
          <Typography variant="body1">NTP Setup Type</Typography>
          <Typography variant="body2">
            {ntpSetupType === NTPSetupType.CLOUD_VENDOR
              ? NTPSetupTypeLabel[ntpSetupType](providerConfig.code)
              : NTPSetupTypeLabel[ntpSetupType]}
          </Typography>
        </div>
      </div>
      <Divider className={styles.horizontalDivider} />
      <YBButton
        className={styles.toggleExpandBtn}
        type="button"
        onClick={() => {
          setIsOverviewExpanded(!isOverviewExpanded);
        }}
      >
        {isOverviewExpanded ? (
          <span>
            Less <i className="fa fa-caret-up" aria-hidden="true" />
          </span>
        ) : (
          <span>
            More <i className="fa fa-caret-down" aria-hidden="true" />
          </span>
        )}
      </YBButton>
      <Collapse in={isOverviewExpanded}>
        <div className={styles.expandedContentContainer}>
          <div className={styles.propertiesRow}>
            {!(typeof hostInfo.aws === 'string' || hostInfo.aws instanceof String) &&
              hostInfo.aws.region && (
                <div>
                  <Typography variant="body1">Host Region</Typography>
                  <Typography variant="body2">{hostInfo.aws.region}</Typography>
                </div>
              )}
            {!(typeof hostInfo.aws === 'string' || hostInfo.aws instanceof String) &&
              hostInfo.aws['vpc-id'] && (
                <div>
                  <Typography variant="body1">Host VPC ID</Typography>
                  <Typography variant="body2">{hostInfo.aws['vpc-id']}</Typography>
                </div>
              )}
            {!(typeof hostInfo.aws === 'string' || hostInfo.aws instanceof String) &&
              hostInfo.aws.privateIp && (
                <div>
                  <Typography variant="body1">Host Private IP</Typography>
                  <Typography variant="body2">{hostInfo.aws.privateIp}</Typography>
                </div>
              )}
            <div>
              <Typography variant="body1">Airgap Installation</Typography>
              <div className={styles.cell}>
                {providerConfig.details.airGapInstall ? (
                  <>
                    <Check />
                    <Typography variant="body2">On</Typography>
                  </>
                ) : (
                  <>
                    <Cancel />
                    <Typography variant="body2">Off</Typography>
                  </>
                )}
              </div>
            </div>
          </div>
        </div>
      </Collapse>
    </div>
  );
};
