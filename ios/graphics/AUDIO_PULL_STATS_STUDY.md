# Optional SDL pull observation

This isolated experiment measures when SDL asks a stream for additional input.
It follows the same observation point used by OpenCrown's native output adapter,
which counts shortfalls in its render callback, but uses SDL's callback estimate:
`additional_amount` can include conversion or resampling headroom and is not a
CoreAudio hardware-underrun count. A positive estimate is evidence that SDL
asked for more unconverted input than was immediately available, not proof of
an audible glitch.

Set `OSL_AUDIO_PULL_STATS=1` for a diagnostic session. The callback adds only
lock-free relaxed atomic counters and performs no allocations, I/O or producer
locking. Counters are reported after stream destruction, once callbacks have
quiesced. The option is off by default; tests on a device should additionally
set `OSL_AUDIO=muted`. The existing producer-side `OSL_AUDIO_STATS` metric is a
separate pre-pacing sample and should not be treated as the same measurement.

Synthetic tests cover a queued stream, an empty stream, disabled/enabled
adapter lifecycle, a live muted dummy stream with injected producer delay, and
existing refill accounting. iPhoneOS and Simulator builds compile. These tests
cannot establish audible quality, physical-device behavior, or that the user's
reported popping is solved.
