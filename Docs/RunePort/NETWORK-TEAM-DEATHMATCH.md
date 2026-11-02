# Team Deathmatch checkpoint

October 26, 2026. Protocol/ALPN 12, native rules 12, snapshot 5. This extends the [Deathmatch checkpoint](NETWORK-DEATHMATCH.md).

Four invitation slots play on alternating Red and Blue teams using the original `RuneI.TeamGame` scripts. Original team and individual scoring, suicide penalties, team victory, respawn team retention, intermission and same-map restart work without reconnecting. Friendly fire is off; native kick/bash contacts also suppress ally damage, knockback and stun. Clients display team totals, player teams and world labels. Invitation version 3 includes the game mode; content agreement rejects a different mode on the same map.

Host with `--rune-net-mode=tdm --rune-net-players=4`. Default map is `DM-Team_ships`. Shared frag/time limits and rotation flags retain the Deathmatch behavior. Fixed-roster private sessions still end when a player leaves; public discovery, late join and reconnect are unfinished.

## Validation

Evidence: [network-team-deathmatch](evidence/network-team-deathmatch/). One representative map only, as requested.

- Four rendered desktop clients on DM-Team_ships: repeated match wins/restarts, original suicide and enemy-kill scoring, team retention after respawn, same authenticated connections. Natural kick and shield bash against an ally caused no health loss, displacement or stun; the original weapon-damage callback also preserved ally health.
- Android player 3 plus three desktop clients: team scores, result, same-map restart and unchanged connection passed. Native 2520×1680 and 4× MSAA verified. Scoreboard screenshot inspected.
- Android displayed FPS: 73.98 over 16.94 seconds, p95 13.88 ms, p99 20.82 ms, maximum 27.77 ms, no intervals over 50 ms; panel 144 Hz. This is a stationary Ships view, not sustained combat or a matched comparison with other maps. It does not establish a networking performance regression or 120 FPS.
- All 12 standalone test groups pass in Release and ASan/UBSan. Mode mismatch and invalid team status records are rejected.
- Normal saves, preferences and the app-specific refresh rule unchanged. The harness leaves the title screen visible after cleanup.

The scripted deaths test original scoring and lifecycle, not human melee fairness. Arena and HeadBall remain separate upcoming modes. No all-map compatibility claim.
