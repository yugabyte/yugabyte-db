// Copyright (c) YugaByte, Inc.
import { Component } from 'react';
import { browserHistory, Link } from 'react-router';
import { YBBanner, YBBannerVariant } from '../common/descriptors';
import { isNonAvailable, isNotHidden, showOrRedirect } from '../../utils/LayoutUtils';
import UserProfileForm from './UserProfileForm';
import { YBLoading } from '../common/indicators';
import { getPromiseState } from '../../utils/PromiseUtils';
import { isRbacEnabled, RBAC_USER_MNG_ROUTE } from '../../redesign/features/rbac/common/RbacUtils';

const BannerContent = () => (
  <>
    <b>Note!</b> “Users” page has moved. You can now{' '}
    <Link className="p-page-banner-link" to={ isRbacEnabled() ? RBAC_USER_MNG_ROUTE : "/admin/user-management"}>
      access Users page
    </Link>{' '}
    from the {isRbacEnabled ? "Access" : "User"} Management section under Admin menu.
  </>
);

export default class CustomerProfile extends Component {
  constructor(props) {
    super(props);
    this.state = {
      statusUpdated: false,
      updateStatus: ''
    };
  }

  componentDidMount() {
    const { customer, runtimeConfigs } = this.props;
    if (!runtimeConfigs) {
      this.props.fetchGlobalRunTimeConfigs();
    }
    this.props.getCustomerUsers();
    this.props.validateRegistration();
    if (isNonAvailable(customer.features, 'main.profile')) browserHistory.push('/');
  }

  handleProfileUpdate = (status) => {
    this.setState({
      statusUpdated: true,
      updateStatus: status
    });
  };

  render() {
    const { customer = {}, apiToken, customerProfile, runtimeConfigs, OIDCToken } = this.props;
    const isOIDCEnhancementEnabled =
      runtimeConfigs?.data?.configEntries?.find(
        (c) => c.key === 'yb.security.oidc_feature_enhancements'
      ).value === 'true';

    if (!isRbacEnabled() && (getPromiseState(customer).isLoading() || getPromiseState(customer).isInit())) {
      return <YBLoading />;
    }

    showOrRedirect(customer.data.features, 'main.profile');

    let profileUpdateStatus = <span />;
    if (this.state.statusUpdated) {
      if (this.state.updateStatus === 'updated-success') {
        profileUpdateStatus = (
          <span className="pull-right request-status yb-success-color yb-dissapear">
            Profile Updated Successfully
          </span>
        );
      } else {
        profileUpdateStatus = (
          <span className="pull-right request-status yb-fail-color yb-dissapear">
            Profile Update Failed
          </span>
        );
      }
      setTimeout(() => {
        this.setState({ statusUpdated: false });
      }, 2000);
    }

    return (
      <>
        {isNotHidden(customer.data.features, 'profile.banner') && (
          <YBBanner
            className="p-page-banner"
            variant={YBBannerVariant.WARNING}
            showBannerIcon={false}
          >
            <BannerContent />
          </YBBanner>
        )}
        <div className="dashboard-container">
          <div className="tab-content">
            <h2 className="content-title">User Profile {profileUpdateStatus}</h2>

            <UserProfileForm
              customer={this.props.customer}
              customerProfile={customerProfile}
              apiToken={apiToken}
              OIDCToken={OIDCToken}
              handleProfileUpdate={this.handleProfileUpdate}
              isOIDCEnhancementEnabled={isOIDCEnhancementEnabled}
              {...this.props}
            />
          </div>
        </div>
      </>
    );
  }
}
