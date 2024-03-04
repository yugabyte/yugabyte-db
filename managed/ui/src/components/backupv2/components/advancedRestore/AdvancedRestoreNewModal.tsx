/*
 * Created on Tue Jan 02 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

/**
 * AdvancedRestoreNewModal is a wrapper over "BackupRestoreNewModal.tsx" to keep the logic same.
 * we control the inner BackupRestoreNewModal by playing with it's context.
 */

import { FC, useCallback, useEffect, useRef } from 'react';
import { useMethods, useMount } from 'react-use';
import { useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import AdvancedRestoreSwitchPages from './AdvancedRestoreSwitchPages';
import { YBButton, YBModal } from '../../../../redesign/components';
import { getBackButDisableState } from '../restore/RestoreUtils';
import {
  AdvancedRestoreContext,
  AdvancedRestoreFormContext,
  PageRef,
  initialAdvancedRestoreContextState
} from './AdvancedRestoreContext';
import { AdvancedRestoreMethods } from './AdvancedRestoreContext';
import {
  RestoreFormContext,
  initialRestoreContextState,
  restoreMethods
} from '../restore/RestoreContext';

type BackupRestoreNewModalProps = {
  visible: boolean;
  onHide: () => void;
  currentUniverseUUID: string;
};

const useStyles = makeStyles(() => ({
  root: {
    padding: '16px 16px'
  }
}));

export const AdvancedRestoreNewModal: FC<BackupRestoreNewModalProps> = ({
  visible,
  currentUniverseUUID,
  onHide
}) => {

  const methods = useMethods(restoreMethods, initialRestoreContextState);

  const advancedRestoreContextData = useMethods(
    AdvancedRestoreMethods,
    initialAdvancedRestoreContextState
  );

  const [
    {
      formProps: { disableSubmit, submitLabel, isSubmitting, currentPage }
    },
    { moveToNextPage, moveToPrevPage, setDisableSubmit, setSubmitLabel, setisSubmitting }
  ] = advancedRestoreContextData;

  const [baseRestoreContext, baseRestoreMethods] = methods;

  const {formData: { generalSettings }} = baseRestoreContext;
  const { saveGeneralSettingsFormData, moveToPage } = baseRestoreMethods;

  const currentPageRef = useRef<PageRef>(null);

  const { t } = useTranslation('translation', {
    keyPrefix: 'advancedRestoreModal'
  });

  const classes = useStyles();

  // update the innerContext so that the stepper display correctly
  useEffect(() => {
    moveToPage(currentPage);
  }, [currentPage]);

  useMount(() => {
    saveGeneralSettingsFormData({
      ...generalSettings,
      targetUniverse: {
        label: '',
        value: currentUniverseUUID
      }
    });
  });

  // override base restore form methods
  const overridedMethods = useCallback(() => {
    return [
      baseRestoreContext,
      {
        ...baseRestoreMethods,
        moveToNextPage: () => {
          moveToNextPage(baseRestoreContext);
        },
        moveToPrevPage: () => {
          moveToPrevPage(baseRestoreContext);
        },
        setDisableSubmit,
        setisSubmitting,
        setSubmitLabel
      },
      {
        hideModal: onHide
      }
    ];
  }, [baseRestoreContext, baseRestoreContext]);

  const getCancelLabel = () => {
    if (
      currentPage === 'PREFETCH_CONFIGS' ||
      (currentPage === 'GENERAL_SETTINGS' && !isSubmitting)
    ) {
      return undefined;
    }
    return isSubmitting
      ? t('waitingMsg', { keyPrefix: 'newRestoreModal' })
      : t('back', { keyPrefix: 'common' });
  };

  return (

    <AdvancedRestoreFormContext.Provider
      value={
        ([
          ...advancedRestoreContextData,
          { hideModal: onHide }
        ] as unknown) as AdvancedRestoreContext
      }
    >

      <RestoreFormContext.Provider value={(overridedMethods() as unknown) as any}>
        <YBModal
          open={visible}
          style={{
            position: 'fixed',
            zIndex: 99999
          }}
          isSubmitting={isSubmitting}
          buttonProps={{
            primary: {
              disabled: disableSubmit
            },
            secondary: {
              disabled: getBackButDisableState(currentPage),
              onClick: () => {
                currentPageRef.current?.onPrev();
              },
              variant: isSubmitting ? 'ghost' : 'secondary'
            }
          }}
          overrideWidth={'1100px'}
          overrideHeight={'990px'}
          size="xl"
          submitLabel={submitLabel}
          title={t('title')}
          cancelLabel={getCancelLabel()}
          dialogContentProps={{
            dividers: true,
            className: classes.root
          }}
          enableBackdropDismiss
          onSubmit={() => {
            currentPageRef.current?.onNext();
          }}
          onClose={() => {
            onHide();
          }}
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
          <AdvancedRestoreSwitchPages ref={currentPageRef} />
        </YBModal>
      </RestoreFormContext.Provider>
    </AdvancedRestoreFormContext.Provider>
  );
};
