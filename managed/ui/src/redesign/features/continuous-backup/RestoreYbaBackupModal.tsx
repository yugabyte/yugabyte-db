import { useState } from 'react';
import { useQuery } from 'react-query';

import { YBModal, YBModalProps } from '@app/redesign/components';
import { api, universeQueryKey } from '@app/redesign/helpers/api';
import { YBLoading } from '@app/components/common/indicators';
import { RestoreOverExistingUniversesWarningModal } from './RestoreOverExistingUniversesWarningModal';
import { RestoreYbaBackupFormModal } from './RestoreYbaBackupFormModal';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';

interface RestoreYbaBackupModalProps {
  modalProps: YBModalProps;
}

const Modal = {
  EARLY_WARNING: 'earlyWarning',
  RESTORE_FORM: 'restoreForm'
} as const;
type OrchestratorStep = typeof Modal[keyof typeof Modal];

export const RestoreYbaBackupModal = ({ modalProps }: RestoreYbaBackupModalProps) => {
  const [displayedModal, setDisplayedModal] = useState<OrchestratorStep>(Modal.EARLY_WARNING);
  const universeListQuery = useQuery(universeQueryKey.ALL, () => api.fetchUniverseList());

  if (universeListQuery.isLoading || universeListQuery.isIdle) {
    return (
      <YBModal size="sm" {...modalProps} hideCloseBtn={true}>
        <YBLoading />
      </YBModal>
    );
  }

  const resetModal = () => {
    setDisplayedModal(Modal.EARLY_WARNING);
  };
  const onClose = () => {
    resetModal();
    modalProps.onClose();
  };

  // If we're not able to determine the list of universes on the platform,
  // then it is safer to assume the user might have a universe and show this warning.
  const shouldShowExistingUniverseWarning =
    universeListQuery.isError || !!universeListQuery.data?.length;

  switch (displayedModal) {
    case Modal.EARLY_WARNING:
      if (!shouldShowExistingUniverseWarning) {
        setDisplayedModal(Modal.RESTORE_FORM);
        return (
          <YBModal size="sm" {...modalProps} hideCloseBtn={true}>
            <YBLoading />
          </YBModal>
        );
      }

      return (
        <RestoreOverExistingUniversesWarningModal
          onSubmit={() => setDisplayedModal(Modal.RESTORE_FORM)}
          modalProps={{ open: modalProps.open, onClose }}
        />
      );
    case Modal.RESTORE_FORM:
      return <RestoreYbaBackupFormModal modalProps={{ ...modalProps, onClose }} />;
    default:
      return assertUnreachableCase(displayedModal);
  }
};
