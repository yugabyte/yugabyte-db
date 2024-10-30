/*
 * Created on Fri Aug 02 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { YBModal } from '../../../components/common/forms/fields';
import WarningIcon from '../../../components/users/icons/warning_icon';

interface DisableAuthProviderModalProps {
  visible: boolean;
  onCancel: () => void;
  onSubmit: () => void;
  type: 'LDAP' | 'OIDC';
}

export const DisableAuthProviderModal: FC<DisableAuthProviderModalProps> = ({
  onCancel,
  visible,
  onSubmit,
  type
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: `userAuth.disableModal`
  });
  return (
    <YBModal
      title={t('title', { type })}
      visible={visible}
      showCancelButton={true}
      submitLabel={t('title', { type })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      cancelBtnProps={{
        className: 'btn btn-default pull-left oidc-cancel-btn'
      }}
      onHide={onCancel}
      onFormSubmit={onSubmit}
    >
      <div className="oidc-modal-c">
        <div className="oidc-modal-c-icon">
          <WarningIcon />
        </div>
        <div className="oidc-modal-c-content">
          <Trans i18nKey="content" t={t} components={{ b: <b /> }} values={{ type }} />
        </div>
      </div>
    </YBModal>
  );
};
