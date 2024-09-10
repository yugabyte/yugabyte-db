import { Box, Typography, useTheme } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';

import { UnavailableUniverseStates } from '../../../../redesign/helpers/constants';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { YBReactSelectField } from '../../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import { api, universeQueryKey } from '../../../../redesign/helpers/api';
import { getUniverseStatus } from '../../../universes/helpers/universeHelpers';
import { DOCS_URL_DR_REPLICA_SELECTION_LIMITATIONS } from '../constants';
import { CreateDrConfigFormValues } from './CreateConfigModal';
import { getPrimaryCluster } from '../../../../utils/universeUtilsTyped';
import InfoIcon from '../../../../redesign/assets/info-message.svg';
import { YBInputField, YBTooltip } from '../../../../redesign/components';
import { INPUT_FIELD_WIDTH_PX, XCLUSTER_CONFIG_NAME_ILLEGAL_PATTERN } from '../../constants';
import { hasNecessaryPerm } from '../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../redesign/features/rbac/ApiAndUserPermMapping';

import { Universe } from '../../../../redesign/helpers/dtos';

import { useModalStyles } from '../../styles';

interface SelectTargetUniverseStepProps {
  isFormDisabled: boolean;
  sourceUniverse: Universe;
}

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
  const theme = useTheme();
  const modalClasses = useModalStyles();
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
      const isDisabled = !hasNecessaryPerm({
        ...ApiPermissionMap.CREATE_DR_CONFIG,
        onResource: universe.universeUUID
      });
      return {
        label: universe.name,
        value: universe,
        isDisabled: isDisabled,
        disabledReason: isDisabled ? t('missingPermissionOnUniverse') : ''
      };
    });

  return (
    <div className={modalClasses.stepContainer}>
      <ol>
        <li>
          <Typography variant="body1" className={modalClasses.instruction}>
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
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
            <div>
              <Typography variant="body2" className={modalClasses.fieldLabel}>
                {t('configName')}
              </Typography>
              <YBInputField
                className={modalClasses.inputField}
                control={control}
                name="configName"
                disabled={isFormDisabled}
                rules={{
                  validate: {
                    required: (configName) => !!configName || t('error.requiredField'),
                    noIllegalCharactersInConfigName: (configName: any) => {
                      return (
                        !XCLUSTER_CONFIG_NAME_ILLEGAL_PATTERN.test(configName) ||
                        t('error.illegalCharactersInConfigName')
                      );
                    }
                  }
                }}
              />
            </div>
            <div>
              <Typography variant="body2" className={modalClasses.fieldLabel}>
                {t('drReplica')}
              </Typography>
              <YBReactSelectField
                control={control}
                name="targetUniverse"
                options={universeOptions}
                autoSizeMinWidth={INPUT_FIELD_WIDTH_PX}
                maxWidth="100%"
                rules={{
                  validate: {
                    required: (targetUniverse) => !!targetUniverse || t('error.requiredField'),
                    hasMatchingTLSConfiguration: (targetUniverse: any) =>
                      getPrimaryCluster(targetUniverse.value.universeDetails.clusters)?.userIntent
                        ?.enableNodeToNodeEncrypt ===
                        getPrimaryCluster(sourceUniverse.universeDetails.clusters)?.userIntent
                          ?.enableNodeToNodeEncrypt || t('error.mismatchedNodeToNodeEncryption')
                  }
                }}
                isDisabled={isFormDisabled}
              />
            </div>
          </Box>
          <Typography variant="body2" className={modalClasses.fieldHelpText}>
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
