# Dark Cloud Expanded → Chronicle port checklist

Expanded adds up to 172 features. 95 are done, 42 are done but partial, 8 were already in Chronicle, 1 needs a check, 5 are skipped, 13 are skipped for now (counted as finished) and 8 are to do.

## Order of work

Enhanced features come first, Expanded features second. Where an effect exists in both, Enhanced's version goes in first and Expanded's rework waits for the second pass.

1. Enhanced, easier: clock in Yellow Drops and Dark Heaven Castle, leave the dungeon from floor select, Repair and Escape Powders as active items, powder stacking, back-floor chest loot, daily shop slot, minibosses on floor entry (all in, untested)
2. Enhanced weapon effects: Babel's Spear and Angel Gear (Enhanced versions). Hercules' Wrath is already on Expanded's version and stays there; anything already Expanded stays Expanded (both in, untested)
3. Enhanced quests: monster hunts, Samba's dagger challenge, Mayor's Demon Shaft quest, repeatable fishing quests, Pickle's % completion (all in, untested)
4. Enhanced, needs the logging build: Ungaga throw speed, Fog Gate cutscene (logging in, waiting on logs)
5. Enhanced, hard: play as any ally in town, new town dialogue for every ally (both in, untested)
6. Expanded zip 1, weapons: built in chronicle-expanded-weapons-1.zip (Babel's Spear, Terra Sword, Cactus Desert Bloom, Angel Gear party heal and Guardian Reflector, Chronicle 2, their menu descriptions). Zip 1b built in chronicle-expanded-weapons-1b.zip (Pockle Call, Spirit Beast, Super Steve's Sphere Inheritance, the last menu descriptions)
7. Expanded zip 2, dungeons and enemies: miniboss scaling and loot tables, more than five shot types per floor, Ungaga's doors for the whole party, harder enemy AI option (all in, untested)
8. Expanded zip 3, fishing and quests: casting and line fixes, bobber camera, fish steering, size changes, Mardan rework and records, Pike/Pao/Sam/Devia tracking, Mint's 17-fish quest, quest tracker, Hello starts quests, Fairy King fix (all in, untested)
9. Expanded zip 4, towns: party menu switch sequence, allies fishing, ally ladder climbs, full town animation sets, overhead Georama camera and fast run (all in, untested)
10. Last, if at all (lowest priority): the map changes. New fishing spots, Queens canal tide, Yellow Drops west bank, Queens snake statue, Brownboo cleanup, Brownboo pond rocks, town camera collision

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
| Fruit of Eden at the Queens fruit stand for 6000 | Expanded | Done |  |
| Gaffer's shop drops the Endurance attachment | Enhanced | Done | Shop fix zip |
| Fairy King sells Metal Breaker and Mimic Breaker | Code only | Done | Shop fix zip |
| Daily rotating item slot in shops | Enhanced | Done, partial | Untested. Gaffer, Wise Owl, Jack, Joker, Brooke, Ledan and the Fairy King sell one extra item from Enhanced's 5-day rotation. Picked from the in-game day, so no save space. Left out: the Fairy King's Crystal Eyeball (its quest flag is unknown) |
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
| Confuse, a new weapon ability | Expanded | Done, partial | Untested. Carried by Babel's Spear. 1 hit in 20 confuses for 20 s, never a boss. A confused enemy chases the nearest enemy or you, and its melee hurts enemies; an enemy it hits fights back. Left out: the stars over confused enemies, its own icon (shows Stop's), friendly fire from enemy projectiles. Friendly-fire kills may still give ABS |
| Empty SynthSphere raises a weapon to +5 while socketed | Code only | Done | Untested. First hole only. Taking it out while the weapon is still +5 undoes it; a level-up makes the levels permanent |
| Faster Ungaga item throw, shorter Fog Gate cutscene | Enhanced | To do | Waiting on logs. Throw speed: Enhanced changes a "22" to "44" in Ungaga's dungeon file (c10b.chr, info.cfg); the logging build saves that file to logs/. Fog Gate: the dark-fog door on Divine Beast Cave floor 9 runs a dungeon system script; logs/events.txt records its number and length, logs/d02_event.stb is the script file |
| Broken Dagger attachment glitch fixed | Enhanced | Done | Shop work buffer now cleared; Broken Dagger fix zip |
| Out of Bounds menu glitch fixed | Enhanced | Skip | Expanded has no code for it; the shop buffer fix is its only shop glitch fix |
| Flapping Duster can spawn in Gallery of Time | Enhanced | Check | Gallery's chest lists hold it 3 times at normal odds; confirm in a playthrough |
| Saving after the credits works | Enhanced | In Chronicle | Chronicle rewrote the save after the ending |

