import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';

import { YBFormSelect } from '../../../common/forms/fields';

import { Universe } from '../../../../redesign/helpers/dtos';
import { Field, FormikProps } from 'formik';
import { getUniverseStatus } from '../../../universes/helpers/universeHelpers';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { api, universeQueryKey } from '../../../../redesign/helpers/api';
import { CreateDrConfigFormValues } from './CreateConfigModal';
import { UnavailableUniverseStates } from '../../../../redesign/helpers/constants';
import { CollapsibleNote } from '../../sharedComponents/CollapsibleNote';

import styles from './SelectTargetUniverseStep.module.scss';

const YB_ADMIN_XCLUSTER_DOCUMENTATION_URL =
  'https://docs.yugabyte.com/preview/admin/yb-admin/#xcluster-replication-commands';

const NOTE_CONTENT = (
  <p>
    <b>Note!</b> Ensure that the
    <b> backup storage config can be accessed from both DR Primary and DR Replica universe </b>
    for bootstrapping. Particularly, <b>Network File System (NFS)</b> based storage configs may not
    be accessible from universes in different regions. In that case, refer to the{' '}
    <a href={YB_ADMIN_XCLUSTER_DOCUMENTATION_URL}>
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
  formik: React.MutableRefObject<FormikProps<CreateDrConfigFormValues>>;
  currentUniverseUuid: string;
}

const TRANSLATION_KEY_PREFIX =
  'clusterDetail.disasterRecovery.config.createModal.step.selectTargetUniverse';

export const SelectTargetUniverseStep = ({
  formik,
  currentUniverseUuid
}: SelectTargetUniverseStepProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const universeListQuery = useQuery<Universe[]>(universeQueryKey.ALL, () =>
    api.fetchUniverseList()
  );

  if (universeListQuery.isLoading || universeListQuery.isIdle) {
    return <YBLoading />;
  }

  if (universeListQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchUniverseList', { keyPrefix: 'queryError' })}
      />
    );
  }

  const { values } = formik.current;
  return (
    <>
      <div className={styles.formInstruction}>{t('instruction')}</div>
      <div className={styles.formFieldContainer}>
        <Field
          name="targetUniverse"
          component={YBFormSelect}
          options={universeListQuery.data
            .filter(
              (universe) =>
                universe.universeUUID !== currentUniverseUuid &&
                !UnavailableUniverseStates.includes(getUniverseStatus(universe).state)
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
          label={t('drReplica')}
        />
      </div>
      <CollapsibleNote noteContent={NOTE_CONTENT} expandContent={NOTE_EXPAND_CONTENT} />
    </>
  );
};
