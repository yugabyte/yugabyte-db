# Review guide for yba-cli

For anyone, human or AI agent, reviewing a change to the `yba` CLI: how it fits the codebase
and holds up over time.
This applies in addition to [managed/REVIEW.md](../REVIEW.md). `make fmt` (golines, 100
columns) owns formatting and line length; do not flag them.

Verify every claim about YBA server behaviour yourself against the YBA source in `managed/src/`:
controllers, `api/v2/handlers/`, form classes under `forms/`, and the task classes. Check
`master`, and the current stable branch when the claim depends on the version. Flag a claim only
when the source contradicts it.

The CLI is an API client that people and automation run against live universes. Scripts, the
YBA MCP server and support runbooks call it without a human reading each prompt. The worst
failure is a command that exits 0 and silently changes something the user did not ask for: a
reset field, a moved master, a turned-off feature. Report at **HIGH** a finding that can change
or delete state the user did not name, leak a secret, or leave a universe locked; report the rest
at **MEDIUM**.

## Change shape

- One logical change per PR. A shared-helper refactor ships first, as its own PR. A Go client
  bump ships with the feature that needs it.
- A defect found in review is removed in the same change, or filed with a concrete fix plan. A
  disabled path left in the tree (a commented-out check, a flag that can never be set) is a
  finding.

## Update and edit commands

Most CLI bugs that reach users are update commands that change more than the user asked for.

- **Start from the current object.** An update reads the object, changes only the fields the
  user named, and sends it back. A request built from an empty struct is a finding unless the
  server API is a true partial update.
- **Copy, do not rebuild.** Converting a response type into a request type field by field drops
  every field the conversion forgets, and every field added to the server later. Send the object
  as read, or convert with a helper that has a test which fails when a field is not copied.
- **Only changed flags.** On update, set a field only when the user gave its flag.
  - Use `cmd.Flags().Changed("<flag>")` for a bool, a number, a list, and a string that the user
    may clear to `""`. The flag's presence is the change request: `--enable-x=false` and
    `--port 0` are values to send, not defaults to skip.
  - A `util.IsEmptyString` guard is enough for a string that can never be empty on the server
    (a name, a UUID, a password).
  - A flag default (`""`, `0`, `false`, a hard-coded value) sent without one of these checks
    overwrites the stored value.
- **Server defaults.** For every field the request leaves out, read the server form class
  (`forms/`, `api/v2/models`) and check its default. A non-null default (default ports, a 24 h
  interval, `null` meaning "delete all") replaces the stored value. Send the current value, and
  file the server bug: a missing field must mean "keep".
- **Boolean flags that describe state** (`--root-and-client-root-ca-same`, `--use-time-sync`)
  take their default from the object read, not from a constant.
- **No side effects.** A command never turns on a server feature or a runtime config key that the
  user did not name. A new behaviour gets its own flag, default off.
- **Required flags** are required on create. On update, a flag whose stored value exists is
  optional.
- **Upsert APIs.** When the server upserts (add on an existing name replaces it), the CLI checks
  for an existing object and fails with a message that names the update command.

## Selecting the object

- A mutating command (update, delete, edit, upgrade, pause, any `--force` path) matches names
  **exactly**. Substring match is for `list` and `describe` filters only.
- Use the server's case rule. When the server compares with `ieq`, the CLI uses
  `strings.EqualFold`.
- After filtering, zero matches is an error. Taking `[0]` is correct only after an exact match
  on a name that YBA keeps unique per customer (universe, storage config, KMS config,
  certificate label). After a substring match, or on a name that is not unique, more than one
  match is an error that lists the candidates; taking `[0]` there is a finding.
- Filter on every attribute that makes the object unique: name and CPU architecture for image
  bundles, platform and architecture for release artifacts, the type named by `--instance-type`
  rather than the provider's first type.
- A command that reads a paged API (an offset and a limit) reads every page, not only the
  first. Its loop rebuilds the request after it changes the offset, as
  `cmd/alert/configuration/list_configuration.go` does. A loop that reuses the first request
  fetches the first page again and again.

