/*
 * Created on Tue Jun 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useRef } from 'react';
import { useMethods, useMount } from 'react-use';
import { useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import SwitchRestoreContextPages from './SwitchRestoreContextPages';
import { YBButton, YBModal } from '../../../../redesign/components';
import { PageRef, RestoreContext, RestoreFormContext, initialRestoreContextState, restoreMethods } from './RestoreContext';
import { IBackup } from '../../common/IBackup';
import { IncrementalBackupProps } from '../BackupDetails';
import { getBackButDisableState } from './RestoreUtils';
import './BackupRestoreNewModal.scss';

/**
 * Backup restore modal switches through pages to display the content. 
 * The datas are saved in the context.
 * SwitchRestoreContextPages - Swithches between the pages.
 * The pages are:
 *  1. Prefetch - prefetch the configs needed for the modal like incremental backups etc.
 *  2. General Settings - The user can choose the universe to restore to , kms configs , etc.
 *  3. Rename Keyspace (Optional) - The user can rename the keyspace
 *  4. Select table (Optional) - The user can select the tables to restore in case of YCQL
 *  5. Restore Final - Page which computes the restore payload and send the API request.
 * 
 * The current page ref's which is displayed is stored in 'currentPageRef'.
 * The Pages will have on 'onNext' function implemented and forwards it as ref to the parent component.
 * whenever the submit button on the modal is clicked, the current component's onNext function is called.
 * The page can use this function to trigger the form submit or move to the next page using 'moveToNextPage' fn.
 */

type BackupRestoreNewModalProps = {
    backupDetails: IBackup;
    visible: boolean;
    onHide: () => void;
    incrementalBackupProps?: IncrementalBackupProps;
}


const useStyles = makeStyles((theme) => ({
    root: {
        padding: theme.spacing(2),
    }
}));

const BackupRestoreNewModal: FC<BackupRestoreNewModalProps> = ({ backupDetails, visible, onHide, incrementalBackupProps }) => {

    const restoreContextData = useMethods(restoreMethods, initialRestoreContextState);
    const currentPageRef = useRef<PageRef>(null);

    const [{ formData: { generalSettings }, formProps: { disableSubmit, submitLabel, currentPage } }, { setBackupDetails, saveGeneralSettingsFormData, moveToPrevPage }] = restoreContextData;

    const { t } = useTranslation();
    const classes = useStyles();


    useMount(() => {
        // save props to the context
        setBackupDetails(backupDetails);
        saveGeneralSettingsFormData({
            ...generalSettings,
            incrementalBackupProps
        });

    });

    return (
        <RestoreFormContext.Provider value={[...restoreContextData, { hideModal: onHide }] as unknown as RestoreContext}>
            <YBModal
                open={visible}
                style={{
                    position: 'fixed',
                    zIndex: 99999
                }}
                buttonProps={{
                    primary: {
                        disabled: disableSubmit
                    },
                    secondary: {
                        disabled: getBackButDisableState(currentPage),
                        onClick: () => {
                            currentPageRef.current?.onPrev();
                        }
                    }
                }}
                overrideWidth={'1100px'}
                overrideHeight={'990px'}
                size="xl"
                submitLabel={submitLabel}

                title={t('newRestoreModal.title')}
                cancelLabel={t('common.back')}
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
                
                actionsInfo={<YBButton
                    variant='secondary'
                    onClick={() => {
                        onHide();
                    }}
                >{t('common.cancel')}
                </YBButton>
                }
            >
                <SwitchRestoreContextPages ref={currentPageRef} />
            </YBModal>
        </RestoreFormContext.Provider>
    );
};

export default BackupRestoreNewModal;