## Dungeon systems

None of these are started; the chest randomizer and minibosses are the biggest.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| Chest randomizer by rarity and dungeon depth | Code only | Done | Untested. Every generated chest is rerolled from Enhanced's lists: small chests get the dungeon's item list, large ones a weapon from its table plus a random trap. About 12% large in the first dungeon, 1.5 points fewer per dungeon, twice as many with Chronicle 2. Powerup Powder is rerolled 4 times in 5. The Map and Magical Crystal chests stay unless you carry one in your bag, which then lights the floor's. The rumour quests' items turn up in a back floor's first small chest (1 in 3, Demon Shaft 1 in 7) until you have one |
| Back-floor chest loot table, weapons on back floors | Enhanced | Done | Untested. Done with the chest randomizer: back-floor small chests use Enhanced's shared back-floor list (gems, attachments, amulets, fruit), large ones the dungeon's weapons |
| Clown randomizer | Code only | Done | Untested. Each box holds one item: half the time from the dungeon's clown weapons, else the common lists (the small one's last entry is the dungeon's back-floor key item). Chronicle 2 makes it always roll the weapon table. Back-floor clowns offer the same as the main floor's |
| Repair and Escape Powders as active items | Enhanced | Done | Untested. Square on an Escape Powder asks first; a second press 0.5 to 3 s later leaves. Repair Powder fills WHP to max with the Auto Repair message and sound, and isn't used if WHP is full |
| Repair, Escape and Revival Powders stack in active slots | Enhanced | Done | Untested. Escape, Revival, Repair and Auto Repair Powder stack in quick-use slots, as in Enhanced |
| Leave the dungeon from the floor select screen | Enhanced | Done | Untested. Circle on the floor select closes it and starts the game's own escape to town (the Escape Powder's). It costs nothing |
| Floor cleared message | Enhanced | Done, partial | Untested. "There is no sign of monsters on this floor" once the last one dies, once per floor. Expanded's line also mentions an escape skill; left out since Chronicle has none |
| Minibosses: a stronger enemy on floor entry | Enhanced | Done, partial | Untested. Replaced in zip 2 by Expanded's minibosses (the two rows below): the roll, size, stats and loot are now Expanded's. Left out: Enhanced's stamina timer change |
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

