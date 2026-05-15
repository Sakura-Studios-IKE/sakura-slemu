# AUR PKGBUILDs — sakura-slemu

This directory contains the Arch Linux [PKGBUILD](https://wiki.archlinux.org/title/PKGBUILD)
files used to publish `sakura-slemu` to the
[Arch User Repository (AUR)](https://aur.archlinux.org/).

## Packages

| Subdirectory        | AUR package         | Source                                |
|---------------------|---------------------|---------------------------------------|
| `sakura-slemu/`     | `sakura-slemu`      | Latest tagged release (`v$pkgver`)    |
| `sakura-slemu-git/` | `sakura-slemu-git`  | Tip of `main` (`git+https://...`)     |

End users install via any AUR helper, e.g.:

```sh
yay -S sakura-slemu        # stable, follows releases
yay -S sakura-slemu-git    # rolling, follows main
```

The two packages `provides`/`conflicts` each other, so only one can be
installed at a time.

## Release pipeline

When CI tags a new release, it:

1. Bumps `pkgver` and refreshes `sha256sums` in `sakura-slemu/PKGBUILD`
   (replacing the placeholder `SKIP`) via `updpkgsums`.
2. Regenerates `.SRCINFO` with `makepkg --printsrcinfo > .SRCINFO`.
3. Pushes the updated `PKGBUILD` + `.SRCINFO` to the AUR git remote:

   ```sh
   git remote add aur ssh://aur@aur.archlinux.org/sakura-slemu.git
   git push aur master
   ```

The `-git` flavor is push-published on the same cadence but does not embed
a fixed `pkgver`; its `pkgver()` function resolves the version at build
time from `git describe --long --tags`.

## Local sanity check

```sh
cd sakura-slemu          # or sakura-slemu-git
makepkg --printsrcinfo   # validate metadata
makepkg -si              # build + install locally
```
