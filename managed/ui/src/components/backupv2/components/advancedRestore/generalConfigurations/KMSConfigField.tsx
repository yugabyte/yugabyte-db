/*
 * Created on Wed Jan 03 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import Select from 'react-select';
import { useQuery } from 'react-query';
import { Controller, useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { components } from 'react-select';
import { find } from 'lodash';
import { Box, makeStyles } from '@material-ui/core';
import { YBLabel } from '../../../../common/descriptors';

import { getKMSConfigs } from '../../../common/BackupAPI';
import { fetchUniversesList } from '../../../../../actions/xClusterReplication';
import { Badge_Types, StatusBadge } from '../../../../common/badge/StatusBadge';
import { IUniverse } from '../../../common/IBackup';
import { AdvancedGeneralConfigs } from './GeneralConfigurations';

const useStyles = makeStyles((theme) => ({
  kmsHelpText: {
    color: '#67666C',
    fontSize: '11.5px',
    marginTop: theme.spacing(1),
    display: 'inline-block'
  },
  controls: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1),
    padding: theme.spacing(2),
    marginTop: theme.spacing(1)
  },
  sourceUniverseLabel: {
    display: 'inline-flex !important',
    alignItems: 'center',
    '& .status-badge': {
      marginLeft: theme.spacing(0.5)
    }
  },
  kmsConfig: {
    marginTop: theme.spacing(3)
  }
}));

export const KMSConfigField = () => {

  const { control, getValues } = useFormContext<AdvancedGeneralConfigs>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'advancedRestoreModal.generalConfig'
  });
  
  const classes = useStyles();

  const { data: universeList, isLoading: isUniverseListLoading } = useQuery(['universe'], () =>
    fetchUniversesList().then((res) => res.data as IUniverse[])
  );

  const { data: kmsConfigs, isLoading: isKMSConfigLoading } = useQuery(['kms_configs'], () =>
    getKMSConfigs()
  );

  const kmsConfigList = kmsConfigs
    ? kmsConfigs.map((config: any) => {
        const labelName = config.metadata.provider + ' - ' + config.metadata.name;
        return { value: config.metadata.configUUID, label: labelName };
      })
    : [];

  const universe = find(universeList, { universeUUID: getValues().targetUniverse?.value });

  let currentActiveKMS = '';
  if (universe && universe?.universeDetails?.encryptionAtRestConfig?.encryptionAtRestEnabled)
    currentActiveKMS = universe?.universeDetails?.encryptionAtRestConfig?.kmsConfigUUID;

  return (
    <Controller
      control={control}
      name="kmsConfig"
      render={({ field: { value, onChange }, fieldState: { error } }) => (
        <YBLabel
          label={t('selectKMSConfig', {
            keyPrefix: 'newRestoreModal.generalSettings.universeSelection'
          })}
          classOverrides={classes.kmsConfig}
          meta={{
            touched: !!error?.message,
            error: error?.message
          }}
        >
          <Select
            options={kmsConfigList}
            isClearable
            value={value}
            components={{
              // eslint-disable-next-line react/display-name
              Option: (props: any) => {
                return (
                  <components.Option {...props}>
                    <Box display="flex" alignItems="center">
                      <span>{props.data.label}</span>{' '}
                      <Box mx={1} display="flex">
                        {props.data.value === currentActiveKMS && (
                          <StatusBadge
                            statusType={Badge_Types.COMPLETED}
                            customLabel={t('activeKMS', {
                              keyPrefix: 'newRestoreModal.generalSettings.universeSelection'
                            })}
                          />
                        )}
                      </Box>
                    </Box>
                  </components.Option>
                );
              }
            }}
            onChange={onChange}
            isLoading={isUniverseListLoading || isKMSConfigLoading}
          />
          <span className={classes.kmsHelpText}>
            <Trans
              i18nKey="newRestoreModal.generalSettings.universeSelection.kmsConfigHelpText"
              components={{ b: <b /> }}
            />
          </span>
        </YBLabel>
      )}
    />
  );
};
