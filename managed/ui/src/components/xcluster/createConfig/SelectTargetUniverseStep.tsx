import React from 'react';
import { useQuery } from 'react-query';

import { YBFormInput, YBFormSelect } from '../../common/forms/fields';

import { CreateXClusterConfigFormValues } from './CreateConfigModal';
import { Universe } from '../../../redesign/helpers/dtos';
import { Field, FormikProps } from 'formik';
import { getUniverseStatus } from '../../universes/helpers/universeHelpers';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { UnavailableUniverseStates } from '../../../redesign/helpers/constants';
import { CollapsibleNote } from '../sharedComponents/CollapsibleNote';
import { api, universeQueryKey } from '../../../redesign/helpers/api';

import { hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import styles from './SelectTargetUniverseStep.module.scss';

const YB_ADMIN_XCLUSTER_DOCUMENTATION_URL =
  'https://docs.yugabyte.com/preview/admin/yb-admin/#xcluster-replication-commands';

const FORM_INSTRUCTION = 'Select the target universe';

const NOTE_CONTENT = (
  <p>
    <b>Note!</b> If the tables to replicate are not empty on this source universe, ensure that the
    <b> backup storage config can be accessed from both source and target universe </b>
    for bootstrapping. Particularly, <b>Network File System (NFS)</b> based storage configs may not
    be accessible from universes in different regions. In that case, refer to the{' '}
    <a href={YB_ADMIN_XCLUSTER_DOCUMENTATION_URL} target="_blank" rel="noopener noreferrer">
      {'documentation for creating replication using yb-admin.'}
    </a>
  </p>
);

const NOTE_EXPAND_CONTENT = (
  <div>
    <b>What is bootstrapping?</b>
    <p>
      Bootstrapping brings your target universe to the same checkpoint as your source universe
      before turning on async replication. First, YBAnywhere creates a backup of the source
      universe. Then it restores the backup to the target universe. Hence the need to ensure the
      backup storage config (which you will select on step 3) is accessible from both source &amp;
      target universe. If the tables selected for xcluster replication are empty on the source
      universe, no bootstrapping is needed.
    </p>
  </div>
);

interface SelectTargetUniverseStepProps {
  formik: React.MutableRefObject<FormikProps<CreateXClusterConfigFormValues>>;
  currentUniverseUUID: string;
}

export const SelectTargetUniverseStep = ({
  formik,
  currentUniverseUUID
}: SelectTargetUniverseStepProps) => {
  const universeListQuery = useQuery<Universe[]>(universeQueryKey.ALL, () =>
    api.fetchUniverseList()
  );

  if (universeListQuery.isLoading || universeListQuery.isIdle) {
    return <YBLoading />;
  }

  if (universeListQuery.isError) {
    return <YBErrorIndicator />;
  }

  const { values } = formik.current;
  return (
    <>
      <div className={styles.formInstruction}>1. {FORM_INSTRUCTION}</div>
      <div className={styles.formFieldContainer}>
        <Field
          name="configName"
          placeholder="Replication name"
          label="Replication Name"
          component={YBFormInput}
        />
        <Field
          name="targetUniverse"
          component={YBFormSelect}
          options={universeListQuery.data
            .filter(
              (universe) =>
                universe.universeUUID !== currentUniverseUUID &&
                !UnavailableUniverseStates.includes(getUniverseStatus(universe).state) &&
                hasNecessaryPerm({
                  ...ApiPermissionMap.CREATE_XCLUSTER_REPLICATION,
                  onResource: universe.universeUUID
                })
            )
            .map((universe) => {
              return {
                label: universe.name,
                value: universe
              };
            })}
          field={{
            name: 'targetUniverse',
            value: values.targetUniverse
          }}
          label="Target Universe"
        />
      </div>
      <CollapsibleNote noteContent={NOTE_CONTENT} expandContent={NOTE_EXPAND_CONTENT} />
    </>
  );
};