## Server contract

- **Go client pin.** The CLI decodes responses through the pinned `platform-go-client` in
  `go.mod`. A field missing from that model is dropped on read and reset on write. When a command
  sends back an object whose model is older than the server, update the client: run
  `sbt swaggerGen`, then `make publish` in `managed/client/go` (it opens a draft PR in
  `platform-go-client`), and after that PR merges, `make updateclient` here, in the same change
  as the feature. Every client PR needs an approval from Deepti-yb; `make publish` requests it.
  When Deepti-yb opened the client PR, another YBA engineer approves it. A bump to a client commit
  that no one approved is a finding.
- **New APIs use the generated client.** A command for a new API calls it through the generated
  client. A hand-written model or a call through `internal/client/rest.go` for a new API is a
  finding; publish the client instead. The existing hand-written models and REST calls are for
  older APIs that the client cannot decode.
- **Client bump with API breaks.** The client PR body lists the Go API changes that break callers
  (apidiff). A change that runs `make updateclient` fixes every listed break the CLI uses in the
  same change. A listed break in a GA API with no reason in the yugabyte-db PR is a finding.
- **Version gates.** The CLI supports YBA from `MinCLIStableVersion` and `MinCLIPreviewVersion`
  (`cmd/util/constants.go`). A command or flag that needs a newer API calls
  `CheckValidYBAVersionForCommand` (or a `*YBAVersionCheck` helper in `internal/client/`) with
  **both** a stable and a preview minimum, verified on `master` and the stable branch. Play
  answers an unknown route with a plain-text 404, so an ungated call fails with a confusing error
  on an older YBA.
- **Validation belongs on the server.** When a rule exists only in the UI or only in the CLI
  (pause on on-prem, architecture match, a GET that reads a different source than the apply
  path), the fix goes into YBA. A CLI check may be added on top, for a clearer message only.
- **UI parity.** When the UI form and the server form disagree on a default (`useTimeSync`:
  UI true, server false), a CLI create takes the UI's value, so a universe created by the CLI
  matches one created in the UI.
- **Every gate lists all its conditions.** A command or usage text that states when something
  works names each condition: version floor, runtime config key, prior setup step, VM or
  Kubernetes. "On universes that support it" is a finding.
- **Runtime config keys.** Code never assumes a runtime config key holds its default. When a
  command works only with a key set, it reads the key first and fails with an error that names
  the key.
- **v1 and v2.** A command that moves from a v1 to a v2 API keeps its flags, output and exit
  codes. A behaviour change hidden in an API migration is a finding.

## Sibling paths

A fix on one path must hold on its siblings, or the PR must say why a sibling is out of scope.

- **VM and Kubernetes:** a fix on a VM universe or provider path gets the matching Kubernetes fix
  in the same change. When that cannot be done, the PR says so.
- **Clusters:** primary and read replica. `create-read-replica` builds its user intent from the
  primary's; a field added to one must be added to the other.
- **Server types:** tserver and dedicated master. `buildDeviceInfo` and `buildMasterDeviceInfo`
  must apply the same inputs (volume encryption, mount points, storage class).
- **Providers:** AWS, GCP, Azure, OCI, on-prem and Kubernetes each have their own create, update
  and instance-type commands. A fix in one provider's command is checked in the others.
- **Command families:** describe, update, delete and test-alert of one object share selection
  and paging code. A fix in one is checked in all of them.
- **Other clients:** the Terraform provider and the YBA MCP server reuse the same APIs and often
  the same request shapes. A server bug found through the CLI is filed for the server.

## Commands, flags and output are contract

Users upgrade the CLI without reading release notes. The first question for any change to an
existing command: what does the same command line, in an existing script, do after the upgrade?
Any answer but "the same thing" is a finding, unless the old behaviour changed state the user
did not ask for. A note in the help text or the release notes does not make a change safe.

