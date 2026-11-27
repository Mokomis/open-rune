# Weapon finishers

Turn on **Laws of Battle → Weapon finishers** in the campaign or in the multiplayer host settings. An existing saved ruleset keeps its selections. Players use the usual Attack and Throw controls. The host decides the outcome for everyone, including bots.

## Finding an opening

A weapon still needs to connect and deal positive damage after the original game's defenses. A clean hit can kill at any health when the attacker catches the target in its rear 90-degree arc, during the brief hard stun from a shield bash, or in the first 240 ms after a committed swing misses. A thrown weapon uses its direction of travel to judge a rear hit. Attacks keep their normal reach and wind-up. An ordinary frontal hit uses the weapon's normal damage. A hit that would already kill can also play its weapon's finishing motion.

Shield and crossed-weapon blocks protect against the execution. A crossed block still admits the usual chip damage. Multiplayer melee keeps its two-tick late-block allowance. Campaign bosses, scripted sequences and player cutscenes retain their original protection.

## What to look for

- **Swords** thrust on a killing body hit. Two equipped swords drive forward together into the same victim. A severing hit to a limb or head uses a cutting motion.
- **Axes** follow through across the target. Edged hits can take a head, arm or leg where the model and gore settings allow. The original game controls which arms and heads can detach.
- **Hammers, maces and clubs** drive downward and give the body the strongest impulse. The support hand follows the grip on heavy weapons.
- **Torches and improvised weapons** use a blunt follow-through. Creatures attacking with teeth or claws keep their own attack animation.
- **Thrown weapons** use the original projectile contact, damage and deflection. The thrower does not play a handheld finishing pose.

A successful run-through starts the victim's ragdoll on the authoritative kill tick. The original death code still grants score and drops inventory. Both blades produce one kill and one body. Weapons that originally destroy the body retain that result. Other lethal hits that use the normal body-damage path, including kicks, bashes and spell impacts, can start the corpse’s physics immediately. They keep their original damage and never receive a free full-health execution. Scripted level events continue to use their own rules.

## Reproducing the checks

On Windows, use the native combat probe on DM-Hildir with an isolated save directory and configuration. For example, add `--direct --rune-network-probe --rune-net-native-combat --probe-seed=1 --rune-finisher-probe=VikingShortSword --fin-case=pair` to the native executable, followed by the original Rune installation path. This sends the usual Attack input through the server, advances the fight and decodes the resulting snapshot on a presentation replica. The `pairtorso` case controls the hit region and checks both rendered blade directions. The `bot` case runs the multiplayer bot's decision loop. The `rear` case isolates the original weapon damage script and can select a package-qualified weapon class.

For campaign tests use `--direct --rune-tactics-probe=dual --probe-seed=1 --fin-campaign=player` or `--fin-campaign=npc`. The latter lets the tactical Dark Viking choose and perform its attack against Ragnar. The `--fin-species` option can exercise a specific original creature rig or confirm boss protection.
