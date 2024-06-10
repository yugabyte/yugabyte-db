Security
===============================================================================


Permissions
------------------------------------------------------------------------------

Here's an overview of what users can do depending on the priviledge they have:

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
by defautt the functions from the `pg_catalog` cannot be used in masking rules.

For more details, read the [Using pg_catalog functions] section.

[Using pg_catalog functions]: masking_functions.md#using-pg_catalog-functions



Security context of the functions
------------------------------------------------------------------------------

Most of the functions of this extension are declared with the `SECURITY INVOKER`
tag.
This means that these functions are executed with the privileges of the user
that calls them. This is an important restriction.

This extension contains another few functions declared with the tag
`SECURITY DEFINER`.

