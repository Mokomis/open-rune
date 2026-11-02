# Open Rune — joint rotation and dynamic skeleton

October 24, 2026. Implementation and runtime ledger. Broader combat and save coverage is recorded in [CAMPAIGN.md](CAMPAIGN.md).

## Recovered behavior

`Actor.GetJointRot` (603) reads the latest joint coordinate frame, removes the resource's rotation offset, and extracts world orientation. Invalid model/joint queries return a zero rotator. The implementation follows the same animated pose and attached-parent transform used for joint positions and sweeps.

`Actor.ApplyJointForce` (608) accepts only joints with the accelerative, spring or grav-joint flags (`0x86`). It transforms a world vector into the current joint coordinate system and **adds it to velocity**. It is an impulse, not an ignored force or a replacement animation. Nonfinite input and singular transforms are rejected defensively.

Each actor now owns transient dynamic state. The original initializer uses animation frame zero and clears velocities. Spring points keep an animated target position and rotation. Their velocity receives the spring restoring term, is damped, and advances the position; the original script `GetSpringJointParms` supplies damping, spring constant and world-space displacement limits. Crossings of the rest position dispatch `JointChangedDirection` with world-space velocity and displacement.

Accelerative joints receive a restoring acceleration along the animation's local position, with magnitude from `GetAccelJointMagnitude`. Their endpoint is constrained to the animation's length, damping and angular stopping threshold come from `GetAccelJointParms`, and optional actor motion lag follows `bApplyLagToAccelerators`. The endpoint direction bends the **parent** joint using Rune's model-axis conversion. Grav joints follow their separate recovered branch; the original position helper clears their target acceleration rather than reading zone gravity. Absolute position/rotation flags are honored. The original scale helper is empty; no dynamic scaling was invented.

Dynamic poses feed CPU skinning, attachment transforms, native joint queries and sweep hulls. Integration happens after actor physics and before attachment updates, so it does not depend on being rendered. State is cleared when the model, relevant flags, default joint flags or animation-cache reset changes. It is recomputable transient state: save/load reinitializes dynamics rather than retaining a swinging object's exact velocity.

## Source evidence

All evidence is static Ghidra recovery through temporary REA 6.3.0 servers, using existing Ghidra 12.1.4 and Java 21. Sessions were closed; integration configuration was not changed. This is not an original-runtime comparison. Original Engine.dll SHA-256: `41458bac7e717a57effbe45d9351664ab79d72d7b821c3103018d057b7c585d0`.

| Function | Address | Evidence ID |
| --- | --- | --- |
| execGetJointRot | 0x103019bf | ev_990cbcd25a069220947102834ce920b10320efbcb9f3d3b39b641042beb74fa3 |
| execApplyJointForce | 0x10301d93 | ev_93c8b370a764b8a9d2f14a8ecb7eff027235575ee03b0251bfaef0056530195a |
| TickDynamicJoint | 0x103e1730 | ev_000a39366abb92bb9de8ff8d7b2149b28241dd62c71fb65d7aacc7a955ba16ea |
| SetAnimFrame | 0x10302a0e | ev_304592576f3bf480a9e7918feb99a4ed47b908ce9e28210f310e29553d8ee540 |
| WorldToJoint | 0x10301938 | ev_ab00d7b5e53e81b9cda68c5ade21f686efdc436d3ab4d7c146b268fa0ffa3a66 |
| LockDSkel | 0x10304b1f | ev_9df0c13f140270bc85e98c9507fc4a69c1c96883a10dee51fba24ea49c5d8dce |
| InitializeDynamicSkeleton | 0x103df810 | ev_780ee150546429ecb55b9538685b81c81f76b0d4526f9ba20c9a06b92cf3e5a7 |
| CalculateChildBones | 0x1030420a | ev_6756db3573a0e95445869558f7e7163997dfa2cce7842a39f9b98dd510709533 |
| ApplyDynamicPose | 0x103dfca0 | ev_7da22548b52cbf9837f17fa3a20998a3032d9e99ec2a1bfb2d7027c7005700db |
| DynamicPosition | 0x103ddec0 | ev_23d28bd71d2b7bc6bab5ea9de06dff52fe915adfb0423fc5f1ca4ac5159bd9a3 |
| DynamicRotation | 0x103dde40 | ev_a81659696171d9248b6c6dea0837cb7ea85fdb2d6cef2ab5ebde3f4497b90875 |
| DynamicScale | 0x103de0d0 | ev_642d2e5e55dc90fd26a077265286a15a6f7b6e6330124fbe3102fb9fdf1ee29d |

Pseudocode and original Evidence JSON are in `evidence/Engine.<name>.cpp/json`. `Tools/RuneRecovery/dynamic_native.py` reproduces the new helper recoveries; existing recovery files retain the two native wrappers.

## Runtime validation

Numerical probes passed on both desktop and the Android ARM build; logs are retained in `evidence/campaign/campaign-headless-final.log` and `campaign-native-dynamic.log`. Both platforms produced the same reported values. `RuneDynamicProbe.h` uses the actual recovered native and model paths, with explicit assertions; these are headless numerical checks, not comparisons against the running original executable.

| Probe | Confirmed result on desktop and ARM |
| --- | --- |
| `Drum1` spring impulse, 1,200 steps at 120 Hz | Maximum displacement `0.787323`, final displacement `0`; remained within the script's displacement bound of `5` and returned to rest. |
| `Tree1` accelerative impulse, 600 steps at 120 Hz | Direction change `4.38159`, maximum skinned-vertex displacement `1.65373`; state remained finite and constrained endpoint length was preserved. |
| Invalid/unflagged controls | Joint indices `-1` and `50`, unflagged joints, and nonfinite force input left state unchanged. |
| Reset | Cleared dynamic velocity and restored the original pose. |

The existing `GetJointRot` desktop probe also passed. The numerical time step above is a simulation input, not a measured display frame rate. Native dynamic state remains transient across save/load: exact spring or pendulum velocity is not serialized.

## Coverage and limits

Use `Drum1` or `Drum2` (joint `drum`) for a spring impulse and `Tree1` or `Gong` for an accelerative impulse. Check actual vertex/joint displacement, limits, settling, and finite state, plus invalid/unflagged controls. A successful numerical probe is not proof that joint/platform collision or every scenery interaction matches the original engine. Frame timing, rotation interpolation and floating-point precision differ from the original cache representation. Keep those distinctions in final claims.

The script audit found no additional native prerequisites in the basic Goblin arm/head severing or Weapon Throw/Drop paths. Severing changes body-part collision and visibility, spawns original limb/blood actors, and can drop the weapon. The thrown Weapon Touch path applies body-joint damage (joint zero); it does not prove animated sweep-mask parity. `RuneCarcass.ApplyPainToJoint` calls the force native, but the call to that helper is commented out in recovered `JointDamaged`; normal corpse damage is not a reliable force test. `DwarfMech` uses GetJointRot for aimed attacks and ApplyJointForce for torso reactions, but also has unrelated projectile/AI behavior requiring its own runtime coverage.
