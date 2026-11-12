# Redacting the LDAP bind password in YBA by parsing, not text scanning

Related tickets: PLAT-12639, PLAT-18364, PLAT-20181, PLAT-23621, PLAT-23622, PLAT-23624,
PLAT-23625, PLAT-23779, PLAT-23809.

## Problem

YSQL LDAP search+bind authentication needs a bind password, and Postgres only reads it from the
HBA record: `ldapbindpasswd=<password>` inside the `ysql_hba_conf_csv` gflag. YBA must not show
that password in API responses (and so the UI), its application log, support bundles, or audit
rows.

The rest of `ysql_hba_conf_csv` is ordinary config that users need to see and edit, so YBA
redacts only the password, not the whole value as the DB does (`TAG_FLAG(ysql_hba_conf_csv,
sensitive_info)` in `pg_wrapper.cc`).

Today most of that redaction is a text scan. `RedactingService.redactSensitiveGFlagFields`, the
`LOGS` path of `filterSecretFields`, serializes the whole JSON tree to a string, looks for
`ldapbindpasswd` with `LdapBindPasswdHbaFormat.redactAllValues`, and parses the result back,
storing a `_redactionParseError` wrapper when that fails. `redactSensitiveInfoInString` does the
same for task params (`Commissioner`), `ValidateGFlags` messages, and support bundle files.

At the point where it finds `ldapbindpasswd=`, the scanner doesn't know what encloses the value.
A quote in the password can appear as `"`, `""` (CSV field), `\"` (JSON string), `\"\"` (CSV field
in JSON), or deeper escapes (JSON inside a JSON string), and the HBA value itself may be quoted,
partly quoted or unquoted. The scanner has to guess where the password ends from the characters
around it. Each wrong guess leaks part of the password or breaks the enclosing JSON. That's
where the PLAT-20181 and PLAT-23624 parser rules come from, along with a 400 response
(`checkLdapBindPasswdQuoting`) for passwords the scanner can't find the end of.

## Goals

- Redact exactly the password, for every HBA value that Postgres accepts.
- Never edit JSON or other structured data as text.
- Keep the stored HBA value byte-for-byte apart from the password, so redacting the stored value
  is deterministic and the gflags round trip keeps working.

Not in scope: how `REDACTED` coming back in a request is merged
(`GFlagsUtil.mergeSensitiveGFlags`, PLAT-20181). Its behavior stays the same.

## Design

### 1. Redact a plain value the way the DB parses it

The input is the gflag value exactly as YBA stores it. The DB reads it in two layers, and the
redactor follows both.

**CSV layer** (`ReadCSVValues` in `src/yb/util/csv_util.cc`). Fields are separated by `,`. A
field containing `,` or `"` must be quoted, and `""` inside a quoted field stands for `"`. Each
field becomes one line of `ysql_hba.conf` (`WritePgHbaConfig` in `pg_wrapper.cc`). The redactor
splits the value into fields, remembering whether each one was quoted.

**HBA layer** (`next_token` and `parse_hba_line` in `src/postgres/src/backend/libpq/hba.c`):

- Tokens are separated by blanks or by commas outside quotes.
- `"` toggles quoting anywhere in a token and isn't part of the token. Inside quotes, `""` is a
  literal `"`.
- `#` outside quotes starts a comment that runs to the end of the line.
- Every token after the auth method must be `name=value`. Postgres splits it at the first `=`,
  and any other token makes the whole line invalid ("authentication option not in name=value
  format").

`next_token` is about 90 lines. Port it with character offsets into the decoded field.

**Replacement.**

1. For each `ldapbindpasswd` option, replace the value's characters in the decoded field (from
   just after `=` to the end of the token) with `REDACTED`.
2. Re-encode only the fields that changed, using each field's original quoting, and join them
   with the untouched raw text of the other fields.

Re-encoding a decoded CSV field with its original quoting gives back exactly the original
text, so every byte except the password is preserved.

**Invalid records.** If a record contains `ldapbindpasswd` but Postgres would reject it (for
example an unquoted password with a space or comma, which leaves a token that isn't
`name=value`), redact from the `ldapbindpasswd` value to the end of that CSV field. That redacts
too much, but only on a record the DB won't load anyway, and it never leaks the rest of a
password.

The same component also gives validation (section 5). It's a small class with no
knowledge of JSON.

### 2. Get to the plain value from structured data

