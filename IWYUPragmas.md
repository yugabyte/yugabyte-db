# IWYU pragmas #

IWYU pragmas are used to give IWYU information that isn't obvious from the
source code, such as how different files relate to each other and which includes
to never remove or include.

All pragmas start with `// IWYU pragma: ` or `/* IWYU pragma: `. They are
case-sensitive and spaces are significant.


## IWYU pragma: keep ##

This pragma applies to a single `#include` directive or forward declaration. It
forces IWYU to keep an inclusion even if it is deemed unnecessary.

    main.cc:
      #include <vector> // IWYU pragma: keep

      class ForwardDeclaration; // IWYU pragma: keep

In this case, `std::vector` isn't used, so `<vector>` would normally be
discarded, but the pragma instructs IWYU to leave it. Similarly the class
`ForwardDeclaration` isn't used but is kept because of the pragma on it.


## IWYU pragma: begin_keep/end_keep ##

This pragma applies to a set of `#include` directives and forward
declarations. It declares that the headers and forward declarations in between
are to be left alone by IWYU.

    main.cc:
      // IWYU pragma: begin_keep
      #include <vector>
      #include <iostream>

      class MyClass;
      // IWYU pragma: end_keep

In the provided case nothing within the bounds of `begin_keep` and `end_keep`
will be discarded.


## IWYU pragma: export ##

This pragma applies to a single `#include` directive or forward declaration. It
says that the current file is to be considered the provider of any symbol from
the included file or declaration.

    facade.h:
      #include "detail/constants.h" // IWYU pragma: export
      #include "detail/types.h" // IWYU pragma: export
      #include <vector> // don't export stuff from <vector>

      class Other; // IWYU pragma: export

    main.cc:
      #include "facade.h"

      // Assuming Thing comes from detail/types.h and MAX_THINGS from
      // detail/constants.h
      std::vector<Thing> things(MAX_THINGS);

      // Satisfied with forward-declaration from facade.h
      void foo(Other* thing);

Here, since `detail/constants.h`, `detail/types.h` and `Other` have all been
exported, IWYU is happy with the `facade.h` include for `Thing` and `MAX_THINGS`
and does not suggest a local forward declaration for `Other`.

In contrast, since `<vector>` has not been exported from `facade.h`, it will be
suggested as an additional include.


## IWYU pragma: begin_exports/end_exports ##

This pragma applies to a set of `#include` directives or forward
declarations. It declares that the including file is to be considered the
provider any symbol from contained included files or declarations. This is the
same as decorating every line with `IWYU pragma: export`.

    facade.h:
      // IWYU pragma: begin_exports
      #include "detail/constants.h"
      #include "detail/types.h"
      class Other;
      // IWYU pragma: end_exports

      #include <vector> // don't export stuff from <vector>


## IWYU pragma: private ##

This pragma applies to the current header file. It says that any symbol from
this file will be provided by another, optionally named, file.

    private.h:
      // IWYU pragma: private, include "public.h"
      struct Private {};

    private2.h:
      // IWYU pragma: private
      struct Private2 {};

    public.h:
      #include "private.h"
      #include "private2.h"

    main.cc:
      #include "private.h"
      #include "private2.h"

      Private p;
      Private2 i;

Using the type `Private` in `main.cc` will cause IWYU to suggest that you
include `public.h`.

Using the type `Private2` in `main.cc` will cause IWYU to suggest that you
include `private2.h`, but will also result in a warning that there's no public
header for `private2.h`.


## IWYU pragma: no_include ##

This pragma applies to the current source file. It declares that the named file
should not be suggested for inclusion by IWYU.

    private.h:
      struct Private {};

    unrelated.h:
      #include "private.h"
      ...

    main.cc:
      #include "unrelated.h"
      // IWYU pragma: no_include "private.h"

      Private i;

The use of `Private` requires including `private.h`, but due to the `no_include`
pragma IWYU will not suggest `private.h` for inclusion. Note also that if you
had included `private.h` in `main.cc`, IWYU would suggest that the `#include` be
removed.

This is useful when you know a symbol definition is already available via some
unrelated header, and you want to preserve that implicit dependency.

The `no_include` pragma is somewhat similar to `private`, but is employed at
point of use rather than at point of declaration.


## IWYU pragma: no_forward_declare ##

This pragma applies to the current source file. It says that the named symbol
should not be suggested for forward-declaration by IWYU.

    public.h:
      struct Public {};

    unrelated.h:
      struct Public;
      ...

    main.cc:
      #include "unrelated.h" // declares Public
      // IWYU pragma: no_forward_declare Public

      Public* i;

