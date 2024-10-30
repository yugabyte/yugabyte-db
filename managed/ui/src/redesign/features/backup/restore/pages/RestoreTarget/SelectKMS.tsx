/*
 * Created on Wed Aug 21 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { find, has } from 'lodash';
import { Trans, useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { components } from 'react-select';
import { YBReactSelectField } from '../../../../../../components/configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';

import { YBTag, YBTag_Types } from '../../../../../../components/common/YBTag';
import {
  ReactSelectComponents,
  ReactSelectStyles
} from '../../../scheduled/create/ReactSelectStyles';
import { api } from '../../../../../helpers/api';
import { getKMSConfigs } from '../../../../../../components/backupv2/common/BackupAPI';
import { RestoreFormModel } from '../../models/RestoreFormModel';
import { GetRestoreContext } from '../../RestoreUtils';

const useStyles = makeStyles((theme) => ({
  title: {
    marginBottom: '8px'
  },
  kmsHelperText: {
    color: theme.palette.ybacolors.textDarkGray,
    fontSize: '12px',
    marginTop: '8px'
  }
}));

const SelectKMS: FC = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.target'
  });

  const classes = useStyles();

  const {
    control,
    formState: { errors },
    setValue
  } = useFormContext<RestoreFormModel>();

  const [{ backupDetails }] = GetRestoreContext();

  const { data: kmsConfigs } = useQuery(['kms_configs'], () => getKMSConfigs(), {
    onSuccess(kmsConfigList) {
      if (backupDetails?.commonBackupInfo.kmsConfigUUID) {
        const kmsUsedDuringBackup = kmsConfigList.find(
          (kms: any) => kms?.metadata.configUUID === backupDetails?.commonBackupInfo.kmsConfigUUID
        );
        if (kmsUsedDuringBackup) {
          setValue('target.kmsConfig', {
            value: kmsUsedDuringBackup.metadata.configUUID,
            label: kmsUsedDuringBackup.metadata.provider + ' - ' + kmsUsedDuringBackup.metadata.name
          });
        }
      }
    }
  });

  let kmsConfigList = kmsConfigs
    ? kmsConfigs.map((config: any) => {
        const labelName = config.metadata.provider + ' - ' + config.metadata.name;
        return { value: config.metadata.configUUID, label: labelName };
      })
    : [];

  const { data: universeList } = useQuery(['universeList'], api.fetchUniverseList);

  const reactSelectComp = ReactSelectComponents(!!errors?.target?.kmsConfig?.message);

  const universe = find(universeList, { universeUUID: backupDetails?.universeUUID });
  let currentActiveKMS: string | null = '';
  if (universe && universe?.universeDetails?.encryptionAtRestConfig?.encryptionAtRestEnabled)
    currentActiveKMS = universe?.universeDetails?.encryptionAtRestConfig?.kmsConfigUUID;

  //kms config used in the universe while taking backup
  const isEncryptedBackup = has(backupDetails?.commonBackupInfo, 'kmsConfigUUID');
  const kmsIdDuringBackup = kmsConfigList.find(
    (config: Record<string, any>) =>
      config?.value === backupDetails?.commonBackupInfo?.kmsConfigUUID
  );
  if (kmsIdDuringBackup) {
    //move currently active kms to top of the list
    kmsConfigList = kmsConfigList.filter(
      (config: Record<string, any>) => config.value !== kmsIdDuringBackup.value
    );
    kmsConfigList.unshift(kmsIdDuringBackup);
  }

  return (
    <div>
      <div className={classes.title}>{t('kmsConfig')}</div>
      <YBReactSelectField
        control={control}
        options={kmsConfigList}
        name="target.kmsConfig"
        width="620px"
        isClearable
        stylesOverride={ReactSelectStyles}
        components={{
          // eslint-disable-next-line react/display-name
          Option: (props: any) => {
            if (isEncryptedBackup && props.data.value === kmsIdDuringBackup?.value) {
              return (
                <components.Option {...props} className="active-kms">
                  <Box display="flex" alignItems="center">
                    <span className="kms-used">{props.data.label}</span>
                    <Box mx={1} display="flex">
                      <YBTag type={YBTag_Types.YB_GRAY}>{t('usedDuringBackup')}</YBTag>
                    </Box>
                    {props.data.value === currentActiveKMS && (
                      <YBTag type={YBTag_Types.YB_GRAY}>{t('activeKMS')}</YBTag>
                    )}
                  </Box>
                </components.Option>
              );
            }
            return (
              <components.Option {...props}>
                <Box display="flex" alignItems="center">
                  <span>{props.data.label}</span>{' '}
                  <Box mx={1} display="flex">
                    {props.data.value === currentActiveKMS && (
                      <YBTag type={YBTag_Types.YB_GRAY}>{t('activeKMS')}</YBTag>
                    )}
                  </Box>
                </Box>
              </components.Option>
            );
          },
          SingleValue: ({ data }: { data: any }) => {
            if (isEncryptedBackup && data.value === kmsIdDuringBackup?.value) {
              return (
                <>
                  <span className="storage-cfg-name">{data.label}</span> &nbsp;
                  <YBTag type={YBTag_Types.YB_GRAY}>{t('usedDuringBackup')}</YBTag>
                </>
              );
            }
            return data.label;
          },
          ...reactSelectComp
        }}
      />
      <div className={classes.kmsHelperText}>
        <Trans i18nKey="kmsConfigHelpText" t={t} components={{ b: <b /> }} />
      </div>
    </div>
  );
};

export default SelectKMS;
