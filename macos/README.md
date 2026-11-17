# Local Mac app

The local alpha runs the CUSA05083 US base 1.00 guest under Rosetta 2 on Apple
silicon. The reference host is M5 Max/macOS 27.2.1. The app bundles its Vulkan
loader and KosmicKrisp Metal driver; no Homebrew runtime, game files or player
data are bundled. This build targets macOS 27.2. Distribution signing,
notarization and other Mac/OS combinations are separate qualification work.

## Build and package

On a mounted external build disk, existing compatible dependency caches can be
passed to `macos/build.sh` as documented in that script. For a fresh dependency
tree:

```sh
python3 macos/dependencies.py "$WORK/dependencies"
```

This uses the pinned SpartanPort dependency builder, including its KosmicKrisp
performance patch, and hash-checked SDL, FreeType, OpenSSL and PNG archives.
It writes the resolved arguments to `dependencies/inputs.json`. The installed
`0757088` build used a newly compiled x86-64 loader, patched KosmicKrisp,
SDL, FreeType, OpenSSL and PNG. The libpng URL selects the v1.6.50 tag archive
whose SHA-256 is pinned in the lock.

```sh
macos/build.sh "$WORK/macos-build" "$VK_PREFIX" "$DEPS_PREFIX" \
    "$FREETYPE_BUILD" "$FREETYPE_INCLUDE" "$SDL3_CMAKE_DIR"
python3 macos/package.py --build-dir "$WORK/macos-build" \
    --vulkan-prefix "$VK_PREFIX" --output "$WORK/Odin Sphere.app" \
    --license-root "$SDL_SOURCE" --license-root "$FREETYPE_SOURCE" \
    --license-root "$OPENSSL_SOURCE" --license-root "$PNG_SOURCE"
```

The packer checks x86-64 dependency architecture, copies and rewrites nested
libraries, writes a relative ICD manifest, includes license texts and build
metadata, compiles a universal native bootstrap, applies the project icon and
signs nested binaries and the app ad hoc. An existing output is retained under
`app-backups/`; replacement failure restores it. It never copies game files.

## Paths and first launch

By default, writable data is in
`~/Library/Application Support/OdinSphere/`, with progress/settings/cache under
`user/` and diagnostics in `~/Library/Logs/OdinSphere/`. The native entry point
sets Rosetta's AVX environment before executing the translated runner. Normal
launch opens the existing settings/Play window. Upstream binary updates are
disabled on Mac. A data-folder lock prevents two normal launches sharing state.

Choose **Graphics → Render resolution → 4K — 3840 × 2160** for native 4K game rendering at 16:9. The Display page controls the window or borderless output. A 1920×1080-point window on the reference Retina display produces a 3840×2160 Metal drawable. A smaller drawable scales the rendered image to fit. See the [combat measurements](PERFORMANCE.md#retina-4k-and-frame-time-headroom-on-the-m5-max).

Use the launcher's package installer for a fresh local library, or configure an
existing prepared game and image before launch:

```sh
"$APP/Contents/MacOS/OdinSphere" --configure "$APP0" "$IMAGE_DIR"
open "$APP"
```

The selected folders remain external to the bundle. For a configured folder
under `/Volumes`, an unavailable disk is rejected before starting the runner.
Reconnect it and launch again. The bootstrap never creates that missing disk.
Application replacement preserves settings/progress because they are external.

`--bootstrap-check` prints the resolved paths without starting the game.
`--runner ...` forwards an exact CLI invocation for import/game checks. Normal
runner options such as `--launcher`, `--osl-skip-launcher` and `--timeout` can
follow the bootstrap executable. Test isolation uses absolute `OSL_DATA_DIR`
and `OSL_LOGS_DIR`; these are not needed for ordinary Finder launch.

The player authorized unattended completion and deferred the hands-on soak.
Continue with local saves and copies of verified progress; no Odin cloud folder
has been selected. Do not infer Odin's save preference from OpenCrown. Configure
cloud syncing only after an explicit Odin choice and a copied-save handoff test.

## Automated bundle checks

```sh
python3 macos/test-bundle.py --app "$APP" --app0 "$APP0" \
    --image "$IMAGE_DIR" --seed "$COPIED_USER_PROFILE" --output "$NEW_TEST_DIR"
```

The suite relocates the app to a space/Unicode path, uses a minimal PATH and
disposable copied profile, rejects bad configuration and an unmounted volume,
checks actual imports, renders all seven launcher pages, rejects concurrent
profile use, and starts/quits/restarts bounded game runs. It checks bundled
driver loading, default performance toggles, immutable bundle contents and
unchanged saved progress bytes. It does not claim to have visibly loaded a
save slot or played 30 minutes of combat. Live gameplay, audio and save reload
have separate player-confirmed evidence in the port plan.


## Installed library and Finder launch

The private console pipeline now copies verified runtime files to
`~/Library/Application Support/OdinSphere/game/` before configuring the app.
This avoids waiting on an external-volume permission request at Finder launch.
Dumps, build trees, dependencies and shader caches remain on the external disk;
the local copy is an installed game library. The wrapper validates and reuses
an existing local library, stages replacements and preserves previous files.
`--external-game` explicitly keeps the earlier external-folder configuration.

On the reference Mac, the installed app rendered all seven launcher pages with
an empty profile. The packaged installer's `--runner --install` path imported
the original US 1.00 package. The resulting boot image and eboot matched the
prepared reference. The visible launcher showed Ready to play, and pressing
Return started a 300-frame render. A separately registered copy with the production runner also passed a mouse
Play test. Launch from a separate macOS login remains unverified. Use disposable profiles for repeat installation tests.
