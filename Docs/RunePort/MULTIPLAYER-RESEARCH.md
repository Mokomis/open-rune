# Open Rune multiplayer research

October 25, 2026. Research and design update only. No networking implementation, dependency import, game build or device change. Read with [the implementation plan](MULTIPLAYER-PLAN.md).

## Findings that change the plan

The custom protocol remains a reasonable project, but its first deliverable must be a precise contract and adversarial harness. Transport reliability, animation readability and authoritative combat fairness are separate problems. AI-assisted networking work exists publicly; none of the inspected projects establishes that our proposed 32-player Rune melee design will meet its targets.

| Decision | Evidence and implication |
| --- | --- |
| Three initial traffic lanes, bounded independently | Valve provides independent lanes with priorities/weights. ENet documents ordering dependencies within a channel. Losing bulk data must not stall fresh input. |
| Fresh packet identity for retransmission; separate message/action identity | Valve's wire documentation separates packet receipt from reliable data. Late originals, duplicate segments and false loss must not duplicate gameplay. |
| Explicit deadlines and queue telemetry | Valve exposes queue-time statistics, but its inspected NoDelay flag does not implement general queue-age expiry. We must specify expiry ourselves. |
| A named congestion-control starting point | RFC 9002 supplies a documented NewReno/loss-recovery reference. Pacing alone is insufficient; freeze our adaptation before implementation. |
| Defender-visible wind-up becomes a test metric | Ubisoft describes how timing changes what an opponent actually sees. Correct final damage does not guarantee a fair-looking duel. |
| No whole-world rollback assumption | GGPO requires reproducible state save/load/replay. Open Rune has not established that capability. Keep movement replay and bounded collision history separate. |
| Independent fixtures and restart security checks | The netcode projects provide concrete interoperability tests and documented replay/nonce pitfalls, including an AI-co-authored fix. |

All Open Rune rates, budgets and latency limits remain proposals. Source inspection is not a security audit, benchmark or runtime validation.

## Valve: transport and gameplay are separate references

Inspected GameNetworkingSockets revision `d534c19aa760df3fb75fd20db13ba1932b8a5463`: selected source paths, API contracts, wire format and network-condition tests. It supplies transport, not Rune actor replication or combat rules.

