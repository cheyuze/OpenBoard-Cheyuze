# Website release channel

Stable GitHub releases automatically mirror their installer, `update.json` and
`SHA256SUMS.txt` to https://xiwang.cheyuze.top/openboard/. Upload all three assets
to a **draft** release first; publish the draft only when every asset is ready.
An existing stable release can be retried using the workflow's `tag` input.

## Publication contract

- `version`, filename, tag, `websiteUrl`, `githubUrl`, `url`, `size`, SHA-256 and
  `SHA256SUMS.txt` must agree. Keep `url` as the GitHub URL for older clients.
- New clients check the website first, fall back to GitHub metadata, and let the
  user choose website, GitHub or the manually maintained Baidu share.
- The receiver streams into a private staging directory, rejects links/path
  traversal/unexpected files, checks SHA-256, installs an immutable version
  directory, and publishes the current manifest last. It refuses downgrades and
  different contents for an existing version. Failed uploads leave the previous
  published release available.
- Do not commit upload credentials. `OPENBOARD_PUBLISH_SSH_KEY` is a repository
  secret; `OPENBOARD_PUBLISH_HOST`, `OPENBOARD_PUBLISH_USER` and
  `OPENBOARD_PUBLISH_KNOWN_HOSTS` are repository variables.
- The dedicated `openboard-publisher` account has no sudo access. Its authorized
  key forces `/usr/bin/python3 /usr/local/lib/openboard-publish.py`, with forwarding,
  agent forwarding, PTY and user RC disabled. The receiver is root-owned; only
  `/opt/platform/proxy/site/openboard` and its private staging area are writable.
- Caddy serves only `/openboard` and `/openboard/*` before the website's login
  middleware. Other paths retain the original authentication behavior. Current
  metadata is `no-store`; versioned packages support Range/206 resume.
- Baidu uploads remain manual. Never claim its package is current without
  checking it. This workflow does not modify the share.

## Recovery

Retry an interrupted workflow with the same tag. Do not delete or replace an
existing version to force publication; build a new patch version. Restoring an
older `update.json` is a separate, intentional server-admin operation, not allowed
through the restricted CI key. Server configuration backups are retained under
`/opt/platform/backups/openboard-191-*`.

The static service shares server bandwidth with teaching tools. Monitor egress
and move package storage behind a CDN/object store if download traffic grows.