IWYU would normally suggest forward-declaring `Public` directly in `main.cc`,
but `no_forward_declare` suppresses that suggestion. A forward-declaration for
`Public` is already available from `unrelated.h`.

This is useful when you know a symbol declaration is already available in a
source file via some unrelated header and you want to preserve that implicit
dependency, or when IWYU does not correctly understand that the definition is
necessary.


## IWYU pragma: friend ##

This pragma applies to the current header file. It says that any file matching
the given regular expression will be considered a friend, and is allowed to
include this header even if it's private. Conceptually similar to `friend` in
C++.

If the expression contains spaces, it must be enclosed in quotes.

    detail/private.h:
      // IWYU pragma: private
      // IWYU pragma: friend "detail/.*"
      struct Private {};

    detail/alsoprivate.h:
      #include "detail/private.h"

      // IWYU pragma: private
      // IWYU pragma: friend "main\.cc"
      struct AlsoPrivate : Private {};

    main.cc:
      #include "detail/alsoprivate.h"

      AlsoPrivate p;


## IWYU pragma: associated ##

Associated headers have special significance in IWYU, they're analyzed together
with their .cpp file to give an optimal result for the whole component.

By default, IWYU uses the .cpp file's stem (filename without extension) to
automatically detect which is the associated header, but sometimes local
conventions don't allow a component's .cpp and header file to share a stem,
which makes life harder for IWYU.

You can explicitly mark an arbitrary `#include` directive as denoting the
associated header with `IWYU pragma: associated`:

    component/public.h:
      struct Foo {
        void Bar();
      };

    component/component.cc:
      #include "component/public.h"  // IWYU pragma: associated

      void Foo::Bar() {
      }

You can mark multiple `#include` directives as associated and they will all be
considered as such.


## IWYU pragma: always_keep ##

This pragma applies to the current header file. It says that any include of this
file will be preserved by all includers.

    header.h:
      // IWYU pragma: always_keep
      struct Unused {};

    main.cc:
      #include "header.h"

Even if no symbols from `header.h` are used in `main.cc`, it will not be
suggested for removal.

This is useful for headers that provide additional type traits that aren't
possible for IWYU to track (e.g. hash functions for a type, to be used by STL
containers).


## Which pragma should I use? ##

Ideally, IWYU should be smart enough to understand your intentions (and
intentions of the authors of libraries you use), so the first answer should
always be: none.

In practice, intentions are not so clear -- it might be ambiguous whether an
`#include` is there by clever design or by mistake, whether an `#include` serves
to export symbols from a private header through a public facade or if it's just
a left-over after some clean-up. Even when intent is obvious, IWYU can make
mistakes due to bugs or not-yet-implemented policies.

IWYU pragmas have some overlap, so it can sometimes be hard to choose one over
the other. Here's a guide based on how I understand them at the moment:

* Use `IWYU pragma: keep` to force IWYU to keep any `#include` directive that
  would be discarded under its normal policies.
* Use `IWYU pragma: always_keep` to force IWYU to keep a header in all
  includers, whether they contribute any used symbols or not.
* Use `IWYU pragma: export` to tell IWYU that one header serves as the provider
  for all symbols in another, included header (e.g. facade headers). Use `IWYU
  pragma: begin_exports/end_exports` for a whole group of included headers.
* Use `IWYU pragma: no_include` to tell IWYU that the file in which the pragma
  is defined should never `#include` a specific header (the header may already
  be included via some other `#include`.)
* Use `IWYU pragma: no_forward_declare` to tell IWYU that the file in which the
  pragma is defined should never forward-declare a specific symbol (a forward
  declaration may already be available via some other `#include`.)
* Use `IWYU pragma: private` to tell IWYU that the header in which the pragma is
  defined is private, and should not be included directly.
* Use `IWYU pragma: private, include "public.h"` to tell IWYU that the header in
  which the pragma is defined is private, and `public.h` should always be
  included instead.
* Use `IWYU pragma: friend ".*favorites.*"` to override `IWYU pragma: private`
  selectively, so that a set of files identified by a regex can include the file
  even if it's private.

The pragmas come in three different classes;

  1. Ones that apply to a single `#include` directive (`keep`, `export`)
  2. Ones that apply to a file being included (`private`, `friend`,
     `always_keep`)
  3. Ones that apply to a file including other headers (`no_include`,
     `no_forward_declare`)

Some files are both included and include others, so it can make sense to mix and
match.