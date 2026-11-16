# Paired audio input diagnostic

This isolated experiment is disabled unless `OSL_AUDIO_TAP` names a writable
file prefix and `OSL_AUDIO` is exactly `muted` or `none`. For each port
opening it creates separate generation-labelled files: `.guest.<channels>ch`
contains the input seen at `output_port`, and `.converted.<channels>ch`
contains the buffer after volume, channel remapping, and any stereo downmix,
before pacing or the SDL queue. Cancellation or a queue failure may mean a
captured converted block was never submitted. Float and signed-16 formats are labelled separately.
On iOS, input has already passed through the guest-memory snapshot bridge;
the files do **not** prove what the mixer wrote before that copy.
The iOS engineering renderer resolves a relative prefix under its Documents
directory, creates parent directories, and restores the original environment
after the guest has joined. Relative paths containing `..` are rejected.
Absolute paths remain subject to the app sandbox. These files can be large;
choose bounded block counts and use a disposable profile.

`OSL_AUDIO_TAP_SKIP` skips a number of non-null submissions per port (default
0, maximum 10,000,000). `OSL_AUDIO_TAP_BLOCKS` limits recorded non-null
submissions (default 4096, maximum 32768). Each submission has the port's
reported frame count. Invalid or out-of-range values use the defaults. A zero
block count does not open a tap. Output from port close/reopen gets a new
generation and cannot overwrite its predecessor. A completed pair is announced
only after both files close successfully; partial pairs, failed writes and
missing completion messages must be treated as incomplete evidence. Verify
file lengths and format before analyzing either file.

When active, the tap copies the input once before conversion and writes both
files while holding the port lock. This changes timing and may change the
sampling of concurrent guest writes. It is diagnostic instrumentation, not a
real-time-safe recorder, an audio fix, or evidence of audible quality or
underrun frequency. Use an isolated disposable profile and a muted device.
Compare identical frames and account for volume, channel order, and downmix;
only an unchanged raw/converted pair in a known format can support a direct
sample-for-sample comparison.

The experimental source has synthetic timer-sink tests for bounded skipping,
volume, eight-channel ordering, stereo downmix, reopen, malformed bounds, and
refusal when audio is not muted. A separate cancellation/close race test covers
the tap-disabled path with both a timer sink and SDL dummy stream. Both tests
pass with AddressSanitizer and UndefinedBehaviorSanitizer; the implementation
also compiles for iPhoneOS and Simulator. No real game capture, physical-device
run, audio playback acceptance or production adoption is claimed.

OpenCrown revision `9ca8945` has an AVAudioEngine platform sink with submitted,
rendered and underrun counters; it does not provide an equivalent Odin
pre-volume-versus-converted paired capture. Odin retains its SDL port here and
uses OpenCrown's explicit diagnostic/acceptance separation.
