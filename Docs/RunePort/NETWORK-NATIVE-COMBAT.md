# Multiplayer dual wield and bloodlust moves

October 27, 2026. Protocol/DTLS ALPN **15**, native rules **15**, snapshot
**6**, invitation **3**. Update the host and every client together. This extends
the existing private multiplayer client; it does not implement public discovery,
late joining, campaign co-op, or original Rune server compatibility.

## Gameplay and authority

- Compatible one-handed inventory weapons automatically form a real pair.
  Original attack timing alternates hands, including the full-body mirrored spin.
  Original weapon sweeps/damage remain server-owned. Offhand contacts now pass
  the same ownership and delayed-contact checks as primary weapons.
- Block crosses the pair. It retains local play's partial damage/momentum
  protection and raise time, rather than becoming an invulnerable shield.
  Double-tapping Block with a pair remains a block, not an impossible shield bash.
- Double-tap Jump within 320 ms to request a bloodlust leap. The first tap is an
  ordinary jump. The server checks the first accepted jump, mapped input times,
  life, meter, state, landing support and cooldown. Arrival jitter does not define
  the double-tap interval. Direction comes from the associated movement input.
- Hold Block and press Attack to request shield charge when the server reports
  it available. A usable equipped shield is required; a shield hidden behind a
  dual-wield pair cannot be used. Original stow/brace/run/recovery pose and swept
  movement are shared with local play.
- Both specials spend half the meter and share the four-second cooldown.
  Original bloodlust gain, decay and damage absorption run on the server.
  Normal play does not grant or pin bloodlust at full strength.
- Charge damage and recoil enter the existing two-tick / at-least-33-ms contact
  queue, including cover, life and team checks. A valid late shield block reduces
  impact and stops a still-running charge. Crossed guard has a narrower cone and
  its existing 96-ms effective raise threshold; it cannot gain instant shield
  protection by arriving late. A frozen crossed block remains partial when
  released after contact, provided the same weapon pair is still equipped.
- Friendly-fire-disabled Team Deathmatch, same-team Arena restrictions and
  HeadBall pre-start restrictions suppress the whole charge contact, including
  recoil. Busy/dead players cannot start another special. Respawn clears native
  transient state and jump history.

The server sends offhand identity, handedness, crossed-guard blend, bloodlust
meter/active state, special phase/age, cooldown and readiness. Clients apply data
and interpolate presentation only: no local damage scripts, duplicate physics,
or special-move prediction. Walking prediction is disabled while a special is
active. Red halos, two-handed rush poses, temporary weapon stow and amber touch
cues use server state. Android's bloodlust HUD now shows the replicated meter;
the hidden shield is not displayed as an equipped guard while dual wielding.
Combat settings in multiplayer say **Host rules**.

Like ordinary weapon hits, charge health/movement is confirmed in snapshots.
The existing separate reliable `CombatOutcome` event still describes kick/bash;
this change does not claim complete multiplayer sound/rumble impact coverage.
A dedicated offhand picker and general inventory selection remain separate work.

## Wire changes

Action values 10 and 11 are `BloodLeap` and `ShieldCharge`. They remain intents;
clients never provide damage, impulse, target, weapon identity or resulting position.

Ordinary entity records remain 77 bytes. Wire flag `0x40` adds an optional 19-byte
native-combat extension: offhand reference (8), flags (1), guard blend (1), meter
percent (1), phase (1), phase milliseconds (2), cooldown milliseconds (2), readiness
bits (1). The presence flag is removed before applying ordinary entity flags.
Empty extensions are noncanonical and rejected. At most 64 entities may carry
extensions, within the existing 512-entity world ceiling. The byte ceiling is
40,960, still 64 pieces of 640 bytes; transport datagram size is unchanged.