- [Lane API](https://github.com/ValveSoftware/GameNetworkingSockets/blob/d534c19aa760df3fb75fd20db13ba1932b8a5463/include/steam/isteamnetworkingsockets.h#L419): independent message numbering, strict priorities and weighted sharing within a priority. Reliable ordering applies within a lane, not across lanes. The header recommends keeping lane count small. Our starting count is three; per-entity lanes would create unnecessary work.
- [Wire format](https://github.com/ValveSoftware/GameNetworkingSockets/blob/d534c19aa760df3fb75fd20db13ba1932b8a5463/src/steamnetworkingsockets/clientlib/SNP_WIRE_FORMAT.md): packet acknowledgements and reliable-stream data have distinct identities. Our contract needs bounded ACK metadata, retransmission under fresh packet numbers and late-original deduplication.
- [Send flags and statistics](https://github.com/ValveSoftware/GameNetworkingSockets/blob/d534c19aa760df3fb75fd20db13ba1932b8a5463/include/steam/steamnetworkingtypes.h#L967): batching and queueing are separate from reliability. The inspected header explicitly says NoDelay's desired long-queue drop check is not implemented. Do not describe it as automatic deadline enforcement. Measure our queue age and make expiration explicit.
- [SNP implementation](https://github.com/ValveSoftware/GameNetworkingSockets/blob/d534c19aa760df3fb75fd20db13ba1932b8a5463/src/steamnetworkingsockets/clientlib/steamnetworkingsockets_snp.cpp): sampled send limits, reassembly limits and token-bucket pacing. A comment referring to future BBR work is not evidence of a complete BBR implementation. We did not audit the entire controller.
- [Connection tests](https://github.com/ValveSoftware/GameNetworkingSockets/blob/d534c19aa760df3fb75fd20db13ba1932b8a5463/tests/test_connection.cpp): impairment scenarios include loss, lag, reordering and bandwidth constraints. Use repeatable adverse conditions, including traffic competing with gameplay; do not validate only an idle LAN.

Source SDK 2013 was inspected separately at `be57e3d01ac7d7aa6a2cacbfef187c0b46616db0`. Its [lag compensation manager](https://github.com/ValveSoftware/source-sdk-2013/blob/be57e3d01ac7d7aa6a2cacbfef187c0b46616db0/src/game/server/player_lagcompensation.cpp) combines measured latency, interpolation and command time with bounds and plausibility checks. This supports explicit clock validation and bounded history; its historical constants are not recommendations for Rune.

The [HL2 multiplayer bludgeon implementation](https://github.com/ValveSoftware/source-sdk-2013/blob/be57e3d01ac7d7aa6a2cacbfef187c0b46616db0/src/game/shared/hl2mp/weapon_hl2mpbasebasebludgeon.cpp) brackets an attack with lag compensation and resolves a line/hull trace. It is useful evidence that melee can use history, but it does not settle Rune's multi-frame sword sweeps, shield timing, interruption or simultaneous trades. Do not generalize this source to all current Valve games.

## ENet: simple API, important ordering consequences

Inspected revision `5a9c537fd464b3c6d3c55e1d3bd47588faf71b42`:

- [Design documentation](https://github.com/lsalzman/enet/blob/5a9c537fd464b3c6d3c55e1d3bd47588faf71b42/docs/design.dox) and [peer dispatch](https://github.com/lsalzman/enet/blob/5a9c537fd464b3c6d3c55e1d3bd47588faf71b42/peer.c) show why reliable loss on a channel can also delay sequenced unreliable traffic on that channel. Merely choosing UDP does not eliminate application-level ordering stalls.
- The peer throttle adjusts unreliable transmission probability using observed RTT behavior. Our inference: semantic expiry and superseding state are better controls for deciding which Open Rune updates to discard. This does not replace congestion control.
- [Protocol implementation](https://github.com/lsalzman/enet/blob/5a9c537fd464b3c6d3c55e1d3bd47588faf71b42/protocol.c) contains timeout/retransmission and bandwidth/window handling. Large-message fragmentation also has flag-dependent behavior. Our wire contract must make maximum size, fragmentation, retry limits and delivery class explicit.

ENet is a useful small transport reference, not a complete modern authentication, replication or melee solution. The research does not select it as our shipped dependency.

## Congestion reference

[RFC 9002](https://www.rfc-editor.org/rfc/rfc9002.html), especially sections 6 and 7, separates loss detection, congestion response and pacing. Its NewReno model tracks bytes in flight, limits growth, reduces the window during recovery and handles persistent congestion. Section 7.7 describes pacing; section 7.8 addresses underutilization.

Our proposal is to document a small adaptation with explicit ACK-delay bounds, reordering behavior, probe timeout and idle restart. All payload traffic consumes the same connection budget. This is a design starting point, not a claim that Open Rune implements QUIC. Preserve [RFC 8085's UDP guidance](https://www.rfc-editor.org/rfc/rfc8085.html), including congestion response and avoiding fragmentation. Exact constants and exceptional probe behavior belong in the reviewed wire/transport contract.

## Melee: what the defender sees matters

Ubisoft's [February 2020 For Honor testing-ground explanation](https://www.ubisoft.com/en-au/game/for-honor/news-updates/9OPowoU28h2YRO8zSvw5z) describes attack timestamps and synchronized completion, while network delay changes the visible beginning of an attack. Buffered and late combo input could expose different amounts of animation. Its experiment standardized concealed startup time, alongside an existing small attack-input delay.

**Our inference:** measure visible warning as well as hit correctness. Replicate attack start/phase/cancel promptly and repair with state snapshots. Compare buffered and late inputs, and log the opponent's first displayed cue. Do not copy For Honor's 100 ms concealment or 33 ms input delay into our separate rewind/finalization limits. They are historical balance choices, not universal recommendations. Any scheduled attack-start experiment must expose its responsiveness cost.

Torn Banner's [October 2021 Chivalry 2 update](https://chivalry2.com/2021/10/28/post-launch-update-re-patch-2-2-0-fight-knight/) identifies reduced replication/animation updates, particularly outside the player's view, as suspected contributors to missing animation and registration problems. That post was an investigation, not proof of a single root cause. **Our inference:** keep relevant combat collision/animation state updated off camera, and test turning toward an attack in progress.

[GGPO's official description](https://www.ggpo.net/) requires a deterministic simulation that can save, load and advance state for rollback. We have not demonstrated those properties for the Rune VM, actors and dynamic joints. Use the principle of immediate local response where safe, but do not promise full rollback or call collision-history queries equivalent to GGPO.

No detailed, verified Mordhau adjudication algorithm was located in this review. GDC catalog material was found, but inaccessible full presentations were not used to support implementation claims. These gaps remain gaps rather than being filled with community guesses.

## GitHub and AI-assisted networking

The search distinguished explicit AI-development evidence from an AI-agent instructions file or an AI-themed repository name. README claims, inspected test definitions and successful measured behavior are different evidence levels. No third-party code was run and no current CI pass was independently verified.

### Strongest relevant precedent: netcode and netcode.rs

Glenn Fiedler's [C netcode README](https://github.com/mas-bandwidth/netcode/blob/778c444d1030cffea05bc63bc4e22e6bbc88e761/README.md) credits Glenn Fiedler and Rowan Claude. More concretely, the [server-restart sequence fix](https://github.com/mas-bandwidth/netcode/commit/dc21b7015ab791b87a1befd8523812ff67defefc) records Claude co-authorship, changes networking/security code, and adds a regression test. This is direct evidence of AI-assisted work on a real networking library, not proof that AI originally designed the entire protocol.

The [Rust implementation](https://github.com/mas-bandwidth/netcode.rs/blob/5ff9c44cbd6ef04e91f4b54bee09971bfddb23e7/README.md) describes a from-scratch implementation of the existing netcode standard and also credits Rowan Claude. We inspected its [golden-vector tests](https://github.com/mas-bandwidth/netcode.rs/blob/5ff9c44cbd6ef04e91f4b54bee09971bfddb23e7/src/wire_compat.rs), [C interoperability tests](https://github.com/mas-bandwidth/netcode.rs/blob/5ff9c44cbd6ef04e91f4b54bee09971bfddb23e7/tests/c_interop.rs) and [CI definition](https://github.com/mas-bandwidth/netcode.rs/blob/5ff9c44cbd6ef04e91f4b54bee09971bfddb23e7/.github/workflows/ci.yml). They check bytes against the reference implementation and exercise C/Rust connections in both directions. Those tests exist in source; this review did not execute them.

The lesson is independently specified behavior and cross-implementation evidence. A new Open Rune protocol has no existing compatible endpoint, so we need separately checked fixtures and a minimal independent model/decoder. Reference transports can later provide performance comparisons, but cannot validate our custom wire compatibility.

The C project's [implementer notes](https://github.com/mas-bandwidth/netcode/blob/778c444d1030cffea05bc63bc4e22e6bbc88e761/IMPLEMENTERS.md) document replay-window overflow, specification errata, restart sequence/nonce separation and a platform random-number trap. These motivate explicit lifecycle and boundary tests. Applying one published fix does not establish complete restart security; our own key/session lifecycle needs review as a whole. The library supplies a secure connection layer, not Rune replication or melee fairness.

### Other examples and limits

| Project | Evidence inspected | Relevance and limit |
| --- | --- | --- |
| [AndrewAltimit/breakpoint](https://github.com/AndrewAltimit/breakpoint) | README explicitly describes AI-authored code under human direction; inspected server game loop, tests and CI configuration. | Multiplayer application development precedent. It uses WebSockets and is not a new UDP transport. The inspected loop also contains unbounded channels; we should retain our bounded-queue requirement. |
| [TheAndersMadsen/dashdance](https://github.com/TheAndersMadsen/dashdance) | README explicitly acknowledges AI-assisted development and use of Slippi networking. | Relevant game/port work, but reuse of existing netcode is not evidence of a newly designed transport. |
| [Krilliac/smellslikenapalm](https://github.com/Krilliac/smellslikenapalm) | README and repository evidence describe custom server/network work; README says a retail client cannot yet fully connect. Agent workflow files alone do not establish authorship. | A useful warning that protocol scaffolding and plans are not working multiplayer. Not counted as proven AI-authored or operational. |
| [ai-2070/net](https://github.com/ai-2070/net) | Inspected browser netcode hardening tests for restart identity, malformed input, rate limits and interpolation; repository offers agent integration. | Useful adversarial-test ideas. Agent integration and the project name do not prove AI authorship, performance or suitability for Rune. |

We are not the only people applying AI to multiplayer networking. The strongest transferable practice is to constrain generated work with a reviewed specification, independent fixtures, fuzzing and interoperability/impairment evidence. None of these examples removes the need for Open Rune's two-player human melee test before scaling to 32.

## Planned gates after this research

1. Freeze the wire contract, queue/ACK bounds, security dependency and clock/late-input policy. Calculate golden examples independently of implementation.
2. Build the portable impairment harness and transport only when implementation is authorized. Verify bounded memory, no stale action replay, restart safety and congestion recovery.
3. Prove fixed server simulation and two-player movement before adding historical melee arbitration.
4. Compare authority policies with recorded input, pose, shield, cue and commitment timelines. Human duels judge readability and responsiveness.
5. Scale active matches through 4/8/16/32 only after the earlier gates pass. All previous proposed performance budgets remain unmeasured.

No external library was imported, no source implementation was copied into the fork, and no upstream project was contacted. Any future dependency choice requires its own technical and licensing review.
