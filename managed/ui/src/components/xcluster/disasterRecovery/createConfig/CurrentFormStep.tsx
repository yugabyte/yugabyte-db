import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { Universe } from '../../../../redesign/helpers/dtos';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { YBBanner, YBBannerVariant } from '../../../common/descriptors';

import { TableSelect, TableSelectProps } from '../../sharedComponents/tableSelect/TableSelect';
import { ConfirmAlertStep } from './ConfirmAlertStep';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { FormStep } from './CreateConfigModal';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';

interface CurrentFormStepProps {
  currentFormStep: FormStep;
  isFormDisabled: boolean;
  sourceUniverse: Universe;
  tableSelectProps: TableSelectProps;
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
  bannerContainer: {
    marginTop: theme.spacing(2)
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.createModal';

export const CurrentFormStep = ({
  currentFormStep,
  isFormDisabled,
  sourceUniverse,
  tableSelectProps
}: CurrentFormStepProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

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
    case FormStep.CONFIGURE_BOOTSTRAP:
      return <ConfigureBootstrapStep isFormDisabled={isFormDisabled} />;
    case FormStep.CONFIGURE_ALERT:
      return <ConfirmAlertStep isFormDisabled={isFormDisabled} sourceUniverse={sourceUniverse} />;
    default:
      return assertUnreachableCase(currentFormStep);
  }
};
