// Copyright (c) YugaByte, Inc.

var LoginForm = React.createClass({
  handleSubmit: function(e) {
    e.preventDefault();
    $.ajax( {
      url: "/api/login",
      type: 'POST',
      data: $('#loginForm').serialize(),
      success: function(response) {
        sessionStorage.setItem("authToken", response.authToken);
        window.location.href = "/";
      },
      error: function(response) {
        if(response.status == 401) {
          var errorJson = $.parseJSON(response.responseText);
          $("#loginFormAlert").text(errorJson.error);
          $("#loginFormAlert").show();
        } else {
          $("#loginFormAlert").hide();
          parseFormErrorResponse(response);
        }
      }
    });

  },

  render: function() {
    return (
      <div className="row">
        <div className="col-sm-4 col-sm-offset-4">
          <div className="panel panel-default login-panel">
            <div className="panel-heading">
              <h3 className="panel-title">Please log in</h3>
            </div>
            <div className="panel-body">
              <form id="loginForm" onSubmit={this.handleSubmit}>
                <fieldset>
                  <YBInputBox name="email" type="text"
                    placeholder="E-mail" label="E-mail" />
                  <YBInputBox name="password" type="password"
                    placeholder="Password" label="Password" />
                  <YBSubmitButton name="Login" />
                </fieldset>
              </form>
              <YBErrorBox name="loginFormAlert" />
            </div>
          </div>
        </div>
      </div>
    );
  }
});

ReactDOM.render(<LoginForm id="loginForm"/>, document.getElementById('content'));
