"""TimeSplitters 2 — Archipelago world (v2: tiered).

Played via the TS2 Redux mod + the AP client DLL. Locations = completing a mission on a difficulty / earning a
trophy tier on an arcade match or challenge; items = unlocking the mission/event. The client DLL enforces locks
(menu entry +0x44 bit 0 for list content; difficulty-list lock keyed on the selected-mission index for the
story carousel) and detects completions/trophies by polling the profile blob (per-mission story difficulty
ordinal at 0xfc0 + level_id*8; the 66-event trophy table at 0x11cc, field i3 = tier).
"""

import logging
from typing import Any, Dict, List, Tuple

from BaseClasses import Item, ItemClassification, Location, LocationProgressType, Region
from Options import OptionError
from worlds.AutoWorld import World, WebWorld

from . import data
from .options import TS2Options

# Game Modes (options.GameModes): TS2's three top-level modes, in canonical order. Held as an ordered tuple rather than
# a set -- what survives decides the pool, and set iteration order is not stable between processes.
MODE_NAMES = ("Story", "Arcade", "Challenge")
# Pre-1.0.2 seeds carried a single game_mode int instead. Kept so a UT regen of an old seed still reads correctly, and
# so fill_slot_data can echo the int back for older clients when the mode set happens to match one of them exactly.
LEGACY_BOTH, LEGACY_STORY_ONLY, LEGACY_ARCADE_ONLY = 0, 1, 2
LEGACY_MODE_SETS = {LEGACY_BOTH: frozenset(MODE_NAMES),
                    LEGACY_STORY_ONLY: frozenset({"Story"}),
                    LEGACY_ARCADE_ONLY: frozenset({"Arcade", "Challenge"})}
# Weapon Shuffle Scope (options.WeaponShuffleScope). within_level is the only one that needs a PER-LEVEL map -- the
# client re-applies the weapon tables on every level load, so the same slot can be a different gun per level.
SCOPE_COMPLETELY_RANDOM, SCOPE_SAME_CLASS, SCOPE_WITHIN_LEVEL = 0, 1, 2
# slot_data schema version. Bump whenever the KEYS the client reads change, so a mismatched client can say so
# instead of silently mis-reading a seed. 10 = game_mode (int) replaced by game_modes / story_content /
# goal_trophy_events / trophy_goal_*; the client still reads the old int so pre-1.0.2 seeds keep working. 9 = added
# arcade_death_link_threshold. 8 = added starting_units / starting_weapons (a Universal Tracker regen must restore the
# seed's starters rather than re-roll them). 7 = added game_mode / arcade_goal_* / weapon_shuffle_scope /
# weapon_remap_by_level (6 = the pre-1.0.0 schema, which used the old content_mode key).
SLOT_DATA_VERSION = 10

# Smallest Time Crystal gate the density clamp is allowed to leave behind. The clamp scales the gate down to what a
# seed's free locations can hold, and on the tightest Story layout (Story-only, Easy, gating on) that bottoms out here.
# Below this it stops feeling like a gate at all, so make the floor explicit rather than leave it emergent. It limits
# only the CLAMP: a player who deliberately asks for fewer crystals -- 0 included -- still gets exactly what they asked.
TC_REQUIRED_FLOOR = 7


class TS2Item(Item):
    game = "TimeSplitters 2"


class TS2Location(Location):
    game = "TimeSplitters 2"


class TS2Web(WebWorld):
    theme = "dirt"
    game_info_languages = ["en"]


