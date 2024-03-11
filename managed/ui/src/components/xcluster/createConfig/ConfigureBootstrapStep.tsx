import { Field, FormikProps } from 'formik';
import { useSelector } from 'react-redux';
import { components } from 'react-select';
import { groupBy } from 'lodash';
import { Box, Typography, useTheme } from '@material-ui/core';
import { YBBanner, YBBannerVariant } from '../../common/descriptors';

import { YBFormSelect } from '../../common/forms/fields';
import { CreateXClusterConfigFormValues } from './CreateConfigModal';
import {
  Badge_Types as BackupConfigBadgeType,
  StatusBadge as BackupStatusBadge
} from '../../common/badge/StatusBadge';
import { formatUuidForXCluster } from '../ReplicationUtils';
import { getTableUuid } from '../../../utils/tableUtils';

import { YBTable } from '../../../redesign/helpers/dtos';
import { IStorageConfig as BackupStorageConfig } from '../../backupv2';

import styles from './ConfigureBootstrapStep.module.scss';

interface ConfigureBootstrapStepProps {
  formik: React.MutableRefObject<FormikProps<CreateXClusterConfigFormValues>>;
  bootstrapRequiredTableUUIDs: string[];
  sourceTables: YBTable[];
}

export const ConfigureBootstrapStep = ({
  formik,
  bootstrapRequiredTableUUIDs,
  sourceTables
}: ConfigureBootstrapStepProps) => {
  const storageConfigs: BackupStorageConfig[] = useSelector((reduxState: any) =>
    reduxState?.customer?.configs?.data.filter(
      (storageConfig: BackupStorageConfig) => storageConfig.type === 'STORAGE'
    )
  );
  const { values, setFieldValue } = formik.current;
  const theme = useTheme();

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

  const bootstrapRequiredTableUUIDsLookup = new Set(bootstrapRequiredTableUUIDs);
  const keyspaces = sourceTables.reduce((keyspaces, table) => {
    if (bootstrapRequiredTableUUIDsLookup.has(formatUuidForXCluster(getTableUuid(table)))) {
      keyspaces.add(table.keySpace);
    }
    return keyspaces;
  }, new Set());

  return (
    <>
      <div className={styles.formInstruction}>3. Configure Full Copy</div>
      <p>
        Creating an initial full copy
        <b> brings your target universe to the same checkpoint as your source universe </b>
        before turning on async replication. It ensures data consistency between the source and
        target universe.
      </p>
      <p>
        {`${bootstrapRequiredTableUUIDs.length} out of ${values.tableUUIDs.length} table(s) in
          ${keyspaces.size} `}
        database(s) selected for replication
        <b> contain data and need an initial full copy</b> to enable replication.
      </p>
      <div className={styles.formFieldContainer}>
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
                    <span>{`${props.data.name}${props.data.regions?.length > 0 ? ',' : ''}`}</span>
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
      </div>
      <YBBanner variant={YBBannerVariant.WARNING} showBannerIcon={false}>
        <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
          <Typography variant="body2" component="p">
            <b>Note!</b>
          </Typography>
          <Typography variant="body2" component="p">
            Creating a full copy is a <b>time intensive</b> process that involves creating a
            checkpoint on the source, deleting the data on target, creating a copy of the source
            data using backup, and replicating the data to target using restore.
          </Typography>
          <Typography variant="body2" component="p">
            <b>Data</b> on the target cluster <b>will be deleted</b> during when creating a full
            copy. Queries to these temporarily deleted tables will error out.{' '}
          </Typography>
          <Typography variant="body2" component="p">
            We recommend <b>creating a full copy during off-peak hours.</b>
          </Typography>
        </Box>
      </YBBanner>
    </>
  );
};
