# IWYU mappings #

One of the difficult problems for IWYU is distinguishing between which header
contains a symbol definition and which header is the actual documented header to
include for that symbol.

For example, in GCC's libstdc++, `std::unique_ptr<T>` is defined in
`<bits/unique_ptr.h>`, but the documented way to get it is to `#include
<memory>`.

Another example is `NULL`. Its authoritative header is `<cstddef>`, but for
practical purposes `NULL` is more of a keyword, and according to the standard
it's acceptable to assume it comes with `<cstring>`, `<clocale>`, `<cwchar>`,
`<ctime>`, `<cstdio>` or `<cstdlib>`. In fact, almost every standard library
header pulls in `NULL` one way or another, and we probably shouldn't force
people to `#include <cstddef>`.

To simplify IWYU deployment and command-line interface, many of these mappings
are compiled into the executable. These constitute the *internal mappings*.

However, many mappings are toolchain- and version-dependent. Symbol homes and
`#include` dependencies change between releases of GCC and are dramatically
different for the standard libraries shipped with Microsoft Visual C++. Also,
mappings such as these are usually necessary for third-party libraries
(e.g. Boost, Qt) or even project-local symbols and headers as well.

Any mappings outside of the default internal set can therefore be specified as
external *mapping files*.


## Internal mappings ##

IWYU's internal mappings are hard-coded in `iwyu_include_picker.cc`, and are
very GCC-centric. There are both symbol- and include mappings for GNU libstdc++
and libc.


## Mapping files ##

The mapping files conventionally use the `.imp` file extension, for "Iwyu
MaPping" (terrible, I know). They have the following general form:

    [
      { <directive>: <data> },
      { <directive>: <data> }
    ]

Directives can be one of the literal strings:

* `include`
* `symbol`
* `ref`

and data varies between the directives, see below.

Note that you can mix directives of different kinds within the same mapping
file.

