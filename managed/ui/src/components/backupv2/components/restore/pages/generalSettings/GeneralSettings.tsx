/*
 * Created on Thu Jun 15 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useCallback, useContext, useEffect, useImperativeHandle } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { noop } from 'lodash';
import { useQuery } from 'react-query';
import { makeStyles } from '@material-ui/core';
import { toast } from 'react-toastify';
import { yupResolver } from '@hookform/resolvers/yup';
import BackupInfoBanner from '../../common/BackupInfoBanner';
import ChooseUniverseConfig from './ChooseUniverseConfig';
import SelectKeyspaceConfig from './SelectKeyspaceConfig';
import ParallelThreadsConfig from './ParallelThreadsConfig';
import { PageRef, RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import { SelectTablesConfig } from './SelectTablesConfig';
import { BackupRestoreStepper } from '../../common/BackupRestoreStepper';
import { getPreflightCheck } from '../../api';
import { isDefinedNotNull } from '../../../../../../utils/ObjectUtils';
import { getValidationSchema } from './ValidationSchema';
import { fetchTablesInUniverse } from '../../../../../../actions/xClusterReplication';
import { TableType } from '../../../../../../redesign/helpers/dtos';
import { isDuplicateKeyspaceExistsinUniverse } from '../../RestoreUtils';
import { IncrementalBackupProps } from '../../../BackupDetails';
import { useTranslation } from 'react-i18next';

type ReactSelectOption = { label: string; value: string } | null;

export type IGeneralSettings = {
  targetUniverse: ReactSelectOption;
  kmsConfig: ReactSelectOption;
  renameKeyspace: boolean;
  forceKeyspaceRename: boolean;
  tableSelectionType: 'ALL_TABLES' | 'SUBSET_OF_TABLES';
  parallelThreads: number;
  selectedKeyspace: ReactSelectOption;
  incrementalBackupProps: IncrementalBackupProps;
};

const useStyles = makeStyles((theme) => ({
  root: {
    '& section': {
      marginBottom: theme.spacing(4)
    },
    '& .yb-field-group': {
      paddingBottom: 0
    }
  },
  form: {
    '& > section': {
      marginTop: theme.spacing(4),
      maxWidth: '650px'
    },
    '& .MuiInputLabel-root, .form-item-label': {
      textTransform: 'unset',
      fontWeight: 400,
      fontSize: '13px',
      color: theme.palette.ybacolors.labelBackground,
      marginBottom: theme.spacing(1)
    }
  },
  stepper: {
    marginBottom: theme.spacing(3)
  }
}));

// eslint-disable-next-line react/display-name
export const GeneralSettings = React.forwardRef<PageRef>((_, forwardRef) => {
  const [
    restoreContext,
    {
      saveGeneralSettingsFormData,
      moveToNextPage,
      savePreflightResponse,
      setSubmitLabel,
      setDisableSubmit
    }
  ]: RestoreContextMethods = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;

  const {
    formData: { generalSettings, preflightResponse },
    backupDetails
  } = restoreContext;

  const methods = useForm<IGeneralSettings>({
    defaultValues: generalSettings ?? {},
    resolver: yupResolver(getValidationSchema(restoreContext))
  });

  const { handleSubmit, watch } = methods;

  const classes = useStyles();
  const { t } = useTranslation();

  const saveValues = useCallback(
    (val: IGeneralSettings) => {
      saveGeneralSettingsFormData(val);
      moveToNextPage();
    },
    [saveGeneralSettingsFormData, moveToNextPage]
  );

  // when the modal's submit button is clicked , save the form values.
  const onNext = useCallback(() => handleSubmit(saveValues)(), [handleSubmit, saveValues]);

  useImperativeHandle(forwardRef, () => ({ onNext, onPrev: noop }), [onNext]);

  const targetUniverseUUID = watch('targetUniverse')?.value;

  useEffect(() => {
    if (targetUniverseUUID === generalSettings?.targetUniverse?.value) return;
    methods.setValue('forceKeyspaceRename', false);
    methods.setValue('renameKeyspace', false);
  }, [targetUniverseUUID]);

  // send the preflight api request , when the user choses the universe
  const { isSuccess } = useQuery(
    ['backup', 'preflight', targetUniverseUUID],
    () =>
      getPreflightCheck({
        backupUUID: backupDetails!.commonBackupInfo.backupUUID,
        storageConfigUUID: backupDetails!.commonBackupInfo.storageConfigUUID,
        universeUUID: targetUniverseUUID!,
        backupLocations: backupDetails!.commonBackupInfo.responseList.map((r) => r.defaultLocation!)
      }),
    {
      enabled: isDefinedNotNull(targetUniverseUUID) && isDefinedNotNull(backupDetails),
      onSuccess(data) {
        savePreflightResponse(data);
        setDisableSubmit(false);
      },
      onError: () => {
        toast.error('Preflight check failed!.');
        setDisableSubmit(true);
      }
    }
  );

  // we do duplicate check only for YSQL and when the preflight check is finished
  const enableVerifyDuplicateTable =
    isDefinedNotNull(targetUniverseUUID) &&
    backupDetails?.backupType === TableType.PGSQL_TABLE_TYPE &&
    isSuccess &&
    isDefinedNotNull(preflightResponse);

  useQuery(
    ['tables', targetUniverseUUID, preflightResponse],
    () => fetchTablesInUniverse(targetUniverseUUID),
    {
      enabled: enableVerifyDuplicateTable,
      onSuccess: (resp) => {
        if (isDuplicateKeyspaceExistsinUniverse(preflightResponse, resp.data)) {
          methods.setValue('forceKeyspaceRename', true);
          methods.setValue('renameKeyspace', true);
        }
      }
    }
  );

  // disables the submit button to prevent the user from moving to next page,
  // till the preflight check is finished.
  useEffect(() => {
    setDisableSubmit(true);
  }, [setDisableSubmit, targetUniverseUUID]);

  useEffect(() => {
    setDisableSubmit(!isSuccess);
  }, [isSuccess, setDisableSubmit]);

  const renameKeyspace = watch('renameKeyspace');
  const tableSelectionType = watch('tableSelectionType');

  // if the user chooses rename keyspaces, or "subset of tables",
  // change the modal's submit button to 'Next'
  useEffect(() => {
    if (renameKeyspace || tableSelectionType === 'SUBSET_OF_TABLES') {
      setSubmitLabel(t('newRestoreModal.generalSettings.next'));
    } else {
      setSubmitLabel(t('newRestoreModal.generalSettings.restore'));
    }
  }, [renameKeyspace, setSubmitLabel, tableSelectionType, t]);

  return (
    <div className={classes.root}>
      <BackupRestoreStepper className={classes.stepper} />
      <section>
        <div>
          <BackupInfoBanner />
        </div>
      </section>
      <FormProvider {...methods}>
        <form className={classes.form}>
          <section>
            <ChooseUniverseConfig />
          </section>
          <section>
            <SelectKeyspaceConfig />
          </section>
          <section>
            <SelectTablesConfig />
          </section>
          {backupDetails?.category === 'YB_BACKUP_SCRIPT' && (
            <section>
              {' '}
              <ParallelThreadsConfig />
            </section>
          )}
        </form>
      </FormProvider>
    </div>
  );
});
