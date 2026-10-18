#!/bin/bash
# build-pkg.sh <version> <LISN StemSplitter.app> <LISN StemSplitter.vst3>  ->  installer/Output/LISN-Mac.pkg
# installer/mac/uv (the aarch64-apple-darwin binary) must be there, as installer/uv.exe is for Windows.
set -euo pipefail
[ $# -eq 3 ] || { echo "usage: build-pkg.sh <version> <app> <vst3>" >&2; exit 2; }
version=$1 app=$2 vst3=$3
here=$(cd "$(dirname "$0")" && pwd)
installer=$(dirname "$here")
[ -x "$here/uv" ] || { echo "missing $here/uv" >&2; exit 1; }
out=$installer/Output
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The Mac app is "LISN" in /Applications; the plugin keeps its name.
root=$work/root scripts=$work/scripts
mkdir -p "$root/Applications" "$root/Library/Audio/Plug-Ins/VST3" "$scripts" "$out"
ditto "$app" "$root/Applications/LISN.app"
ditto "$vst3" "$root/Library/Audio/Plug-Ins/VST3/LISN StemSplitter.vst3"
codesign --force --deep --sign - "$root/Applications/LISN.app"
codesign --force --deep --sign - "$root/Library/Audio/Plug-Ins/VST3/LISN StemSplitter.vst3"

cp "$here/setup-engine.sh" "$here/uv" "$installer/requirements-macos.txt" "$installer/sitecustomize.py" "$scripts/"
cat > "$scripts/postinstall" <<'POST'
#!/bin/bash
# Runs as root. The pkg was downloaded, so Gatekeeper may have marked what it installed: clear that on our own files.
here=$(cd "$(dirname "$0")" && pwd)
xattr -dr com.apple.quarantine /Applications/LISN.app "/Library/Audio/Plug-Ins/VST3/LISN StemSplitter.vst3" 2>/dev/null
"$here/setup-engine.sh" "/Library/Application Support/LISN/engine"
POST
chmod +x "$scripts/postinstall" "$scripts/setup-engine.sh" "$scripts/uv"

# Installer.app otherwise "upgrades" any other copy of these bundles it finds (a build folder), not /Applications.
pkgbuild --analyze --root "$root" "$work/components.plist" >/dev/null
for i in 0 1; do /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$work/components.plist"; done
pkgbuild --root "$root" --component-plist "$work/components.plist" --scripts "$scripts" \
    --identifier digital.lisn.pkg --version "$version" --install-location / "$work/lisn.pkg"

cat > "$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>LISN</title>
    <options customize="never" require-scripts="false" hostArchitectures="arm64"/>
    <volume-check><allowed-os-versions><os-version min="14.0"/></allowed-os-versions></volume-check>
    <choices-outline><line choice="lisn"/></choices-outline>
    <choice id="lisn" title="LISN" description="The LISN app, the LISN StemSplitter VST3 plugin and their AI engine.">
        <pkg-ref id="digital.lisn.pkg"/>
    </choice>
    <pkg-ref id="digital.lisn.pkg" version="$version" onConclusion="none">lisn.pkg</pkg-ref>
</installer-gui-script>
XML
productbuild --distribution "$work/distribution.xml" --package-path "$work" "$out/LISN-Mac.pkg"
echo "built $out/LISN-Mac.pkg"
