import PropTypes from "prop-types";
import React from "react";
import { bindActionCreators } from "redux";
import { connect } from "react-redux";
import Toaster from "./Toaster";
import { removeToast } from "../actions/toaster";

const Toasters = ({ actions, toasts }) => {
  const { removeToast } = actions;
  return (
    <ul>
      {toasts.map(toast => {
        const { id } = toast;
        return (
          <Toaster {...toast} key={id} onDismissClick={() => removeToast(id)} />
        );
      })}
    </ul>
  );
};

Toasters.propTypes = {
  actions: PropTypes.shape({
    removeToast: PropTypes.func.isRequired
  }).isRequired,
  toasts: PropTypes.arrayOf(PropTypes.object).isRequired
};

const mapDispatchToProps = dispatch => ({
  actions: bindActionCreators({ removeToast }, dispatch)
});

const mapStateToProps = state => {
  console.log('State -- ', state);
  return { toasts: state.toast}
};

export default connect(mapStateToProps, mapDispatchToProps)(Toasters);