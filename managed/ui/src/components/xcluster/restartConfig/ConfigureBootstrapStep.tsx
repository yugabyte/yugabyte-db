import React from 'react';
import { Field, FormikProps } from 'formik';
import { useSelector } from 'react-redux';
import { components } from 'react-select';
import { groupBy } from 'lodash';
import { Trans, useTranslation } from 'react-i18next';
import { Typography } from '@material-ui/core';

import { YBFormSelect } from '../../common/forms/fields';
import {
  Badge_Types as BackupConfigBadgeType,
  StatusBadge as BackupStatusBadge
} from '../../common/badge/StatusBadge';

import { IStorageConfig as BackupStorageConfig } from '../../backupv2';
import { RestartXClusterConfigFormValues } from './RestartConfigModal';

import styles from './ConfigureBootstrapStep.module.scss';

interface ConfigureBootstrapStepProps {
  isDrInterface: boolean;
  formik: React.MutableRefObject<FormikProps<RestartXClusterConfigFormValues>>;

  storageConfigUuid?: string;
}

const TRANSLATION_KEY_PREFIX =
  'clusterDetail.xCluster.restartReplicationModal.step.configureBootstrap';

export const ConfigureBootstrapStep = ({
  isDrInterface,
  formik,
  storageConfigUuid
}: ConfigureBootstrapStepProps) => {
  const { t } = useTranslation('translation');
  const storageConfigs: BackupStorageConfig[] = useSelector((reduxState: any) =>
    reduxState?.customer?.configs?.data.filter(
      (storageConfig: BackupStorageConfig) => storageConfig.type === 'STORAGE'
    )
  );
  const storageConfigName =
    storageConfigs?.find((storageConfig) => storageConfig.configUUID === storageConfigUuid)
      ?.configName ?? '';
  const { values, setFieldValue } = formik.current;

  if (storageConfigs.length === 1 && values.storageConfig === undefined) {
    const { configUUID, configName, name, data } = storageConfigs[0];
    setFieldValue('storageConfig', {
      value: configUUID,
      label: configName,
      name: name,
      regions: data?.REGION_LOCATIONS
    });
  }

  const storageConfigsOptions = storageConfigs.map((storageConfig) => {
    return {
      value: storageConfig.configUUID,
      label: storageConfig.configName,
      name: storageConfig.name,
      regions: storageConfig.data?.REGION_LOCATIONS
    };
  });

  const groupedStorageConfigOptions = Object.entries(
    groupBy(storageConfigsOptions, (configOption) => configOption.name)
  ).map(([label, options]) => ({ label, options }));

  return (
    <>
      <div className={styles.formFieldContainer}>
        {isDrInterface && (
          <div className={styles.formSectionDescription}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.infoText.dr`}
                components={{ bold: <b /> }}
              />
            </Typography>
          </div>
        )}
        {/* We collect backup storage uuid for xCluster, but for DR we already have this information so we don't 
            need to ask the user again. */}
        {!isDrInterface ? (
          <Field
            name="storageConfig"
            component={YBFormSelect}
            label="Select the storage config you want to use for your backup"
            options={groupedStorageConfigOptions}
            components={{
              // eslint-disable-next-line react/display-name
              SingleValue: ({ data }: { data: any }) => (
                <>
                  <span className={styles.backupConfigLabelName}>{data.label}</span>
                  <BackupStatusBadge
                    statusType={BackupConfigBadgeType.DELETED}
                    customLabel={data.name}
                  />
                </>
              ),
              // eslint-disable-next-line react/display-name
              Option: (props: any) => {
                return (
                  <components.Option {...props}>
                    <div className={styles.backupConfigOptionLabel}>{props.data.label}</div>
                    <div className={styles.backupConfigOptionMeta}>
                      <span>{`${props.data.name}${
                        props.data.regions?.length > 0 ? ',' : ''
                      }`}</span>
                      {props.data.regions?.length > 0 && <span>Multi-region support</span>}
                    </div>
                  </components.Option>
                );
              }
            }}
            styles={{
              singleValue: (props: any) => {
                return { ...props, display: 'flex' };
              }
            }}
          />
        ) : storageConfigName ? (
          <Typography variant="body2">
            <Trans
              i18nKey={`clusterDetail.disasterRecovery.backupStorageConfig.currentStorageConfigInfo`}
              components={{ bold: <b /> }}
              values={{ storageConfigName: storageConfigName }}
            />
          </Typography>
        ) : (
          <Typography variant="body2">
            {t('missingStorageConfigInfo', {
              keyPrefix: 'clusterDetail.disasterRecovery.backupStorageConfig'
            })}
          </Typography>
        )}
      </div>
      {!isDrInterface && (
        <div className={styles.note}>
          <p>
            <b>Note!</b>
          </p>
          <p>
            Creating a full copy brings the tables on your target universe to the same checkpoint as
            your source universe before re-enabling asynchronous replication.
          </p>
          <p>
            Creating a full copy is a <b>time intensive</b> process that involves creating a
            checkpoint on the source, deleting the data on target, creating a copy of the source
            data using backup, and replicating the data to target using restore.
          </p>
          <p>
            <b>Data</b> on the target cluster <b>will be deleted</b> when creating a full copy.
            Queries to these temporarily deleted tables will error out.
          </p>
          <p>
            We recommend <b>creating a full copy during off-peak hours.</b>
          </p>
        </div>
      )}
    </>
  );
};
