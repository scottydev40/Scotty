# Releasing Scotty

A release ships two things from one tag:

- **AppImage** — attached to the GitHub Release. AppImage users self-update in-app.
- **Flatpak** — built, GPG-signed, and pushed to the self-hosted OSTree repo at
  `https://scottydev40.github.io/repo/`. Flatpak users get it on `flatpak update`
  (or the in-app updater).

Both are produced automatically by the `Release Nearby QML Tray App` workflow
(`.github/workflows/release-nearby-qml-tray.yaml`). **The workflow only runs on a
version tag** (`on: push: tags: v*`). Pushing commits to `main` builds nothing on
the release side — the tag is what releases.

## Cut a release

```bash
cd ~/Desktop/scotty

# 1. Land your changes on main (directly, or via a merged PR).
git add -A && git commit -m "..."

# 2. Bump the version users see: edit the top <release> entry in
#    sharing/linux/qml_tray_app/packaging/dev.scotty.Scotty.metainfo.xml
#    (this drives the Flatpak / software-centre "Version"). Keep it in sync
#    with the tag you are about to cut.

# 3. Push code, then cut and push the tag. The TAG push is what triggers CI.
git push origin main
git tag v0.2.0-beta4
git push origin v0.2.0-beta4
```

Version scheme: tags are `vMAJOR.MINOR.PATCH[-betaN]` (e.g. `v0.2.0-beta4`). The
running app's version comes from `git describe --tags`, so an untagged commit
reports `…-N-gSHA` — fine for dev, but cut a clean tag for a real release.

## Watch / debug from the terminal

No browser needed:

```bash
gh run list -R scottydev40/Scotty -L 5              # recent runs + status
gh run view <id> -R scottydev40/Scotty             # jobs in a run
gh run view <id> --log-failed -R scottydev40/Scotty  # only the failed steps
gh run watch  <id> -R scottydev40/Scotty            # live, until it finishes
```

A healthy release run shows **two jobs**:

- `Build and Release Linux Bundle` — AppImage → GitHub Release.
- `Publish Flatpak to self-hosted repo` — signed flatpak → Pages repo.

If the flatpak job says it **skipped**, a required secret is missing (see below).

## Required GitHub secrets (set once)

The flatpak job needs two repo secrets on `scottydev40/Scotty`
(Settings → Secrets and variables → Actions):

| Secret | What | How to produce it |
| --- | --- | --- |
| `SCOTTY_FLATPAK_GPG_KEY` | Armored private signing key ("Scotty Releases", `FB5293E0EAEE66FB5235DE0278F60844B9C1E3F6`) | `gpg --armor --export-secret-keys FB5293E0EAEE66FB5235DE0278F60844B9C1E3F6` |
| `PAGES_DEPLOY_TOKEN` | Token that can push to `scottydev40.github.io` | Fine-grained PAT, owner `scottydev40`, repo `scottydev40.github.io`, permission **Contents: Read and write** |

Notes:

- The flatpak job pushes to a **different** repo than the workflow lives in, so the
  built-in `GITHUB_TOKEN` cannot do it — hence the separate `PAGES_DEPLOY_TOKEN`.
- If the flatpak job silently starts skipping later, the PAT probably **expired** —
  regenerate it and update the secret.
- Security: whoever holds `SCOTTY_FLATPAK_GPG_KEY` can sign flatpaks as Scotty. It's
  in CI by choice; if you'd rather keep signing local, delete the secret and the job
  self-skips — then publish with the local script below.

## Publishing the flatpak locally (fallback)

If CI can't publish (secret issue, or you want to do it by hand), the same build +
sign + push runs locally:

```bash
# Needs the signing key in the local GPG keyring and a checkout of the Pages repo
# at ~/Desktop/scotty-flatpak-pages (its git remote must be able to push).
packaging/flatpak/publish-repo.sh
```

This builds the flatpak from source (offline in-sandbox), exports it signed into
`~/Desktop/scotty-flatpak-pages/repo`, then commits and pushes the Pages repo.
GitHub Pages then serves the new build within a minute or two; `flatpak update
dev.scotty.Scotty` pulls it.

Gotchas seen in practice:

- The Pages repo's `main` branch must track its upstream, or the script's final
  `git push` fails with *"main has no upstream branch"* — set it once with
  `git -C ~/Desktop/scotty-flatpak-pages push -u origin main`.
- Pages deploy lags the push by ~1–2 min; poll `flatpak remote-info scotty
  dev.scotty.Scotty` until the `Commit:` changes.

## After a release

```bash
flatpak update dev.scotty.Scotty     # pull it to your own machine
```

Verify the GitHub Release carries only `Scotty-x86_64.AppImage` and its `.sha256`
(plus GitHub's automatic Source-code archives, which can't be removed). Nothing
else should be attached.
