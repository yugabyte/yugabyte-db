import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';

import { UnavailableUniverseStates } from '../../../../redesign/helpers/constants';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { YBReactSelectField } from '../../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import { api, universeQueryKey } from '../../../../redesign/helpers/api';
import { getUniverseStatus } from '../../../universes/helpers/universeHelpers';

import { Universe } from '../../../../redesign/helpers/dtos';
import { CreateDrConfigFormValues } from './CreateConfigModal';
import { getPrimaryCluster } from '../../../../utils/universeUtilsTyped';
import { DR_DROPDOWN_SELECT_INPUT_WIDTH } from '../constants';

interface SelectTargetUniverseStepProps {
  isFormDisabled: boolean;
  sourceUniverse: Universe;
}

const useStyles = makeStyles((theme) => ({
  stepContainer: {
    '& ol': {
      paddingLeft: theme.spacing(2),
      listStylePosition: 'outside',
      '& li::marker': {
        fontWeight: 'bold'
      }
    }
  },
  instruction: {
    marginBottom: theme.spacing(4)
  },
  fieldLabel: {
    marginBottom: theme.spacing(1)
  }
}));

const TRANSLATION_KEY_PREFIX =
  'clusterDetail.disasterRecovery.config.createModal.step.selectTargetUniverse';

/**
 * Component requirements:
 * - An ancestor must provide form context using <FormProvider> from React Hook Form
 */
export const SelectTargetUniverseStep = ({
  isFormDisabled,
  sourceUniverse
}: SelectTargetUniverseStepProps) => {
  const { control } = useFormContext<CreateDrConfigFormValues>();
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();

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

  const universeOptions = universeListQuery.data
    .filter(
      (universe) =>
        universe.universeUUID !== sourceUniverse.universeUUID &&
        !UnavailableUniverseStates.includes(getUniverseStatus(universe).state)
    )
    .map((universe) => {
      return {
        label: universe.name,
        value: universe
      };
    });
  return (
    <div className={classes.stepContainer}>
      <ol>
        <li>
          <Typography variant="body1" className={classes.instruction}>
            {t('instruction')}
          </Typography>
          <Typography variant="body2" className={classes.fieldLabel}>
            {t('drReplica')}
          </Typography>
          <YBReactSelectField
            control={control}
            name="targetUniverse"
            options={universeOptions}
            width={DR_DROPDOWN_SELECT_INPUT_WIDTH}
            rules={{
              validate: {
                required: (targetUniverse: { label: string; value: Universe }) =>
                  !!targetUniverse || t('error.targetUniverseRequired'),
                hasMatchingTLSConfiguration: (targetUniverse: { label: string; value: Universe }) =>
                  getPrimaryCluster(targetUniverse.value.universeDetails.clusters)?.userIntent
                    ?.enableNodeToNodeEncrypt ===
                    getPrimaryCluster(sourceUniverse.universeDetails.clusters)?.userIntent
                      ?.enableNodeToNodeEncrypt || t('error.mismatchedNodeToNodeEncryption')
              }
            }}
            isDisabled={isFormDisabled}
          />
        </li>
      </ol>
    </div>
  );
};
