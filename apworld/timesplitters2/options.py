from dataclasses import dataclass

from Options import Range, Choice, DeathLink, PerGameCommonOptions, StartInventoryPool, Toggle

from . import data


class GameMode(Choice):
    """Which parts of the game are randomized.

    both        - Story missions AND Arcade + Challenge events all produce checks (the full game). Goal: clear Space
                  Station on your Max Story Difficulty, once you have enough Time Crystals.
    story_only  - ONLY the story missions and their objectives produce checks; Arcade and Challenge are left empty.
                  Goal: clear Space Station on your Max Story Difficulty, once you have enough Time Crystals.
    arcade_only - ONLY Arcade matches and Challenges produce checks; the story is left empty. There is no Space Station
                  to clear, so the goal is instead to complete Arcade Goal Percentage of those checks. Time Crystals
                  are not used, and Weapon Gating / Weapon Shuffle are forced OFF (they only affect story missions).

    Time Crystals are ITEMS sent to you from the multiworld -- other players' checks find them, and they arrive like any
    other item. Space Station stays locked until you have collected the number set by Time Crystals Required to Finish,
    so the final mission is gated on the multiworld rather than on your own progress."""
    display_name = "Game Mode"
    option_both = 0
    option_story_only = 1
    option_arcade_only = 2
    default = 0   # both


class ArcadeGoalPercentage(Range):
    """ARCADE-ONLY goal: the percentage of the Arcade + Challenge checks you must complete to win. Ignored unless
    Game Mode is arcade_only (the story modes finish on Space Station instead)."""
    range_start = 10
    range_end = 100
    display_name = "Arcade Goal Percentage"
    default = 90


class StoryDifficulty(Choice):
    """The MAXIMUM story difficulty you intend to play. Each story mission and its objectives produce checks for every
    difficulty up to and including this (so Normal = Easy + Normal checks; objectives that only exist above it become
    filler-only). The GOAL triggers when you clear Space Station on THIS difficulty. (Story modes only.)"""
    display_name = "Max Story Difficulty"
    option_easy = 0
    option_normal = 1
    option_hard = 2
    default = 1   # Normal


class TrophyTier(Choice):
    """The MAXIMUM trophy tier you intend to earn on Arcade matches + Challenges. Each event produces checks for every
    tier up to and including this (so Gold = Bronze + Silver + Gold checks; a single Gold clear fires all three, since
    trophies are cumulative in the save)."""
    display_name = "Max Trophy Tier"
    option_bronze = 0
    option_silver = 1
    option_gold = 2
    option_platinum = 3
    default = 2   # Gold


class TimeCrystalsRequired(Range):
    """How many Time Crystals you must collect before the final mission (Space Station) unlocks.

    Time Crystals are ITEMS sent to you from the multiworld -- they are scattered among all players' checks, so you
    receive them as other people play. Until this many have arrived, Space Station stays locked. Auto-clamped down if
    the seed has too few free item slots. Ignored in arcade_only (no Space Station to unlock)."""
    display_name = "Time Crystals Required to Finish"
    range_start = 0
    range_end = 60
    default = 8


class TimeCrystalsExtra(Range):
    """How many EXTRA Time Crystals are placed beyond the required count (padding, so collecting them isn't all
    mandatory). Total placed = Required + Extra, auto-clamped to the free item slots -- so 'required' can never exceed
    what's actually placed."""
    display_name = "Extra Time Crystals"
    range_start = 0
    range_end = 30
    default = 4


class StartingUnlocksPerCategory(Range):
    """Starting unlock items granted at the top of each active category (story / arcade / challenge) so the seed
    is enterable from the start."""
    display_name = "Starting Unlocks Per Category"
    range_start = 1
    range_end = 3
    default = 1


class BonusHealthPacks(Range):
    """One-time Health Packs placed in the pool. Receiving one fully restores your health -- immediately if you're in
    a mission/challenge, otherwise on your next mission entry. Replaces junk filler (Banana)."""
    display_name = "Health Packs"
    range_start = 0
    range_end = 30
    default = 3


class BonusArmorPacks(Range):
    """One-time Armor Packs. Receiving one fully restores your armor (immediately in a mission, else on next entry).
    Replaces junk filler."""
    display_name = "Armor Packs"
    range_start = 0
    range_end = 30
    default = 3