- Command paths, flag names, short flags, flag defaults, JSON output (`-o json`) fields and exit
  codes are used by scripts and the MCP server. To rename a GA flag, add the new name and keep
  the old one with `MarkDeprecated`, whose message names the replacement. Remove a deprecated GA
  flag only after a deprecation period that the PR states, with a "Breaking change" note (as
  `--enable-volume-encryption` was, a year after its deprecation, in PLAT-23249). Removing a GA
  flag or command without that deprecation is a finding. A preview flag or command may change
  without it.
- Changing a flag's default changes what every existing script does. It is a finding unless the
  old default was a bug that changed state the user did not ask for.
- A command that starts a YBA task honours the global `--wait` flag (default true) and waits
  through the shared `WaitForTask` helpers. With `--wait=false` it prints the task, so the
  caller can poll it with `yba task`.
- A destructive command (delete, pause, a rolling restart, `--skip-validations`) asks through
  `util.ConfirmCommand` and skips the prompt only with `--force`.
- Every new command or flag is added as preview first, through `util.PreviewCommand` or
  `util.PreviewFlag`. It shows only with `YBA_FF_PREVIEW=true`, and its help says it may change.
  A new command or flag added straight to GA is a finding. Moving it to GA is a separate change,
  after the command has been used against a live YBA and its flags and output are stable.

## Errors, output and secrets

- API errors go through the helpers that decode YBA's error body. In a command's `Run`, a failed
  call ends with `util.FatalHTTPError(resp, err, entity, operation)`. A helper that returns an
  error, so its caller can fall back or add context, returns
  `util.ErrorFromHTTPResponse(resp, err, entity, operation)`. Printing the raw `err` or response
  body is a finding.
- Other failures use `logrus.Fatalf` with `formatter.Colorize(..., formatter.RedColor)`. An API
  error swallowed into a success message, or a `return` after a failed call that exits 0, is a
  finding.
- No unchecked type assertion on `map[string]interface{}` data from the server. A missing key
  panics; use `v, ok := ...` and treat a missing key as unset.
- A secret (password, token, secret ID, kubeconfig, webhook URL, API key) is never printed in
  table, JSON or log output, and never echoed in an error. When the server returns a secret
  unmasked, file it for the server and mask it in the formatter meanwhile.
- A secret flag on update is sent only when given. Sending `""` for an unchanged password clears
  it or sends an empty credential.
- Uploads and other long calls do not use the REST client's fixed 30 s timeout. They derive the
  timeout from `--timeout` and stream large bodies instead of buffering them in memory.

## Code and usage conventions

- **Reuse first.** For each new function in the diff, search for an existing one that does the
  same job. Do not rely on a fixed list; util code lives in packages and single files across
  the tree. Find it with `find cmd internal -name '*util*'`, then grep those paths,
  `internal/client/` and `internal/formatter/` for the job (for example "paginate", "wait",
  "confirm"). A new helper that does what an existing one does is a finding. A helper that two
  command families need goes into `cmd/util/`; it is not copied. Use the existing small
  helpers: `util.IsEmptyString` for empty checks, `util.Get*Pointer` for optional request fields.
- **Names follow the public feature name** (UI and docs), never the Java class or model field.
  The Terraform provider and the MCP server take their names from the CLI, so a poor name
  spreads.
- **Command names and aliases.** Use the standard verbs and their aliases: `list` (`ls`),
  `describe` (`get`), `create` (`add`), `update` (`edit`), `delete` (`remove`, `rm`). Get the
  client with `ybaAuthClient.NewAuthAPIClientAndCustomer()`.
- **Flag names** are kebab-case (`--storage-config-name`). The short flags `-n` (name),
  `-u` (uuid), `-f` (force) and `-s` (skip-validations) mean the same thing on every command;
  a new flag does not reuse one of these letters for another meaning.
