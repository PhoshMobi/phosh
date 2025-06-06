# Contributing to Phosh

Thank you for considering contributing to Phosh. See below for
contributing guidelines.

Please make sure to check our [Code of Conduct][coc], for interactions
in this repository [GNOME's Code of Conduct][gnome-coc] also applies.

## Building

For build instructions, see the [README.md](./README.md)

## Merge requests

Before filing a pull request, run the tests:

```sh
meson test -C _build --print-errorlogs
```

Use descriptive commit messages, see

   <https://wiki.gnome.org/Git/CommitMessages>

and check

   <https://wiki.openstack.org/wiki/GitCommitMessages>

for good examples. The commits in a merge request should have "recipe"
style history rather than being a work log. See
[here](https://www.bitsnbites.eu/git-history-work-log-vs-recipe/) for
an explanation of the difference. The advantage is that the code stays
bisectable and individual bits can be cherry-picked or reverted.

### Checklist

When submitting a merge request consider checking the following first:

- [ ] Does the code use the coding patterns below?
- [ ] Is the commit history in recipe style (see above)?
- [ ] Do the commit messages reference the bugs they fix? If so,
      - Use `Helps:` if the commit partially addresses a bug or contributes
        to a solution.
      - Use `Closes:` if the commit fully resolves the bug. This allows the
        release script to detect & mention it in `NEWS` file.
- [ ] Does the code crash or introduce new `CRITICAL` or `WARNING`
      messages in the log or when run from the console. If so, fix
      these first?
- [ ] Is the new code covered by any tests? This could be a unit test,
      an added [screenshot test](./tests/test-take-screenshots.c),
      a tool to exercise new DBus API (see, e.g.
      [tools/check-mount-operation](./tools/check-mount-operation)).
- [ ] Are property assignments to default values removed from UI files? (See
      `gtk-builder-tool simplify file.ui`)

If any of the above criteria aren't met yet, it's still fine (and
encouraged) to open a merge request marked as draft. Please indicate
why you consider it draft in this case. As Phosh is used on a wide
range of devices and distributions please indicate in which scenarios
you tested your code.

## Coding

### Coding Style

For coding style see our [developer documentation][dev-docs].

For internal API documentation as well as notes for application
developers see [here][phosh-api].

### Public API

Phosh's lockscreen and quick setting plugins can use the ABI provided by the
[plugins symbols file][].

Phosh also provides a shared library to run the "shell in a box" to be
e.g. used by greeters. The ABI available to library users is the
plugins ABI plus the symbols from the [library symbols file][]. If you
need a new symbol for the library but not the plugins, consider adding
it there.

Symbols in these files can only be changed in a backward compatible manner or
we need to bump the library API version.

[1]: https://gitlab.gnome.org/GNOME/libhandy/blob/master/HACKING.md#coding-style
[plugins symbols file]: src/phosh-exported-symbols.txt.in
[library symbols file]: src/libphosh.syms.in
[dev-docs]: http://dev.phosh.mobi/docs/development/coding-style/
[coc]: https://ev.phosh.mobi/resources/code-of-conduct/
[gnome-coc]: https://conduct.gnome.org/
[phosh-api]: https://world.pages.gitlab.gnome.org/Phosh/phosh/
