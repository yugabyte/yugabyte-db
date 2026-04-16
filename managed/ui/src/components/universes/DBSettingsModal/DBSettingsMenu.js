import React from 'react';
import { useTranslation } from 'react-i18next';
import { MenuItem } from 'react-bootstrap';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBMenuItem } from '../UniverseDetail/compounds/YBMenuItem';

export const DBSettingsMenu = ({ backToMainMenu, showEnableYSQLModal, showEnableYCQLModal }) => {
  const { t } = useTranslation();
  return (
    <>
      <MenuItem onClick={backToMainMenu}>
        <YBLabelWithIcon icon="fa fa-chevron-left fa-fw">{t('common.back')}</YBLabelWithIcon>
      </MenuItem>
      <MenuItem divider />
      <YBMenuItem onClick={showEnableYSQLModal}>
        {t('universeActions.editYSQLSettings.modalTitle')}
      </YBMenuItem>
      <YBMenuItem onClick={showEnableYCQLModal}>
        {t('universeActions.editYCQLSettings.modalTitle')}
      </YBMenuItem>
    </>
  );
};