- **PreRun checks input, Run calls the API.** A command with input that `MarkFlagRequired` cannot
  check (an empty value, flags that exclude each other, a flag needed only with another) checks
  it in `PreRun`. When a needed value is missing, it calls `cmd.Help()` and exits with a red
  error, before any API call. A command whose only input is required flags, or none (most
  `list` commands), needs no `PreRun`. A version gate runs before the first API call that needs
  it.
- **Copyright header.** Every new Go file starts with the `Copyright (c) YugabyteDB, Inc.` header.
- **Flag usage text** starts with `[Required]` or `[Optional]`. An always-required flag also calls
  `MarkFlagRequired`. A flag that is required only in some cases is `[Optional]` and names the
  case in green: `formatter.Colorize("Required when <case>", formatter.GreenColor)`. Flags that
  must be given together use `MarkFlagsRequiredTogether`.
- **Messages end with `\n`.** Every `logrus.Fatalf` and `logrus.Infof` message ends with a newline,
  or the next output or shell prompt runs onto the same line. Errors are red
  (`formatter.RedColor`); the object name in a success message is green
  (`formatter.GreenColor`).
- **Output goes through `internal/formatter`.** `list`, `describe` and the result of a create or
  update print through the object's formatter context, so `-o table`, `json`, `pretty` and `raw`
  all work. Printing an object with `fmt.Print` is a finding.

## Tests

- `make test` in `managed/yba-cli` is an empty target and runs nothing. Run
  `go test -vet=off ./...`: plain `go test` fails on vet's printf check for the existing
  `logrus.Fatalf(formatter.Colorize(...))` calls, before any test runs.
- Every fix to an update or edit command adds a round-trip test: read the object, run the
  command with one flag, read it again, and check that every field the flag does not name is
  unchanged. Use a fixture whose optional fields hold non-default values (custom ports,
  `dedicatedTo`, `gflagGroups`, `useTimeSync`, volume encryption).
- A selection change adds fixtures with overlapping names (`admin`, `admin-ro`), names that
  differ only by case, and objects that differ only by architecture or platform.
- Keep only tests that exercise a decision, a merge rule, a selection rule or a guard. Delete
  tests that check setters or exact message text.
- The test covers the claim in the commit message. "Keeps the stored value" needs a fixture
  where the stored value differs from the default, not only an empty object.
- Code that detects a server condition by matching error text has a unit test for the match, so
  a YBA wording change fails a test instead of changing behaviour silently.
- A known YBA bug that blocks a test is a `t.Skip` with the ticket in a short comment, next to
  the test.
- A command that changes a universe is also run once against a live YBA before merge. The PR
  names the YBA version and the command lines used.

## Docs

- `docs/` is generated by `make doc` from the command tree. A change to a command, flag or usage
  text regenerates `docs/` in the same change. Review the Go source, not the generated pages.
- Usage text says what the flag does and its default, in the user's words. YBA class names,
  ticket numbers and file lines do not belong in usage text.
- A YBA version in usage text or docs is a stable release (`2025.2.0.0`). Preview versions
  (`2.29.0.0`) and `-bN` builds are internal: the code may gate on them, the help never shows
  them.
- Help text describes supported behaviour. It does not warn that a feature is broken; file the
  bug instead.
- A command's `Example` shows a working command line for each mutually exclusive flag set.
- Usage text, a command's `Long` and `Example`, the docs and the commit message match the code in
  the same diff. A contradiction between them is a finding.

## Where to look harder, and what to skip

- `cmd/universe/universeutil/` and `cmd/universe/build_universe.go`: every universe create and
  edit builds its request here. A dropped field here reaches every universe.
- `cmd/universe/edit/` and `cmd/universe/upgrade/`: each command is a rolling restart or a
  placement change on a live universe. Check what the request carries over from the current
  universe.
- `internal/client/client.go`: the HTTP client, version checks and timeouts for every command.
- `cmd/util/`: shared helpers. A change here changes every command.
- Skip `docs/` (generated), `go.sum`, and release binaries and tarballs in the tree.
