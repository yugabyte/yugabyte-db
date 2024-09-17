/* eslint-disable react/display-name */
/*
 * Created on Wed Jul 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useEffect, useImperativeHandle } from 'react';
import { useForm } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { yupResolver } from '@hookform/resolvers/yup';
import { components } from 'react-select';

import { FormLabel, makeStyles } from '@material-ui/core';
import { YBInputField } from '../../../../../../components';
import { YBLoading } from '../../../../../../../components/common/indicators';
import { YBTag, YBTag_Types } from '../../../../../../../components/common/YBTag';
import { YBReactSelectField } from '../../../../../../../components/configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import ParallelThreadsField from './ParallelThreadsField';
import MultiRegionNodesSupport from './MultiRegionNodesSupport';

import {
  Page,
  PageRef,
  ScheduledBackupContext,
  ScheduledBackupContextMethods
} from '../../models/ScheduledBackupContext';
import { GeneralSettingsModel } from '../../models/IGeneralSettings';
import { fetchStorageConfigs } from '../../../../../../../components/backupv2/common/BackupAPI';
import { groupStorageConfigs } from '../../../ScheduledBackupUtils';
import { getValidationSchema } from './GeneralSettingsValidation';
import { ReactSelectComponents, ReactSelectStyles } from '../../ReactSelectStyles';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '24px',
    display: 'flex',
    gap: '24px',
    flexDirection: 'column',
    '& .MuiFormLabel-root': {
      fontSize: '13px',
      textTransform: 'capitalize',
      color: theme.palette.ybacolors.labelBackground,
      fontWeight: 400
    }
  },
  policyName: {
    width: '320px'
  },
  storageConfig: {
    width: '550px',
    border: `1px solid ${theme.palette.ybacolors.ybGray}`,
    borderRadius: '8px'
  },
  error: {
    color: `${theme.palette.error[500]} !important`,
    borderColor: `${theme.palette.error[500]} !important`,
    backgroundColor: `${theme.palette.error[100]} !important`
  },
  control: {
    borderColor: `${theme.palette.ybacolors.ybBorderGray} !important`,
    boxShadow: 'none !important'
  },
  tag: {
    minWidth: 0
  }
}));

const GeneralSettings = forwardRef<PageRef>((_, forwardRef) => {
  const [scheduledBackupContext, { setGeneralSettings, setPage, setDisableSubmit }] = (useContext(
    ScheduledBackupContext
  ) as unknown) as ScheduledBackupContextMethods;

  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.generalSettings'
  });

  const { data: storageConfigs, isLoading } = useQuery('storageConfig', fetchStorageConfigs, {
    select: (data) => groupStorageConfigs(data.data)
  });

  const {
    formData: { generalSettings }
  } = scheduledBackupContext;

  const {
    control,
    handleSubmit,
    setValue,
    formState: { errors, isValid }
  } = useForm<GeneralSettingsModel>({
    resolver: yupResolver(getValidationSchema(scheduledBackupContext, t)),
    defaultValues: {
      ...generalSettings
    }
  });

  useEffect(() => {
    setDisableSubmit(!isValid);
  }, [isValid]);

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        handleSubmit((values) => {
          setGeneralSettings(values);
          setPage(Page.BACKUP_OBJECTS);
        })();
      },
      onPrev: () => {}
    }),
    []
  );

  const reactSelecctComp = ReactSelectComponents(!!errors.storageConfig?.message);

  if (isLoading)
    return (
      <YBLoading
        text={t('loadingMsg.fetchingStorageConfig', { keyPrefix: 'backup.scheduled.create' })}
      />
    );

  return (
    <div className={classes.root}>
      <YBInputField
        control={control}
        name="scheduleName"
        label={t('policyName')}
        helperText={t('helpText')}
        className={classes.policyName}
        placeholder={t('policyNamePlaceholder')}
        data-testid="scheduleName"
      />

      <div>
        <FormLabel>{t('storageConfig')}</FormLabel>
        <YBReactSelectField
          options={storageConfigs as any}
          control={control}
          name="storageConfig"
          placeholder={t('storageConfigPlaceholder')}
          defaultValue={generalSettings.storageConfig}
          onChange={(e) => setValue('storageConfig', e as any, { shouldValidate: true })}
          isClearable
          stylesOverride={ReactSelectStyles}
          data-testid="storageConfig"
          components={{
            SingleValue: ({ data }: { data: any }) => (
              <>
                <span>{data.label}</span>
                <YBTag type={YBTag_Types.YB_GRAY} className={classes.tag}>
                  {data.name}
                </YBTag>
                {data.regions?.length > 0 && (
                  <YBTag type={YBTag_Types.YB_GRAY}>{t('multiRegionSupport')}</YBTag>
                )}
              </>
            ),
            Option: (props: any) => {
              return (
                <components.Option {...props}>
                  <div>{props.data.label}</div>
                  <div>
                    <YBTag type={YBTag_Types.YB_GRAY} className={classes.tag}>
                      {props.data.name}
                    </YBTag>
                    {props.data.regions?.length > 0 && (
                      <YBTag type={YBTag_Types.YB_GRAY} className={classes.tag}>
                        {t('multiRegionSupport')}
                      </YBTag>
                    )}
                  </div>
                </components.Option>
              );
            },

            ...reactSelecctComp
          }}
        />
      </div>
      <MultiRegionNodesSupport control={control} />
      <ParallelThreadsField control={control} />
    </div>
  );
});

GeneralSettings.displayName = 'GeneralSettings';
export default GeneralSettings;
