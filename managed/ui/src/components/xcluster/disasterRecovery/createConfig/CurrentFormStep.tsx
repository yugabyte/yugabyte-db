import { Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { Universe } from '../../../../redesign/helpers/dtos';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { YBBanner, YBBannerVariant } from '../../../common/descriptors';

import { TableSelect, TableSelectProps } from '../../sharedComponents/tableSelect/TableSelect';
import { ConfirmAlertStep } from '../../sharedComponents/ConfirmAlertStep';
import { FormStep } from './CreateConfigModal';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';
import { ConfigurePitrStep } from './ConfigurePitrStep';
import { BootstrapSummary } from '../../sharedComponents/bootstrapSummary/BootstrapSummary';
import { CategorizedNeedBootstrapPerTableResponse } from '../../XClusterTypes';

import { useModalStyles } from '../../styles';

interface CurrentFormStepProps {
  currentFormStep: FormStep;
  isFormDisabled: boolean;
  sourceUniverse: Universe;
  tableSelectProps: TableSelectProps;
  categorizedNeedBootstrapPerTableResponse: CategorizedNeedBootstrapPerTableResponse | null;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.createModal';

export const CurrentFormStep = ({
  currentFormStep,
  isFormDisabled,
  sourceUniverse,
  tableSelectProps,
  categorizedNeedBootstrapPerTableResponse
}: CurrentFormStepProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useModalStyles();

  switch (currentFormStep) {
    case FormStep.SELECT_TARGET_UNIVERSE:
      return (
        <SelectTargetUniverseStep isFormDisabled={isFormDisabled} sourceUniverse={sourceUniverse} />
      );
    case FormStep.SELECT_TABLES:
      return (
        <div className={classes.stepContainer}>
          <ol start={2}>
            <li>
              <Typography variant="body1">{t('step.selectDatabases.instruction')}</Typography>
            </li>
          </ol>
          <TableSelect {...tableSelectProps} />
          <div className={classes.bannerContainer}>
            <YBBanner variant={YBBannerVariant.INFO}>
              <Typography variant="body2">
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.step.selectDatabases.pitrSetUpNote`}
                  components={{ bold: <b /> }}
                />
              </Typography>
            </YBBanner>
          </div>
        </div>
      );
    case FormStep.BOOTSTRAP_SUMMARY:
      return (
        <div className={classes.stepContainer}>
          <ol start={3}>
            <li>
              <Typography variant="body1" className={classes.instruction}>
                {t('step.bootstrapSummary.instruction')}
              </Typography>
              <BootstrapSummary
                sourceUniverseUuid={sourceUniverse.universeUUID}
                isFormDisabled={isFormDisabled}
                isDrInterface={true}
                isCreateConfig={true}
                categorizedNeedBootstrapPerTableResponse={categorizedNeedBootstrapPerTableResponse}
              />
            </li>
          </ol>
        </div>
      );
    case FormStep.CONFIGURE_PITR:
      return <ConfigurePitrStep isFormDisabled={isFormDisabled} />;
    case FormStep.CONFIRM_ALERT:
      return <ConfirmAlertStep isDrInterface={true} sourceUniverse={sourceUniverse} />;
    default:
      return assertUnreachableCase(currentFormStep);
  }
};
