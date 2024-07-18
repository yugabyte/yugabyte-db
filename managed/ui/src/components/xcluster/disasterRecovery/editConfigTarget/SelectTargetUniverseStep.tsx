import { makeStyles, Typography } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';

import { EditConfigTargetFormValues } from './EditConfigTargetModal';
import { UnavailableUniverseStates } from '../../../../redesign/helpers/constants';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { YBReactSelectField } from '../../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import { api, universeQueryKey } from '../../../../redesign/helpers/api';
import { getUniverseStatus } from '../../../universes/helpers/universeHelpers';
import { INPUT_FIELD_WIDTH_PX } from '../../constants';

import { Universe } from '../../../../redesign/helpers/dtos';

interface SelectTargetUniverseStepProps {
  isFormDisabled: boolean;
  sourceUniverseUuid: string;
  targetUniverseUuid: string;
}

const useStyles = makeStyles((theme) => ({
  fieldLabel: {
    marginBottom: theme.spacing(1)
  }
}));

const TRANSLATION_KEY_PREFIX =
  'clusterDetail.disasterRecovery.config.editTargetModal.step.selectTargetUniverse';

/**
 * Component requirements:
 * - An ancestor must provide form context using <FormProvider> from React Hook Form
 */
export const SelectTargetUniverseStep = ({
  isFormDisabled,
  sourceUniverseUuid,
  targetUniverseUuid
}: SelectTargetUniverseStepProps) => {
  const { control } = useFormContext<EditConfigTargetFormValues>();
  const classes = useStyles();
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

  // Remove DR primary and current DR replica from the list of universe options in addition
  // to the universes which are unavailable for new tasks.
  const universeOptions = universeListQuery.data
    .filter(
      (universe) =>
        universe.universeUUID !== sourceUniverseUuid &&
        universe.universeUUID !== targetUniverseUuid &&
        !UnavailableUniverseStates.includes(getUniverseStatus(universe).state)
    )
    .map((universe) => {
      return {
        label: universe.name,
        value: universe
      };
    });
  return (
    <>
      <Typography variant="body2" className={classes.fieldLabel}>
        {t('drReplica')}
      </Typography>
      <YBReactSelectField
        control={control}
        name="targetUniverse"
        options={universeOptions}
        rules={{ required: t('error.targetUniverseRequired') }}
        isDisabled={isFormDisabled}
        autoSizeMinWidth={INPUT_FIELD_WIDTH_PX}
        maxWidth="100%"
      />
    </>
  );
};
