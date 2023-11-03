import { Col, Row } from 'react-bootstrap';
import { Field } from 'redux-form';
import { YBButton, YBSelectWithLabel } from '../../../../common/forms/fields';

export const ChangeOrAddProvider = ({
  selectProvider,
  configuredProviders,
  providerType,
  setCurrentViewCreateConfig
}) => {
  const currentCloudProviders =
    configuredProviders?.data?.filter?.((provider) => provider.code === providerType) || [];

  return (
    <Row className="provider-row-flex" data-testid="change-or-add-provider">
      <Col md={2}>
        <Field
          name="change.provider"
          type="select"
          component={YBSelectWithLabel}
          label="Change provider"
          onInputChanged={selectProvider}
          options={[
            currentCloudProviders.map((cloudProvider) => (
              <option key={cloudProvider.uuid} value={cloudProvider.uuid}>
                {cloudProvider.name}
              </option>
            ))
          ]}
        />
      </Col>
      <Col md={2}>
        <div className="yb-field-group add-provider-col">
          <YBButton
            btnClass="btn btn-orange add-provider-config"
            btnText="Add Configuration"
            onClick={setCurrentViewCreateConfig}
          />
        </div>
      </Col>
    </Row>
  );
};
