Security
===============================================================================


Permissions
------------------------------------------------------------------------------

Here's an overview of what users can do depending on the privileges they have:

| Action                                   | Superuser | Owner | Masked Role |
| :--------------------------------------- | :-------: | :---: | :---------: |
| Create the extension                     |    Yes    |       |             |
| Drop the extension                       |    Yes    |       |             |
| Init the extension                       |    Yes    |       |             |
| Reset the extension                      |    Yes    |       |             |
| Configure the extension                  |    Yes    |       |             |
| Put a mask upon a role                   |    Yes    |       |             |
| Start dynamic masking                    |    Yes    |       |             |
| Stop  dynamic masking                    |    Yes    |       |             |
| Create a table                           |    Yes    |  Yes  |             |
| Declare a masking rule                   |    Yes    |  Yes  |             |
| Insert, delete, update a row             |    Yes    |  Yes  |             |
| Static Masking                           |    Yes    |  Yes  |             |
| Select the real data                     |    Yes    |  Yes  |             |
| Regular Dump                             |    Yes    |  Yes  |             |
| Anonymous Dump                           |    Yes    |  Yes  |             |
| Use the masking functions                |    Yes    |  Yes  |     Yes     |
| Select the masked data                   |    Yes    |  Yes  |     Yes     |
| View the masking rules                   |    Yes    |  Yes  |     Yes     |



Limit masking filters only to trusted schemas
------------------------------------------------------------------------------

By default, the database owner can only write masking rules with functions
that are located in the trusted schemas which are controlled by the superusers.

Out of the box, only the `anon` schema is declared as trusted. This means that
by default the functions from the `pg_catalog` cannot be used in masking rules.

For more details, read the [Using pg_catalog functions] section.

[Using pg_catalog functions]: masking_functions.md#using-pg_catalog-functions



Timing attacks in LDP functions
------------------------------------------------------------------------------

> This section is intended for maintainers of this extension, not end users.

The GRRM perturbation function (`anon.ldp_grrm`) decides at runtime whether to
keep the original value or replace it with a random lie. A naive implementation
using an `if/else` branch can leak which path was taken through differences in
execution time. An attacker who can measure response times precisely enough could
use this to figure out whether a particular response is the true value or a
perturbed one, which defeats the whole purpose of the differential privacy
guarantee.

To prevent this, the function uses **branchless bitwise selection**: both the
true value and the lie value are always computed, and a bitmask is used to pick
one of the two without any conditional jump. This makes the execution time
constant regardless of which path is taken.

If you modify this function or add new LDP perturbation methods, keep this
pattern in mind. Avoid `if/else` or `match` on any value that depends on the
random keep-or-lie decision. Instead compute both outcomes and select with
bitwise operations.


Security context of the functions
------------------------------------------------------------------------------

Most of the functions of this extension are declared with the `SECURITY INVOKER`
tag.
This means that these functions are executed with the privileges of the user
that calls them. This is an important restriction.

This extension contains another few functions declared with the tag
`SECURITY DEFINER`.
