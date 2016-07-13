// Copyright (c) YugaByte, Inc.

var RegisterForm = React.createClass({
    handleSubmit: function(e) {
        e.preventDefault();
        $.ajax( {
            url: "/api/register",
            type: 'POST',
            data: $('#registerForm').serialize(),
            success: function(response) {
                sessionStorage.setItem("authToken", response.authToken);
                window.location.href = "/";
            },
            error: function(response) {
                if(response.status == 401) {
                    var errorJson = $.parseJSON(response.responseText);

                } else {
                    $("#registerFormAlert").hide();
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
                            <h3 className="panel-title">Register Customer</h3>
                        </div>
                        <div className="panel-body">
                            <form id="registerForm" onSubmit={this.handleSubmit}>
                                <fieldset>
                                    <YBInputBox name="name" type="text"
                                                placeholder="Name" label="Name" />
                                    <YBInputBox name="email" type="text"
                                                placeholder="E-mail" label="E-mail" />
                                    <YBInputBox name="password" type="password"
                                                placeholder="Password" label="Password" />
                                    <YBSubmitButton name="Register" />
                                </fieldset>
                            </form>
                            <YBErrorBox name="registerFormAlert" />
                        </div>
                    </div>
                </div>
            </div>
        );
    }
});

ReactDOM.render(<RegisterForm id="registerForm"/>, document.getElementById('content'));
