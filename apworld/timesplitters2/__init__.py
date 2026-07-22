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
from worlds.AutoWorld import World, WebWorld

from . import data
from .options import TS2Options

# Game Mode (options.GameMode). story = the 10 missions + their objectives; arcade = the 66 trophy events
# (45 Arcade matches + 21 Challenges, which the option treats as one "Arcade" bucket).
MODE_BOTH, MODE_STORY_ONLY, MODE_ARCADE_ONLY = 0, 1, 2
# Weapon Shuffle Scope (options.WeaponShuffleScope). within_level is the only one that needs a PER-LEVEL map -- the
# client re-applies the weapon tables on every level load, so the same slot can be a different gun per level.
SCOPE_COMPLETELY_RANDOM, SCOPE_SAME_CLASS, SCOPE_WITHIN_LEVEL = 0, 1, 2
# slot_data schema version. Bump whenever the KEYS the client reads change, so a mismatched client can say so
# instead of silently mis-reading a seed. 7 = added game_mode / arcade_goal_* / weapon_shuffle_scope /
# weapon_remap_by_level (6 = the pre-1.0.0 schema, which used the old content_mode key).
SLOT_DATA_VERSION = 7


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
            self.mode = pt.get("game_mode", MODE_BOTH)
            self.arcade_goal_pct = pt.get("arcade_goal_percentage", 90)
            return
        self._ut = False
        self.mode = self.options.game_mode.value                       # 0=both 1=story_only 2=arcade_only
        self.arcade_goal_pct = self.options.arcade_goal_percentage.value
        self.gating = bool(self.options.weapon_gating.value)
        self.shuffle = bool(self.options.weapon_shuffle.value)
        if self.mode == MODE_ARCADE_ONLY:
            # Weapon Gating / Shuffle only ever affect STORY missions (the client applies them behind g_inStory), and
            # arcade_only has no story checks -- so they would be silent no-ops that still bloat the item pool. Force off.
            if self.gating or self.shuffle:
                logging.info("TimeSplitters 2 (player %d): arcade_only -- Weapon Gating/Shuffle forced off "
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
        if self.mode != MODE_STORY_ONLY:                      # Arcade matches + Challenges
            for _, _, name in data.TROPHY_EVENTS:
                for t in data.TROPHY_TIERS:
                    if t in tiers:
                        out.append((f"{data.DISPLAY_OF[name]} ({t})", data.ITEM_OF[name]))
        if self.mode != MODE_ARCADE_ONLY:                     # story missions + their objectives
            for m in data.STORY:
                for d in data.STORY_DIFFICULTIES:
                    if d in diffs:
                        out.append((f"{data.DISPLAY_OF[m]} ({d})", data.ITEM_OF[m]))
            for m in data.STORY:                              # objectives are ALWAYS checks (density + the Time Crystal/weapon pool)
                for tid, name, prim, sec in data.OBJECTIVES.get(m, []):
                    if (prim | sec) & diffs:                  # objective appears on a difficulty up to the chosen max
                        out.append((data.objective_location_name(m, name), data.ITEM_OF[m]))
        return out

    def _arcade_event_items(self) -> List[str]:
        """The distinct unlock items behind the active Arcade + Challenge checks (one per trophy event)."""
        seen, out = set(), []
        for _, _, name in data.TROPHY_EVENTS:
            it = data.ITEM_OF[name]
            if it not in seen:
                seen.add(it)
                out.append(it)
        return out

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
        n = self.options.starting_unlocks_per_category.value
        story_units = [u for u in units if u in set(data.STORY_ITEMS) and u != data.FINAL_STORY_ITEM]
        arcade_units = [u for u in units if u in data.item_name_groups["Arcade"]]
        chal_units = [u for u in units if u in data.item_name_groups["Challenge"]]
        starters = (self.random.sample(story_units, min(n, len(story_units)))
                    + self.random.sample(arcade_units, min(n, len(arcade_units)))
                    + self.random.sample(chal_units, min(n, len(chal_units))))
        for name in starters:
            self.multiworld.push_precollected(self.create_item(name))

        pool = [self.create_item(u) for u in units if u not in starters]

        # weapons-as-items (A): one PROGRESSION item per gated weapon slot, only when Weapon Gating is on (they gate the
        # per-level objective logic). Precollect the Silenced Pistol (slot 1) so sphere 0 always has a baseline gun.
        # Objective checks are always on, so there is always room for the full set -- no clamp (a skipped weapon would
        # silently break logic; if a pathological seed ever overflowed, a loud FillError is preferable).
        weapon_items: List[TS2Item] = []
        if self.gating:
            # arm EACH starting story mission: precollect the item for that mission's PRIMARY weapon (remap-aware) so
            # every sphere-0 story mission is actually playable, instead of a fixed Silenced Pistol.
            mission_of_item = {data.ITEM_OF[m]: m for m in data.STORY}
            starter_weapons = set()
            for st in starters:
                m = mission_of_item.get(st)
                if m is not None and m in data.LEVEL_PRIMARY_SLOT:
                    ps = data.LEVEL_PRIMARY_SLOT[m]
                    starter_weapons.add(data.WEAPON_ITEM_OF[self._remap_for(m).get(ps, ps)])
            if not starter_weapons:                       # no story starter -> a baseline gun so sphere 0 is still armed
                starter_weapons.add(data.WEAPON_ITEM_OF[1])
            for it in starter_weapons:
                self.multiworld.push_precollected(self.create_item(it))
            weapon_items = [self.create_item(data.WEAPON_ITEM_OF[s]) for s in data.weapon_gated_slots()
                            if data.WEAPON_ITEM_OF[s] not in starter_weapons]
        pool += weapon_items

        # Time Crystals: progression that gates the final stage. They open no location, so they fit only in
        # the NON-excluded "slack" beyond one-per-unit; keep ~20% as Banana filler so the self-gating chain always has
        # leaf locations. Total placed = Required + Extra (so Required can never exceed what's placed), clamped to slack.
        if self.mode == MODE_ARCADE_ONLY:
            # No final stage to gate (the goal is a percentage of the Arcade/Challenge checks), so Time Crystals would be
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
            self.tc_total = min(want_total, cap)
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

        # GOAL. Story modes: the final stage (Space Station) needs its own unlock AND the required Time Crystals;
        # has(...,0) is True. Arcade-only: there is no Space Station, so the goal is completing Arcade Goal Percentage
        # of the Arcade/Challenge checks -- every such check sits behind its event's unlock and each event contributes
        # the same number of tier checks, so "that share of the checks" == "that share of the event unlocks".
        if self.mode == MODE_ARCADE_ONLY:
            event_items = self._arcade_event_items()
            need_events = max(1, -(-len(event_items) * self.arcade_goal_pct // 100))   # ceil
            self.arcade_goal_events = need_events

            def goal_rule(state) -> bool:
                return sum(1 for it in event_items if state.has(it, p)) >= need_events
        else:
            self.arcade_goal_events = 0

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

        get("Victory", p).access_rule = goal_rule
        self.multiworld.completion_condition[p] = lambda state: state.has("Victory", p)

    # ---- client handshake ----
    def fill_slot_data(self) -> Dict[str, Any]:
        # arcade_only GOAL: the client fires it once this many Arcade/Challenge checks are done (every trophy event
        # contributes one check per selected tier). 0 in the story modes, where the goal is the Space Station clear.
        arcade_goal_checks = 0
        if self.mode == MODE_ARCADE_ONLY:
            total = len(data.TROPHY_EVENTS) * len(self.tiers)
            arcade_goal_checks = max(1, -(-total * self.arcade_goal_pct // 100))   # ceil
        return {
            "game_mode": self.mode,                          # 0=both 1=story_only 2=arcade_only
            "arcade_goal_percentage": self.arcade_goal_pct,
            "arcade_goal_checks": arcade_goal_checks,        # arcade_only: fire GOAL at this many Arcade/Challenge checks
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
            "death_link": bool(self.options.death_link.value),
            "version": SLOT_DATA_VERSION,
        }
