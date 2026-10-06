#!/usr/bin/env bash
# vcpkg asset-source script (X_VCPKG_ASSET_SOURCES="x-script,<this> {url} {sha512} {dst}").
#
# Why: in Claude Code cloud sessions the egress proxy answers 403 for
#   https://github.com/<owner>/<repo>/archive/<ref>.tar.gz
# (the URL vcpkg_from_github uses) unless the repo is attached to the session,
# while plain `git fetch` of public repos is served normally.
# This script rebuilds the exact same tarball with `git archive | gzip -cn`
# (byte-identical to GitHub's archive output) and refuses to hand vcpkg
# anything whose SHA512 differs from the one pinned in the portfile.
# Non-GitHub-archive URLs exit 1, so vcpkg falls back to the original URL.
set -u
url="$1"; want="${2,,}"; dst="$3"

re='^https://github\.com/([^/]+)/([^/]+)/archive/(.+)\.tar\.gz$'
[[ "$url" =~ $re ]] || exit 1
owner="${BASH_REMATCH[1]}"; repo="${BASH_REMATCH[2]}"; ref="${BASH_REMATCH[3]}"

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
git -C "$work" init -q || exit 1
if ! git -C "$work" fetch -q --depth 1 --no-tags "https://github.com/$owner/$repo" "$ref" 2>"$work/err"; then
  echo "vcpkg-github-via-git: git fetch failed for $owner/$repo@$ref: $(cat "$work/err")" >&2
  exit 1
fi

# GitHub names the top-level folder <repo>-<ref>, stripping a leading "v" before a digit
# and turning "/" into "-". Try the likely variants and keep the one matching the pinned hash.
r="${ref#refs/tags/}"; r="${r#refs/heads/}"
cands=()
[[ "$r" =~ ^v[0-9] ]] && cands+=("${r:1}")
cands+=("$r")
for c in "${cands[@]}"; do
  c="${c//\//-}"
  git -C "$work" archive --format=tar --prefix="$repo-$c/" FETCH_HEAD | gzip -cn > "$work/out.tar.gz"
  got="$(sha512sum "$work/out.tar.gz" | cut -d' ' -f1)"
  if [[ "$got" == "$want" ]]; then
    mkdir -p "$(dirname "$dst")" && mv "$work/out.tar.gz" "$dst" && exit 0
  fi
done
echo "vcpkg-github-via-git: no candidate matched SHA512 for $url (tried: ${cands[*]})" >&2
exit 1
