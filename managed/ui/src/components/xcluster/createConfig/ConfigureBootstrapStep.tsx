import { Field, FormikProps } from 'formik';
import { useSelector } from 'react-redux';
import { components } from 'react-select';
import { groupBy } from 'lodash';

import { YBFormSelect, YBNumericInput } from '../../common/forms/fields';
import { CreateXClusterConfigFormValues } from './CreateConfigModal';
import {
  Badge_Types as BackupConfigBadgeType,
  StatusBadge as BackupStatusBadge
} from '../../common/badge/StatusBadge';
import { formatUuidForXCluster } from '../ReplicationUtils';

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
  const { values, errors, setFieldValue } = formik.current;

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
    if (bootstrapRequiredTableUUIDsLookup.has(formatUuidForXCluster(table.tableUUID))) {
      keyspaces.add(table.keySpace);
    }
    return keyspaces;
  }, new Set());

  return (
    <>
      <div className={styles.formInstruction}>3. Configure bootstrap</div>
      <p>
        Bootstrapping
        <b> brings your target universe to the same checkpoint as your source universe </b>
        before turning on async replication. It ensures data consistency between the source and
        target universe.
      </p>
      <p>
        {`${bootstrapRequiredTableUUIDs.length} out of ${values.tableUUIDs.length} table(s) in
          ${keyspaces.size} `}
        database(s) selected for replication
        <b> contain data and need to be bootstrapped</b> to enable replication.
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
        <Field
          name="parallelThreads"
          component={YBNumericInput}
          input={{
            onChange: (val: number) => setFieldValue('parallelThreads', val),
            value: values.parallelThreads
          }}
          minVal={1}
          label="Parallel threads (Optional)"
        />
        {errors.parallelThreads && <span className="standard-error">{errors.parallelThreads}</span>}
      </div>
      <div className={styles.note}>
        <p>
          <b>Note!</b>
        </p>
        <p>
          Bootstrapping is a <b>time intensive</b> process that involves creating a checkpoint on
          the source, deleting the data on target, creating a copy of the source data using backup,
          and replicating the data to target using restore.
        </p>
        <p>
          <b>Data</b> on the target cluster <b>will be deleted</b> during bootstrapping. Queries to
          these temporarily deleted tables will error out.
        </p>
        <p>
          We recommend <b>bootstrapping during off-peak hours.</b>
        </p>
      </div>
    </>
  );
};
