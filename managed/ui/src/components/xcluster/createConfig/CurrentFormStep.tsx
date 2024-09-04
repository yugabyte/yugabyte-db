import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';

import { TableSelect, TableSelectProps } from '../sharedComponents/tableSelect/TableSelect';
import { FormStep } from './CreateConfigModal';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';
import { BootstrapSummary } from '../sharedComponents/bootstrapSummary/BootstrapSummary';
import { ConfirmAlertStep } from '../sharedComponents/ConfirmAlertStep';

import { TableType, Universe } from '../../../redesign/helpers/dtos';
import { CategorizedNeedBootstrapPerTableResponse } from '../XClusterTypes';

import { useModalStyles } from '../styles';

interface CurrentFormStepProps {
  currentFormStep: FormStep;
  isFormDisabled: boolean;
  sourceUniverse: Universe;
  tableSelectProps: TableSelectProps;
  categorizedNeedBootstrapPerTableResponse: CategorizedNeedBootstrapPerTableResponse | null;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.createConfigModal';

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
                isDrInterface={false}
                categorizedNeedBootstrapPerTableResponse={categorizedNeedBootstrapPerTableResponse}
              />
            </li>
          </ol>
        </div>
      );
    case FormStep.CONFIRM_ALERT:
      return <ConfirmAlertStep isDrInterface={false} sourceUniverse={sourceUniverse} />;
    default:
      return assertUnreachableCase(currentFormStep);
  }
};
