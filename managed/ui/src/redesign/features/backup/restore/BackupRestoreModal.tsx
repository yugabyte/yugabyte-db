/*
 * Created on Mon Aug 19 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useRef } from 'react';
import { useMethods, useMount } from 'react-use';
import { useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { makeStyles } from '@material-ui/core';
import { YBButton, YBModal } from '../../../components';
import {
  defaultRestoreFormValues,
  initialRestoreContextState,
  Page,
  PageRef,
  RestoreContext,
  RestoreFormContext,
  restoreMethods
} from './models/RestoreContext';
import RestoreSummary from './pages/RestoreSummary';
import { YBStepper } from '../../../components/YBStepper/YBStepper';
import SwitchRestorePages from './SwitchRestorePages';
import { validationSchemaResolver } from './ValidationSchemaResolver';
import { RestoreFormModel } from './models/RestoreFormModel';
import { IBackup } from '../../../../components/backupv2';
import { IncrementalBackupProps } from '../../../../components/backupv2/components/BackupDetails';

const useStyles = makeStyles((theme) => ({
  root: {
    display: 'flex',
    height: '100%',
    '& .MuiFormLabel-root': {
      fontSize: '13px',
      textTransform: 'capitalize',
      color: theme.palette.ybacolors.labelBackground,
      fontWeight: 700,
      marginBottom: '16px'
    }
  },
  modalRoot: {
    padding: 0
  },
  configs: {
    width: '800px'
  },
  summary: {
    width: 'fit-content',
    flexGrow: 1
  },
  stepper: {
    padding: '16px 24px',
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  '@global': {
    '.Toastify__toast-container': {
      zIndex: 100000
    }
  }
}));

type BackupRestoreNewModalProps = {
  backupDetails: IBackup;
  visible: boolean;
  onHide: () => void;
  incrementalBackupProps?: IncrementalBackupProps;
};

const BackupRestoreModal: FC<BackupRestoreNewModalProps> = ({ visible, backupDetails, incrementalBackupProps, onHide }) => {
  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore'
  });

  const currentPageRef = useRef<PageRef>(null);

  const restoreContextData = useMethods(restoreMethods, initialRestoreContextState);

  const [
    {
      formProps: { disableSubmit, currentPage, isSubmitting, submitLabel }
    },
    { setBackupDetails, setIncrementalBackupProps }
  ] = restoreContextData;

  const formMethods = useForm<RestoreFormModel>({
    defaultValues: defaultRestoreFormValues,
    resolver: (data) => validationSchemaResolver(restoreContextData[0], data, t)
  });

  const pages: Partial<Record<keyof typeof Page, string>> = {
    [Page.SOURCE]: t('pages.selectKeyspaceConfigs'),
    [Page.TARGET]: t('pages.selectUniverse')
  };

  useMount(() => {
    // save props to the context
    setBackupDetails(backupDetails);
    setIncrementalBackupProps(incrementalBackupProps);
  });

  const shouldRenameKeyspace = formMethods.watch('target.renameKeyspace');

  // Add the rename keyspace page if the user has selected to rename the keyspace
  if (shouldRenameKeyspace) {
    pages[Page.RENAME_KEYSPACES] = t('pages.renameKeyspaces');
  } else {
    delete pages[Page.RENAME_KEYSPACES];
  }

  const getCancelLabel = () => {
    if (
      currentPage === Page.PREFETCH_DATA ||
      (currentPage === Page.SOURCE && !isSubmitting)
    ) {
      return undefined;
    }
    return isSubmitting ? t('buttonLabels.waitingMsg', { keyPrefix: 'backup' }) : t('back', { keyPrefix: 'common' });
  };

  return (
    <RestoreFormContext.Provider
      value={([...restoreContextData, { hideModal: onHide }] as unknown) as RestoreContext}
    >
      <YBModal
        open={visible}
        onClose={onHide}
        style={{
          position: 'fixed',
          zIndex: 99999
        }}
        overrideWidth={'1100px'}
        overrideHeight={'880px'}
        isSubmitting={isSubmitting}
        buttonProps={{
          primary: {
            disabled: disableSubmit
          },
          secondary: {
            disabled: isSubmitting,
            onClick: () => {
              currentPageRef.current?.onPrev();
            },
            variant: isSubmitting ? 'ghost' : 'secondary'
          }
        }}
        size="xl"
        title={t('title')}
        dialogContentProps={{
          dividers: true,
          className: classes.modalRoot
        }}
        enableBackdropDismiss
        footerAccessory={
          <YBButton
            variant="secondary"
            onClick={() => {
              onHide();
            }}
          >
            {t('cancel', { keyPrefix: 'common' })}
          </YBButton>
        }
        submitLabel={submitLabel}
        cancelLabel={getCancelLabel()}
        onSubmit={() => {
          currentPageRef.current?.onNext();
        }}
      >
        <FormProvider {...formMethods}>
          <div className={classes.root}>
            <div className={classes.configs}>
              <div className={classes.stepper}>
                <YBStepper steps={pages} currentStep={currentPage} />
              </div>
              <SwitchRestorePages ref={currentPageRef} />
            </div>
            <div className={classes.summary}>
              <RestoreSummary />
            </div>
          </div>
        </FormProvider>
      </YBModal>
    </RestoreFormContext.Provider>
  );
};

export default BackupRestoreModal;
