# Rune Power activation

Rune Power remains the original weapon resource. `RuneOfPower` raises the maximum
and fills the meter (up to 200); `RuneOfPowerRefill` restores the current meter
without raising the maximum. Powering a weapon spends its authored
`RunePowerRequired` amount and starts that weapon's original timed ability.
The meter does not rise just from combat. Original `bAutoPickup=False` means a
player must press Use near a rune, unless auto pickup is enabled.

Android now has a themed, movable Rune Power touch button. Select/View on a
physical controller activates it. Both use the original `E=Powerup` binding
in local play. The touch button hides with other action buttons while a
controller is connected. Existing touch layout preferences and saves remain
unchanged; the new control has its own `touchLayout.power.*` fields.

The private multiplayer protocol now has a separate authenticated Powerup
action. E requests Powerup; Ctrl/Use still requests pickup. The server checks
the equipped weapon and resource and executes the original script. Snapshots
carry the authoritative 0–200 Rune Power amount/capacity and powered weapon
state for the client HUD and presentation. Protocol and DTLS ALPN are version
16, and native rules revision is 16; host and clients must update together.

## Validation

- On macOS and the OPPO, an isolated original-script fixture raised the meter
  from 0 to 200 with a Rune of Power, spent 25 with a powered Hand Axe, then
  restored 25 with a refill. The Android run used a 2520×1680 surface and
  4× MSAA. The fixture temporarily enabled auto pickup to exercise the normal
  Touch path, then restored the original setting.
- Android View dispatch verified that tapping Rune Power presses and releases
  E; the default layout has no overlapping controls. The OPPO screenshot was
  visually checked for placement.
- The native network fixture rejected a Powerup with an empty meter, accepted
  a charged weapon, spent the authored cost, and delivered the remaining meter
  and powered weapon state to a replica. Replication and wire checks cover the
  versioned data path.

These are bounded fixtures. They do not establish sustained FPS or a complete
multiplayer playthrough.

Evidence: [local script run](evidence/rune-power/local.log),
[Android script run](evidence/rune-power/android.log),
[Android touch dispatch](evidence/rune-power/android-touch.log),
[network authority/replica](evidence/rune-power/network.log), and
[wire/authentication checks](evidence/rune-power/network-tests.log).
