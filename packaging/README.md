# Packaging

Two PKGBUILDs, differing only in where the source comes from.

| | builds | use for |
| --- | --- | --- |
| `arch/` | the checkout it sits in | an installable package from local work |
| `aur/` | `git clone` of the published repo | the AUR |

Keep the metadata in step between them; only `pkgname`, `source` and the
version scheme differ.

## A local package

```sh
cd packaging/arch
makepkg -si                                 # build and install
PKGDEST="$PWD/../../dist" makepkg -f        # build only, into dist/
```

`pkgver()` names the commit HEAD is on and appends `.dirty` when the tree has
uncommitted changes, because what gets packaged is the tree, not the commit.
makepkg rewrites the `pkgver=` line in place on every build; that is normal and
the checked-in value is only a starting point.

`namcap` reports one warning that is expected:

```
W: Dependency included, but may not be needed ('plasma-workspace')
```

Nothing in the package refers to plasmashell by file, but it is what hosts the
applet. `namcap PKGBUILD` additionally reports `File referenced in $startdir`
for `arch/PKGBUILD` — that is the whole point of that file.

## The AUR

`aur/` is the content of the AUR git repo, at its root:

```sh
git clone ssh://aur@aur.archlinux.org/plasma-nethogs-git.git aur-repo
cp packaging/aur/{PKGBUILD,.SRCINFO,plasma-nethogs.install} aur-repo/
```

`plasma-nethogs.install` in `arch/` is a symlink to the copy in `aur/`, so the
two cannot drift. The AUR side is the real file, which is why the copy goes in
that direction — an AUR repo containing a symlink out of the tree is useless.

Regenerate `.SRCINFO` after every PKGBUILD edit — the AUR rejects a push whose
`.SRCINFO` disagrees:

```sh
cd packaging/aur && makepkg --printsrcinfo > .SRCINFO
```

Once there is a tagged release, the AUR package for it is this one with
`pkgname=plasma-nethogs`, `pkgver=1.0`, the `pkgver()` function dropped, and

```sh
source=("$_srcname-$pkgver.tar.gz::$url/archive/v$pkgver.tar.gz")
```

checksummed with `updpkgsums`.

## Build-time kernel requirement

Both PKGBUILDs generate `vmlinux.h` from `/sys/kernel/btf/vmlinux`, so the
*build* machine's kernel needs `CONFIG_DEBUG_INFO_BTF=y`. devtools chroots
mount `/sys`, so `extra-x86_64-build` works. CO-RE resolves the relocations
against the running kernel at load time, so the resulting package is not tied
to the kernel that built it.
