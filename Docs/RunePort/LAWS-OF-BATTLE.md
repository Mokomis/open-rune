# Laws of Battle

Laws of Battle selects added gameplay mechanics independently. It is not a collection
of visual settings and does not change original Rune Power, ordinary jump/climbing,
weapon attacks, shield blocking, food, quest triggers or map scripts.

## Menu flow

Windows and Android's themed menus offer the checklist before beginning a new
campaign, and under Settings → Gameplay → Laws of Battle during a campaign. Choices
persist and apply to loaded campaigns too. They do not change a save's difficulty.
Giant leap is visibly disabled and is rejected for **every campaign participant**,
even if a modified preference or direct action tries to enable it. The separate
two-handed aerial slam remains selectable.

Host a match offers its own Laws of Battle page, also reachable from Match rules.
These choices persist separately from campaign preferences. Set them before starting
the server. During a match, Gameplay → Laws of Battle displays the current host's
rules read-only. Joining and leaving do not overwrite campaign preferences.

Each checklist scrolls vertically through thirteen entries, with an explanation and the
activating actions. Windows supports mouse, keyboard, controller and page/wheel
navigation; Android uses touch and controller navigation. Changing a campaign law
applies immediately; an in-progress carry is safely dropped if its interaction law
is disabled. Existing launched objects finish their motion.

## Choices

| Law | Activation / effect |
| --- | --- |
| Kick | F, right-stick click or touch Kick; close-range kick, shield protection retained. |
| Shield bash | Double-tap Block while holding a shield; swipe, recoil and brief stun. |
| Dual wield | Use a second eligible one-handed weapon; Attack alternates weapons. |
| Crossed-weapon block | Hold Block while dual wielding; weaker than shield defense. Inactive without Dual wield. |
| Giant leap | Double-tap Jump in a direction; multiplayer only. |
| Shield rush | Block + Attack with a shield; stows weapon and charges forward. |
| Two-handed aerial slam | Jump, tap Jump again in the air, then Attack with a heavy two-handed weapon. |
| Corpse pickup & throw | Use by an intact finished corpse; Attack/Throw launches it, Use drops it. |
| Loose prop interaction | Use lifts safe stools, dwarf stools, kegs and buckets; Attack/Throw launches, Use drops; Kick shoves them. Requires Standard/Cinematic physics. |
| Physical knockdowns | Strong impacts produce recoverable living ragdolls. Rush can knock down in Legacy physics too. Off retains non-ragdoll combat effects. |
| Tactical campaign enemies | Existing optional tactical campaign humanoid AI. Campaign-only checkbox; multiplayer bots always run their own planner and obey the other laws. |
| Special moves without rage | Removes the bloodlust requirement/cost for enabled leap/rush; cooldowns and equipment requirements remain. |
| Spider Monkeys | Jump toward a wall, then Use to grab. Move to climb/traverse, Jump away, Use/Crouch to drop. Original full-body ledge animations; campaign default Off. |

Physics mode is Legacy / Cinematic / Standard, with Standard as the default. Standard retains the former Chaos behavior; stored values remain 0 / 1 / 2. This is separate from move
permission. Props remain deliberately limited: tagged, attached, scripted and static
scenery is protected. This feature does not expand which map objects are props.
Boss/scripted-state/character-equipment safeguards still apply; a checked box does
not give incompatible monsters humanoid moves.

## Authority and AI

`RuneBattleLaws.h` owns the validated mask, dependencies and descriptions.
`RuneBattleLaws.java` mirrors the bit order and descriptions for Android. Campaign
and multiplayer masks are independent. A scoped host/client rules context restores
the previous campaign context after exit, including exceptions.

Eligibility, activation, guard/damage resolution, readiness and relevant AI choices
check permissions. Bots do not merely attempt disabled moves and wait for rejection:
checks cover kick/bash eligibility, crossed guard, leap/rush planning and second tap,
slam setup, second-weapon pickup and corpse/prop choice. Execution gates also reject
untrusted client requests. Previously queued kick/bash contacts cannot commit after
the relevant campaign law is disabled.

Invitation format 5 carries the mask. Native, Android and discovery validators reject
out-of-range masks. The effective mask is domain-separated and SHA-256-bound into
the existing authenticated gameplay hash, including travel contracts. Altering an
invitation's laws causes match-contract rejection. Credential renewal must retain
the authority's laws. This is not a new network packet layout: wire/ALPN remain 25,
while gameplay compatibility becomes 33 (release dev.4/build 4). Matching updated
client/server builds are required.

