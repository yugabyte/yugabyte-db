/*
 * Created on Thu Jul 21 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { Tab } from 'react-bootstrap';
import { useDispatch } from 'react-redux';
import { fetchCustomerConfigs, fetchCustomerConfigsResponse } from '../../../../actions/customers';
import { YBTabsPanel } from '../../../panels';

import awss3Logo from '../images/aws-s3.png';
import azureLogo from '../images/azure_logo.svg';
import gcsLogo from '../images/gcs-logo.png';
import nfsIcon from '../images/nfs.svg';
import { AWSBackupConfig } from './AWS';
import { AzureBackupConfig } from './AZURE';
import { GCSBackupConfig } from './GCS';
import { IStorageProviders } from './IStorageConfigs';
import { NFSBackupConfig } from './NFS';

const getTabTitle = (configName: IStorageProviders) => {
  switch (configName) {
    case IStorageProviders.AWS:
      return (
        <span>
          <img src={awss3Logo} alt="AWS S3" className="s3-logo" /> Amazon S3
        </span>
      );
    case IStorageProviders.GCS:
      return (
        <span>
          <img src={gcsLogo} alt="Google Cloud Storage" className="gcs-logo" /> Google Cloud Storage
        </span>
      );
    case IStorageProviders.NFS:
      return (
        <span>
          <img src={nfsIcon} alt="NFS" className="nfs-icon" /> Network File System
        </span>
      );
    case IStorageProviders.AZURE:
      return (
        <span>
          <img src={azureLogo} alt="Azure" className="azure-logo" /> Azure Storage
        </span>
      );
    default:
      throw new Error('Undefined storage');
  }
};

interface NewStorageConfigurationProps {
  activeTab: string | undefined;
}

export const NewStorageConfiguration: FC<NewStorageConfigurationProps> = ({ activeTab }) => {
  const tabToDisplay = activeTab ?? IStorageProviders.AWS;
  const dispatch: any = useDispatch();

  const fetchConfigs = () => {
    dispatch(fetchCustomerConfigs()).then((response: any) => {
      dispatch(fetchCustomerConfigsResponse(response.payload));
    });
  };

  return (
    <YBTabsPanel
      defaultTab={tabToDisplay}
      activeTab={tabToDisplay}
      id="storage-config-tab-panel"
      className="config-tabs"
      routePrefix="/config/newBackupConfig/"
    >
      <Tab
        eventKey={IStorageProviders.AWS}
        title={getTabTitle(IStorageProviders.AWS)}
        key={IStorageProviders.AWS}
        unmountOnExit={true}
      >
        <AWSBackupConfig
          visible={tabToDisplay === IStorageProviders.AWS}
          fetchConfigs={fetchConfigs}
        />
      </Tab>
      <Tab
        eventKey={IStorageProviders.GCS}
        title={getTabTitle(IStorageProviders.GCS)}
        key={IStorageProviders.GCS}
        unmountOnExit={true}
      >
        <GCSBackupConfig
          visible={tabToDisplay === IStorageProviders.GCS}
          fetchConfigs={fetchConfigs}
        />
      </Tab>
      <Tab
        eventKey={IStorageProviders.NFS}
        title={getTabTitle(IStorageProviders.NFS)}
        key={IStorageProviders.NFS}
        unmountOnExit={true}
      >
        <NFSBackupConfig
          visible={tabToDisplay === IStorageProviders.NFS}
          fetchConfigs={fetchConfigs}
        />
      </Tab>
      <Tab
        eventKey={IStorageProviders.AZURE}
        title={getTabTitle(IStorageProviders.AZURE)}
        key={IStorageProviders.AZURE}
        unmountOnExit={true}
      >
        <AzureBackupConfig
          visible={tabToDisplay === IStorageProviders.AZURE}
          fetchConfigs={fetchConfigs}
        />
      </Tab>
    </YBTabsPanel>
  );
};
