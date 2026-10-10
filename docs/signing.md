# Code signing (SignPath Foundation)

Releases are signed by SignPath Foundation's free open-source program. Until the GitHub secret below exists, `build.yml` skips signing and the release is unsigned, as before.

## One-time setup

1. Apply at https://signpath.org/apply.html (repository: github.com/EdiKrasniqi11/lisn-vst, license AGPL-3.0). Turn on two-factor login for GitHub first; SignPath requires it.
2. Once approved, in SignPath:
   - Add the predefined **GitHub.com** trusted build system and link it to the project.
   - Project slug `lisn-vst`, signing policy slug `release-signing` (manual approval), artifact configuration slug `installers` with the XML below.
   - Create an API token for a user who can submit to that policy.
3. In GitHub, under repo Settings > Secrets and variables > Actions:
   - Secret `SIGNPATH_API_TOKEN`: the token.
   - Variable `SIGNPATH_ORGANIZATION_ID`: the organization ID from SignPath.
4. Push a `v*` tag. The build stops at "Sign installers" until you approve the request in SignPath (it waits up to a day).

## Artifact configuration `installers`

`upload-artifact` zips the two installers, so the root is `<zip-file>`. The version comes from the tag (`v0.3.0` → `0.3.0`), and the installers carry it via `VersionInfoProductTextVersion`.

```xml
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <parameters>
    <parameter name="version" />
  </parameters>
  <zip-file>
    <pe-file path="LISN-StemSplitter-Setup.exe" product-name="LISN StemSplitter" product-version="${version}">
      <authenticode-sign />
    </pe-file>
    <pe-file path="LISN-Setup.exe" product-name="LISN StemSplitter" product-version="${version}">
      <authenticode-sign />
    </pe-file>
  </zip-file>
</artifact-configuration>
```

## What is and isn't signed

Only the two installers are signed. SmartScreen and "Unknown publisher" look at the file you downloaded, so that's what matters. LISN.exe, the VST3 and the uninstallers inside are unsigned: signing them would need a second signing request (and a second approval) before Inno Setup packs them.