The `.imp` format looks like [JSON](http://json.org/), but IWYU actually uses
LLVM's [YAML](https://yaml.org/) parser to interpret the mapping files, which
technically allows a richer syntax. We try to use a minimum of YAML features to
get basic functionality (trailing comma syntax, `#` comments), but please be
conservative to keep it easy to do machine rewrites.


### Include mappings ###

The `include` directive specifies a mapping between two include names (relative
path, including quotes or angle brackets.)

This is typically used to map from a private implementation detail header to a
public facade header, such as our `<bits/unique_ptr.h>` to `<memory>` example
above.

Data for this directive is a list of four strings containing:

* The include name to map from
* The visibility of the include name to map from
* The include name to map to
* The visibility of the include name to map to

For example;

    { "include": ["<bits/unique_ptr.h>", "private", "<memory>", "public"] }

Most of the original mappings were generated with shell scripts (as evident from
the embedded comments) so there are several multi-step mappings from one private
header to another, to a third and finally to a public header. This reflects the
`#include` chain in the actual library headers. A hand-written mapping could be
reduced to one mapping per private header to its corresponding public header.

Include mappings support a special wildcard syntax for the first entry:

    { "include": ["@<internal/.*>", "private", "<public>", "public"] }

The `@` prefix is a signal that the remaining content is a regex, and can be
used to re-map a whole subdirectory of private headers to a public facade
header.

The `include-what-you-use` program has a `--regex` argument to select dialect;

* `llvm`: a basic, fast implementation (default)
* `ecmascript`: a more capable, slower implementation with support for
  e.g. negative lookaround

The performance hit of `ecmascript` can be quite significant for large mapping
files with many regex patterns, so mind your step.


### Symbol mappings ###

The `symbol` directive maps from a qualified symbol name to its authoritative
header.

Data for this directive is a list of four strings containing:

* The symbol name to map from
* The visibility of the symbol
* The include name to map to
* The visibility of the include name to map to

For example;

    { "symbol": ["NULL", "private", "<cstddef>", "public"] }

The symbol visibility is largely redundant -- it must always be `private`. It
isn't entirely clear why symbol visibility needs to be specified, and it might
be removed moving forward.

Unlike `include`, `symbol` directives do not support the `@`-prefixed regex
syntax in the first entry. Track the [following
bug](https://github.com/include-what-you-use/include-what-you-use/issues/233)
for updates.

#### Template specializations ####

A bare template name in a symbol mapping corresponds to the primary template,
i.e. any use of its implicit specialization (and not any implicit specialization
of its partial specialization) matches the mapping. To map an explicit or
partial specialization, append the template argument list to the template name.
Note that, currently, IWYU expects a specific format of template argument lists.
For example, a list consisting of a pointer to `int` should be written as
`<int *>` and not as e.g. `<int*>`. When in doubt, run IWYU with verbosity level
6 on some code using the specialization, and look for a string like `Marked
full-info use of decl template-name<...> (from ...` to see how IWYU spells
the template arguments.

To denote a partial specialization, you should replace all the template
parameters in the template argument list with `:N`, where `N` is the parameter
number, starting from 0. Note that it is the number of the partial
specialization template parameter, not of any primary template parameter. For
example, for such a code:

    template <typename, typename, int>
    class Tpl;

    template <typename T1, typename T2>
    class Tpl<T1, T2, 5> {};

    template <typename T>
    class Tpl<T, T, 5> {};

the first partial specialization can be referred to in a mapping file as
`Tpl<:0, :1, 5>`, and the second one as `Tpl<:0, :0, 5>`. IWYU does not
currently have a proper support for mappings of partial specializations
of templates being members of other templates.

At present, only class and variable template specializations are supported, not
function template ones.


### Mapping refs ###

The last kind of directive, `ref`, is used to pull in another mapping file, much
like the C preprocessor's `#include` directive. Data for this directive is a
single string: the filename to include.

For example;

    { "ref": "more.symbols.imp" },
    { "ref": "/usr/lib/other.includes.imp" }

The rationale for the `ref` directive was to make it easier to compose
project-specific mappings from a set of library-oriented mapping files. For
example, IWYU might ship with mapping files for [Boost](http://www.boost.org),
the SCL, various C standard libraries, the Windows API, the [Poco
Library](http://pocoproject.org), etc. Depending on what your specific project
uses, you could easily create an aggregate mapping file with refs to the
relevant mappings.


### Command-line switches for mapping files ###

Mapping files are specified on the command-line using the `--mapping_file`
switch:

    $ include-what-you-use -Xiwyu --mapping_file=foo.imp some_file.cc

The switch can be added multiple times to add more than one mapping file.

If the mapping filename is relative, it will be looked up relative to the
current directory.

`ref` directives are first looked up relative to the current directory and if
not found, relative to the referring mapping file.

The internal mappings can be turned off (e.g. for baremetal projects like an OS
kernel) using the `--no_internal_mappings` switch:

    $ include-what-you-use -Xiwyu --no_internal_mappings \
        --mapping_file=kernel_libc.imp kernel/main.c

We used to ship `.imp` files to mirror IWYU's internal mappings to allow use of
`--no_internal_mappings` combined with edited internal mappings to let users
customize IWYU's default behavior for their environment. We no longer do, but
the internal mappings can now be exported from IWYU using:

    $ include-what-you-use -Xiwyu --export_mappings=./mappings

The generated `.imp` files are named directly after the symbols in
`iwyu_include_picker.cc`, so it should be straightforward to correlate and
adjust what needs to be changed. The internal mappings can then be replaced by
the customized ones using something like:

    $ include-what-you-use \
        -Xiwyu --no_internal_mappings \
        -Xiwyu --mapping_file=./mappings/libc_include_map.imp \
        -Xiwyu --mapping_file=./mappings/libc_symbol_map.imp \
        ... \
        myprogram.c

Note that the mapping export doesn't care about compilation mode or target
information, but rather just dumps out all mapping declarations, so you may need
to construct per-target subsets manually when using them explicitly.


## Generating mapping files ##

As part of the IWYU project, we maintain a set of scripts to generate mappings
for widely-used libraries. See the `mapgen/` directory for the available
generators. Most of them are best-effort for users to run in their own
environments and generate external mappings out of whatever header source tree
they have available.

But one of them, `mapgen/iwyu-mapgen-libstdcxx.py`, is used to generate the
internal mappings for GNU libstdc++ shipped with IWYU. The procedure for
refreshing internal mappings is:

```
$ mapgen/iwyu-mapgen-libstdcxx.py --lang=c++ \
    /usr/include/c++/11 \
    /usr/include/x86_64-linux-gnu/c++/11 \
    > libstdcxx_11.cc
```

The C++ mappings declarations are generated into a temporary file,
`libstdcxx_11.cc`, and can be copy/pasted from there into
`iwyu_include_picker.cc` to replace the `libstdcpp_include_map` table.

There are two placeholders above:

* `11` -- the version of libstdc++ installed
* `x86_64-linux-gnu` -- the default target for the system; this may vary
  depending on CPU architecture, etc

The generator script makes no difference between the plain `/usr/include` and
the per-target `/usr/include/x86_64-linux-gnu` directories and analyzes header
dependencies in both (they are both necessary, as some symbols are defined in
private per-target headers, but mapped to plain public headers).

The generated mappings obviously work best on systems where both libstdc++
version and target match, but they should port pretty well.

There is no strong policy for updating the internal mappings, we try to use a
mainstream target and a middle-aged libstdc++ version.

Another one, `mapgen/iwyu-mapgen-std-symbol.py`, is used to generate
`std_symbol_map.inc` file containing the C++ standard library symbol mapping.
That file is automatically included into `iwyu_include_picker.cc` by
a `#include` directive. To regenerate it, you should have a C++ standard LaTeX
source downloaded. You can obtain it by executing

    git clone https://github.com/cplusplus/draft.git

in any convenient directory. Then run the following command from
include-what-you-use directory:

```
$ mapgen/iwyu-mapgen-std-symbol.py path_to_draft/source > std_symbol_map.inc
```