---
title: AUTH
---
This command is to authenticate a client connection to Yugabyte Redis Server.

## SYNOPSIS
% <code>AUTH password</code>
<li>YugaByte server can be setup to require a password before allowing a client to execute Redis commands. The password verification can be enforced by setting the requirepass option in server configuration file.</li>
<li>If the given <code>password</code> matches with the server configured password, server returns the status string "OK" and begins processing commands from the authenticated client.</li>
<li>If the given <code>password</code> does not match with the server configured password, an error is raised</li>

## RETURN VALUE
Returns a status string.

## EXAMPLES
% <code>AUTH "yugapass"</code><br>
% "OK"<br>

## SEE ALSO
