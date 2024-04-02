import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { YBBanner, YBBannerVariant } from '../../../common/descriptors';

import { TableSelect, TableSelectProps } from '../../sharedComponents/tableSelect/TableSelect';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { FormStep } from './EditTablesModal';

interface CurrentFormStepProps {
  currentFormStep: FormStep;
  isFormDisabled: boolean;
  isDrInterface: boolean;
  tableSelectProps: TableSelectProps;

  storageConfigUuid?: string;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.editTablesModal';

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

export const CurrentFormStep = ({
  currentFormStep,
  isFormDisabled,
  tableSelectProps,
  isDrInterface,
  storageConfigUuid
}: CurrentFormStepProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  switch (currentFormStep) {
    case FormStep.SELECT_TABLES:
      return (
        <>
          <Typography variant="body1">{t('instruction')}</Typography>
          <TableSelect {...tableSelectProps} />
          <div className={classes.bannerContainer}>
            <YBBanner variant={YBBannerVariant.INFO} showBannerIcon={false}>
              <Typography variant="body2">
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.step.selectTables.matchingTableNote`}
                  components={{ bold: <b /> }}
                />
              </Typography>
            </YBBanner>
          </div>
        </>
      );
    case FormStep.CONFIGURE_BOOTSTRAP:
      return (
        <ConfigureBootstrapStep
          isDrInterface={isDrInterface}
          isFormDisabled={isFormDisabled}
          storageConfigUuid={storageConfigUuid}
        />
      );
    default:
      return assertUnreachableCase(currentFormStep);
  }
};
