# Dark Cloud Expanded → Chronicle port checklist

Expanded adds up to 173 features. 110 are done, 31 are done but partial, 8 were already in Chronicle, 1 needs a check, 6 are skipped, 13 are skipped for now (counted as finished) and 4 are to do.

## Order of work

Enhanced features come first, Expanded features second. Where an effect exists in both, Enhanced's version goes in first and Expanded's rework waits for the second pass.

1. Enhanced, easier: clock in Yellow Drops and Dark Heaven Castle, leave the dungeon from floor select, Repair and Escape Powders as active items, powder stacking, back-floor chest loot, daily shop slot, minibosses on floor entry (all in, untested)
2. Enhanced weapon effects: Babel's Spear and Angel Gear (Enhanced versions). Hercules' Wrath is already on Expanded's version and stays there; anything already Expanded stays Expanded (both in, untested)
3. Enhanced quests: monster hunts, Samba's dagger challenge, Mayor's Demon Shaft quest, repeatable fishing quests, Pickle's % completion (all in, untested)
4. Enhanced, needs the logging build: Ungaga throw speed (built in chronicle-ungaga-throw.zip, untested), Fog Gate cutscene (logging in, waiting on logs from the playthrough)
5. Enhanced, hard: play as any ally in town, new town dialogue for every ally (both in, untested)
6. Expanded zip 1, weapons: built in chronicle-expanded-weapons-1.zip (Babel's Spear, Terra Sword, Cactus Desert Bloom, Angel Gear party heal and Guardian Reflector, Chronicle 2, their menu descriptions). Zip 1b built in chronicle-expanded-weapons-1b.zip (Pockle Call, Spirit Beast, Super Steve's Sphere Inheritance, the last menu descriptions)
7. Expanded zip 2, dungeons and enemies: miniboss scaling and loot tables, more than five shot types per floor, Ungaga's doors for the whole party, harder enemy AI option (all in, untested)
8. Expanded zip 3, fishing and quests: casting and line fixes, bobber camera, fish steering, size changes, Mardan rework and records, Pike/Pao/Sam/Devia tracking, Mint's 17-fish quest, quest tracker, Hello starts quests, Fairy King fix (all in, untested)
9. Expanded zip 4, towns: party menu switch sequence, allies fishing, ally ladder climbs, full town animation sets, overhead Georama camera and fast run (all in, untested)
10. Expanded zip 5, the missing weapon effects: every visual left out of the weapon rows (dimming, tints, glows, smoke, lightning, judgement blade, explosion, Dark Genie beam, the cat's wings and cape, the Reflector's animation and fades), the gameplay pieces with them (Big Bang's explosion immunity, Cactus and Babel's Spear blocking shots, enemies attacking the Reflector, Confuse's stars, icon and projectile friendly fire, Super Steve's Xiao shot versions of Toan's moves), plus attack voices only, the stuck-landing fix and Toan's name swapped in the ally lines (built in chronicle-weapon-visuals.zip, untested; the file dump is taken out again)
11. Partials cleanup: the Fairy King's Crystal Eyeball, the fountains off for Samba's challenge, the heart in the ally lines, Xiao's lower talk camera, Atlamillia Insurance kept with the save, Star Breaker's sphere into the bag, the 7 Branch Sword's break cut off mid-animation, the enemy named in the steal notice, Confuse's own icon, the light blue Babel beam, the small visuals (rock and cactus shadows, Angel Shooter sparkle, judgement blade glow disc, Cactus overshoot and fade, Mirage shimmer and dissolve, Zeus's camera hold, cowering from the Sun Sword, the Reflector's gauge and draw), Super Steve's shot versions of the sword moves and the Mirage, Enhanced's miniboss stamina (built in chronicle-partials.zip, untested). Left for after the playthrough: the Divine Beast cat's wings, mask and cape, the Double Impact and Matador pellet looks; Xiao's guard columns through a sphere, Super Steve's HUD sphere icon and a slingshot sphere's pellet sprite built in chronicle-super-steve-columns.zip (zip 20, untested)
12. Your full playthrough (planned in a separate chat), then playthrough bug fixes
13. Then the map changes, in the order below (zip 1, Brownboo, built in chronicle-map-brownboo.zip; zip 2, Yellow Drops, built in chronicle-map-yellow-drops.zip; zip 3, Queens ground and snake statue, built in chronicle-map-queens.zip; all untested)

## Map changes plan (scoped after zip 20)

How Chronicle loads towns: each town's scene.scn is stored as .gltf bundles, but DataReadLoadFile rebuilds the original bytes from them into the load buffer (model_gltf::Splice, byte for byte). So a hook right after the scene, mapinfo.cfg or img.pak loads sees the original format, which is what Expanded's patches are written for. Patches go there, made from Expanded's code at load time; no game data goes into the repo

PAL check: every one of the 28 node names Expanded's map patches look for (e03, e05, s04, s13) is in the PAL files, and so is every vanilla text they search mapinfo.cfg for (Yellow Drops' WATER_SURFACE rows, Queens' water rows and WATER_SHAKE, GROUND). Expanded's patches stop if a vanilla value doesn't match, so a mismatch shows up as a log line rather than a broken town. No NTSC dump needed for now

To check in the first map zip: several patches make the scene bigger (Brownboo's camera hull and rocks, Queens' rebuilt collision and canal cap). The scene loads into EdNPCBuffer with the next town data placed right after it (editloop.cpp), so the first zip logs the room left after each patched scene

Two kinds of work:
- Data patches on the town files (Expanded's IsoPatch/): node-name and byte edits, rebuilt collision blocks (CollisionGeom, CollisionMdsWriter, SceneBaker) and the generated geometry in IsoPatch/Data/ (QueensCollisionData, BrownbooCollisionData, SceneCollisionData, about 184 KB of C# arrays to turn into C++ data; Expanded is BSD 2-Clause, so keep its notice with them)
- Game code (Expanded's Elf*Patches, tools/stubs/*.s and Town/), rewritten natively as the weapons were: the town camera (ElfCameraPatches, town_camera_collision.s, camera_norm_side.s, camera_height.s), the canal (ElfCanalPatches, canal_evict_fade_hook.s), water drawing order (ElfWaterPatches, water_order_gate.s), fishing (ElfFishingPatches, FishingSpots.cs, FishingScripts.cs, FishingCatchCamera.cs, fishline_*.s). Town/TownCameraPolyBuffer.cs fixes a PS2 buffer overrun; check whether Chronicle's camera gather has the same fixed 2,000-poly limit

Planned zips:
1. Built in chronicle-map-brownboo.zip, untested. Brownboo (smallest; proves the load-time patching works): upper crater walls culled, see-through houses and stray corner tris (TownSceneBakes), the camera hull s04g01_v rebuilt and the pond rocks added to the player collision (BrownbooCollision, TownCollisionBakes), plus a test helper that writes your position and the floor height under you to the log
2. Built in chronicle-map-yellow-drops.zip, untested. Yellow Drops: water raised to 4.25 (mapinfo WATER_SURFACE rows and the s1302 sheet), the west bank reshaped (YellowDropsWestBank, 519 lines)
3. Built in chronicle-map-queens.zip, untested. Queens ground and structures: rebuilt ground and camera collision (QueensCollision, QueensTerrainRules), the climbable snake statue (SnakeStatueCollision, the part swap in TownScenePartBakes). Expanded's other Queens parts (the fishing signs, the ladder, the ripples, the canal water tuning, the west-end cap) go with zips 4 and 6
4. The Queens canal: tide and its map-jump on the black fade, the ladder carve, the drained floor, the west-end cap, the ripple texture, the waterfall spray, the healing-spring water look (CanalLadderCarve, CanalWestEndCap, CanalRippleTexture, ElfCanalPatches, ElfWaterPatches)
5. The town camera collision overhaul (ElfCameraPatches and its stubs)
6. The new fishing spots (Queens, Brownboo, Yellow Drops) with 6 fish per spot (5 at the Oasis), the pool swaps, the line split, per-spot camera heights and casts stopping at the canal walls. Last, since the Queens and Yellow Drops spots sit on the changed maps


## How to read this

- **To do**: not ported yet
- **Done**: ported in one of our zips
- **Done, partial**: ported, but something was left out or works differently from the mod; the notes say what ("Left out", "Not ported", or the difference)
- **In Chronicle**: Chronicle already has it, sometimes as an option you turn on
- **Check**: might already be fixed in Chronicle; confirm in game or in code
- **Skip**: only useful for building the mod, or Chronicle handles it differently

The **From** column says where a feature is listed: the Expanded changelog, your Enhanced v1.00 PDF, both, or code only (in neither changelog). Built from Tororoi/DarkCloud-Expanded at commit 5a56f0e and redlambert/Chronicle at commit 2645560.

## Data, prices and shops

The weapon tables, prices, shop lists, daily shop slot and text fixes are done.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| Weapon stats, elements and monster bonuses | Both | Done | Data pack |
| Fixed weapon abilities (Poor, ABS Up, Critical, Stop, Heal, Drain) | Expanded | Done | Data pack |
| Extra attachment slots | Both | Done | Data pack |
| Build-up path changes | Both | Done | Data pack |
| Weapon, key, amulet and bait prices | Enhanced | Done, partial | Data pack; 7 prices capped at 32,767 |
| 50% Fragile on new Bone Slingshots and Hardshooters | Expanded | Done |  |
| Lamb's Sword changes at 50% durability | Both | Done |  |
| Fishing prize: Flamingo for 1000 FP | Expanded | Done |  |
| Fruit of Eden at the Queens fruit stand for 6000 | Expanded | Skip | Zip 19: taken out (your call: easy max HP from the middle of the game, on top of Enhanced's 250 cap). The Queens fruit stand sells its vanilla list again |
| Gaffer's shop drops the Endurance attachment | Enhanced | Done | Shop fix zip |
| Fairy King sells Metal Breaker and Mimic Breaker | Code only | Done | Shop fix zip |
| Daily rotating item slot in shops | Enhanced | Done | Untested. Gaffer, Wise Owl, Jack, Joker, Brooke, Ledan and the Fairy King sell one extra item from Enhanced's 5-day rotation. Picked from the in-game day, so no save space. Zip 17: the Fairy King also sells the Crystal Eyeball for 5,000 once the Mayor's bonus quests open (Enhanced's flag for it is the Mayor's quest unlock, not a Fairy King quest) |
| Game text typo and name fixes | Expanded | Done | Untested. Applied as each line is read; Expanded's 47 swaps plus its two spacing rules |
| Mystery circle "Funds increased" 2.2x down to 1.5x | Enhanced | Done | Untested. Circle Amplifier makes it 2x or 2.5x |

## Combat and game mechanics

Five are done and five were already in Chronicle; the WHP billing row matters most, since five weapon effects depend on it.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| Heal ability ticks every 3 seconds | Expanded | Done |  |
| Enemy poison wears off | Expanded | Done |  |
| A hit breaks your freeze | Expanded | Done |  |
| Ungaga's charge shot hits cost no WHP | Expanded | Done |  |
| No-drop enemies can drop items | Expanded | In Chronicle |  |
| Bombs knock you away from the blast | Expanded | In Chronicle |  |
| Element picker on D-pad Up | Expanded | In Chronicle | Options, Game page. Expanded's also offers None; not compared yet |
| Switching keeps your lock-on | Expanded | In Chronicle | Options, Game page |
| Weapon effects bill WHP through the game's wear routine | Expanded | Done | Untested; nothing uses it yet. Paid on the next walking step through the game's own wear. Needed by Solar Flash, Big Bang, Zeus, Terra Sword, Hercules' Wrath |
| Confuse, a new weapon ability | Expanded | Done, partial | Untested. Carried by Babel's Spear. 1 hit in 20 confuses for 20 s, never a boss. A confused enemy chases the nearest enemy or you, and its melee hurts enemies; an enemy it hits fights back. Zip 16: the dizzy stars (gedit/s04/chara/e114ex.chr) spin over the 6 confused enemies nearest you, and a confused enemy's shots hurt enemies too. Zip 17: its own icon, Expanded's art painted into the empty tile beside Steal as the menu's icon sheet loads (console line [Effects] when it paints). Works differently: friendly-fire kills may still give ABS |
| Empty SynthSphere raises a weapon to +5 while socketed | Code only | Done | Untested. First hole only. Taking it out while the weapon is still +5 undoes it; a level-up makes the levels permanent |
| Faster Ungaga item throw | Enhanced | Done | Untested. Zip 18: Ungaga's throw (motion 27, frames 186 to 206) plays at 0.44 instead of 0.22, set on the loaded model each time his dungeon model loads (any of the three load paths go through LoadChara2). Same change as Enhanced's "22" to "44" in c10b.chr's config and Expanded's CheckSwap; the motion comes from Expanded's docs/character-motion-table.md. The console prints [UngagaThrow] with the old and new speed, or what it found if the motion isn't the vanilla one |
| Shorter Fog Gate cutscene | Enhanced | To do | Waiting on logs. The dark-fog door on Divine Beast Cave floor 9 runs a dungeon system script; logs/events.txt records its number and length, logs/d02_event.stb is the script file. Enhanced's change isn't in its public source |
| Broken Dagger attachment glitch fixed | Enhanced | Done | Shop work buffer now cleared; Broken Dagger fix zip |
| Out of Bounds menu glitch fixed | Enhanced | Skip | Expanded has no code for it; the shop buffer fix is its only shop glitch fix |
| Flapping Duster can spawn in Gallery of Time | Enhanced | Check | Gallery's chest lists hold it 3 times at normal odds; confirm in a playthrough |
| Saving after the credits works | Enhanced | In Chronicle | Chronicle rewrote the save after the ending |

## Dungeon systems

None of these are started; the chest randomizer and minibosses are the biggest.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| Chest randomizer by rarity and dungeon depth | Code only | Done | Untested. Every generated chest is rerolled from Enhanced's lists: small chests get the dungeon's item list, large ones a weapon from its table plus a random trap. About 12% large in the first dungeon, 1.5 points fewer per dungeon, twice as many with Chronicle 2. Powerup Powder is rerolled 4 times in 5 (1 in 2 with Chronicle 2 on main floors). The Map and Magical Crystal chests stay unless you carry one in your bag, which then lights the floor's. The rumour quests' items turn up in a back floor's first small chest (1 in 3, Demon Shaft 1 in 7) until you have one |
| Back-floor chest loot table, weapons on back floors | Enhanced | Done | Untested. Done with the chest randomizer: back-floor small chests use Enhanced's shared back-floor list (gems, attachments, amulets, fruit), large ones the dungeon's weapons |
| Clown randomizer | Code only | Done | Untested. Each box holds one item: half the time from the dungeon's clown weapons, else the common lists (the small one's last entry is the dungeon's back-floor key item). Chronicle 2 makes it always roll the weapon table. Back-floor clowns offer the same as the main floor's |
| Repair and Escape Powders as active items | Enhanced | Done | Untested. Square on an Escape Powder asks first; a second press 0.5 to 3 s later leaves. Repair Powder fills WHP to max with the Auto Repair message and sound, and isn't used if WHP is full |
| Repair, Escape and Revival Powders stack in active slots | Enhanced | Done | Untested. Escape, Revival, Repair and Auto Repair Powder stack in quick-use slots, as in Enhanced |
| Leave the dungeon from the floor select screen | Enhanced | Done | Untested. Circle on the floor select closes it and starts the game's own escape to town (the Escape Powder's). It costs nothing |
| Floor cleared message | Enhanced | Done, partial | Untested. "There is no sign of monsters on this floor" once the last one dies, once per floor. Expanded's line also mentions an escape skill; left out since Chronicle has none |
| Minibosses: a stronger enemy on floor entry | Enhanced | Done | Untested. Replaced in zip 2 by Expanded's minibosses (the two rows below): the roll, size, stats and loot are now Expanded's. Zip 17: Enhanced's stamina timer change: a miniboss stays in the stamina state while it lives (twice the damage it deals, half what it takes), on top of Expanded's scaling. Zip 19: behind the new Miniboss Stamina option (Game options, default on) |
| Miniboss combat scaling (hitbox, shots, aggro range) | Expanded | Done, partial | Untested. Each eligible enemy (not a boss, the key holder or the pockle; mimics and flyers count) has a 1 in (15 minus ineligible) chance, every winner promoted. 1.5x model and body spheres, 1.6x attack spheres, 1.5x lock-on cursor, 3x HP, ABS and money, 1.5x defense, melee and own-damage shots, 1/10 item damage, walk clip slowed 1.3x (not the bats). Engages from 1.3x as far (1.5x plants and chariot). Back-floor trips keep the surviving minibosses on each side, and a respawned key moves off them. Works differently: reach is per enemy (Expanded shares it per species via the nearest one) and only scales distance-to-player reads. Minibosses take status hits as their species does (Expanded's minibosses shrug them off, a side effect of how it writes their stats) |
| Miniboss loot tables and boosted weapon drops | Expanded | Done, partial | Untested. Per-dungeon species loot: 5% rare drop, else 30% flavour drop, else 35% back-floor key, 15% weapon, 80% attachment (60% gem), else item (60% Powerup or Auto Repair Powder). Boosted rare/flavour weapons (Gladius, Bandit Slingshot, Frozen Tuna, Super Steve, Dark Cloud...) get their stats, elements and abilities once picked up after the kill; cancelled on floor change. Works differently: the weapon roll uses Enhanced's dungeon weapon table, and the boost watches every character's bag (Expanded only Toan's) |
| More than five monster shot types per floor | Expanded | Done, partial | Untested. Works differently: the shot pack simply holds 18 effects (two for each of 9 monster kinds) instead of Expanded's swapping of five slots. Console logs "[SharedShots]" if all 18 fill |
| Ungaga's doors passable for the whole party | Code only | Done | Untested. Sun and Moon Temple, Moon Sea and Gallery of Time event scripts get Expanded's door values (both 150 trigger distances to 50, three bytes to 30) at load, found at Expanded's offsets or by pattern. Console logs "[UngagaDoors]" either way |

## Enemies

All to do; the randomizer pieces build on each other, and the three new Gemrons are the hardest.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| Randomize enemies option | Expanded | Skip for now | Whole randomizer group skipped for now; counts as finished. Opt-in in Expanded (Gameplay toggle, off by default) |
| Themed floors | Expanded | Skip for now | Needs the randomizer |
| Stat normalization for out-of-place enemies | Expanded | Skip for now | Needs the randomizer |
| Model memory guard for randomized floors | Expanded | Skip for now | Needs the randomizer |
| Monster sounds on any floor | Expanded | Skip for now | Needs the randomizer |
| Steve comments on any enemy | Expanded | Skip for now | Needs the randomizer |
| Bosses and minibosses on regular floors | Expanded | Skip for now | Hard |
| Faster enemies option | Expanded | Skip for now |  |
| Stronger enemies option | Expanded | Skip for now |  |
| Harder enemy AI option | Expanded | Done, partial | Untested. Options > Game > Harder Enemy AI (off by default). Native revivers (Master Jacket, Mummy, Gacious, Horn Head, Silver Gear and Enhanced versions) revive 45%; the 23 others with a real get-up revive 30% at 40% HP, after lying still a moment, with Expanded's spliced script code. Console logs "[HarderAI]". Works differently: the toggle takes effect from the next floor (no mid-floor undo) |
| Bomb Gemron | Expanded | Skip for now | Hard: new model and shots |
| Crystal Gemron | Expanded | Skip for now | Hard: new model and shots |
| Atla Gemron | Expanded | Skip for now | Hard: new model and shots |

## Weapon effects

Every effect is done and waiting on your test; the Mardan swords are under Fishing.

### Toan and shared

| Character | Effect | Status | Notes |
| --- | --- | --- | --- |
| All | In-game weapon descriptions match the new effects | Done | Untested. Every weapon with a new effect now has its description, Super Steve's included |
| All | Super Steve versions of every effect | Done, partial | Untested. Built as one system: see the Super Steve: Sphere Inheritance row for what carries over and what is left out |
| Toan | Baselard: Heavy Hand | Done | Super Steve version built (untested): every pellet throws its enemy |
| Toan | Gladius: Jacket Hunter | Done | Super Steve version built (untested) |
| Toan | Crysknife: Crystal Affinity | Done | Super Steve version built (untested) |
| Toan | Crysknife and Magical Hammer: Circle Amplifier | Done | Untested |
| Toan | Bone Rapier: Skeleton Key and Gravedigger | Done | Tested working: both Skeleton Key and Gravedigger |
| Toan | Shamshir: Swift Strikes | Done | Untested. Zip 17: Super Steve version built (Xiao's draw 1.6x faster, her shoot at 0.95); the Partisan's sphere gives it too |
| Toan | Small Sword: Quick Draw | Done | Untested. Zip 17: Super Steve version built (the shot fires the moment it's let go, the gauge between shots refills at once); Tsukikage and Heaven's Cloud spheres give it too |
| Toan | Buster Sword: Buster Boost | Done | Untested. No Super Steve version, as in Expanded |
| Toan | Kitchen Knife: Spring's Blessing | Done | Untested. No Super Steve version, as in Expanded |
| Toan | Sax: Fine Fare | Done | Untested. Super Steve version built |
| Toan | Wise Owl Sword: Wise Owl Always Knows | Done | Untested. No Super Steve version, as in Expanded |
| Toan | Dusack: No Fool's Gold | Done | Untested. Super Steve version built |
| Toan | Tsukikage: Moonlit Focus | Done | Untested. Zip 17: Super Steve version built (pellets at 2x speed); a Heaven's Cloud sphere gives it too |
| Toan | Evilcise: Jealous Soul | Done | Untested. Super Steve version built: curses Xiao |
| Toan | Macho Sword: Overtraining | Done | Untested. No Super Steve version, as in Expanded |
| Toan | Sun Sword: Solar Harvest | Done | Untested. Super Steve version built |
| Toan | Sun Sword: Solar Flash | Done, partial | Untested. Core gameplay: blinding uses the game's Stop status. Zip 16: the blade whitens as the guard charges it, a gold glow breathes on Toan while primed, and the flash washes the room out to white, easing back over the 5 s blindness. Zip 17: blinded enemies cower in their guard pose (each species' guard from Expanded's table; one with no guard keeps its pose). Works differently: the glow is a tint on Toan instead of Expanded's torch-flame sprite; the cowering enemies hold still in the guard's first pose, as the blinding is the Stop status |
| Toan | Claymore: Greatsword | Done | Untested. Super Steve version built: every pellet throws its enemy (the big blade stays the sword's) |
| Toan | Maneater: Blood Price | Done | Untested. Super Steve version built: curses Xiao |
| Toan | Aga's Sword: Defensive Legacy | Done | Untested. Super Steve version built |
| Toan | Brave Ark: Hero's Courage | Done | Untested. Super Steve version built |
| Toan | 7 Branch Sword: Sevenfold Rite | Done | Untested. Zip 17: Status Break can be picked below +7 again; the break is cut off once the sword has faded out, the sword comes back whole and the window says "Status Break fails below level 7." |
| Toan | Heaven's Cloud: Typhoon | Done | Untested. Blade up to 3x over a 4 s hold; the whirlwind is drawn and hits as wide |
| Toan | Cross Hinder: Sanctifier | Done | Untested. Super Steve version built: pellet damage, ABS and reach |
| Toan | Atlamillia Sword: Atlamillia Insurance | Done | Untested. Sphere in a small chest on a random floor with atla (you chose chest over atla). Zip 17: the waiting spheres are kept with the save (quests.json), so they survive closing the game (console line [Insurance] on load) |
| Toan | Dark Cloud: Guard Crush | Done | Untested. Super Steve version built |
| Toan | Big Bang: Detonate | Done, partial | Untested. Whirlwind blast and locked-on blast with the flash, 20 WHP each. Zip 16: the blast is the game's bomb blast model (dun/effect/explosion.chr) in place of the whirlwind; primed and locked on, a 2x copy of the sword hangs point-down over the target and falls on the primed swing, the blast and flash going off where it lands; blue blade and glow while primed, the room dims; immune to the game's explosions (pumpkin bombs, self-destructs, bombs) while held, Super Steve's sphere too. Zip 17: a soft blue glow disc rides halfway down the hanging blade (the Sword of Zeus's gets a white-blue one). Works differently: the hanging blade grows in instead of fading in; the disc is a soft disc made at run time |
| Toan | 7th Heaven: Divine Guard | Done | Untested |
| Toan | Sword of Zeus: Lightning | Done, partial | Untested. Bolts, combo chain, 6-enemy volley and charged lunge bolt, 10 WHP each. Zip 16: every bolt is the Divine Beast Cave cutscene's lightning (gedit/s99/chara/lightning.chr) at 10x with a crack and a short blue-white flash; the judgement blade hangs and falls as Big Bang's does, the bolt coming down where it lands; the room dims while primed and as the lunge charge builds, the blade whitening. Zip 17: the camera holds its height through the charged lunge, and the hanging blade carries a glow disc. Works differently: the crack is the Thunder Gem's sound |
| Toan | Chronicle Sword: hits all nearby targets | Done | Untested. Splash never kills: leaves 1 HP and poisons. Zip 16: splashed enemies flash purple |
| Toan | Chronicle 2: big chests and Powerup rolls | Done, partial | Already matched (untested). Expanded's code doubles the big-chest rate (880 + 15 per dungeon becomes twice that minus 1000), keeps every Powerup Powder roll on main floors, and makes the clown always roll weapons, as the chest and clown randomizers already do. Its changelog's "every chest is a big chest" isn't what the code does. Owned-item menu description added. Zip 19: works differently (your call): with Chronicle 2, Powerup Powder on main floors is rerolled 1 in 2 instead of never |

### Xiao, Goro, Ruby, Ungaga and Osmond

| Character | Effect | Status | Notes |
| --- | --- | --- | --- |
| Xiao | Bone Slingshot: Skeleton Key and Gravedigger | Done | Tested working: both Skeleton Key and Gravedigger |
| Xiao | Steel Slingshot: Endurance Up | Done | Untested |
| Xiao | Bandit Slingshot: Steal Shot | Done, partial | Untested. Pellets fire the victim's own loaded shot at 2x. Zip 17: the notice names the enemy ("Master Jacket's projectile is now yours on this floor"), names from Enhanced's enemy list. Works differently: it's the mod's own notice, not a line added under the game's item notice |
| Xiao | Flamingo: Lock-On Distance | Done | Untested. Dragon's Y, Divine Beast Title, Angel Shooter and Angel Gear get the reach too |
| Xiao | Flamingo: Bait Flamboyance | Done | Untested. +10 notice radius per Flamingo owned, up to 3 |
| Xiao | Hardshooter: Ricochet | Done | Untested. A ricochet never ricochets again |
| Xiao | Double Impact: two pellets | Done, partial | Untested. Both pellets ricochet. Pellets keep their own sprite (Expanded draws Steel Slingshot stones) |
| Xiao | Matador: Charging Bull | Done, partial | Untested. Charged pellet drawn 2x size; Expanded's flying slingshot model and glow not ported |
| Xiao | Super Steve: Sphere Inheritance | Done, partial | Untested. Super Steve carries the effect of the weapon its SynthSphere came from: every effect that works on kills, hits, guards, ailments, chests, curses, lock-on or Xiao's own shots, plus Pockle Call and Spirit Beast. Zip 16: a charged shot with a Sun Sword, Big Bang or Sword of Zeus sphere carries Solar Shot (the blinding flash), Detonate (the blast where it hits) or Lightning (a bolt on what it hits). Zip 17: Swift Strikes (draw 1.6x, shoot at 0.95), Quick Draw (fires on release, gauge refills at once), Moonlit Focus (pellets at 2x speed), Heaven's Cloud's two-stage charge (empowered at 3/8 s, most at 3 s: pellet up to 8x and 1.5x damage, a wind burst of 60 to 160 units on impact), the Halberd line's charged pellet (0.5 s hold: its form's speed and size, 1.5x damage), the Mobius Ring ramp (x1.5 every 1.5 s held, pellet up to 15x) and the Mirage decoy from Xiao's guard. Zip 20: the sphere weapon's icon over Steve on the dungeon HUD (20 px, just above his raised hands, copied into the HUD sheet's spare cell (64, 32) with the equipped weapon's, fading with the HUD); a slingshot sphere's own pellet sprite (its cell of basefx01); Xiao's guard columns through the column system Ungaga's weapons use: Cactus sphere raises Queens' trees (gedit/e03/scene.scn part e03t01 with its grass square dropped, picture e03b04 from img.pak's e03b01.img, texture block 0x3E) 10 ahead after 0.25 s of guard, 0.7x, a wall of radius 9.5 for 10 s that blocks enemies and their shots, no damage and no shadow, then a 0.5 s fade; Babel's Spear sphere raises a 4x upright statue of Super Steve after 5 s, spinning with the beam at 0.7x, its hands hitting within 6 units for half the attack every 0.75 s, solid at radius 2, with the floor-wide confusion; Terra Sword sphere primes after 5 s (green) and on lock-on drops the nut (gedit/s04/chara/e114kinomi.chr, 2x, block 0x3F) onto the target's head: half the attack through any guard, the enemy confused while the nut stays, the nut hops off away from Xiao and rests 20 s on the floor, then fades. Console lines [SuperSteve] and [Expanded] say what loaded. Works differently: the nut doesn't block (Expanded's code doesn't say whether it does); the statue also gets the spear's 1x tip strike as it rises (I couldn't confirm Expanded's does); a sphere also carries a few effects Expanded's list leaves out, such as Supernova's ailments; the wind burst's look is the summon smoke with the Wind Gem's sound |
| Xiao | Dragon's Y: Dragon's Breath | Done | Untested. Element ball only where a floor monster fires it, else a 5x pellet (Expanded's own fallback). Lock-on speed done too |
| Xiao | Divine Beast Title: Spirit Beast | Done, partial | Untested. The charged shot flies as the town cat (gedit/s86/chara/c04cat.chr): grows in, falls, lands, walks to the locked-on or nearest enemy, crouches and leaps for 1.5x damage with a heavy kick, sits with nothing to chase, fades after 10 s. Not ported: the wings, mask, cape and glow, the float-up leap clip, a floor probe (it walks at Xiao's height), the pellet shrinking as the cat grows on it. Watch: the cat's facing, size and animation; the console says whether c04cat.chr loaded |
| Xiao | Angel Shooter: Guardian Grace | Done, partial | Untested. Heal ticks every second while guarding; Angel Gear gets it too. Zip 16: a soft glow on Xiao while she guards, brightening a moment with each heal, in place of the white pulses. Zip 17: the Angel Shooter's guard starts with the healing spring's sparkle, its chime and a gentle white flash. Not ported: the Angel Gear's golden character-change burst at guard start |
| Xiao | Angel Gear: Guardian Reflector | Done, partial | Untested. While Xiao guards, a 2x copy of the slingshot stands 12 units ahead, turned to the nearest incoming shot, else the nearest enemy. Shots reaching it or her are caught and fired back one every 0.25 s at the nearest enemy (the monster's own shot, weapon attack, its element and ailments). 5 enemy blows break it, with the weapon-break sound; back after 4 s. Zip 16: enemies within 60 units go for it instead of her. Zip 17: its health shows on the attack gauge (a fifth gone per blow, refilling over the 4 s once broken, so her own shot waits for it too), and it draws back before each shot it fires and snaps forward. Works differently: it grows in and shrinks away instead of fading; the whole slingshot draws back, not just its pouch. Watch: the copy stands on its long axis, so check it faces the right way |
| Xiao | Angel Gear: party heal | Done | Untested. Expanded's version replaces Enhanced's: with Xiao out, each turn of the Heal ability's clock (every 3 s, every second while guarding) mends the party 1 HP, knocked-out members excepted, Xiao only if the weapon's own Heal isn't live |
| Goro | Frozen Tuna: Cold Storage | Done | Untested. The freeze rolls once per step |
| Goro | Tall Hammer: shrinks enemies | Done | Untested |
| Goro | Inferno: power from missing HP and thirst | Done | Untested |
| Ruby | Pocklekul: Pockle Call | Done, partial | Untested. Guard 1 s: one of the 8 pockles at random joins the floor's monsters, grows in, goes for the locked-on or nearest enemy and walks beside Ruby otherwise. Its blows only hit enemies, the enemies it hits fight back, your hits pass through it, no lock-on, no ABS or drops. Zip 16: it appears in Queens' smoke puff (gedit/e03/chara/e228ex.chr), lit white as it grows in. Works differently: it joins the floor's own monster set (needs a free kind slot of 9 and monster slot of 16; the console says why when none comes), and summoned pockles don't throw shots. Watch: textures on the floor's other monsters after a summon |
| Ruby | Bandit's Ring: Steal Shot | Done | Untested. Quick shots only; the charged shot stays hers |
| Ruby | Mobius Ring: longer charge, more damage | Done | Untested. After a full charge, x1.5 every 1.5 s held; orbs up to 5x size and hit radius |
| Ruby | Secret Armlet: Favoured Circles | Done, partial | Untested. Rage circle slows enemies instead; stat gains capped at 999, elements and bonuses at 99 |
| Ungaga | Javelin: Marine Hunter | Done | Untested |
| Ungaga | Halberd: Tornado Charge Buff | Done | Untested. Scorpion, Mirage, Cactus, Hercules' Wrath, Terra Sword and Babel's Spear get stronger forms |
| Ungaga | DeSanga: Vampire | Done | Untested |
| Ungaga | Scorpion: Venom | Done | Untested |
| Ungaga | Scorpion: Venom Lure | Done | Untested. Poisonous Apple noticed from twice as far |
| Ungaga | Partisan: Quick Combo | Done | Untested |
| Ungaga | Mirage: Decoy | Done, partial | Untested. Guard 0.25 s: a still copy of Ungaga in the guard pose lures every enemy for 12 s (18 s with Hercules' Wrath); an enemy that's hurt turns back to him. New guard replaces it; ends on weapon or party change and new floor. Built on two shared pieces for the other summons: PoseCopy (a still model copy) and Lure (enemy target redirect). Zip 17: the copy stands in the game's torch heat shimmer, and a new cast dissolves the old copy over 0.25 s before the new one fades in; Super Steve with a Mirage or Hercules' Wrath sphere casts it from Xiao's guard. Works differently: the shimmer is at full strength the whole time (two torch-sized hazes, at the waist and the shoulders), and enemies turn to the new copy at once. Like Expanded's, there's no shimmer on a floor without torches |
| Ungaga | Cactus: Absorb | Done | Untested. Rock, metal and undead enemies give nothing |
| Ungaga | Cactus: Desert Bloom | Done | Untested. Guard 0.25 s: a 3x copy of the held Cactus rises point-up 10 units ahead and stands 10 s, solid (radius 6) to enemies and Ungaga, pricking enemies within 2 units for 1/6 attack every 0.25 s with a short shove, no WHP. A new hold moves it. Zip 16: it rises in the summon smoke and stops enemy shots. Zip 17: it overshoots to 3.4x as it comes up before settling at 3x, casts a round shadow, and fades away over 0.5 s instead of sinking |
| Ungaga | Hercules' Wrath: Air Strike | Done | Untested. Guard 5.25 s primes while the decoy stands; the next swing blasts on the decoy (spear tip if none), 2x reach, 20 WHP, and ends the decoy. This is Expanded's version. Zip 16: the spear turns gold as it charges and stays gold while primed, the room dims, and the blast plays a sparkle swirl (gedit/s78/chara/e508_ex.chr) with a gold flash |
| Ungaga | Terra Sword: Big Rock | Done | Untested. Guard 5 s primes (green flash); then lock on and Master Utan's boulder (loaded from gedit/s96/chara/iwa.chr, texture in block 0x30) drops from 500 units, growing as it falls and following the target: 4x attack to enemies within 26, ~2x Big Bang's shove, through guards, 10 WHP. It rests 20 s as a solid column (radius 21) hitting enemies under it for 1x attack each second, then sinks. Zip 16: the sword glows green as it charges and while primed, the target darkens as the rock falls, and the landing sends out the bombs' shockwave with Goro's smash sound. Zip 17: its round shadow on the floor, growing from nothing over the last 300 units of the fall, gone as it sinks. Watch: if the rock never drops, the log says whether iwa.chr loaded |
| Ungaga | Babel's Spear: Curse of Babel | Done | Untested. Expanded's version replaces Enhanced's stop. Guard 5 s: a 5x copy of the spear rises point-up under the locked target (else the nearest within 60, else 15 ahead), following it through the 0.5 s rise; the tip strikes for 1x attack with the Baselard's throw through guards. It stands 20 s spinning at 240°/s, solid (radius 8), spikes hitting within 2 units for 1/6 attack every 0.25 s, then sinks over 3.7 s. Every regular enemy on the floor is confused until it's gone. Zip 16: the spear tints light blue as the guard charges, the Dark Genie's small beam (dun/monstor/c17_beem_s.chr) rises, stands and vanishes with it, and it stops enemy shots. Zip 17: the beam is recoloured light blue at 0.3 of its opacity as Expanded's is (only the spear's copy; the Dark Genie's own stays pink) |
| Osmond | Blessing Gun: Blessed Bait | Done | Untested |
| Osmond | Snail: Slime Trail | Done | Untested. 5% per hit |
| Osmond | Skunk: Longer Flame | Done | Untested |
| Osmond | Star Breaker: Shooting Stars | Done | Untested. 2% per kill. Zip 17: the sphere goes straight into the bag; with no room for an attachment the kill doesn't roll, as in Expanded |
| Osmond | Supernova: random status on hit | Done | Untested. 11% per hit: stop, poison, anger or goo |

## Fishing

Zip 3 is in, untested; what's left is the map work (new spots, the Queens tide). The Brownboo rocks (map zip 1) and the Yellow Drops bank (map zip 2) are in, untested.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| New fishing spots: Queens, Brownboo, Yellow Drops | Expanded | To do | Hard. Goes with it: Expanded's 6 fish per spot (5 at the Oasis), and its pool tweaks at Matataki Waterfall (Gummy becomes Niler) and East Harbor (Piccoly becomes Gobbler) |
| Queens canal tide, ladder and drained canal floor | Expanded | To do | Hard |
| Yellow Drops west bank reshaped | Expanded | Done | Untested. Map zip 2: the water goes from 0 up to 4.25 (the WATER_SURFACE plane and the suimenn sheet), and the west bank bulges west by up to 70, rebuilt in s1301: the grid10 and grid11 meshes, the crown wall and floor, the camera wall, the factory's camera ring pulled in, and camera hulls for the two torii-gate pillars. Made when Yellow Drops loads, each part checking the vanilla data first. Works differently: Expanded's block offsets are fixed to the USA s1301; here they come from the part's own record, checked to lay out the same way. Goes with the new fishing spots: the fish walls along the new waterline and spot 23's water height |
| Casting and line fixes, camera centered on the bobber | Expanded | Done, partial | Untested. The camera watches the float, not the point between you and the float, and goes back to its distance and turn after a catch instead of staying close in. A float above the water only spoils the cast once it stops moving, so long casts still in the air land. Fish catch walls on their +Z side as on the others. Left out (they go with the new fishing spots): the line split above and below the float, the per-spot camera heights, casts stopping at the Queens canal walls |
| Brownboo pond rocks have collision | Expanded | Done | Untested. Map zip 1: Expanded's three simplified pond rocks are added to Brownboo's player collision (s04g01_a) when the scene loads. Check with --where: R3 on a rock logs the floor under you |
| Fishing quest tracking for Pike, Pao, Sam and Devia | Expanded | Done | Untested. Already counted by the Enhanced fishing quests (zip 4). New: Sam's special reward. After three of his quests, you can see the fish in the Queens Sea while fishing there, and he reminds you how many are left |
| Fish steer toward you at Matataki Waterfall and East Harbor | Expanded | Done | Untested. Every 10 s each fish there turns toward you |
| Mardan swords rework (Eins, Twei, Arise) | Expanded | Done | Untested. Counts in any bag or storage. Fishing Points x1.2 (Eins), x1.5 (Twei), x2 (Arise), Garayans excepted. Twei and Arise give every other fish a second Garayan roll at the spot's own rare odds for the time of day, 1 in 5 of them Baron, at every spot as in Expanded. All three turn Garayans and Umadakaras toward you every 4, 3 or 2 s, divided by how much they like the bait on the hook. Arise scales sizes toward twice the largest. Console logs "[Mardan]" |
| Smoother fish size distribution | Expanded | In Chronicle | Chronicle's own fish size roll already lifts sizes into the largest the same way (Expanded's code says it matches Chronicle's) |
| Rerolled fish use the game's size formula | Expanded | Done | Untested. A rerolled fish loads as its new kind, so the game's own size roll sizes it |
| Fishing records give Arise Mardan Max Magic | Expanded | Done | Untested. Each species' best catch: +1 at its largest size, up to +43 (+117 for the two Garayans) at twice it, on Expanded's curve, capped at 999. Updated after each catch, when a save loads and on a new game. Console logs "[AriseMagic]" |
| Records screen shows one best catch per species | Expanded | Done | Untested. The records list keeps each species' best catch, largest first. Console logs "[FishRecords]" when it rewrites the list |
| Fish pull the bait down from farther away | Enhanced | Done | Untested. Bite reach 1 to 2 units (Enhanced pnach "more consistent fishing", CFish::BiteHook) |

## Towns, allies and camera

Playing as allies in town is in, with their full animation sets and ladder moves; what's left is the map work.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| Play as any ally in town, swapped in place | Both | Done, partial | Untested. Town party menu: pick any ally in the party and they take over once you leave the menu (pick Toan to switch back). Uses Enhanced's models (the allies' cutscene models) and its Xiao and Ruby motion fixes; Xiao's first-person camera sits lower. Toan comes back for every cutscene and on every new map. Event points are off for allies in maps 11, 13, 14, 33, 35 and 37, as in Enhanced. Zip 16: an ally without a full town moveset no longer gets stuck in the landing clip (Enhanced's fix; allies with their own moveset keep their landing clip). Zip 17: Xiao's talk camera looks lower. Works differently: Enhanced does it by pointing the camera at leftover memory past the game's three talk views; here the point it looks at comes down from 15 to 10 units |
| Party menu commit plays the dungeon's switch sequence | Expanded | Done | Untested. Picking a different character in the town party menu: the confirm chime, the portraits spread off-screen, the menu closes itself and the ally takes over. Picking the one already walking closes the page as before; an ally not in the party yet gets the reject beep |
| Full town animation sets for all five allies | Expanded | Done | Untested. Expanded's town models, rebuilt the same way from your game data the first time each ally is picked: every clip it copies between each ally's models (run, doors, item get, refusal, fall, land and the ladder clips), its held, moved, reversed and frozen windows, Ruby's added shadow and Osmond's helicopter backpack with its spin. Checked against your own files: all five build, with every joint matched but a few per clip. Ungaga now walks in Expanded's model (the one with his cloth). Xiao sits down after standing still 1.5 s. Works differently: built in memory instead of patched into the disc image. If a build fails, logs/town.txt says why and that ally walks as before |
| Each ally climbs ladders their own way | Expanded | Done, partial | Untested. Cross at a ladder: Xiao walks up, backs off, crouches and leaps over (down: hops off), landing on her own; Goro climbs in four hops and jumps down after a gathering crouch; Ruby floats up or down; Osmond flies up on his helicopter backpack and dives down; Ungaga refuses at both ends. Toan's ladder camera during the move. Works differently: played by the port's own step player instead of Expanded's generated town scripts, so the camera's hand-back is close but may not match exactly. Console logs "[Ladder]" for each move |
| Allies can fish | Expanded | Done | Untested. The fishing session runs as Toan (it loads Toan's fishing model, as in Expanded), and the ally comes back once you're walking again. town.txt in logs/ notes each return |
| Town camera collision overhaul | Expanded | To do | Hard |
| Queens snake statue is climbable | Expanded | Done | Untested. Map zip 3: the snake statue's (e03h06) player collision becomes its whole visual mesh with Expanded's surgery (400 triangles out, 68 in), and its camera hull an open 16-sided cylinder round the head. Goes with it: Queens' ground and camera collision rebuilt from the town's own structure meshes plus Expanded's walls (player collision per ground part with the loading-zone triggers kept, camera collision gathered on one part, the bridge gates' torches as columns), and the waterfalls' Z-write on so you hide behind them. Made when Queens loads, each part checking the vanilla data first |
| Brownboo cleanup: edit view, see-through houses | Expanded | Done | Untested. Map zip 1: the upper crater walls and the houses are drawn single-sided, the crater rings' and floors' stray corner triangles are gone, and the camera collision (s04g01_v) is rebuilt with the iwa01 tunnel rock's smooth hull. Made from the scene each time Brownboo loads; each part checks the vanilla bytes first and is skipped with a [TownMap] line in town.txt if they don't match |
| Overhead Georama camera and fast run in every town | Expanded | Done, partial | Untested. Select opens the overhead camera on every town map. Holding Square doubles your walking speed anywhere in town, except while fishing. Leaving the overhead camera still snaps you to the ground. Works differently: outside the five Georama towns the overhead camera can't open the Georama menu, since there's nothing to build there. Left out: the developers' debug jump, which Expanded switches on by accident with the fast run |
| Clock and time in Yellow Drops and Dark Heaven Castle | Enhanced | Done | Untested. Yellow Drops (map 23) and Dark Heaven Castle (38, 40) skip their map script's time stop, so the clock shows and time runs there as in other towns |
| Town music starts earlier in the day | Enhanced | Done | Untested. Music comes back at 9:00 instead of 11:00 (Enhanced pnach, EdSetBgmVol) |
| Demon Shaft unlock check | Code only | Done | Untested. The world map hides Demon Shaft until the game is cleared (the save's clear flag) |

## Quests and dialogue

All in, built on Chronicle's own quest system; zip 3's are untested.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| Quest talk menus: Hello starts a quest | Expanded | Done, partial | Untested. Chronicle's menus say Talk where Expanded's say Hello. Monster hunts, fishing quests and rumours: while one is on offer, Talk starts it and its own menu line is gone; while one is under way the menu shows "About the quest." instead. The Mayor's bonus quests work the same once they open. Works differently: before then the Mayor keeps his own words behind Talk and his "Any quests for me?" line, since his own talk hands out Norune's supplies. Samba, the puzzle and Pickle keep their lines (Expanded's list leaves them out) |
| Quest tracker | Expanded | Done | Untested. Journal > Quests: every quest under way or waiting for its reward (hunts, fishing, rumours, Samba, the Mayor, Mint), refreshed each time you open it, four at a time, Up and Down to scroll. The Mayor's line uses the allies' default names |
| Monster hunts: Macho, Gob, Jake, Chief Bonka | Enhanced | Done | Untested. Started from Talk since zip 3 (see Quest talk menus): defeat 8 to 18 of one monster in a dungeon you've reached (Demon Shaft kills don't count), reward a Powerup Powder. Also in: the rumour quests from Laura, Ro, Phil, Zabo and a Brownboo villager ("Heard any rumours?") |
| Samba's dagger-only challenge on Moon Sea 7 | Enhanced | Done, partial | Untested. Opens once all 6 have joined. Moon Sea 7's back floor with Toan and the Dagger only; a throwable or a swap cancels it. Reward: the Map. Also in: Yellow Drops' crystal puzzle, reward the Magical Crystal. Zip 17: the healing fountains are off while the challenge runs. Works differently: Enhanced turns healing off on every Toan-only floor (its challenge makes the floor one); here only during the challenge, and no floor data is changed |
| Mayor's clear-the-floor quest in Demon Shaft | Enhanced | Done, partial | Untested. "Any quests for me?" opens once Pickle sees the full collection, raising the Fruit of Eden cap to 250 and Gourds to 12 drops. Clear a random Demon Shaft back floor with one named ally, no throwables or swaps; a boost item each time until every stat is maxed. Not Chronicle's Mayor supplies quest. Left out: his lines before the quests open are approximations, since Enhanced's flags for "Dark Genie beaten" and "mid-journey" are unknown |
| Repeatable fishing quests | Enhanced | Done | Untested. Pike, Pao, Sam and Devia: catch some of one fish, or one fish in a size range, in their town. Reward: Fishing Points. Sam's 3-quest bonus added in zip 3. Now started from Talk (see Quest talk menus) |
| Pickle tracks your % completion | Enhanced | Done | Untested. Brownboo ("My collection"): counts items, attachments, ultimate weapons and secret items carried or stored. 100% gives the Flame Key and opens the Mayor's bonus quests |
| Mint's catch-all-17-fish quest | Code only | Done, partial | Untested. Brownboo villager 5: Talk gives her challenge, then the fish still missing. Reward: the Saving Book, again if you lose it. Works differently: a kind also counts if it's in your fishing records, so catches from before this zip count (Expanded counts only catches made with the mod on). Shows in the quest tracker |
| New town dialogue for every ally | Enhanced | Done | Untested. All of Enhanced's villager lines for the five allies in all 8 towns (two per villager, by turns). A villager with quest choices keeps them, with the ally's line behind Talk. Zip 16: while an ally walks the town, the game's own lines and the shop lines name the ally where they name Toan. Zip 17: the heart symbol on Enhanced's seven lines that end with one (the game's own heart icon) |
| Fairy King dialogue fix | Code only | Done, partial | Untested. His line about calling allies now says you can call them everywhere in the world. Works differently: Expanded rewrites it by memory address; here it's found by its words (a first page about allies ending "however..."). Console logs "[FairyKing]" when it changes the line, or when it skips a line about calling allies |

## Options, audio, cheats and dev tools

Most are small; the dev tools are skipped since they were only for building the mod.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| FOV and widescreen | Code only | In Chronicle | Chronicle's aspect setting |
| Graphics improvements toggle | Enhanced | Skip | PCSX2 rendering patches; Chronicle has its own renderer and a soft focus option |
| Weapon break beep on or off | Enhanced | Done | Untested. Options, Game page: Weapon Beep |
| Battle music or all music on or off | Enhanced | Done | Untested. Options, Audio page: Music and Battle Music |
| Attack voices on or off | Enhanced | Done | Untested. Options, Audio page: Attack Voices. Zip 16: mutes only the characters' attack shouts (voice rows 20 to 29), as Enhanced does; hurt and other cries still play |
| Hold L1 L2 R1 R2 Select Start for 3 s to reset to the title | Enhanced | Done | Untested. Works in towns and dungeons |
| Cheat codes (10 inputs, paused in a dungeon) | Enhanced | Skip for now | Skipped for now; counts as finished |
| Dungeon debug menu (R3) and inventory debug menu (L3) | Enhanced | In Chronicle | Retail's debug menus; turn on game.debug_mode or use the debug chord |
| Sandbox tab: spawn roster editor, Fish Data Farmer | Expanded | Skip | Tools for building the mod |
| Dev mode tabs, code cave scanner, TAS, test weapon grant | Code only | Skip | Tools for building the mod |
| Options saved inside the save file | Expanded | Skip | Chronicle keeps options in config.json |

## Playthrough bug fixes

| Bug | Status | Notes |
| --- | --- | --- |
| Mayor green and see-through in the windmill | Fixed, untested | The Babel's Spear beam recolour (red and green swapped, 0.3 opacity) ran on every texture entered into block 0x36. The effect props use 0x32 to 0x37 because no dungeon does, but town villagers load into 0x36 and up, so the first villager (the Mayor) got the beam's look. The recolour now runs only while the beam prop's own pack is loading (expanded_effects.cpp) |
| Cave Bat with green wings and a pink body (Divine Beast Cave 1) | To check | Not the beam recolour: enemies load into block 42. Waiting on whether the bat was a miniboss and how it looks in vanilla |