class TS2World(World):
    """TimeSplitters 2 (Homefront port / TS2 Redux)."""
    game = "TimeSplitters 2"
    options_dataclass = TS2Options
    options: TS2Options
    web = TS2Web()

    item_name_to_id = data.item_name_to_id
    location_name_to_id = data.location_name_to_id
    item_name_groups = data.item_name_groups

    # ---- Universal Tracker support ----
    # UT re-generates the world to rebuild the logic graph. Without this hook it would generate with DEFAULT options
    # (gating off, Normal difficulty, no shuffle) and compute the WRONG reachability -- the tracker's frontier dead-ends.
    # interpret_slot_data returns the server slot_data, which makes UT re-run generation with it in
    # multiworld.re_gen_passthrough; generate_early reads that and rebuilds every logic-affecting value from the seed.
    ut_can_gen_without_yaml = True

    def _ut_passthrough(self) -> dict:
        return getattr(self.multiworld, "re_gen_passthrough", {}).get("TimeSplitters 2", {})

    def interpret_slot_data(self, slot_data: dict) -> dict:
        return slot_data

    def _set_modes(self, keep) -> None:
        """Record the modes this seed keeps, in canonical order, plus the three flags the rest of the world reads."""
        keep = set(keep or MODE_NAMES)
        self.modes = tuple(m for m in MODE_NAMES if m in keep)
        self.story = "Story" in keep
        self.arcade = "Arcade" in keep
        self.challenge = "Challenge" in keep

    def generate_early(self) -> None:
        pt = self._ut_passthrough()
        if pt:   # UT regen: options are defaults here, so rebuild every logic-affecting value from the seed's slot_data
            self._ut = True
            self.diffs = frozenset(pt["story_difficulty_checks"])
            self.tiers = frozenset(pt["trophy_tier_checks"])
            self.goal_difficulty = pt["goal_difficulty"]
            self.gating = bool(pt["weapon_gating"])
            self.shuffle = bool(pt["weapon_shuffle"])
            self.weapon_remap = {int(k): v for k, v in pt.get("weapon_remap", {}).items()}
            self.scope = pt.get("weapon_shuffle_scope", SCOPE_COMPLETELY_RANDOM)
            # within_level: rebuild {mission: {slot: weapon}} from the index-keyed form sent in slot_data
            self.weapon_remap_by_level = {data.STORY[int(mi)]: {int(k): v for k, v in mp.items()}
                                          for mi, mp in pt.get("weapon_remap_by_level", {}).items()
                                          if int(mi) < len(data.STORY)}
            self.tc_required = pt["time_crystals_required"]
            self.tc_total = pt["time_crystals_total"]
            self._set_modes(pt.get("game_modes") or LEGACY_MODE_SETS.get(pt.get("game_mode", LEGACY_BOTH)))
            self.trophy_goal_pct = pt.get("trophy_goal_percentage", pt.get("arcade_goal_percentage", 90))
            return
        self._ut = False
        keep = set(self.options.game_modes.value)
        if not keep:
            raise OptionError(f"TimeSplitters 2 (player {self.player}): Game Modes is empty. Keep at least one of "
                              f"{', '.join(MODE_NAMES)} -- with none of them there is nothing to randomize.")
        self._set_modes(keep)
        self.trophy_goal_pct = self.options.trophy_goal_percentage.value
        self.gating = bool(self.options.weapon_gating.value)
        self.shuffle = bool(self.options.weapon_shuffle.value)
        if not self.story:
            # Weapon Gating / Shuffle only ever affect STORY missions (the client applies them behind g_inStory), so
            # without Story they would be silent no-ops that still bloat the item pool. Force off.
            if self.gating or self.shuffle:
                logging.info("TimeSplitters 2 (player %d): Story not in Game Modes -- Weapon Gating/Shuffle forced off "
                             "(they only affect story missions).", self.player)
            self.gating = False
            self.shuffle = False
            # Write the forced values back onto the OPTIONS too, so everything that reads them later agrees: the spoiler
            # log's option echo, slot_data, and any tooling. Otherwise a spoiler could claim "Weapon Gating: Yes" for a
            # seed where it was actually off, which is confusing to read back weeks later.
            self.options.weapon_gating.value = 0
            self.options.weapon_shuffle.value = 0
        # Max story difficulty / trophy tier -> the CUMULATIVE set of tiers that produce checks (Normal => Easy+Normal;
        # Gold => Bronze+Silver+Gold). Easy/Bronze are always included (any max >= them), so a goal/story check exists.
        di = self.options.story_difficulty.value                       # 0=Easy 1=Normal 2=Hard (the MAX)
        self.diffs = frozenset(data.STORY_DIFFICULTIES[:di + 1])
        self.goal_difficulty = data.STORY_DIFFICULTIES[di]             # the goal triggers on clearing Space Station HERE
        ti = self.options.trophy_tier.value                             # 0=Bronze .. 3=Platinum (the MAX)
        self.tiers = frozenset(data.TROPHY_TIERS[:ti + 1])
        # global weapon remap (capability B). A cross-category shuffle can strand a mission (e.g. leave Wild West with no
        # Regular weapon for its REG-only objective), so re-roll until every story mission stays completable with its
        # full shuffled weapon set on every checked difficulty; fall back to identity (no shuffle) if none is found.
        self.weapon_remap = {}                 # global {slot: weapon} (completely_random / same_class)
        self.weapon_remap_by_level = {}        # {mission: {slot: weapon}} (within_level) -- takes precedence when set
        self.scope = self.options.weapon_shuffle_scope.value    # 0=completely_random 1=same_class 2=within_level
        if self.shuffle:
            if self.scope == SCOPE_WITHIN_LEVEL:
                # A within-level permutation keeps each level's weapon SET identical, so coverage is preserved by
                # construction -- but it can still move a needed gun from an early pickup to a late one, so the same
                # coverability check applies.
                for _ in range(200):
                    byl = data.weapon_remap_within_level(self.random)
                    if all(data.mission_coverable(m, byl.get(m, {}), d) for m in data.STORY for d in self.diffs):
                        self.weapon_remap_by_level = byl
                        break
                else:
                    logging.warning("TimeSplitters 2 (player %d): no completable within-level weapon shuffle found in "
                                    "200 tries -- using identity (no shuffle).", self.player)
            else:
                gen = data.weapon_remap_same_class if self.scope == SCOPE_SAME_CLASS else data.weapon_global_remap
                for _ in range(200):
                    remap = gen(self.random)
                    if all(data.mission_coverable(m, remap, d) for m in data.STORY for d in self.diffs):
                        self.weapon_remap = remap
                        break
                else:
                    logging.warning("TimeSplitters 2 (player %d): no completable weapon shuffle found in 200 tries -- "
                                    "using identity (no shuffle).", self.player)

    def _remap_for(self, mission: str) -> dict:
        """The slot->weapon map in force for <mission>. within_level gives each level its own; the other scopes share
        one global map. Empty when Weapon Shuffle is off (callers treat a missing slot as identity)."""
        if self.weapon_remap_by_level:
            return self.weapon_remap_by_level.get(mission, {})
        return self.weapon_remap

    # ---- active set derived from options: list of (location_name, unit_name) ----
    def _active_locations(self) -> List[Tuple[str, str]]:
        tiers = self.tiers
        diffs = self.diffs
        out: List[Tuple[str, str]] = []        # (AP location name, AP item name that unlocks it)
        for kind, _, name in data.TROPHY_EVENTS:              # Arcade matches and Challenges, each gated on its own
            if not self._kind_kept(kind):
                continue
            for t in data.TROPHY_TIERS:
                if t in tiers:
                    out.append((f"{data.DISPLAY_OF[name]} ({t})", data.ITEM_OF[name]))
        if self.story:                                        # story missions + their objectives
            for m in data.STORY:
                for d in data.STORY_DIFFICULTIES:
                    if d in diffs:
                        out.append((f"{data.DISPLAY_OF[m]} ({d})", data.ITEM_OF[m]))
            for m in data.STORY:                              # objectives are ALWAYS checks (density + the Time Crystal/weapon pool)
                for tid, name, prim, sec in data.OBJECTIVES.get(m, []):
                    if (prim | sec) & diffs:                  # objective appears on a difficulty up to the chosen max
                        out.append((data.objective_location_name(m, name), data.ITEM_OF[m]))
        return out

    def _kind_kept(self, kind: str) -> bool:
        """Is this trophy event's mode ("arcade" / "challenge") one the player kept?"""
        return self.arcade if kind == "arcade" else self.challenge

    def _trophy_event_items(self) -> List[str]:
        """The distinct unlock items behind the trophy checks this seed KEEPS (one per event, in catalog order).
        Follows Game Modes, so a Challenge-only seed counts its 21 challenges and nothing else."""
        seen, out = set(), []
        for kind, _, name in data.TROPHY_EVENTS:
            if not self._kind_kept(kind):
                continue
            it = data.ITEM_OF[name]
            if it not in seen:
                seen.add(it)
                out.append(it)
        return out

    def _goal_trophy_eis(self) -> List[int]:
        """Event indices whose trophies count toward the goal -- the client counts only these, so an Arcade trophy
        can never advance a Challenge-only goal."""
        return [i for i, (kind, _, _) in enumerate(data.TROPHY_EVENTS) if self._kind_kept(kind)]

    def _excluded(self, loc_name: str, unit: str) -> bool:
        # filler-only (no progression) locations: Space Station's terminal checks (behind the goal+Time Crystal gate),
        # and objectives that are only OPTIONAL (secondary, never primary) on the selected difficulties.
        if unit == data.FINAL_STORY_ITEM:
            return True
        info = data.OBJ_LOC_INFO.get(loc_name)
        if info is not None:
            return not (info[1] & self.diffs)   # not primary on a difficulty up to the chosen max
        return False

    def _active_units(self, active_locs) -> List[str]:
        seen, ordered = set(), []
        for unit in data.ITEM_NAMES:           # stable order; unit == AP item name
            if any(u == unit for _, u in active_locs) and unit not in seen:
                seen.add(unit)
                ordered.append(unit)
        return ordered

    # ---- items ----
    tc_total = 0       # Time Crystals actually placed (set in create_items, after option clamping)
    tc_required = 0    # Time Crystals needed for the final stage (clamped to tc_total)
    _ut = False        # True under Universal Tracker regen (state restored from slot_data, not recomputed)

    def create_item(self, name: str) -> TS2Item:
        # filler (Banana) is junk. Weapons-as-items GATE the per-level objective logic when Weapon Gating is on, so
        # they're progression then (the fill must place them reachably); without gating they're cosmetic -> useful.
        # Everything else (unit unlocks + the Time Crystal) is progression.
        if name == data.FILLER_ITEM or name in data.BONUS_ITEMS:
            classification = ItemClassification.filler         # Banana + one-time bonus packs: non-progression
        elif name in data.TRAP_CHEATS:
            classification = ItemClassification.trap           # cheat traps (client fires them in Story missions)
        elif name in data.WEAPON_ITEM_NAMES:
            classification = (ItemClassification.progression if self.gating
                              else ItemClassification.useful)
        else:
            classification = ItemClassification.progression
        return TS2Item(name, classification, self.item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        return data.FILLER_ITEM

    def create_items(self) -> None:
        active_locs = self._active_locations()
        units = self._active_units(active_locs)

        # starter unlocks: RANDOMLY pick n units per active category (story / arcade / challenge) and grant them as
        # starting items, so sphere 0 is non-empty and WHICH units you start with varies per seed. The final story
        # (Space Station) is excluded from the story starters -- it sits behind the goal gate, so starting with it
        # unlocked would just waste a starter.
        # A UT regen must reproduce the SEED's starters, never re-roll them. Two things differ on that path: the options
        # are defaults (so `n` is wrong), and generate_early returns before the weapon-shuffle re-roll, so self.random is
        # at a different position and sample() picks different units. Either one makes the tracker compute logic for a
        # game nobody is playing -- e.g. crediting you with a mission's starting weapon you were never given.
        pt = self._ut_passthrough()
        if pt.get("starting_units") is not None:
            starters = [u for u in pt["starting_units"] if u in units]
        else:
            n = self.options.starting_unlocks_per_category.value
            story_units = [u for u in units if u in set(data.STORY_ITEMS) and u != data.FINAL_STORY_ITEM]
            arcade_units = [u for u in units if u in data.item_name_groups["Arcade"]]
            chal_units = [u for u in units if u in data.item_name_groups["Challenge"]]
            starters = (self.random.sample(story_units, min(n, len(story_units)))
                        + self.random.sample(arcade_units, min(n, len(arcade_units)))
                        + self.random.sample(chal_units, min(n, len(chal_units))))
        self.starters = list(starters)                 # echoed in slot_data so a UT regen can restore them exactly
        for name in starters:
            self.multiworld.push_precollected(self.create_item(name))

        pool = [self.create_item(u) for u in units if u not in starters]

        # weapons-as-items (A): one PROGRESSION item per gated weapon slot, only when Weapon Gating is on (they gate the
        # per-level objective logic). Precollect the Silenced Pistol (slot 1) so sphere 0 always has a baseline gun.
        # Objective checks are always on, so there is always room for the full set -- no clamp (a skipped weapon would
        # silently break logic; if a pathological seed ever overflowed, a loud FillError is preferable).
        weapon_items: List[TS2Item] = []
        self.starter_weapons: List[str] = []
        if self.gating:
            # arm EACH starting story mission: precollect the item for that mission's PRIMARY weapon (remap-aware) so
            # every sphere-0 story mission is actually playable, instead of a fixed Silenced Pistol.
            if pt.get("starting_weapons") is not None:    # UT regen: restore, don't recompute (see the note above)
                starter_weapons = {w for w in pt["starting_weapons"] if w in data.WEAPON_ITEM_OF.values()}
            else:
                mission_of_item = {data.ITEM_OF[m]: m for m in data.STORY}
                starter_weapons = set()
                for st in starters:
                    m = mission_of_item.get(st)
                    if m is not None and m in data.LEVEL_PRIMARY_SLOT:
                        ps = data.LEVEL_PRIMARY_SLOT[m]
                        starter_weapons.add(data.WEAPON_ITEM_OF[self._remap_for(m).get(ps, ps)])
                if not starter_weapons:                   # no story starter -> a baseline gun so sphere 0 is still armed
                    starter_weapons.add(data.WEAPON_ITEM_OF[1])
            self.starter_weapons = sorted(starter_weapons)
            for it in starter_weapons:
                self.multiworld.push_precollected(self.create_item(it))
            weapon_items = [self.create_item(data.WEAPON_ITEM_OF[s]) for s in data.weapon_gated_slots()
                            if data.WEAPON_ITEM_OF[s] not in starter_weapons]
        pool += weapon_items

        # Time Crystals: progression that gates the final stage. They open no location, so they fit only in
        # the NON-excluded "slack" beyond one-per-unit; keep ~20% as Banana filler so the self-gating chain always has
        # leaf locations. Total placed = Required + Extra (so Required can never exceed what's placed), clamped to slack.
        if not self.story:
            # No final stage to gate (the goal is a percentage of the trophy checks), so Time Crystals would be
            # dead progression items. Place none; the freed slots become ordinary bonus/filler below.
            if not self._ut:
                self.tc_total = 0
                self.tc_required = 0
        elif not self._ut:   # (under UT, tc_required/tc_total were restored from slot_data in generate_early)
            excluded = sum(1 for ln, u in active_locs if self._excluded(ln, u))
            slack = max(0, len(active_locs) - excluded - len(units) - len(weapon_items))
            cap = slack * 4 // 5
            want_required = self.options.time_crystals_required.value
            want_total = want_required + self.options.time_crystals_extra.value
            floor = min(want_required, TC_REQUIRED_FLOOR)   # honours a deliberately small request
            # Keep the gate at the floor even when the density heuristic wants less, but never place more crystals
            # than there are free locations to hold them.
            self.tc_total = min(max(min(want_total, cap), floor), slack)
            self.tc_required = min(want_required, self.tc_total)
            if self.tc_total < want_total or self.tc_required < want_required:
                logging.warning("TimeSplitters 2 (player %d): Time Crystals reduced to %d placed / %d required (wanted "
                                "%d/%d) -- low check density.", self.player, self.tc_total, self.tc_required,
                                want_total, want_required)
        pool += [self.create_item(data.TIME_CRYSTAL_ITEM) for _ in range(self.tc_total)]

        # bonus + trap items: pure filler-class, so they just REPLACE Banana 1:1 (no logic impact). Clamp to the free
        # filler slots left after units + weapons + Time Crystals; if over, trim at random with a warning (rare -- typical
        # seeds have 150-280 filler slots). Traps are drawn randomly from the named set for variety.
        room = len(active_locs) - len(pool)
        extras = ([data.BONUS_ITEMS[0]] * self.options.bonus_health_packs.value
                  + [data.BONUS_ITEMS[1]] * self.options.bonus_armor_packs.value
                  + [data.BONUS_ITEMS[2]] * self.options.bonus_ammo_packs.value)
        if self.options.trap_count.value and data.TRAP_ITEMS:
            extras += self.random.choices(data.TRAP_ITEMS, k=self.options.trap_count.value)
        if len(extras) > room:
            logging.warning("TimeSplitters 2 (player %d): %d trap/bonus items requested but only %d free filler slots "
                            "-- trimming to fit.", self.player, len(extras), room)
            self.random.shuffle(extras)
            extras = extras[:room]
        pool += [self.create_item(e) for e in extras]

        while len(pool) < len(active_locs):
            pool.append(self.create_item(data.FILLER_ITEM))
        self.multiworld.itempool += pool

    # ---- regions / locations ----
    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)

        self._unit_of = {}
        for loc_name, unit in self._active_locations():
            loc = TS2Location(self.player, loc_name, self.location_name_to_id[loc_name], menu)
            # Space Station's checks sit behind the goal gate; objectives only optional (secondary) on the selected
            # difficulties may be skipped -> both are filler-only so the generator never strands progression there.
            if self._excluded(loc_name, unit):
                loc.progress_type = LocationProgressType.EXCLUDED
            menu.locations.append(loc)
            self._unit_of[loc_name] = unit

        victory = TS2Location(self.player, "Victory", None, menu)
        victory.place_locked_item(TS2Item("Victory", ItemClassification.progression, None, self.player))
        menu.locations.append(victory)

    # ---- rules ----
    def set_rules(self) -> None:
        p = self.player
        get = self.multiworld.get_location
        final = data.FINAL_STORY_ITEM
        tc = data.TIME_CRYSTAL_ITEM
        req = self.tc_required                                     # clamped count (see create_items)
        gating = self.gating
        # NB: the map is looked up PER MISSION (self._remap_for) -- within_level gives every level its own.
        sel_diffs = set(self.diffs)

        # GOAL. With Story kept: the final stage (Space Station) needs its own unlock AND the required Time Crystals;
        # has(...,0) is True. Without Story there is no Space Station, so the goal is completing Trophy Goal Percentage
        # of the trophy checks THIS SEED KEPT -- every such check sits behind its event's unlock and each event
        # contributes the same number of tier checks, so "that share of the checks" == "that share of the event unlocks".
        if not self.story:
            event_items = self._trophy_event_items()
            need_events = max(1, -(-len(event_items) * self.trophy_goal_pct // 100))   # ceil
            self.trophy_goal_events = need_events

            def goal_rule(state) -> bool:
                return sum(1 for it in event_items if state.has(it, p)) >= need_events
        else:
            self.trophy_goal_events = 0

            def goal_rule(state) -> bool:
                return state.has(final, p) and state.has(tc, p, req)

        DORDER = ["Easy", "Normal", "Hard"]
        # h(token): a category is satisfied iff THIS level provides (on <difficulty>) an unlocked weapon of that group
        # (remap-aware; late pickups still count); a tool iff its item is unlocked (no-shuffle, always in its level).
        def make_h(mission, difficulty, state):
            def h(tok):
                if tok in data.WEAPON_GROUPS:
                    items = data.level_category_items(mission, tok, self._remap_for(mission), difficulty)
                    return bool(items) and state.has_any(items, p)
                return state.has(data.WEAPON_ITEM_OF[data.WEAPON_TOOL_SLOT[tok]], p)
            h.difficulty = difficulty                    # lets difficulty-conditional rules (e.g. Aztec golems) branch
            return h

        # "armed from the start": an EARLY combat weapon (available on <difficulty>) is unlocked. Required for ANY
        # in-mission location so the player never has to fist the opening while waiting on a deep pickup.
        def armed(mission, difficulty, state):
            items = data.level_early_combat_items(mission, self._remap_for(mission), difficulty)
            return bool(items) and state.has_any(items, p)

        # complete = armed + every primary objective's weapon rule satisfiable on <difficulty> (Escape itself excluded).
        def complete_ok(mission, difficulty, diffs, state):
            if not armed(mission, difficulty, state):
                return False
            if difficulty == "Hard":                              # Hard demands a guaranteed SECOND weapon (one gun is brutal)
                sec = data.LEVEL_HARD_SECONDARY.get(mission)
                if sec is not None:
                    item = data.WEAPON_ITEM_OF.get(self._remap_for(mission).get(sec, sec))
                    if item is not None and not state.has(item, p):
                        return False
            h = make_h(mission, difficulty, state)
            for tid, _name, prim, _sec in data.OBJECTIVES.get(mission, []):
                if tid != data.ESCAPE_TEXTID and (prim & diffs) and not data.objective_rule(mission, tid)(h):
                    return False
            return True

        misscomp = {f"{data.DISPLAY_OF[m]} ({d})": (m, d)
                    for m in data.STORY for d in data.STORY_DIFFICULTIES}
        easiest = next((d for d in DORDER if d in sel_diffs), "Easy")
        def easiest_for(prim, sec):                                # easiest SELECTED difficulty the objective appears on
            appears = (prim | sec) & sel_diffs
            return next((d for d in DORDER if d in appears), easiest)

        for loc_name, unit in self._unit_of.items():
            # Space Station and its objectives sit behind the goal gate (its own unlock + the required Time Crystals)
            # IN ADDITION to the normal weapon logic. The gate used to REPLACE that logic, which meant the final
            # mission was considered clearable with no weapon at all -- and worse, its own early weapon could then be
            # placed on one of its locations, which cannot be collected without first clearing it.
            gate = goal_rule if unit == final else None
            info = data.OBJ_LOC_INFO.get(loc_name)
            mc = misscomp.get(loc_name)
            if gating and info is not None:                       # in-mission objective
                tid, prim, sec, mission = info
                if tid == data.ESCAPE_TEXTID or (mission, tid) in data.MISSION_COMPLETE_OBJS:   # earned by finishing the mission
                    d = easiest_for(prim, sec)                    # Escape (prim=all) -> global easiest; others -> easiest they appear on
                    rule = (lambda state, m=mission, d=d, u=unit:
                        state.has(u, p) and complete_ok(m, d, frozenset({d}), state))
                else:
                    d = easiest_for(prim, sec)
                    r = data.objective_rule(mission, tid)
                    rule = (lambda state, m=mission, d=d, r=r, u=unit:
                        state.has(u, p) and armed(m, d, state) and r(make_h(m, d, state)))
            elif gating and mc is not None:                       # story mission-completion on a difficulty
                m, d = mc
                rule = (lambda state, m=m, d=d, u=unit:
                    state.has(u, p) and complete_ok(m, d, frozenset({d}), state))
            else:
                rule = (lambda state, u=unit: state.has(u, p))
            get(loc_name, p).access_rule = rule if gate is None else (
                lambda state, rule=rule, gate=gate: gate(state) and rule(state))

        # Victory needs more than the Time Crystals: you actually have to CLEAR Space Station on the goal difficulty,
        # which (under gating) needs its weapons -- the Plasma Autorifle, plus the Minigun on Hard. goal_rule alone
        # (unlock + Time Crystals) let go-mode fire before those weapons were in logic, so a tracker showed go-mode while
        # the Space Station checks stayed dark. Only in story modes with gating; goal_rule stays the per-location gate
        # (each Space Station location already ANDs in its OWN difficulty's weapon rule, so it must not inherit Hard's).
        if self.story and gating:
            gd = self.goal_difficulty
            def victory_rule(state) -> bool:
                return goal_rule(state) and complete_ok(data.FINAL_STORY_MISSION, gd, frozenset({gd}), state)
        else:
            victory_rule = goal_rule
        get("Victory", p).access_rule = victory_rule
        self.multiworld.completion_condition[p] = lambda state: state.has("Victory", p)

    # ---- client handshake ----
    def fill_slot_data(self) -> Dict[str, Any]:
        # No-Story GOAL: the client fires it once this many trophy checks are done. Counted over the events this seed
        # KEPT (each contributes one check per selected tier), so a Challenge-only seed needs challenge trophies alone.
        # 0 whenever Story is kept, where the goal is the Space Station clear instead.
        goal_eis = self._goal_trophy_eis()
        trophy_goal_checks = 0
        if not self.story:
            total = len(goal_eis) * len(self.tiers)
            trophy_goal_checks = max(1, -(-total * self.trophy_goal_pct // 100))   # ceil
        # Legacy hint for pre-1.0.2 clients, which only understood a single game_mode int. Sent ONLY when the kept set
        # is exactly one of the three old modes; for a new combination it is omitted rather than approximated, so an old
        # client falls back to "both" and simply never auto-fires a goal instead of firing the wrong one early.
        legacy = next((v for v, ms in LEGACY_MODE_SETS.items() if ms == frozenset(self.modes)), None)
        out = {
            "game_modes": list(self.modes),                  # canonical order; the client reads story/arcade/challenge
            "trophy_goal_percentage": self.trophy_goal_pct,
            "trophy_goal_checks": trophy_goal_checks,         # fire GOAL at this many trophy checks (0 = Space Station goal)
            "goal_trophy_events": goal_eis,                   # event indices that COUNT toward that goal
            "story_content": self.story,                      # is there a Space Station clear to win on?
            "time_crystals_required": self.tc_required,      # client keeps Space Station locked until this many arrive
            "time_crystals_total": self.tc_total,
            "story_difficulty_checks": sorted(self.diffs),               # cumulative tiers up to the chosen max
            "trophy_tier_checks": sorted(self.tiers),
            "goal_difficulty": self.goal_difficulty,                     # GOAL fires on clearing Space Station HERE (the max)
            "objective_checks": True,                                    # objectives are always checks now (client sends them)
            # per-objective primary-difficulty bitmask (Easy=1 Normal=2 Hard=4), keyed by the same index the client
            # uses for g_objDone (missionIdx*64 + textID-OBJ_TEXTID_BASE).  On a mission clear the client auto-fires
            # the objectives that are PRIMARY at the cleared difficulty -- they're guaranteed done by clearing, so
            # the check never gets missed even if the level-end objective never renders for the live hook.
            "objective_primary": data.objective_primary_mask(),
            # weapons-as-items: gating (A) and the global remap (B), which also applies to enemy weapons. The client
            # decodes a received weapon item as slot = item_id - BASE_ID - weapon_item_base, and gates / shuffles by slot.
            # NB: send the EFFECTIVE values (self.gating/self.shuffle), not the raw options -- arcade_only forces both
            # off, and the UT regen path restores them from slot_data.
            "weapon_gating": bool(self.gating),
            "weapon_shuffle": bool(self.shuffle),
            "enemy_weapon_shuffle": bool(self.shuffle),
            "weapon_item_slots": data.weapon_gated_slots(),          # gated slots (everything else is always-available)
            "weapon_item_base": data.WEAPON_ITEM_ID_BASE,
            "weapon_shuffle_scope": self.scope,               # 0=completely_random 1=same_class 2=within_level
            "weapon_remap": {str(s): v for s, v in self.weapon_remap.items()},   # {} when shuffle off / within_level
            # within_level: one map per MISSION INDEX (matches the client's mission idx). The client re-applies the
            # weapon tables on every level load, so it just picks the map for the level being loaded. {} otherwise.
            "weapon_remap_by_level": {str(data.STORY.index(m)): {str(s): v for s, v in mp.items() if v != s}
                                      for m, mp in self.weapon_remap_by_level.items()},
            # Same mappings by NAME, for humans reading slot_data / bug reports ("Silenced Pistol -> Shotgun (x2)").
            # The client ignores these and uses the numeric forms above. Only slots that actually CHANGED are listed,
            # so an unshuffled seed sends {} / [] and a shuffled one reads as a short, obvious swap list.
            "weapon_remap_names": {data.WEAPONS.get(s, f"slot {s}"): data.WEAPONS.get(v, f"slot {v}")
                                   for s, v in sorted(self.weapon_remap.items()) if v != s},
            "weapon_remap_readable": [f"{data.WEAPONS.get(s, f'slot {s}')} -> {data.WEAPONS.get(v, f'slot {v}')}"
                                      for s, v in sorted(self.weapon_remap.items()) if v != s],
            "weapon_remap_by_level_readable": {
                m: [f"{data.WEAPONS.get(s, f'slot {s}')} -> {data.WEAPONS.get(v, f'slot {v}')}"
                    for s, v in sorted(mp.items()) if v != s]
                for m, mp in self.weapon_remap_by_level.items()},
            # The units + weapons this seed GRANTED at the start. Echoed purely so a Universal Tracker regen can restore
            # them instead of re-rolling: on that path the options are defaults and generate_early consumes none of the
            # randomness a real generation spends, so a fresh sample() picks different starters entirely. The client
            # ignores both (the server sends precollected items normally).
            "starting_units": sorted(getattr(self, "starters", [])),
            "starting_weapons": sorted(getattr(self, "starter_weapons", [])),
            "death_link": bool(self.options.death_link.value),
            # DeathLink outgoing throttle for Arcade + Challenge (deathmatch there means constant deaths).
            # The client sends one death per this many; 0 = never send from those modes. Story always sends
            # every death, and incoming deaths always apply everywhere, so this only limits what we broadcast.
            "arcade_death_link_threshold": self.options.arcade_death_link_threshold.value,
            "version": SLOT_DATA_VERSION,
        }
        if legacy is not None:
            out["game_mode"] = legacy
        return out
