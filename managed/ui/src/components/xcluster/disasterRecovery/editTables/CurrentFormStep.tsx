import { Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { BootstrapSummary } from '../../sharedComponents/bootstrapSummary/BootstrapSummary';

import { TableSelect, TableSelectProps } from '../../sharedComponents/tableSelect/TableSelect';
import { useModalStyles } from '../../styles';
import { CategorizedNeedBootstrapPerTableResponse } from '../../XClusterTypes';
import { FormStep } from './EditTablesModal';

interface CommonCurrentFormStepProps {
  currentFormStep: FormStep;
  isFormDisabled: boolean;
  tableSelectProps: TableSelectProps;
  sourceUniverseUuid: string;
  categorizedNeedBootstrapPerTableResponse: CategorizedNeedBootstrapPerTableResponse | null;
}

type CurrentFormStepProps =
  | (CommonCurrentFormStepProps & {
      isDrInterface: true;
      storageConfigUuid: string;
    })
  | (CommonCurrentFormStepProps & { isDrInterface: false });

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.editTablesModal';

export const CurrentFormStep = (props: CurrentFormStepProps) => {
  const {
    currentFormStep,
    isFormDisabled,
    tableSelectProps,
    sourceUniverseUuid,
    categorizedNeedBootstrapPerTableResponse
  } = props;
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const modalClasses = useModalStyles();

  switch (currentFormStep) {
    case FormStep.SELECT_TABLES:
      return (
        <>
          <Typography variant="body1">
            {t(`step.selectTables.instruction.${props.isDrInterface ? 'dr' : 'xCluster'}`)}
          </Typography>
          <TableSelect {...tableSelectProps} />
        </>
      );
    case FormStep.BOOTSTRAP_SUMMARY:
      return (
        <>
          <Typography variant="body1" className={modalClasses.instruction}>
            {t('step.bootstrapSummary.instruction')}
          </Typography>
          {props.isDrInterface ? (
            <BootstrapSummary
              isDrInterface={true}
              storageConfigUuid={props.storageConfigUuid}
              isFormDisabled={isFormDisabled}
              sourceUniverseUuid={sourceUniverseUuid}
              categorizedNeedBootstrapPerTableResponse={categorizedNeedBootstrapPerTableResponse}
            />
          ) : (
            <BootstrapSummary
              isDrInterface={false}
              isFormDisabled={isFormDisabled}
              sourceUniverseUuid={sourceUniverseUuid}
              categorizedNeedBootstrapPerTableResponse={categorizedNeedBootstrapPerTableResponse}
            />
          )}
        </>
      );
    default:
      return assertUnreachableCase(currentFormStep);
  }
};
