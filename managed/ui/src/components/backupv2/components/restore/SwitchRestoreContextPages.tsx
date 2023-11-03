/*
 * Created on Mon Jul 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */


import React, { useContext, useImperativeHandle, useRef } from 'react';
import RenameKeyspace from './pages/renameKeyspace/RenameKeyspace';
import RestoreFinalStep from './pages/RestoreFinalStep';
import PrefetchConfigs from './pages/prefetch/PrefetchConfigs';
import { PageRef, RestoreContextMethods, RestoreFormContext } from './RestoreContext';
import { GeneralSettings } from './pages/generalSettings/GeneralSettings';
import { SelectTables } from './pages/selectTables/TablesSelect';

type Props = {}

//rotates the pages in the modal
// eslint-disable-next-line react/display-name
const SwitchRestoreContextPages = React.forwardRef((_props: Props, forwardRef) => {

    const [{ formProps }] = useContext(RestoreFormContext) as unknown as RestoreContextMethods;
    const currentComponentRef = useRef<PageRef>(null);

    useImperativeHandle(forwardRef, () => currentComponentRef.current, [currentComponentRef.current]); // eslint-disable-line react-hooks/exhaustive-deps

    const getCurrentComponent = () => {
        switch (formProps.currentPage) {
            case 'PREFETCH_CONFIGS':
                return <PrefetchConfigs ref={currentComponentRef} />;
            case 'GENERAL_SETTINGS':
                return <GeneralSettings ref={currentComponentRef} />;
            case 'SELECT_TABLES':
                return <SelectTables ref={currentComponentRef} />;
            case 'RENAME_KEYSPACES':
                return <RenameKeyspace ref={currentComponentRef} />;
            case 'RESTORE_FINAL':
                return <RestoreFinalStep ref={currentComponentRef} />;
            default:
                return null;
        }
    };

    return getCurrentComponent();
});

export default SwitchRestoreContextPages;
