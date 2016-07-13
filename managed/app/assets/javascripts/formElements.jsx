// Copyright (c) YugaByte, Inc.

var YBInputBox = React.createClass({
  handleChange: function(event) {
    this.setState({value: event.target.value});
  },
  render: function() {
    return(
      <div className="form-group">
        <label for={this.props.name}>{this.props.label}</label>
        <input
          type={this.props.type} id={this.props.name} name={this.props.name}
          className="form-control" value={this.props.value}
          placeholder={this.props.placeholder}
          onChange={this.handleChange}></input>
        <small id={this.props.name} class="help-block"></small>
      </div>
    );
  }
});

var YBSubmitButton = React.createClass({
  render: function() {
    return(
      <input
        className="btn btn-lg btn-success btn-block"
        type="submit"
        value={this.props.name}></input>
    );
  }
});

var YBErrorBox = React.createClass({
  render: function() {
    return(
      <div
        className="alert alert-danger form-error-alert"
        id={this.props.name}></div>
    );
  }
});