| Location | How the plain value is found |
|---|---|
| JSON trees: API responses, task params, audit payloads, request bodies | Walk the tree. For every object field named `ysql_hba_conf_csv`, run section 1 on its text. `redactLdapBindPasswdInYsqlHbaConf` already does this walk for `APIS`; `LOGS` gets the same walk in place of `toString()` + scan |
| JSON stored in a string field | If a known field holds a serialized JSON document, parse it, walk it, and write it back. Never scan it as text |
| Audit `additional_details` entries `{"name", "old", "new", "default"}` | By gflag name, as #36916 already does |
| `server.conf` / gflag files | One flag per line; the plain value is everything after `--ysql_hba_conf_csv=` on that line |
| Gflag maps (`Map<String, String>`) | Look up the key |

Whole-value secrets need no parsing and are redacted by key in the same walk:
- `ycql_ldap_bind_passwd`;
- `ldapServiceAccountPassword`;
- `ldapBindPassword`;
- the sensitive gflags from `getSensitiveGflagsForRedaction`.

### 3. Remove the text-scan paths

- `redactSensitiveGFlagFields` becomes a tree walk (section 2). The `Json.parse` of rewritten text
  and the `_redactionParseError` fallback go away.
- Callers of `redactSensitiveInfoInString` that hold an object (task params in `Commissioner`,
  gflag maps in `ValidateGFlags`) redact the object before turning it into text.
- `LdapBindPasswdHbaFormat.redactAllValues` is deleted.

### 4. Free text that can't be parsed

Some text has no structure to rely on:
- YBA log files written by older versions, collected into support bundles;
- shell output passed to `redactShellProcessOutput`.

There, redact from `ldapbindpasswd` to the end of the line. Redacting too much in a log line
costs nothing, and the rule needs no knowledge of quoting. New YBA code must not log a
`ysql_hba_conf_csv` value that hasn't been through section 2.

### 5. Validation

Replace `checkLdapBindPasswdQuoting` with an HBA validity check built on the same tokenizer:
reject a new `ysql_hba_conf_csv` that has a record Postgres would reject, with Postgres's own
reason. The quoting rule goes away because every valid value is now redacted exactly. Keep the
existing exception: values already stored on the universe pass, so an old value doesn't block
unrelated edits.

### 6. Round trip

`mergeSensitiveGFlags` keeps its rules:
1. redact the stored value and compare it with the incoming one;
2. if they're equal, keep the stored value;
3. otherwise reject.

Section 1 makes the comparison reliable: redaction keeps every byte except the password, so the
same stored value always redacts to the same string.

## Upgrade and rollback

- No migration and no new flag. `yb.api.enable_gflags_sensitive_data_redaction` still controls
  redaction in API responses.
- The redacted `ysql_hba_conf_csv` in v1 responses changes only where the current heuristics get
  it wrong. A value fetched before the upgrade and sent back unchanged can fail the round-trip
  comparison for those shapes; fetching it again fixes that. PLAT-23624 has the same caveat.
- Existing `_redactionParseError` audit rows stay as they are. Bump the
  `R__Redact_Secrets_From_Audit` checksum so other rows are re-redacted with the new code.
- New values Postgres would reject now get 400 instead of reaching the nodes, where the DB
  refuses to load them.

## Testing

- **Agreement with Postgres.** Generate HBA records (quoting, partial quoting, `""`, `#`, blanks,
  commas, invalid records) and load them into a real Postgres. Read `pg_hba_file_rules`, which
  shows the parsed `ldapbindpasswd=` option and an `error` column for rejected lines. Commit the
  result as a golden file, and have a unit test check that the tokenizer returns the same
  password, or the same rejection, for every record.
- **Encodings.** The existing `LdapBindPasswdHbaFormatRandomizedTest` corpus, run through the
  JSON, nested-JSON, `server.conf` and gflag-map paths. The password never appears in the output,
  everything else is byte-identical, and every JSON result parses.
- **Round trip.** Redact, merge back with `mergeSensitiveGFlags`, and expect the stored value.

## Alternatives considered

- **Keep text scanning and add rules (current approach).** Every new encoding or quoting shape
  needs another rule, and a mistake either leaks the password or breaks JSON.
- **Redact the whole `ysql_hba_conf_csv` value whenever it has `ldapbindpasswd`.** Simple, and
  how the DB behaves, but users lose sight of their HBA rules and must re-enter the whole value
  to change anything.
- **Keep the password out of the HBA value.** The DB already supports
  `ldapbindpasswd=YSQL_LDAP_BIND_PWD_ENV`, which makes Postgres read the password from that
  environment variable (`get_ldap_password` in `auth.c`, #13367). YBA would store the password as
  a secret and set the variable on the tserver (systemd environment on VMs, a Secret on
  Kubernetes), and could then reject literal passwords in new values.
  - This removes the round trip for new universes.
  - But changing the password then needs a rolling restart, and existing values still need
    redaction.
  - It works alongside this design and isn't required by it.
