/*
 * Created on Wed Aug 14 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { find, flatten, uniq } from 'lodash';
import { Control, useWatch } from 'react-hook-form';
import { useQuery } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';

import { AlertVariant, YBAlert } from '../../../../../../components';

import { GetUniverseUUID } from '../../../ScheduledBackupUtils';
import { api } from '../../../../../../helpers/api';
import { Universe } from '../../../../../../helpers/dtos';
import { GeneralSettingsModel } from '../../models/IGeneralSettings';

interface MultiRegionNodesSupportProps {
  control: Control<GeneralSettingsModel>;
}

// this component is used to check if the selected regions in the storage config are satisfied by the nodes in the universe
// if not, it will show an alert

const MultiRegionNodesSupport: FC<MultiRegionNodesSupportProps> = ({ control }) => {
  const universeUUID = GetUniverseUUID();

  const { data: universeInfo } = useQuery<Universe>(['fetchUniverse', universeUUID], () =>
    api.fetchUniverse(universeUUID!)
  );

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.generalSettings'
  });

  const storageConfig = useWatch({ control, name: 'storageConfig' });

  const nodesInRegionsList =
    uniq(
      flatten(
        universeInfo?.universeDetails?.clusters.map((e: any) => e.regions.map((r: any) => r.code))
      )
    ) ?? [];

  const regions_satisfied_by_config = nodesInRegionsList.every((e) => {
    if (!storageConfig || !storageConfig.regions) return true;

    return find(storageConfig.regions, { REGION: e });
  });

  if (regions_satisfied_by_config) return null;

  return (
    <YBAlert
      open
      variant={AlertVariant.Warning}
      text={<Trans t={t} i18nKey="unsatisfiedNodes" components={{ b: <b /> }} />}
    />
  );
};

export default MultiRegionNodesSupport;
