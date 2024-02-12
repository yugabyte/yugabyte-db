import { makeStyles, Typography, useTheme } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';

import { UnavailableUniverseStates } from '../../../../redesign/helpers/constants';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { YBReactSelectField } from '../../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import { api, universeQueryKey } from '../../../../redesign/helpers/api';
import { getUniverseStatus } from '../../../universes/helpers/universeHelpers';
import {
  DOCS_URL_DR_REPLICA_SELECTION_LIMITATIONS,
  DR_DROPDOWN_SELECT_INPUT_WIDTH_PX
} from '../constants';
import { CreateDrConfigFormValues } from './CreateConfigModal';
import { getPrimaryCluster } from '../../../../utils/universeUtilsTyped';
import InfoIcon from '../../../../redesign/assets/info-message.svg';
import { YBTooltip } from '../../../../redesign/components';

import { Universe } from '../../../../redesign/helpers/dtos';

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
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    marginBottom: theme.spacing(4)
  },
  fieldLabel: {
    marginBottom: theme.spacing(1)
  },
  fieldHelpText: {
    marginTop: theme.spacing(1),

    color: theme.palette.ybacolors.textGray,
    fontSize: '12px'
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
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
            {t('instruction.text')}
            <YBTooltip
              title={
                <Typography variant="body2">
                  <Trans
                    i18nKey={`${TRANSLATION_KEY_PREFIX}.instruction.tooltip`}
                    components={{ paragraph: <p />, bold: <b /> }}
                  />
                </Typography>
              }
            >
              <img src={InfoIcon} alt={t('infoIcon', { keyPrefix: 'imgAltText' })} />
            </YBTooltip>
          </Typography>
          <Typography variant="body2" className={classes.fieldLabel}>
            {t('drReplica')}
          </Typography>
          <YBReactSelectField
            control={control}
            name="targetUniverse"
            options={universeOptions}
            autoSizeMinWidth={DR_DROPDOWN_SELECT_INPUT_WIDTH_PX}
            maxWidth="100%"
            rules={{
              validate: {
                required: (targetUniverse) => !!targetUniverse || t('error.targetUniverseRequired'),
                hasMatchingTLSConfiguration: (targetUniverse: any) =>
                  getPrimaryCluster(targetUniverse.value.universeDetails.clusters)?.userIntent
                    ?.enableNodeToNodeEncrypt ===
                    getPrimaryCluster(sourceUniverse.universeDetails.clusters)?.userIntent
                      ?.enableNodeToNodeEncrypt || t('error.mismatchedNodeToNodeEncryption')
              }
            }}
            isDisabled={isFormDisabled}
          />
          <Typography variant="body2" className={classes.fieldHelpText}>
            <Trans
              i18nKey={`${TRANSLATION_KEY_PREFIX}.drReplicaSelectionHelpText`}
              components={{
                drReplicaPrerequisiteDocsLink: (
                  <a
                    href={DOCS_URL_DR_REPLICA_SELECTION_LIMITATIONS}
                    target="_blank"
                    rel="noopener noreferrer"
                  />
                )
              }}
            />
          </Typography>
        </li>
      </ol>
    </div>
  );
};
