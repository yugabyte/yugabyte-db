/*
 * Created on Thu Jun 15 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useCallback, useContext, useImperativeHandle } from 'react';
import { Control, FieldValues, useForm } from 'react-hook-form';
import { useMount } from 'react-use';
import { yupResolver } from '@hookform/resolvers/yup';
import { useTranslation } from 'react-i18next';
import { get, has } from 'lodash';
import { makeStyles } from '@material-ui/core';
import { PageRef, RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import { YBTable } from './YBTable';
import { BackupRestoreStepper } from '../../common/BackupRestoreStepper';
import { getTablesFromPreflighResponse } from '../../RestoreUtils';
import { getValidationSchema } from './ValidationSchema';
import { YBLabel } from '../../../../../common/descriptors';

export type ISelectTables = {
  selectedTables: string[];
};

const useStyles = makeStyles((theme) => ({
  stepper: {
    marginBottom: theme.spacing(3)
  }
}));

// eslint-disable-next-line react/display-name
export const SelectTables = React.forwardRef<PageRef>((_, forwardRef) => {
  const restoreContext = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;

  const [
    {
      formData: { selectedTables, preflightResponse }
    },
    { saveSelectTablesFormData, moveToNextPage, moveToPrevPage, setSubmitLabel }
  ] = restoreContext;

  const { t } = useTranslation();
  const classes = useStyles();

  const methods = useForm<ISelectTables>({
    defaultValues: selectedTables,
    resolver: yupResolver(getValidationSchema(restoreContext[0], t))
  });

  const {
    handleSubmit,
    control,
    setValue,
    getValues,
    formState: { errors }
  } = methods;

  const saveValues = useCallback(
    (val: ISelectTables) => {
      saveSelectTablesFormData(val);
      moveToNextPage();
    },
    [saveSelectTablesFormData, moveToNextPage]
  );

  const onNext = useCallback(() => handleSubmit(saveValues)(), [handleSubmit, saveValues]);

  const onPrev = useCallback(() => {
    saveSelectTablesFormData(getValues());
    moveToPrevPage();
  }, [saveSelectTablesFormData, getValues, moveToPrevPage]);

  useImperativeHandle(forwardRef, () => ({ onNext, onPrev }), [onNext]);

  useMount(() => {
    setSubmitLabel(t('newRestoreModal.generalSettings.restore'));
  });

  const tablesInPreflight = getTablesFromPreflighResponse(preflightResponse!);

  return (
    <form>
      <BackupRestoreStepper className={classes.stepper} />
      {has(errors, 'selectedTables') && (
        <YBLabel
          meta={{
            touched: !!get(errors, 'selectedTables.message'),
            error: get(errors, 'selectedTables.message')
          }}
        ></YBLabel>
      )}
      <YBTable
        name="selectedTables"
        defaultValues={selectedTables.selectedTables}
        table={tablesInPreflight}
        tableHeader={[t('newRestoreModal.selectTables.table')]}
        control={(control as unknown) as Control<FieldValues>}
        setValue={(table) => {
          setValue('selectedTables', table);
        }}
      />
    </form>
  );
});