66 of the 69 effects are done and waiting on your test; the Mardan swords are under Fishing.

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
| Toan | Shamshir: Swift Strikes | Done | Untested. Super Steve version not ported (Expanded's faster draw and shot for Xiao) |
| Toan | Small Sword: Quick Draw | Done | Untested. Super Steve version not ported (Expanded's instant fire-on-release for Xiao) |
| Toan | Buster Sword: Buster Boost | Done | Untested. No Super Steve version, as in Expanded |
| Toan | Kitchen Knife: Spring's Blessing | Done | Untested. No Super Steve version, as in Expanded |
| Toan | Sax: Fine Fare | Done | Untested. Super Steve version built |
| Toan | Wise Owl Sword: Wise Owl Always Knows | Done | Untested. No Super Steve version, as in Expanded |
| Toan | Dusack: No Fool's Gold | Done | Untested. Super Steve version built |
| Toan | Tsukikage: Moonlit Focus | Done | Untested. Super Steve version not ported (Expanded's 2x shot speed for Xiao) |
| Toan | Evilcise: Jealous Soul | Done | Untested. Super Steve version built: curses Xiao |
| Toan | Macho Sword: Overtraining | Done | Untested. No Super Steve version, as in Expanded |
| Toan | Sun Sword: Solar Harvest | Done | Untested. Super Steve version built |
| Toan | Sun Sword: Solar Flash | Done, partial | Untested. Core gameplay: blinding uses the game's Stop status. Not ported: Expanded's room dimming, Toan's glow, blade tint, and enemies cowering in guard |
| Toan | Claymore: Greatsword | Done | Untested. Super Steve version built: every pellet throws its enemy (the big blade stays the sword's) |
| Toan | Maneater: Blood Price | Done | Untested. Super Steve version built: curses Xiao |
| Toan | Aga's Sword: Defensive Legacy | Done | Untested. Super Steve version built |
| Toan | Brave Ark: Hero's Courage | Done | Untested. Super Steve version built |
| Toan | 7 Branch Sword: Sevenfold Rite | Done, partial | Untested. Status Break greys out below +7 instead of cancelling mid-animation |
| Toan | Heaven's Cloud: Typhoon | Done | Untested. Blade up to 3x over a 4 s hold; the whirlwind is drawn and hits as wide |
| Toan | Cross Hinder: Sanctifier | Done | Untested. Super Steve version built: pellet damage, ABS and reach |
| Toan | Atlamillia Sword: Atlamillia Insurance | Done, partial | Untested. Sphere in a small chest on a random floor with atla (you chose chest over atla). Lost if the game closes before you take it |
| Toan | Dark Cloud: Guard Crush | Done | Untested. Super Steve version built |
| Toan | Big Bang: Detonate | Done, partial | Untested. Whirlwind blast and locked-on blast with the flash, 20 WHP each. Not ported: the falling judgement blade, explosion animation, explosion immunity |
| Toan | 7th Heaven: Divine Guard | Done | Untested |
| Toan | Sword of Zeus: Lightning | Done, partial | Untested. Bolts, combo chain, 6-enemy volley and charged lunge bolt, 10 WHP each. Not ported: the lightning model, thunderclap, room dimming, judgement blade |
| Toan | Chronicle Sword: hits all nearby targets | Done, partial | Untested. Splash never kills: leaves 1 HP and poisons. Expanded's colour flash on splashed enemies not ported |
| Toan | Chronicle 2: big chests and Powerup rolls | Done | Already matched (untested). Expanded's code doubles the big-chest rate (880 + 15 per dungeon becomes twice that minus 1000), keeps every Powerup Powder roll on main floors, and makes the clown always roll weapons, as the chest and clown randomizers already do. Its changelog's "every chest is a big chest" isn't what the code does. Owned-item menu description added |

### Xiao, Goro, Ruby, Ungaga and Osmond

| Character | Effect | Status | Notes |
| --- | --- | --- | --- |
| Xiao | Bone Slingshot: Skeleton Key and Gravedigger | Done | Tested working: both Skeleton Key and Gravedigger |
| Xiao | Steel Slingshot: Endurance Up | Done | Untested |
| Xiao | Bandit Slingshot: Steal Shot | Done, partial | Untested. Pellets fire the victim's own loaded shot at 2x; the notice doesn't name the enemy |
| Xiao | Flamingo: Lock-On Distance | Done | Untested. Dragon's Y, Divine Beast Title, Angel Shooter and Angel Gear get the reach too |
| Xiao | Flamingo: Bait Flamboyance | Done | Untested. +10 notice radius per Flamingo owned, up to 3 |
| Xiao | Hardshooter: Ricochet | Done | Untested. A ricochet never ricochets again |
| Xiao | Double Impact: two pellets | Done, partial | Untested. Both pellets ricochet. Pellets keep their own sprite (Expanded draws Steel Slingshot stones) |
| Xiao | Matador: Charging Bull | Done, partial | Untested. Charged pellet drawn 2x size; Expanded's flying slingshot model and glow not ported |
| Xiao | Super Steve: Sphere Inheritance | Done, partial | Untested. Super Steve carries the effect of the weapon its SynthSphere came from: every effect that works on kills, hits, guards, ailments, chests, curses, lock-on or Xiao's own shots, plus Pockle Call and Spirit Beast. Not ported: the sphere weapon's icon over Steve on the HUD, a slingshot sphere's pellet sprite, the Xiao shot versions of Toan's sword moves (Solar Shot, Detonate, Lightning, Heaven's Cloud's two-stage charge, Quick Draw, Swift Strikes, Moonlit Focus, the Halberd line's charged pellet), the Mobius Ring ramp, Mirage, and Ungaga's guard columns. Works differently: a sphere also carries a few effects Expanded's list leaves out, such as Supernova's ailments |
| Xiao | Dragon's Y: Dragon's Breath | Done | Untested. Element ball only where a floor monster fires it, else a 5x pellet (Expanded's own fallback). Lock-on speed done too |
| Xiao | Divine Beast Title: Spirit Beast | Done, partial | Untested. The charged shot flies as the town cat (gedit/s86/chara/c04cat.chr): grows in, falls, lands, walks to the locked-on or nearest enemy, crouches and leaps for 1.5x damage with a heavy kick, sits with nothing to chase, fades after 10 s. Not ported: the wings, mask, cape and glow, the float-up leap clip, a floor probe (it walks at Xiao's height), the pellet shrinking as the cat grows on it. Watch: the cat's facing, size and animation; the console says whether c04cat.chr loaded |
| Xiao | Angel Shooter: Guardian Grace | Done, partial | Untested. Heal ticks every second while guarding; Angel Gear gets it too. White pulses instead of Expanded's sparkle and smooth tint |
| Xiao | Angel Gear: Guardian Reflector | Done, partial | Untested. While Xiao guards, a 2x copy of the slingshot stands 12 units ahead, turned to the nearest incoming shot, else the nearest enemy. Shots reaching it or her are caught and fired back one every 0.25 s at the nearest enemy (the monster's own shot, weapon attack, its element and ailments). 5 enemy blows break it; back after 4 s. Not ported: enemies walking to it and swinging at it instead of her, shield HP on the attack gauge, its draw-and-shoot animation, fades, break sound. Watch: the copy stands on its long axis, so check it faces the right way |
| Xiao | Angel Gear: party heal | Done | Untested. Expanded's version replaces Enhanced's: with Xiao out, each turn of the Heal ability's clock (every 3 s, every second while guarding) mends the party 1 HP, knocked-out members excepted, Xiao only if the weapon's own Heal isn't live |
| Goro | Frozen Tuna: Cold Storage | Done | Untested. The freeze rolls once per step |
| Goro | Tall Hammer: shrinks enemies | Done | Untested |
| Goro | Inferno: power from missing HP and thirst | Done | Untested |
| Ruby | Pocklekul: Pockle Call | Done, partial | Untested. Guard 1 s: one of the 8 pockles at random joins the floor's monsters, grows in, goes for the locked-on or nearest enemy and walks beside Ruby otherwise. Its blows only hit enemies, the enemies it hits fight back, your hits pass through it, no lock-on, no ABS or drops. Not ported: the summon smoke and tint. Works differently: it joins the floor's own monster set (needs a free kind slot of 9 and monster slot of 16; the console says why when none comes), and summoned pockles don't throw shots. Watch: textures on the floor's other monsters after a summon |
| Ruby | Bandit's Ring: Steal Shot | Done | Untested. Quick shots only; the charged shot stays hers |
| Ruby | Mobius Ring: longer charge, more damage | Done | Untested. After a full charge, x1.5 every 1.5 s held; orbs up to 5x size and hit radius |
| Ruby | Secret Armlet: Favoured Circles | Done, partial | Untested. Rage circle slows enemies instead; stat gains capped at 999, elements and bonuses at 99 |
| Ungaga | Javelin: Marine Hunter | Done | Untested |
| Ungaga | Halberd: Tornado Charge Buff | Done | Untested. Scorpion, Mirage, Cactus, Hercules' Wrath, Terra Sword and Babel's Spear get stronger forms |
| Ungaga | DeSanga: Vampire | Done | Untested |
| Ungaga | Scorpion: Venom | Done | Untested |
| Ungaga | Scorpion: Venom Lure | Done | Untested. Poisonous Apple noticed from twice as far |
| Ungaga | Partisan: Quick Combo | Done | Untested |
| Ungaga | Mirage: Decoy | Done, partial | Untested. Guard 0.25 s: a still copy of Ungaga in the guard pose lures every enemy for 12 s (18 s with Hercules' Wrath); an enemy that's hurt turns back to him. New guard replaces it; ends on weapon or party change and new floor. Built on two shared pieces for the other summons: PoseCopy (a still model copy) and Lure (enemy target redirect). Not ported: heat shimmer, dissolve hand-off between copies |
| Ungaga | Cactus: Absorb | Done | Untested. Rock, metal and undead enemies give nothing |
| Ungaga | Cactus: Desert Bloom | Done, partial | Untested. Guard 0.25 s: a 3x copy of the held Cactus rises point-up 10 units ahead and stands 10 s, solid (radius 6) to enemies and Ungaga, pricking enemies within 2 units for 1/6 attack every 0.25 s with a short shove, no WHP. A new hold moves it. Not ported: summon smoke, squash-and-stretch overshoot, blocking enemy shots; it sinks away instead of fading |
| Ungaga | Hercules' Wrath: Air Strike | Done, partial | Untested. Guard 5.25 s primes while the decoy stands; the next swing blasts on the decoy (spear tip if none), 2x reach, 20 WHP, and ends the decoy. This is Expanded's version. Not ported: gold spear, sparkle, dimming |
| Ungaga | Terra Sword: Big Rock | Done, partial | Untested. Guard 5 s primes (green flash); then lock on and Master Utan's boulder (loaded from gedit/s96/chara/iwa.chr, texture in block 0x30) drops from 500 units, growing as it falls and following the target: 4x attack to enemies within 26, ~2x Big Bang's shove, through guards, 10 WHP. It rests 20 s as a solid column (radius 21) hitting enemies under it for 1x attack each second, then sinks. Not ported: green spear tint, shockwave, shadow, target dimming. Watch: if the rock never drops, the log says whether iwa.chr loaded |
| Ungaga | Babel's Spear: Curse of Babel | Done, partial | Untested. Expanded's version replaces Enhanced's stop. Guard 5 s: a 5x copy of the spear rises point-up under the locked target (else the nearest within 60, else 15 ahead), following it through the 0.5 s rise; the tip strikes for 1x attack with the Baselard's throw through guards. It stands 20 s spinning at 240°/s, solid (radius 8), spikes hitting within 2 units for 1/6 attack every 0.25 s, then sinks over 3.7 s. Every regular enemy on the floor is confused until it's gone. Not ported: the Dark Genie beam, the light-blue weapon tint, blocking enemy shots |
| Osmond | Blessing Gun: Blessed Bait | Done | Untested |
| Osmond | Snail: Slime Trail | Done | Untested. 5% per hit |
| Osmond | Skunk: Longer Flame | Done | Untested |
| Osmond | Star Breaker: Shooting Stars | Done, partial | Untested. 2% per kill; drops the sphere at the kill instead of straight into the bag |
| Osmond | Supernova: random status on hit | Done | Untested. 11% per hit: stop, poison, anger or goo |

## Fishing

Zip 3 is in, untested; what's left is the map work (new spots, the Queens tide, the Yellow Drops bank, the Brownboo rocks).

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| New fishing spots: Queens, Brownboo, Yellow Drops | Expanded | To do | Hard. Goes with it: Expanded's 6 fish per spot (5 at the Oasis), and its pool tweaks at Matataki Waterfall (Gummy becomes Niler) and East Harbor (Piccoly becomes Gobbler) |
| Queens canal tide, ladder and drained canal floor | Expanded | To do | Hard |
| Yellow Drops west bank reshaped | Expanded | To do | Hard |
| Casting and line fixes, camera centered on the bobber | Expanded | Done, partial | Untested. The camera watches the float, not the point between you and the float, and goes back to its distance and turn after a catch instead of staying close in. A float above the water only spoils the cast once it stops moving, so long casts still in the air land. Fish catch walls on their +Z side as on the others. Left out (they go with the new fishing spots): the line split above and below the float, the per-spot camera heights, casts stopping at the Queens canal walls |
| Brownboo pond rocks have collision | Expanded | To do |  |
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
| Play as any ally in town, swapped in place | Both | Done, partial | Untested. Town party menu: pick any ally in the party and they take over once you leave the menu (pick Toan to switch back). Uses Enhanced's models (the allies' cutscene models) and its Xiao and Ruby motion fixes; Xiao's first-person camera sits lower. Toan comes back for every cutscene and on every new map. Event points are off for allies in maps 11, 13, 14, 33, 35 and 37, as in Enhanced. Left out: Xiao's lower talk camera, the stuck-landing fix |
| Party menu commit plays the dungeon's switch sequence | Expanded | Done | Untested. Picking a different character in the town party menu: the confirm chime, the portraits spread off-screen, the menu closes itself and the ally takes over. Picking the one already walking closes the page as before; an ally not in the party yet gets the reject beep |
| Full town animation sets for all five allies | Expanded | Done | Untested. Expanded's town models, rebuilt the same way from your game data the first time each ally is picked: every clip it copies between each ally's models (run, doors, item get, refusal, fall, land and the ladder clips), its held, moved, reversed and frozen windows, Ruby's added shadow and Osmond's helicopter backpack with its spin. Checked against your own files: all five build, with every joint matched but a few per clip. Ungaga now walks in Expanded's model (the one with his cloth). Xiao sits down after standing still 1.5 s. Works differently: built in memory instead of patched into the disc image. If a build fails, logs/town.txt says why and that ally walks as before |
| Each ally climbs ladders their own way | Expanded | Done, partial | Untested. Cross at a ladder: Xiao walks up, backs off, crouches and leaps over (down: hops off), landing on her own; Goro climbs in four hops and jumps down after a gathering crouch; Ruby floats up or down; Osmond flies up on his helicopter backpack and dives down; Ungaga refuses at both ends. Toan's ladder camera during the move. Works differently: played by the port's own step player instead of Expanded's generated town scripts, so the camera's hand-back is close but may not match exactly. Console logs "[Ladder]" for each move |
| Allies can fish | Expanded | Done | Untested. The fishing session runs as Toan (it loads Toan's fishing model, as in Expanded), and the ally comes back once you're walking again. town.txt in logs/ notes each return |
| Town camera collision overhaul | Expanded | To do | Hard |
| Queens snake statue is climbable | Expanded | To do |  |
| Brownboo cleanup: edit view, see-through houses | Expanded | To do |  |
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
| Samba's dagger-only challenge on Moon Sea 7 | Enhanced | Done, partial | Untested. Opens once all 6 have joined. Moon Sea 7's back floor with Toan and the Dagger only; a throwable or a swap cancels it. Reward: the Map. Also in: Yellow Drops' crystal puzzle, reward the Magical Crystal. Left out: switching off that floor's fountains |
| Mayor's clear-the-floor quest in Demon Shaft | Enhanced | Done, partial | Untested. "Any quests for me?" opens once Pickle sees the full collection, raising the Fruit of Eden cap to 250 and Gourds to 12 drops. Clear a random Demon Shaft back floor with one named ally, no throwables or swaps; a boost item each time until every stat is maxed. Not Chronicle's Mayor supplies quest. Left out: his lines before the quests open are approximations, since Enhanced's flags for "Dark Genie beaten" and "mid-journey" are unknown |
| Repeatable fishing quests | Enhanced | Done | Untested. Pike, Pao, Sam and Devia: catch some of one fish, or one fish in a size range, in their town. Reward: Fishing Points. Sam's 3-quest bonus added in zip 3. Now started from Talk (see Quest talk menus) |
| Pickle tracks your % completion | Enhanced | Done | Untested. Brownboo ("My collection"): counts items, attachments, ultimate weapons and secret items carried or stored. 100% gives the Flame Key and opens the Mayor's bonus quests |
| Mint's catch-all-17-fish quest | Code only | Done, partial | Untested. Brownboo villager 5: Talk gives her challenge, then the fish still missing. Reward: the Saving Book, again if you lose it. Works differently: a kind also counts if it's in your fishing records, so catches from before this zip count (Expanded counts only catches made with the mod on). Shows in the quest tracker |
| New town dialogue for every ally | Enhanced | Done, partial | Untested. All of Enhanced's villager lines for the five allies in all 8 towns (two per villager, by turns). A villager with quest choices keeps them, with the ally's line behind Talk. Left out: Toan's name swapped for the ally's in the game's own lines and shop lines, the heart symbol |
| Fairy King dialogue fix | Code only | Done, partial | Untested. His line about calling allies now says you can call them everywhere in the world. Works differently: Expanded rewrites it by memory address; here it's found by its words (a first page about allies ending "however..."). Console logs "[FairyKing]" when it changes the line, or when it skips a line about calling allies |

## Options, audio, cheats and dev tools

Most are small; the dev tools are skipped since they were only for building the mod.

| Feature | From | Status | Notes |
| --- | --- | --- | --- |
| FOV and widescreen | Code only | In Chronicle | Chronicle's aspect setting |
| Graphics improvements toggle | Enhanced | Skip | PCSX2 rendering patches; Chronicle has its own renderer and a soft focus option |
| Weapon break beep on or off | Enhanced | Done | Untested. Options, Game page: Weapon Beep |
| Battle music or all music on or off | Enhanced | Done | Untested. Options, Audio page: Music and Battle Music |
| Attack voices on or off | Enhanced | Done, partial | Untested. Options, Audio page: Voices. Mutes every character voice, not just attacks |
| Hold L1 L2 R1 R2 Select Start for 3 s to reset to the title | Enhanced | Done | Untested. Works in towns and dungeons |
| Cheat codes (10 inputs, paused in a dungeon) | Enhanced | Skip for now | Skipped for now; counts as finished |
| Dungeon debug menu (R3) and inventory debug menu (L3) | Enhanced | In Chronicle | Retail's debug menus; turn on game.debug_mode or use the debug chord |
| Sandbox tab: spawn roster editor, Fish Data Farmer | Expanded | Skip | Tools for building the mod |
| Dev mode tabs, code cave scanner, TAS, test weapon grant | Code only | Skip | Tools for building the mod |
| Options saved inside the save file | Expanded | Skip | Chronicle keeps options in config.json |
