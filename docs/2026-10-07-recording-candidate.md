# Gravel recording candidate and review

Source begins at c1b4798. Shared native writer/reader pin is toolkit 85147b8,
separate from Gravel's existing v0.8.0 packet/device headers. Runtime installation
and accepted 4b7fd75 renderer bytes have not been changed by this work.

Implemented: observation independent of UDP, menu-side state independent of
telemetry Enabled, optional one-launch sampled capture, bounded/exclusive shared
session stream, source availability/ages, original requested-force summaries
before muted forwarding, private request/evidence tooling and strict inspection.
No pose writes, input injection, display changes, stage automation or tyre model
replacement. See RECORDING.md for exact commands and interpretation limits.

Claude's first review caught two high defects before any install: device Escape
was hooked at EnumCreatedEffectObjects' slot23 rather than24, and the proxy's COM
factory export bypassed guarded interface creation. Both are corrected. Tests now
derive the SDK interfaces and call through their virtual methods, separately from
the direct-hook tests. Capture QueryInterface returns only the guarded interface
or its IUnknown; legacy interfaces and aggregation are refused.

Second-review follow-up: `EnumDevicesBySemantics` returns a device directly to
the application's callback, bypassing `CreateDevice`. The candidate now wraps
both ANSI and Unicode callbacks and guards the borrowed device before exposing
it. Failure stops enumeration and returns an error without calling the game.
`ConfigureDevices` is refused during capture because its internal device/UI
route is not qualified. Ordinary sessions retain direct forwarding.

The SDK-interface fixture now builds and runs in both ANSI and Unicode forms.
It enumerates before any `CreateDevice` call, tests guarded device calls inside
the callback, preserves callback flags/context, rejects a different unguardable
device implementation without a callback, and checks normal forwarding.
This closes that specific alternate creation route. It does not close direct
system32 COM activation, shared-vtable concurrency/lifetime or device-property
restoration. Those remain reasons to leave the accepted install untouched.
The API route is documented by Microsoft in
[EnumDevicesBySemantics](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417807(v=vs.85)).

Validation:

- Shared C++ writer fixtures pass the real managed SessionReader, including
  precision/escaping, three limits and incomplete/invalid refusal. Existing
  recording library: 49 tests pass.
- Actual proxy hooks with fake COM retain +0.6/-0.4 requests while forwarding
  zero gain, remove auto-start, block Start/commands/Escape, guard acquisition and
  properties, cover nonselected devices/table capacity, and preserve normal mode.
- Actual SDK interfaces verify guarded factory creation, correct device Escape
  versus enumeration slots, IUnknown/legacy-query handling and aggregation refusal.
- Actual native producer writes three synthetic rows through the managed Gravel
  inspector: missing force omitted, known RPM zero retained, +0.61 request retained,
  measured sampling gap exposed, capture complete and mute still latched.
- Request tests pass in PS7 and PS5.1: wrong candidate refused, exclusive arm,
  different-ID cancel refused, own cancel archived, owner INI unchanged.
- Existing source checks pass in PS7 and PS5.1: CI28, provenance43, installer235,
  setup141. A pre-existing multi-result Get-Command problem in the runner and its
  own fixture is fixed by selecting the first matching executable.

Open before unattended wheel use: CoCreateInstance activation through system32
can bypass this DLL's factory export; full device-property restoration and the
live acquisition path also need qualification. The installed EXE is Steam build
3477925, SHA256 28032B882FFF6DAE07B185B93FD5989ADE09DC36C2EC070ECC1D05B3A73D2A95;
read-only PE inspection confirms both DirectInput8Create and CoCreateInstance
imports. Do not call the diagnostic mute complete on the strength of fake tests.
The capture inspector deliberately reports hardwareMuteQualified=false.

Route playback remains open: a verified physics tick/local vehicle identity,
pose/velocity, an offline/scoring gate, cold-start navigation and exact restoration
are missing. Native request summaries do not carry ordered effect lifecycles;
they cannot certify physical torque or full force replay/normalization.
