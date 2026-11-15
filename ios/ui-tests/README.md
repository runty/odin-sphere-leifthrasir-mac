# Optional iPad touch acceptance

This isolated XCTest harness targets the already installed disposable
`com.wadscorp.odinsphere.startup` probe. It requires an explicit authorized iPad
UDID, a signing team, a fresh test profile and the existing
`Documents/DisplayInput/pending.txt` script (one two-frame Cross press at frame
1,100). Back up the profile and quit the probe before running it. It does not
install or replace the game app, import assets or select a phone automatically.

```sh
python3 ios/ui-tests/run.py --root NEW_EXTERNAL_DIRECTORY \
  --team YOUR_TEAM --device AUTHORIZED_IPAD_UDID
```

The test launches the probe with a three-minute guest budget. It locates the
large filled positive button in an actual app screenshot and taps through
XCTest's device input path. Completion requires the restored diagnostic text
view to report a finished renderer, one deliberate dialog completion, button 1,
result 0, balanced worker joins and no memory fault. Screenshots, tap location
and the full report become xcresult attachments. The startup script does not
contain a second confirmation press. A passing run still needs profile and
diagnostic verification outside the UI runner; it does not qualify a person's
finger gestures or physical controllers.

On 2026-11-15 the harness compiles, signs and installs on the iPad mini A20 Pro,
but XCTest fails **before any test method executes**: `Timed out while enabling
automation mode.` Testmanagerd connection and test-runner authorization succeed;
Developer Mode and DDI services are enabled. Therefore no touch, screenshot
selection or save result from this harness is verified. Evidence remains in
`/Volumes/office/Build/OdinSphere/device-touch-check/Result.xcresult` and its
exported test-manager/runner logs. The player is asleep and is not being asked
to change device settings. Ordinary bounded CoreDevice game runs remain the
independent verification path; keep this acceptance item open.

The generated project follows the official
[XcodeGen project specification](https://github.com/yonaskolb/XcodeGen/blob/master/Docs/ProjectSpec.md),
and compiles against the installed Xcode 27.2 XCTest/XCUIAutomation APIs.