class BonusAmmoPacks(Range):
    """One-time Ammo Packs. Receiving one fully refills your ammo (immediately in a mission, else on next entry).
    Replaces junk filler."""
    display_name = "Ammo Packs"
    range_start = 0
    range_end = 30
    default = 3


class TrapCount(Range):
    """How many Trap items to place. Each trap, when received during a STORY mission, switches on a disruptive TS2
    cheat (invisible enemies, big heads, rotating heads, ...) for a few seconds. Drawn randomly from the named trap
    set. Replaces junk filler; auto-trimmed if there aren't enough free filler slots."""
    display_name = "Trap Count"
    range_start = 0
    range_end = 60
    default = 8


class WeaponGating(Toggle):
    """Weapons as items (A): you can only pick up / use weapons you've received as AP items. Un-received weapons can't be
    acquired -- floor pickups give nothing, and your mission starting loadout is stripped to what you've received. Story-
    required weapons (Timed Mine, TNT) and tools (Fire Extinguisher, ElectroTool, Camera, Temporal Uplink) stay
    always-available, so missions remain completable.

    STORY MISSIONS ONLY -- Arcade and Challenge always give their normal fixed loadouts. Forced OFF when Game Mode is
    arcade_only (there would be nothing for it to affect)."""
    display_name = "Weapon Gating"


class WeaponShuffle(Toggle):
    """Weapon randomizer (B): one global mapping per seed swaps what each weapon is in-game (e.g. every pistol becomes a
    brick, every sniper a rocket launcher). Applies to floor pickups, your starting loadout, AND enemy weapons (an enemy
    that would carry a pistol instead carries whatever pistols map to). The story-required / tool whitelist is never
    shuffled.

    STORY MISSIONS ONLY -- Arcade and Challenge keep their normal weapons. Forced OFF when Game Mode is
    arcade_only (there would be nothing for it to affect)."""
    display_name = "Weapon Shuffle"


class WeaponShuffleScope(Choice):
    """How far Weapon Shuffle is allowed to reach. Ignored unless Weapon Shuffle is on.

    completely_random - any shuffleable gun can become any other (mines still swap only with mines so a placeable
                        explosive stays placeable). The wildest option: a level can end up holding weapons that are
                        native to some other level.
    same_class        - a gun only becomes one of its own class: regulars with regulars, launchers with launchers,
                        mines with mines. The Flamethrower is the only member of its class, so it never changes.
    within_level      - each level reshuffles only its OWN pickups, so every level still offers exactly the weapons it
                        always did -- just from different pickups. The most conservative option, and it can never
                        strand a mission. (The same pickup slot can be a different gun in a different level.)"""
    display_name = "Weapon Shuffle Scope"
    option_completely_random = 0
    option_same_class = 1
    option_within_level = 2
    default = 0


class ArcadeDeathLinkThreshold(Range):
    """DeathLink, ARCADE + CHALLENGE only: how many deaths there before one is sent out.

    Arcade matches are deathmatches -- you die constantly -- so sending every one would flood the multiworld. Deaths
    accumulate across matches and challenges for the whole session; each time the count reaches this number, one
    DeathLink is sent and the count resets. Deaths you RECEIVE never count toward it.

    Story missions are unaffected: every story death sends immediately. Incoming deaths always apply everywhere,
    Arcade and Challenge included. 1 = send every arcade/challenge death. 0 = never send from Arcade or Challenge
    (you still receive). Ignored unless DeathLink is on."""
    display_name = "Arcade DeathLink Threshold"
    range_start = 0
    range_end = 50
    default = 10


@dataclass
class TS2Options(PerGameCommonOptions):
    game_mode: GameMode
    arcade_goal_percentage: ArcadeGoalPercentage
    story_difficulty: StoryDifficulty
    trophy_tier: TrophyTier
    time_crystals_required: TimeCrystalsRequired
    time_crystals_extra: TimeCrystalsExtra
    starting_unlocks_per_category: StartingUnlocksPerCategory
    bonus_health_packs: BonusHealthPacks
    bonus_armor_packs: BonusArmorPacks
    bonus_ammo_packs: BonusAmmoPacks
    trap_count: TrapCount
    weapon_gating: WeaponGating
    weapon_shuffle: WeaponShuffle
    weapon_shuffle_scope: WeaponShuffleScope
    death_link: DeathLink
    arcade_death_link_threshold: ArcadeDeathLinkThreshold
    start_inventory_from_pool: StartInventoryPool
