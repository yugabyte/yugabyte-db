/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Typography } from '@material-ui/core';

import { YBModal } from '@app/redesign/components';

interface EditUniverseRollbackConfirmModalProps {
  visible: boolean;
  onClose: () => void;
  onSubmit: () => void;
}

const MODAL_NAME = 'EditUniverseRollbackConfirmModal';
const MODAL_WIDTH_PX = 600;
const TRANSLATION_KEY_PREFIX = 'taskDetails.editUniverseTaskBanner.rollbackConfirmModal';

export const EditUniverseRollbackConfirmModal: FC<EditUniverseRollbackConfirmModalProps> = ({
  visible,
  onClose,
  onSubmit
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });

  if (!visible) return null;

  return (
    <YBModal
      open={visible}
      onClose={onClose}
      onSubmit={onSubmit}
      title={t('title')}
      submitLabel={t('submitLabel')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      size="fit"
      overrideWidth={MODAL_WIDTH_PX}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
    >
      <Typography variant="body2">{t('message')}</Typography>
    </YBModal>
  );
};
