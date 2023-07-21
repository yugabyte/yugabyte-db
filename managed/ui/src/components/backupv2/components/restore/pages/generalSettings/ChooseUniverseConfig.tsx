/*
 * Created on Tue Jun 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useContext, useEffect } from 'react';
import Select from 'react-select';
import { Trans, useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { Typography, makeStyles } from '@material-ui/core';
import { components } from 'react-select';
import { useQuery } from 'react-query';
import { IGeneralSettings } from './GeneralSettings';
import { fetchUniversesList } from '../../../../../../actions/xClusterReplication';
import { IUniverse } from '../../../../common/IBackup';
import { RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import { Badge_Types, StatusBadge } from '../../../../../common/badge/StatusBadge';
import { getKMSConfigs } from '../../../../common/BackupAPI';
import { YBLabel } from '../../../../../common/descriptors';
import { isYBCEnabledInUniverse } from '../../RestoreUtils';

const useStyles = makeStyles((theme) => ({
  root: {},
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

const ChooseUniverseConfig = () => {
  const { t } = useTranslation();
  const { control, watch, setError, clearErrors } = useFormContext<IGeneralSettings>();
  const classes = useStyles();
  const [{ backupDetails }] = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;

  const { data: universeList, isLoading: isUniverseListLoading } = useQuery(['universe'], () =>
    fetchUniversesList().then((res) => res.data as IUniverse[])
  );

  const { data: kmsConfigs } = useQuery(['kms_configs'], () => getKMSConfigs());

  let sourceUniverseNameAtFirst: IUniverse[] = [];

  // display source universe at top
  if (universeList && universeList.length > 0) {
    sourceUniverseNameAtFirst = [...universeList.filter((u) => u.universeUUID)];
    const sourceUniverseIndex = universeList.findIndex(
      (u) => u.universeUUID === backupDetails?.universeUUID
    );
    if (sourceUniverseIndex) {
      const sourceUniverse = sourceUniverseNameAtFirst.splice(sourceUniverseIndex, 1);
      sourceUniverseNameAtFirst.unshift(sourceUniverse[0]);
    }
    sourceUniverseNameAtFirst = sourceUniverseNameAtFirst.filter(
      (u) => !u.universeDetails.universePaused
    );
  }

  const kmsConfigList = kmsConfigs
    ? kmsConfigs.map((config: any) => {
        const labelName = config.metadata.provider + ' - ' + config.metadata.name;
        return { value: config.metadata.configUUID, label: labelName };
      })
    : [];

  const targetUniverse = watch('targetUniverse')?.value;

  useEffect(() => {
    if (targetUniverse) {
      if (
        backupDetails?.category === 'YB_CONTROLLER' &&
        !isYBCEnabledInUniverse(universeList ?? [], targetUniverse)
      ) {
        setError('targetUniverse', {
          type: 'custom',
          message: t(
            'newRestoreModal.generalSettings.universeSelection.validationMessages.ybcNotEnabled'
          )
        });
        return;
      }
    }
    clearErrors('targetUniverse');
  }, [targetUniverse, t, setError, universeList, backupDetails, clearErrors]);

  return (
    <div className={classes.root}>
      <Typography variant="body1">
        {t('newRestoreModal.generalSettings.universeSelection.title')}
      </Typography>
      <div className={classes.controls}>
        <Controller
          control={control}
          name="targetUniverse"
          render={({ field: { value, onChange }, fieldState: { error } }) => (
            <YBLabel
              label={t('newRestoreModal.generalSettings.universeSelection.selectTargetUniverse')}
              meta={{
                touched: !!error?.message,
                error: error?.message
              }}
            >
              <Select
                options={sourceUniverseNameAtFirst?.map((universe: IUniverse) => {
                  return {
                    label: universe.name,
                    value: universe.universeUUID
                  };
                })}
                onChange={onChange}
                value={value}
                styles={{
                  singleValue: (props: any) => {
                    return { ...props, display: 'flex' };
                  }
                }}
                isLoading={isUniverseListLoading}
                isClearable
                components={{
                  // eslint-disable-next-line react/display-name
                  Option: (props: any) => {
                    if (props.data.value === backupDetails?.universeUUID) {
                      return (
                        <components.Option {...props} className={classes.sourceUniverseLabel}>
                          {props.data.label}{' '}
                          <StatusBadge
                            statusType={Badge_Types.DELETED}
                            customLabel="Backup Source"
                          />
                        </components.Option>
                      );
                    }
                    return <components.Option {...props} />;
                  },
                  SingleValue: ({ data }: { data: any }) => {
                    if (data.value === backupDetails?.universeUUID) {
                      return (
                        <>
                          <span>{data.label}</span> &nbsp;
                          <StatusBadge
                            statusType={Badge_Types.DELETED}
                            customLabel={'Backup Source'}
                          />
                        </>
                      );
                    }
                    return data.label;
                  }
                }}
              />
            </YBLabel>
          )}
        />
        <Controller
          control={control}
          name="kmsConfig"
          render={({ field: { value, onChange } }) => (
            <YBLabel
              label={t('newRestoreModal.generalSettings.universeSelection.selectKMSConfig')}
              classOverrides={classes.kmsConfig}
            >
              <Select options={kmsConfigList} value={value} onChange={onChange} />
              <span className={classes.kmsHelpText}>
                <Trans
                  i18nKey="newRestoreModal.generalSettings.universeSelection.kmsConfigHelpText"
                  components={{ b: <b /> }}
                />
              </span>
            </YBLabel>
          )}
        />
      </div>
    </div>
  );
};

export default ChooseUniverseConfig;
