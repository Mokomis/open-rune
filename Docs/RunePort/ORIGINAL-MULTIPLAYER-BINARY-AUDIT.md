# Original Rune multiplayer binary audit

October 26, 2026. Read-only inspection of the locally owned Rune Classic installation. This is an inventory of recoverable behavior, not original source or proof that Open Rune can run the old protocol.

## Artifact identity and method

REA `open_binary` identified `System/Engine.dll` and `System/IpDrv.dll` as x86 PE native binaries. SHA-256: `Engine.dll` `41458bac7e717a57effbe45d9351664ab79d72d7b821c3103018d057b7c585d0`; `IpDrv.dll` `62c0c56cc1dbc567d45ebc40d0b1d2ab7a31271f585a23c946611972569985a2`. The initial native-provider search failed because no decompiler was configured. After user approval, official Ghidra 12.1.4 was installed and verified, its macOS arm64 native decompiler was built, and REA's Ghidra-backed CLI analyzed focused functions. Raw REA output is retained outside the repository under `outputs/original-multiplayer-re/`; there was no original-game execution. The supplementary export inventory below was read from the same binaries with the system PE/COFF `objdump` tool.

## Observed native multiplayer entry points

`Engine.dll` exports named functions for `UNetDriver::InitListen`, `InitConnect`, `TickDispatch`, and `TickFlush`; `UNetConnection::ReceivedPacket`, `ReceivedRawPacket`, and `CreateChannel`; `UChannel::SendBunch`; `UActorChannel::ReceivedBunch` and `ReplicateActor`; `AActor::ProcessRemoteFunction`; and `ULevel::TickNetClient` and `TickNetServer`. These names establish that an original native driver, packet/channel handling, actor replication, remote-call routing, and client/server level ticking are present as binary entry points. They do not establish their internal algorithms, wire format, compatibility, or current security properties.

`IpDrv.dll` exports `UTcpNetDriver::InitListen`, `InitConnect`, and `TickDispatch`, and `UTcpipConnection::LowLevelSend`. The `Tcp` class spelling is historical and by itself does not identify the game packet transport. The PE import listing references the corresponding engine connection and driver types. Function bodies need inspection before drawing conclusions about packet format or transport behavior.

## Focused REA function evidence

- `Engine.dll`, `UActorChannel::ReplicateActor` at `0x10302d47`, REA Evidence `ev_33877f880c619a08a13b2ccb201903a82a67ff72ce5f286fb27e660562219fa9`: Ghidra pseudocode shows class/field network-cache lookup, an `FFrame::Step` evaluation of a script condition, bit-level field IDs and value serialization, `UChannel::SendBunch`, and tracking of sent fields. This is direct static evidence that original actor replication is integrated with the class metadata and script VM. Offsets and some types remain uncertain decompiler output.
- `Engine.dll`, `AActor::ProcessRemoteFunction`, REA Evidence `ev_410a0e6d425422a52dd1e89f3a7ad6c5a4b249121ddde823aeecf1e5357b7c8d`: checks function/actor state, evaluates a function condition through `FFrame::Step`, selects a connection and passes the function/arguments to another native routine. The exact flag meanings and wire encoding remain unresolved.
- `IpDrv.dll`, `UTcpNetDriver::TickDispatch`, REA Evidence `ev_0af161e68602936244797f4202b7cc1d18326521a43fe80a36c5d28dc35241cb`: polls an OS socket, reads a bounded datagram-like buffer, identifies or creates a connection, and calls `UNetConnection::ReceivedRawPacket`. Socket calls appear by `WSOCK32.dll` ordinal; their names require ordinal verification before a more precise transport claim.
- `IpDrv.dll`, `UTcpipConnection::LowLevelSend`, REA Evidence `ev_2c7f425d6fe257c1cb841a81f2682aac083e35f2fe6d75b812658c4d51a475ed`: sends a caller-supplied byte buffer to a stored address through an OS socket call. The full connection and reliability behavior is in other functions.

These are static reconstruction findings. REA's generated pseudocode is not original source, and this audit does not prove interoperability or safe handling of hostile packets.

## Recoverable game logic

The installed package's recovered `Engine/PlayerPawn.uc` under `Tools/RuneRecovery/recovered-scripts/` has replication declarations identifying reliable and unreliable property/function traffic. It declares `ServerMove` client-to-server and `ClientAdjustPosition` server-to-client. The recovered `ServerMove` body discards old timestamps and carries an optional prior move for loss recovery. `ClientAdjustPosition` installs authoritative position, velocity, physics state and base; `ClientUpdatePosition` replays saved moves after an adjustment. This is a usable behavioral reference, though recovered script text has not been fully checked against bytecode or an observed live match.

## Use for Open Rune

The binary confirms that original Rune multiplayer was much more than menu scripts. We can use the recovered replication and movement behavior to check Open Rune's adapter and, if desired, investigate specific original protocol behaviors. It is not drop-in native ARM64 code: Open Rune's `UNetDriver`, `UNetConnection`, and `UActorChannel` are shell classes; its normal startup is standalone. Original replication depends on engine metadata and VM evaluation that Open Rune would have to recreate. Directly porting the old protocol would also need new platform sockets, connection/session behavior, integration of kick/bash, and validation under modern network conditions. The focused evidence strengthens the case for selective behavior reuse rather than binary or protocol transplant; it does not quantify engineering time saved.

## Next evidence gate

If protocol compatibility becomes an explicit goal, inspect `UNetConnection::ReceivedPacket`, `UChannel::SendBunch`, actor receive/spawn logic, and connection initialization with REA, then validate against a controlled original-game exchange. If the goal remains a new protocol and fair two-player combat, prioritize original movement correction, field relevance, and match-rule behavior as comparison fixtures. Keep original binaries and raw decompiler output out of the public repository.
