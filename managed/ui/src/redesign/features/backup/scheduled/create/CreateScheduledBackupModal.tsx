/*
 * Created on Wed Jul 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useRef } from 'react';
import { makeStyles } from '@material-ui/core';
import { useMethods } from 'react-use';
import { YBButton, YBModal } from '../../../../components';
import { useTranslation } from 'react-i18next';

import {
  initialScheduledBackupContextState,
  Page,
  PageRef,
  ScheduledBackupContext,
  scheduledBackupMethods
} from './models/ScheduledBackupContext';
import SwitchScheduledBackupPages from './SwitchScheduledBackupPages';
import { YBStepper } from '../../../../components/YBStepper/YBStepper';

interface CreateScheduledBackupModalProps {
  visible: boolean;
  onHide: () => void;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: 0
  },
  stepper: {
    padding: '16px 24px',
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  }
}));

const CreateScheduledBackupModal: FC<CreateScheduledBackupModalProps> = ({ visible, onHide }) => {
  const classes = useStyles();
  const currentPageRef = useRef<PageRef>(null);

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create'
  });

  const scheduledBackupContextData = useMethods(
    scheduledBackupMethods,
    initialScheduledBackupContextState
  );

  const [
    {
      formProps: { currentPage, isSubmitting, disableSubmit }
    },
    { reset }
  ] = scheduledBackupContextData;

  const resetContextAndHide = () => {
    reset();
    onHide();
  };

  const pages: Record<keyof typeof Page, string> = {
    GENERAL_SETTINGS: t('stepper.generalSettings'),
    BACKUP_OBJECTS: t('stepper.backupObjects'),
    BACKUP_FREQUENCY: t('stepper.backupFrequency'),
    BACKUP_SUMMARY: t('stepper.backupSummary')
  };

  const getSubmitLabel = () => {
    return currentPage === Page.BACKUP_SUMMARY ? t('createPolicy') : t('next');
  };

  return (
    <ScheduledBackupContext.Provider
      value={
        ([
          ...scheduledBackupContextData,
          { hideModal: resetContextAndHide }
        ] as unknown) as ScheduledBackupContext
      }
    >
      <YBModal
        open={visible}
        style={{
          position: 'fixed',
          zIndex: 99999
        }}
        isSubmitting={isSubmitting}
        cancelLabel="Prev"
        buttonProps={{
          primary: {
            disabled: disableSubmit
          },
          secondary: {
            onClick: () => {
              currentPageRef.current?.onPrev();
            },
            disabled: currentPage === Page.GENERAL_SETTINGS,
            variant: isSubmitting ? 'ghost' : 'secondary'
          }
        }}
        overrideWidth={'1000px'}
        overrideHeight={'880px'}
        size="xl"
        submitLabel={getSubmitLabel()}
        title={t('title')}
        dialogContentProps={{
          dividers: true,
          className: classes.root
        }}
        enableBackdropDismiss={true}
        onSubmit={() => {
          currentPageRef.current?.onNext();
        }}
        onClose={resetContextAndHide}
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
      >
        <div className={classes.stepper}>
          <YBStepper steps={pages} currentStep={currentPage} />
        </div>
        <SwitchScheduledBackupPages ref={currentPageRef} />
      </YBModal>
    </ScheduledBackupContext.Provider>
  );
};

export default CreateScheduledBackupModal;
