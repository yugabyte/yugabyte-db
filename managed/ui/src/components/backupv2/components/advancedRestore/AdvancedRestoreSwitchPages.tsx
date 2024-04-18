/*
 * Created on Mon Jul 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useContext, useImperativeHandle, useRef } from 'react';
import { GeneralConfigurations } from './generalConfigurations/GeneralConfigurations';
import { SelectTables } from '../restore/pages/selectTables/TablesSelect';
import RenameKeyspace from '../restore/pages/renameKeyspace/RenameKeyspace';
import RestoreFinalStep from '../restore/pages/RestoreFinalStep';
import {
  AdvancedRestoreContextMethods,
  AdvancedRestoreFormContext,
  PageRef
} from './AdvancedRestoreContext';

type Props = {};

//rotates the pages in the modal
// eslint-disable-next-line react/display-name
const AdvancedRestoreSwitchPages = React.forwardRef((_props: Props, forwardRef) => {
  const [{ formProps }] = (useContext(
    AdvancedRestoreFormContext
  ) as unknown) as AdvancedRestoreContextMethods;
  const currentComponentRef = useRef<PageRef>(null);
  useImperativeHandle(forwardRef, () => currentComponentRef.current, [currentComponentRef.current]); // eslint-disable-line react-hooks/exhaustive-deps

  const getCurrentComponent = () => {
    switch (formProps.currentPage) {
      case 'GENERAL_SETTINGS':
        return <GeneralConfigurations ref={currentComponentRef} />;
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

export default AdvancedRestoreSwitchPages;