Decoding checks bounds, reserved flags, coherent readiness, valid generations,
real weapon references, a distinct main/offhand pair, ownership and attachment.
Snapshot installation validates the whole candidate before changing actors.
Dropped/dead/offhand-cleared states remove the replica's native pair. Procedural
guard blend and rush age interpolate between matching phases/lives, alongside
the existing independent body/torso animation interpolation.

## Validation

- All 12 standalone test groups pass in Release and ASan/UBSan. The independent
  Python wire oracle includes both new actions, native state, malformed references,
  bounds, reserved flags, empty extensions and old-schema rejection. A maximum
  extended world still fits the 64-piece transfer.
- Native engine fixture passes on macOS and ARM Android. Four real alternating
  attacks produced 60 total right-hand and 88 total left-hand damage in its
  controlled setup. A crossed 20-base-damage contact committed 10 after original
  game modifiers, rather than zero. Shield charge produced 22 open / 4 shielded
  damage after original modifiers, exactly one target contact per charge.
  These are fixture outcomes, not fixed damage promises for every weapon/state.
- It checks replica equipment clearing, replica simulation rejection, leap
  intent/cost/cooldown, airborne prediction exclusion, rush windup/recovery,
  blocked impact and death cancellation. The added macOS pose comparison differs
  by at most 0.006515 in joint-matrix components after wire quantization.
  Separate mode-rule fixtures verify charge suppression before damage/recoil
  in Team Deathmatch, Arena and HeadBall.
- Existing engine replication, movement prediction and 12 melee-window cases
  pass. The fixture's post-death requests were corrected to use the new life
  generation rather than hard-coding life 1; production life validation remains.
- Two windowed macOS clients pass dual and bloodlust scenarios at 60 ms each-way
  latency, 8 ms jitter, 1% loss and 1% duplication. Both observe left swings,
  crossed guards, leap/charge phases and halos through encrypted snapshots.
- OPPO plus macOS passes both scenarios at **2520×1680, native rendering, 4× AA**.
  Initial 900-frame Android engine samples: dual **96.42 FPS**, bloodlust
  **100.39 FPS**. These are short scene samples, not a sustained/all-map claim
  or a before/after performance comparison. Saved Hardware Scaling 50%, 4× AA,
  the user's refresh policy and campaign saves are preserved.

- Real visible Android touch-view dispatch also passes double-tap Jump and the
  two-finger Block + Attack combination, with the corresponding actions accepted
  by the remote server. The touch dispatch regression checks independent finger
  release, cancellation, controller-hide behavior and amber readiness cues.
  Additional short touch-run samples were 89.0–92.7 FPS; the range across all
  these sequential scene samples is roughly 89–100 FPS, not a locked 120 FPS.
  SharedPreferences SHA-256 matched before and after testing.

Logs and results: [validation evidence](evidence/network-native-combat/). The
updated APK is installed, and Open Rune is returned to its normal title/menu
without test launch options.

Reproducible entry points:

```sh
# Uses isolated saves; no renderer or physical input required.
SurrealEngine --direct --rune-network-probe --rune-net-native-combat \
  --save-directory=/absolute/isolated-saves /absolute/rune-classic

python3 Tools/RuneValidation/network_native_combat.py \
  --binary /absolute/SurrealEngine --game /absolute/rune-classic \
  --output /absolute/new-evidence --fixture dual --latency 60
# Replace dual with blood for the leap/charge scenario.
# Android: --device SERIAL --host LAPTOP_LAN_IP (restarts the app).
# Android real touch dispatch: --fixture blood --touch-gesture leap|charge.
```

The explicit fixture grants equipment and one full bloodlust meter at startup.
It is gated behind command-line/test launch flags and isolated saves. Do not use
fixture behavior to describe normal match loadouts or meter availability.

The [server browser and hosting roadmap](MULTIPLAYER-PLAN.md#server-browser-and-hosting-roadmap-accepted-october-27)
remains planned work: own directory, dedicated community hosts first, LAN/direct,
private invitations and configurable community directories, then NAT/relay.
