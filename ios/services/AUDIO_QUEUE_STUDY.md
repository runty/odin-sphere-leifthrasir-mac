# Experimental muted audio queue

This isolated branch studies an opt-in `OSL_AUDIO_QUEUE_BUFFERS=2..16`
queue depth. With no setting, the existing policy remains in effect. The
study pre-fills the requested number of guest-period blocks before the device
resumes and refills that depth after an empty-queue observation. It does not
change the guest samples or repair zeros already present in them. Extra queue
depth increases audio latency; it is not enabled by default or integrated.

On the development Mac, three paired 480-submission, 256-frame CoreAudio
runs of muted digital silence with an injected 30 ms delay every 40 submissions
found one empty queue after warm-up per baseline run and none in the candidate
runs. Baseline peak queue was 22,528 bytes; candidate peaks were 30,720–32,768
bytes. Three dummy-driver pairs, 128 format/length/prefill cases, the existing
PCM snapshot/close/reopen regression (native and ASan/UBSan), and iPhoneOS and
Simulator runtime-object compiles also pass. These synthetic, short trials do
not establish game sound quality, long-run latency, device behavior or the
cause of the observed guest PCM gaps. No phone or iPad was contacted. Exact
inputs, logs and checksums are in `/Volumes/office/Build/OdinSphere/audio-queue-study-v1/verification.json`.