## Dedicated server

The native server uses the same rules. Default is all laws enabled; campaign
restrictions are independent. A UI-created server passes its explicit mask.

```
--rune-battle-laws=4095
```

Zero disables all additions represented by the mask. Use individual named switches
to avoid calculating masks; named switches override the base mask:

```
--rune-law-kick=0
--rune-law-bash=1
--rune-law-dual-wield=1
--rune-law-crossed-block=0
--rune-law-giant-leap=1
--rune-law-shield-rush=1
--rune-law-aerial-slam=1
--rune-law-corpses=1
--rune-law-props=1
--rune-law-knockdowns=1
--rune-law-tactics=1
--rune-law-normal-moves=1
```

Values must be 0 or 1. `--rune-physics=0`, `1`, `2` selects Legacy, Cinematic, Standard. Omitting the option uses Standard.
The older explicit normal-bloodlust switch is still accepted; named normal-moves
is the final override. No public host or long-running match is started by these tests.

## Validation boundary

Evidence: `C:/OpenRune/outputs/laws-of-battle-20261107` and the Mac's
`~/OpenRuneArtifacts/LawsOfBattle-20261107`.

Windows and Mac native gameplay fixtures exercise disabled execution gates,
independent crossed block, bot decisions in four mode contexts, existing combat,
physical rush recovery and the campaign no-leap restriction. These are bounded
fixtures, not another full campaign or all-map playthrough. A real localhost server
with an all-off mask accepts matching bot clients and rejects a tampered rules mask.
Android preference/host/invitation model tests cover migration, all 4096 preference
masks, independent persistence and invalid data. Native desktop menu tests exercise
new-campaign setup, disabled leap, hosting, campaign re-entry, paging and restart
persistence; captured pages are visually reviewed. Android packaging compiles the
actual views/JNI and includes the updated discovery bundle.

Mac runtime builds share the rules and are tested; the themed checklist navigation
belongs to the existing Windows/Android menus. This change does not port the
Windows menu framework to the Mac. No tablet installation, installed Mac-app
replacement, Git commit, push or publication is implied by building the feature.

## November 8: scrolling checklist and combat icons (dev.12)

Android and Windows now display one continuous vertical checklist instead of three
pages. Back and Physics / Begin campaign stay outside the scrolling area. Each
row has a control-derived combat icon, its activation description and an On/Off
switch. Dual wield uses two upright blades; crossed block mirrors both sword
points upward; aerial slam uses a hammer, as requested. Android shares its
existing vector artwork with the touch controls; Windows uses the same silhouettes.

Android retains the scroll position and stable controller focus when a rule
changes. Windows supports mouse-wheel scrolling, a draggable scrollbar, Page
Up/Down and keyboard/controller focus that reveals offscreen rules. The desktop
menu clips tiles and glyphs to the viewport so rows cannot cover the heading or
footer. Campaign leap restrictions, host-read-only rules and dual-wield/crossed
block dependencies are unchanged. No network or gameplay rule revision changes.

Validation: Windows native build and isolated rendered menu probes passed campaign
and hosting navigation, all twelve rows, toggles and dependency behavior. Android
model checks passed all 4,096 campaign masks plus host isolation and invitation
validation; navigation model retains all 52 settings. The original-key dev.12
Android APK is installed; tablet screenshots cover list top/middle/bottom and
corrected icons. Original saves and INIs are preserved. These UI checks do not
constitute new combat or FPS qualification.

Evidence: `C:/OpenRune/outputs/battle-laws-menu-20261108` and controller-side
`~/OpenRuneArtifacts/BattleLawsMenu-20261108`. Mac's installed menu implementation
was not extended or redeployed in this UI change. No commit or push implied.

November 8 dev.13 icon correction: Corpse pickup & throw now uses a standing
carrier holding a limp body overhead, replacing the axe-throw glyph. Android and
Windows use the same figure layout; the ordinary Throw control keeps its axe.
No rule or gameplay behavior changed. Native builds passed and the original-key
Android update is installed, with tablet visual review and data hashes recorded
in the BattleLawsMenu-20261108/corpse-icon evidence folder.

November9: Spider Monkeys adds bit4096 (mask8191), with matching native/Android/discovery validation. See [wall climbing](SPIDER-MONKEYS.md). Existing saved masks are retained.
