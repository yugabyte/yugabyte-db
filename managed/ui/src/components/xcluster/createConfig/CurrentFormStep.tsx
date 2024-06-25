import { makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';
import { YBBanner, YBBannerVariant } from '../../common/descriptors';

import { TableSelect, TableSelectProps } from '../sharedComponents/tableSelect/TableSelect';
import { FormStep } from './CreateConfigModal';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { ConfirmAlertStep } from '../sharedComponents/ConfirmAlertStep';

import { TableType, Universe } from '../../../redesign/helpers/dtos';

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

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.createConfigModal';

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
              <Typography variant="body1">
                {t(
                  `step.selectTables.instruction.${
                    tableSelectProps.tableType === TableType.PGSQL_TABLE_TYPE ? 'ysql' : 'ycql'
                  }`
                )}
              </Typography>
            </li>
          </ol>
          <TableSelect {...tableSelectProps} />
        </div>
      );
    case FormStep.CONFIGURE_BOOTSTRAP:
      return <ConfigureBootstrapStep isFormDisabled={isFormDisabled} />;
    case FormStep.CONFIRM_ALERT:
      return <ConfirmAlertStep isDrInterface={false} sourceUniverse={sourceUniverse} />;
    default:
      return assertUnreachableCase(currentFormStep);
  }
};
