import { Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';

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

export const CurrentFormStep = ({
  currentFormStep,
  isFormDisabled,
  tableSelectProps,
  isDrInterface,
  storageConfigUuid
}: CurrentFormStepProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  switch (currentFormStep) {
    case FormStep.SELECT_TABLES:
      return (
        <>
          <Typography variant="body1">{t('instruction')}</Typography>
          <TableSelect {...tableSelectProps} />
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
