import React, { FC } from 'react';
import { I18n } from '../../../../uikit/I18n/I18n';
import './Summary.scss';
import { WizardStepsFormData } from '../../UniverseWizard';

const universeNameStub = 'Universe Name';
const providerNameStub = 'Provider Name';

interface SummaryProps {
  formData: WizardStepsFormData;
}

export const Summary: FC<SummaryProps> = ({ formData }) => {
  // TODO: trigger "/universe_resources" api on form data change

  return (
    <div className="wizard-summary">
      <div className="wizard-summary__title">
        <I18n>Summary</I18n>
      </div>
      <div className="wizard-summary__content">
        <div className="wizard-summary__universe">
          {formData.cloudConfig.universeName || universeNameStub}
        </div>
        <div className="wizard-summary__provider">
          {formData.cloudConfig.provider?.code || providerNameStub}
        </div>
        <div>
          <div>...</div>
          <div>...</div>
          <div>...</div>
          <div>...</div>
          <div>...</div>
        </div>
      </div>
    </div>
  );
};
