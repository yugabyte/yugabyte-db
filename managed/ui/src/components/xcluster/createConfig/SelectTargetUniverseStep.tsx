import React, { useState } from 'react';
import { useQuery } from 'react-query';
import { Collapse } from 'react-bootstrap';

import { YBFormInput, YBFormSelect } from '../../common/forms/fields';

import { CreateXClusterConfigFormValues } from './CreateConfigModal';
import { Universe } from '../../../redesign/helpers/dtos';
import { Field, FormikProps } from 'formik';
import { getUniverseStatus, universeState } from '../../universes/helpers/universeHelpers';
import { fetchUniversesList } from '../../../actions/xClusterReplication';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';

import styles from './SelectTargetUniverseStep.module.scss';

const YB_ADMIN_XCLUSTER_DOCUMENTATION_URL =
  'https://docs.yugabyte.com/preview/admin/yb-admin/#xcluster-replication-commands';

interface SelectTargetUniverseStepProps {
  formik: React.MutableRefObject<FormikProps<CreateXClusterConfigFormValues>>;
  currentUniverseUUID: string;
}

export const SelectTargetUniverseStep = ({
  formik,
  currentUniverseUUID
}: SelectTargetUniverseStepProps) => {
  const [isNoteDetailsExpanded, setIsNoteDetailsExpanded] = useState(false);

  const universeListQuery = useQuery<Universe[]>(['universeList'], () =>
    fetchUniversesList().then((res) => res.data)
  );

  if (universeListQuery.isLoading) {
    return <YBLoading />;
  }

  if (universeListQuery.isError || universeListQuery.data === undefined) {
    return <YBErrorIndicator />;
  }

  const { values } = formik.current;
  return (
    <>
      <div className={styles.formInstruction}>1. Select the target universe</div>
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
                getUniverseStatus(universe).state !== universeState.PAUSED
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
      <div className={styles.note}>
        <i className="fa fa-exclamation-circle" aria-hidden="true" />
        <div className={styles.noteText}>
          <p>
            <b>Note!</b> If the tables to replicate are not empty on this source universe, ensure
            that the
            <b> backup storage config can be accessed from both source and target universe </b>
            for bootstrapping. Particularly, <b>Network File System (NFS)</b> based storage configs
            may not be accessible from universes in different regions. In that case, refer to the
            based storage configs may not be accessible from universes in different regions. In that
            case, refer to the{' '}
            <a href={YB_ADMIN_XCLUSTER_DOCUMENTATION_URL}>
              {'documentation for creating replication using yb-admin.'}
            </a>
          </p>
          <button
            className={styles.toggleNoteDetailsBtn}
            onClick={(e) => {
              setIsNoteDetailsExpanded(!isNoteDetailsExpanded);
            }}
            type="button"
          >
            {isNoteDetailsExpanded ? (
              <span>
                Less details <i className="fa fa-caret-up" aria-hidden="true" />
              </span>
            ) : (
              <span>
                More details <i className="fa fa-caret-down" aria-hidden="true" />
              </span>
            )}
          </button>
          <Collapse in={isNoteDetailsExpanded}>
            <div>
              <h6>What is bootstrapping?</h6>
              <p>
                Bootstrapping brings your target universe to the same checkpoint as your source
                universe before turning on async replication. First, YBAnywhere creates a backup of
                the source universe. Then it restores the backup to the target universe. Hence the
                need to ensure the backup storage config (which you will select on step 3) is
                accessible from both source &amp; target universe. If the tables selected for
                xcluster replication are empty on the source universe, no bootstrapping is needed.
              </p>
            </div>
          </Collapse>
        </div>
      </div>
    </>
  );
};
