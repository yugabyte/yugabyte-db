/*
 * Created on Fri Aug 17 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useState } from 'react';
import { StorageConfigurationList } from '../common/StorageConfigurationList';
import { IStorageProviders } from '../IStorageConfigs';
import { CreateAzureConfigForm } from './CreateAzureConfigForm';

export const AzureBackupConfig = ({
  visible,
  fetchConfigs
}: {
  visible: boolean;
  fetchConfigs: () => void;
}) => {
  const [showCreationForm, setShowCreationForm] = useState(false);
  const [editConfigData, setEditConfigData] = useState<any>({});

  if (!visible) {
    return null;
  }

  return (
    <>
      {showCreationForm ? (
        <CreateAzureConfigForm
          visible={showCreationForm}
          editInitialValues={editConfigData}
          onHide={() => setShowCreationForm(false)}
          fetchConfigs={fetchConfigs}
        />
      ) : (
        <StorageConfigurationList
          type={IStorageProviders.AZURE}
          showStorageConfigCreation={() => {
            setEditConfigData({});
            setShowCreationForm(true);
          }}
          setEditConfigData={(row: any) => {
            setEditConfigData(row);
            setShowCreationForm(true);
          }}
          fetchConfigs={fetchConfigs}
        />
      )}
    </>
  );
};
