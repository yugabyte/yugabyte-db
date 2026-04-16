import { connect } from 'react-redux';
import { UserManagement } from './UserManagement';

const mapDispatchToProps = () => {
  return {};
};

function mapStateToProps(state) {
  return {
    currentUserInfo: state.customer.currentUser?.data
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UserManagement);
