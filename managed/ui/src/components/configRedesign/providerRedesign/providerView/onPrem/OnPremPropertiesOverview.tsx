/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useState } from 'react';
import { Collapse, Typography } from '@material-ui/core';
import { Cancel, Check } from '@material-ui/icons';

import { NTPSetupType, NTPSetupTypeLabel, ProviderCode } from '../../constants';
import { YBButton } from '../../../../../redesign/components';
import { getLatestAccessKey, getNtpSetupType } from '../../utils';

import { YBProvider } from '../../types';

import styles from './OnPremPropertiesOverview.module.scss';

interface OnPremPropertiesOverviewProps {
  providerConfig: YBProvider;
  isProviderInUse: boolean;
}

export const OnPremPropertiesOverview = ({
  providerConfig,
  isProviderInUse
}: OnPremPropertiesOverviewProps) => {
  const [isOverviewExpanded, setIsOverviewExpanded] = useState<boolean>(false);

  if (providerConfig.code !== ProviderCode.ON_PREM) {
    return null;
  }

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
