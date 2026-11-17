# Multiplayer names and team identification

Human players choose a name before their first normal UI join, including Find
games, private invitations, rejoin and joining their own host. The name persists
across matches/restarts and is editable from the multiplayer menu. Android uses
Player profile; desktop menus have Player name. Direct desktop joining also
provides a first-use input dialog, with an isolated --player-name CLI override
for automated tests. Native standalone desktop names are saved in the local
SurrealEngine/MultiplayerName.txt profile.

Names permit up to24 characters/48UTF-8bytes, with letters/numbers and simple
punctuation. Controls, invalid UTF-8, bidi formatting, multiline names and forged
[BOT] prefixes are rejected. Profile persistence is independent of graphics
resets. Existing settings and normal game saves are preserved.

Players and bots have visible overhead names in Deathmatch and other individual
modes. Team Deathmatch, HeadBall and team-sized Arena use team-colored names and
a compact team shield/letter (red R,blue B; green/yellow for supported four-team
variants). Tags require a living visible actor, bounded distance and world
occlusion; they do not reveal opponents through walls.

Player names On/Off is available in the multiplayer menu/profile and HUD settings.
Turning it Off hides overhead names but retains team shields/letters. Scoreboards
retain names regardless of this choice. Bots receive random Nordic-flavored names
from a64-name pool, drawn without replacement per hosted session. Their assigned
names stay with stable invitation slots. See [the full pool](BOT-NAMES.md).
**[BOT] appears only in scoreboard rows**, never in overhead names or team markers.
The name is stable through death, respawn, reconnect and map rotation.

Profiles and labels are bounded authenticated Control records; individual label
messages fit within768-byte transport limits. Names are not added to snapshots
or the already-bounded MatchStatus roster. Updates are coalesced by the server,
include newly joining clients, and are republished after map travel. A client
cannot publish another player's label. dev18/build18/native-rules38 requires
matching client/host builds; combat and snapshot layouts remain unchanged.

## Validation

Model tests cover first-use emptiness, persistence, editing, visibility and
validation. Wire tests cover profiles/labels, truncation, malformed lengths,
role flags, player IDs and different map epochs. A live authenticated library
regression checks changing a name and receiving the updated label. Independent
wire fixtures assemble bytes without using the production serializers.

The existing impaired-link accounting assertion now counts unique action IDs: an
unconfirmed notification is provisional and can later be authenticated, as its
existing explicit regression already establishes. The added handshake record
exposed this test-only double-count; no action admission/timing behavior changed.

Current task evidence: controller OpenRuneArtifacts/PlayerNames-20261109 and
Windows C:/OpenRune/outputs/player-names-20261109. Follow final receipts for actual
platform qualification and deployment; compilation is not device validation.
