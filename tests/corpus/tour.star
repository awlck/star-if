# =============================================================================
#  tour.star — the Stardata conformance corpus
# =============================================================================
#
#  This file exercises every construct in docs/stardata-spec.md. It is parsed
#  and validated as part of CI: if the specification and the parser diverge,
#  the build breaks. It doubles as the format's worked example, so it is
#  written as a coherent (if very small) scenario rather than as a grab-bag.
#
#  Scenario: KEPLER STATION. You wake in a holding cell aboard a research
#  station that has evidently had a bad week. Your ship, the WAYFARER, is
#  docked and stays live while you explore (pinned sector). Kira is a
#  companion; Vex is a quartermaster with a schedule and opinions.
#
#  Section index:
#     1  Project manifest              10  Actions
#     2  Calendars and time            11  Rules
#     3  Enumerations                  12  Turn hooks
#    3b  Globals and constants
#     4  Styles                        13  Quests
#     5  Schema extension              14  Dialogue
#     6  Classes, extensions, traits   15  NPC behaviour
#     7  Sectors                       16  The party
#     8  Rooms and geography          16b  The lexicon
#     9  Things and people             17  Localisation
#                                      18  Edge cases and syntax corners
#
#  Every comment style, operator, scalar kind and annotation defined by the
#  specification appears somewhere below. See §18 for the ones that have no
#  natural home in a scenario.
#
#  Checked by: python3 tests/check_stardata.py --check-docs --self-test --strict
#
#  §18 declares loc entries purely to demonstrate string syntax, and §17
#  declares the engine's own fallback message, which is referenced by the
#  runtime rather than by data. Neither is a real unused string, so the
#  unused-key warning is suppressed for this file only.
# check: allow W-LOC-UNUSED
#
# =============================================================================


# =============================================================================
#  1. PROJECT MANIFEST                                          (spec §13.1)
# =============================================================================

project = {
    id      = tour
    title   = "A Tour of Kepler Station"
    author  = "Adrian Welcker"
    version = "0.1.0"
    ifid    = "8F4B2C1A-0000-4000-8000-000000000001"
    source_language = en

    # Load order: stdlib/stdlib is implicit and always first.
    uses = { starscape }

    player       = pc
    start_room   = your_cell
    entry_sector = station_alpha

    defaults = {
        action_duration = 60          # one minute per action
        advances_turn   = on_success
        # Narrative voice applies to every adaptive substitution (spec §9.5.3).
        # A past-tense game costs one declaration, not a rewrite.
        narrative_person = second     # first | second | third
        narrative_tense  = present    # present | past
    }

    simulation = {
        offstage_default        = catch_up
        simulate_max_rounds     = 5000
        simulate_time_budget_ms = 20000
        simulate_progress       = yes
        default_combat_response = flee
    }
}


# =============================================================================
#  2. CALENDARS AND TIME                                    (spec §11.6, §6.2)
# =============================================================================

# There is exactly one clock. Calendars describe how it is displayed and how
# clock_time values are compiled; they do not create independent timelines.

calendar = {
    id             = terran_standard
    tick_seconds   = 1
    seconds_per_minute = 60
    minutes_per_hour   = 60
    hours_per_day      = 24
    days_per_week      = 7
    day_names = { monday tuesday wednesday thursday friday saturday sunday }

    month = { id = january   days = 31 }
    month = { id = february  days = 28  leap_days = 1 }
    month = { id = march     days = 31 }

    leap_rule       = gregorian_leap        # a script; see §12 of the spec
    epoch           = "2384-03-11 06:00:00"
    display_default = $fmt_stardate
}

# A world with a longer day. Used by the surface sector's local_clock below,
# which affects display and schedule authoring only.
calendar = {
    id             = kepler_iv
    tick_seconds   = 1
    seconds_per_minute = 60
    minutes_per_hour   = 60
    hours_per_day      = 30
    days_per_week      = 6
    day_names = { first second third fourth fifth sixth }
    month = { id = long_dark   days = 40 }
    month = { id = long_light  days = 40 }
    epoch = "0001-01-01 00:00:00"
}


# =============================================================================
#  3. ENUMERATIONS                                               (spec §6.2)
# =============================================================================

enum = {
    id     = condition_enum
    values = { breathable toxic underwater vacuum }
}

enum = {
    id     = relation_enum
    values = { in on under behind carried worn part_of }
}

enum = {
    id     = advances_turn_enum
    values = { on_success always never }
}

enum = {
    id     = combat_response_enum
    values = { join_ally join_hostile defend_self flee panic observe ignore }
}

enum = {
    id     = alert_level_enum
    values = { none low elevated high }
}

enum = {
    id     = mood_enum
    values = { warm neutral wary hostile }
}

# An enum used as a `flags<E>` bitset further down.
enum = {
    id     = damage_type_enum
    values = { kinetic thermal ionising corrosive }
}


# =============================================================================
#  3b. GLOBALS AND CONSTANTS                                     (spec §6.4)
# =============================================================================
# Not all author data belongs to an object. Globals are mutable, typed, saved
# world state; constants are immutable and not saved.

global = {
    id      = alert_level
    type    = enum<alert_level_enum>
    initial = none
    doc     = $g_alert_level_doc
}

global = { id = times_caught  type = int   initial = 0 }
global = { id = last_accused  type = ref<person>  initial = none }

# Flags are sugar over a declared bool global, NOT a separate store. Requiring
# the declaration turns `flag_set = captain_finded` from a silent permanent
# bug into a compile error (spec §6.4.1).
global = { id = captain_found        type = bool  initial = no }
global = { id = coolant_unlocked     type = bool  initial = no }
global = { id = coolant_vented       type = bool  initial = no }
global = { id = log_recovered        type = bool  initial = no }
global = { id = saw_the_manifest     type = bool  initial = no }
global = { id = heard_vex_slip       type = bool  initial = no }
global = { id = captain_confronted   type = bool  initial = no }

# A collection-valued global, mutated at runtime (spec §6.5).
global = { id = seen_endings  type = set<identifier>  initial = { } }

# A map-valued global, so `includes` has both a key domain and a value domain
# to search (spec §6.5.1).
global = {
    id      = npc_moods
    type    = map<identifier, mood_enum>
    initial = { quartermaster_vex = wary  companion_kira = warm }
}

const = { id = max_reactor_temp  type = int  value = 1200 }

# ...and a use of it, so the never-read warning stays meaningful.
global = { id = core_temp     type = int  initial = 300 }
global = { id = hydration     type = int  initial = 0 }
global = { id = intoxication  type = int  initial = 0 }


# =============================================================================
#  4. STYLES                                                     (spec §9.3)
# =============================================================================
# Styling is semantic: authors name a style, the theme maps it to attributes.
# An undeclared style name is a compile error, so they all live here.

style = { id = item_name }
style = { id = danger }
style = { id = flavour }
style = { id = stat_line }
style = { id = tooltip_card }
style = { id = speaker_vex }
style = { id = speaker_kira }
style = { id = narrator }


# =============================================================================
#  5. SCHEMA EXTENSION                                      (spec §7.1, §7.2)
# =============================================================================
# Declaring a schema adds a new top-level form. Starbase renders an inspector
# for it with no editor code, which is the mechanism behind the RPG layer's
# "special kinds appear in the editor".

schema = {
    id        = loot_table
    doc       = $schema_loot_table_doc
    top_level = yes

    key = { name = id       type = identifier  required = yes  unique_in = loot_table }
    key = { name = rolls    type = int         default  = 1 }
    key = { name = entry    type = block<loot_entry>  arity = many }
}

schema = {
    id = loot_entry
    key = { name = item    type = ref<thing>  required = yes  editor = object_picker }
    key = { name = weight  type = int         default  = 1 }
    key = { name = conditions  type = condition_block }
}

# Adding a key to an EXISTING form — including a sealed core one. Sealing
# prevents redefinition, not extension (spec §7.5).
schema_extension = {
    of_schema = action
    key = { name = stamina_cost  type = int  default = 0 }
}

# ...and an instance of the form the schema just declared.
loot_table = {
    id    = derelict_locker
    rolls = 2
    entry = { item = ration_pack     weight = 10 }
    entry = { item = plasma_cutter   weight = 2 }
    entry = { item = captains_log    weight = 1
              conditions = { NOT = { flag_set = log_recovered } } }
}


# =============================================================================
#  6. CLASSES, EXTENSIONS AND TRAITS                     (spec §8.1–§8.3)
# =============================================================================

# --- 6.1 Extending a class supplied by the standard library -----------------

class_extension = {
    of_class = room
    prop_def = {
        condition = condition_enum
    }
    condition = breathable            # a new default for all rooms
}

class_extension = {
    of_class = thing
    prop_def = {
        # Dialogue presentation properties live on `thing`, not on `person`,
        # so that objects can be "spoken to" (see §14, the reactor console).
        portrait      = resource
        portrait_mood = map<identifier, resource>
        speaker_style = identifier
        speaker_name  = text
        # Tooltip content, evaluated lazily on hover.
        tooltip       = text
    }
}

class_extension = {
    of_class = person
    prop_def = {
        combat_response = combat_response_enum
        strength        = int
        presence        = int
    }
    combat_response = flee            # the safe default; see project.simulation
    strength        = 10
    presence        = 10
}

# --- 6.2 New classes --------------------------------------------------------

class = {
    id       = container
    of_class = thing
    prop_def = {
        capacity         = int
        holding_relation = enum<relation_enum>
    }
    capacity         = 10
    holding_relation = in
}

class = {
    id       = supporter
    of_class = thing
    prop_def = { holding_relation = enum<relation_enum> }
    holding_relation = on
}

class = {
    id       = outdoors_room
    of_class = room
    condition = toxic
}

class = {
    id       = sea_room
    of_class = room
    condition = underwater
}

# A weapon class, with a decimal property and a flags bitset.
class = {
    id       = weapon
    of_class = thing
    prop_def = {
        damage       = dice
        damage_types = flags<damage_type_enum>
        range        = int
        weight       = decimal
    }
    damage       = "1d6"
    damage_types = { kinetic }
    range        = 1
    weight       = 1.500                 # fixed-point, exactly three digits
}

class = {
    id       = armor
    of_class = thing
    prop_def = {
        protection = int
        slot       = identifier
    }
    protection = 1
    slot       = torso
}

# --- 6.3 Traits -------------------------------------------------------------
# Orthogonal capability bundles. A class has one parent but many traits.

trait = {
    id = openable
    # A prop_def entry may be a bare type or a block carrying markers. The
    # engine never learns the name `open`; it learns which properties affect
    # scope and is told which ones do (spec §7.2.3).
    prop_def = {
        open             = { type = bool  affects_scope = yes }
        openable_by_hand = bool
    }
    open             = no
    openable_by_hand = yes

    # A trait may carry rules; they attach to whatever mixes the trait in.
    rule = {
        of_action  = open
        conditions = { noun = { has_trait = openable } }
        restrictions = {
            noun = { open == no  failureMsg = $already_open }
        }
        effects    = { set = { target = noun  prop = open  value = yes } }
        successMsg = $opened_default
    }
}

trait = {
    id = lockable
    prop_def = {
        locked   = bool
        lock_key = ref<thing>
    }
    locked   = no
    lock_key = none                      # `none` is distinct from `inherit`
}

trait = {
    id = portable
}

trait = {
    id = drinkable
    prop_def = { volume_ml = int }
    volume_ml = 250
}

trait = {
    id = alcoholic
}

trait = {
    id = fixed_in_place
}

trait = {
    id = animate
}

trait = {
    id = cursed
}

trait = {
    id = hot_potato
}

# Two traits that both declare `open`, to demonstrate explicit resolution.
trait = {
    id = trapped
    prop_def = {
        open      = bool
        trap_armed = bool
    }
    open       = no
    trap_armed = yes
}

class = {
    id       = trick_chest
    of_class = container
    traits   = { openable trapped }
    # Both traits declare `open`. Without this line, a compile error.
    # `resolve` maps each contested property to the trait it comes from.
    resolve  = { open = openable }
}

class = {
    id       = door
    of_class = thing
    traits   = { openable lockable fixed_in_place }
}


# =============================================================================
#  7. SECTORS                                              (spec §11.4, §11.6)
# =============================================================================

sector = {
    id           = station_alpha
    display_name = $sector_station_alpha
    neighbours   = { docking_ring }
    on_deactivate = serialize                # serialize | freeze | discard_changes | never
    offstage = {
        model  = catch_up                    # none | catch_up | simulate | continuous
        script = station_alpha_offstage
    }
}

sector = {
    id            = docking_ring
    display_name  = $sector_docking_ring
    neighbours    = { station_alpha }
    on_deactivate = serialize
    offstage      = { model = catch_up }
}

# The player's ship. `never` means it holds a permanent pin: it stays fully
# live wherever the player goes, so radioing Kira aboard it always works
# without a stand-in copy of her following the player around.
sector = {
    id            = ship_wayfarer
    display_name  = $sector_wayfarer
    on_deactivate = never
    offstage      = { model = continuous  script = wayfarer_systems_tick }
}

# A surface sector on a 30-hour day. One clock, local display only.
sector = {
    id            = kepler_iv_surface
    display_name  = $sector_surface
    local_clock   = { calendar = kepler_iv  offset = "+04:30" }
    on_deactivate = serialize
    offstage      = { model = simulate }     # expensive, correct, opt-in
}


# =============================================================================
#  8. ROOMS AND GEOGRAPHY                                   (spec §7.4, §8.5)
# =============================================================================
# The class name goes on the LEFT and the object's id inside. This is what
# lets the schema layer dispatch on the top-level key.

room = {
    id     = your_cell
    sector = station_alpha
    name   = $room_your_cell
    # Adjacent string literals concatenate with no separator inserted.
    # A single literal may not span a line terminator (spec §3.5).
    description = "A cell three paces by two. The door hangs open, which is "
                  "either an oversight or an invitation."
    exits  = { north = corridor }
}

room = {
    id     = corridor
    sector = station_alpha
    name   = $room_corridor
    exits  = {
        south = your_cell
        east  = front_office
        west  = control_room
        out   = airlock
    }
}

room = {
    id        = control_room
    sector    = station_alpha
    name      = $room_control_room
    exits     = { south = corridor }
    condition = breathable
}

room = {
    id     = front_office
    sector = station_alpha
    name   = $room_front_office
    exits  = {
        south = corridor
        east  = storage
        north = antecourt
    }
}

room = {
    id     = storage
    sector = station_alpha
    name   = $room_storage
    exits  = { west = front_office }
}

# An `outdoors_room`, which inherits `condition = toxic` from its class.
outdoors_room = {
    id     = antecourt
    sector = station_alpha
    name   = $room_antecourt
    exits  = { south = front_office }
}

room = {
    id     = airlock
    sector = station_alpha
    name   = $room_airlock
    # An exit may name a two-sided object instead of a room; the engine
    # resolves through it to the far side (spec §8.6.1). This keeps the map
    # graph and the door object from drifting apart.
    exits  = { in = corridor  out = airlock_hatch }
}

room = {
    id     = docking_gantry
    sector = docking_ring
    name   = $room_gantry
    exits  = { in = airlock_hatch  aft = wayfarer_bridge }
}

room = {
    id     = wayfarer_bridge
    sector = ship_wayfarer
    name   = $room_wayfarer_bridge
    exits  = { fore = docking_gantry }
}


# =============================================================================
#  9. THINGS AND PEOPLE                                    (spec §8.4, §8.5)
# =============================================================================

# --- 9.1 Placement by relation ----------------------------------------------

# `ornate_box` and `plain_box` are both boxes. Without a distinguishing name,
# nothing the player types could ever select the plain one, because its every
# name is also a name of the ornate one. Starforge warns about exactly this
# (proposal §6.4.1); `disambiguation_name` is the authored fix.
container = {
    id                  = plain_box
    in                  = storage
    name                = $thing_plain_box
    disambiguation_name = "plain box"
    synonyms            = { box }
    traits              = { openable portable }
}

container = {
    id       = ornate_box
    in       = storage
    synonyms = { box ornate }
    name     = $thing_ornate_box
    traits   = { openable lockable portable }
    lock_key = brass_key
    capacity = 3
}

# The long form of placement. `in = ornate_box` is sugar for exactly these two
# `starcore.object` properties (spec §8.5, §8.1.1); both spellings are legal
# and produce identical data, but writing both in one block is an error.
thing = {
    id       = brass_key
    holder   = ornate_box
    relation = in
    name     = $thing_brass_key
    traits   = { portable }
    synonyms = { key brass }
}

supporter = {
    id   = mess_table
    in   = front_office
    name = $thing_mess_table
    traits = { fixed_in_place }
}

thing = {
    id   = tarnished_mug
    on   = mess_table                    # relation `on`
    name = $thing_mug
    traits = { portable }
}

# A collection held as an object PROPERTY rather than as a global — the case
# that makes the datum-reference rule of spec §6.6 necessary.
thing = {
    id       = navcomp
    in       = control_room
    name     = $thing_navcomp
    traits   = { fixed_in_place }
    prop_def = { waypoints = list<identifier> }
    waypoints = { docking_gantry antecourt storage }
}

thing = {
    id      = reactor_console
    in      = control_room
    name    = $thing_console
    traits  = { fixed_in_place }

    # Most custom properties are one-offs belonging to a single object, so a
    # class is not required for them. These exist on THIS object only; no
    # sibling gains them, and the class is untouched (spec §8.7).
    prop_def = {
        times_rebooted  = int
        diagnostic_code = string
    }
    times_rebooted  = 0
    diagnostic_code = "E-114"
    # An object that can be "spoken to" — see the dialogue in §14.
    portrait     = "res/portraits/console_ui.png"
    speaker_name = $console_speaker_name
    speaker_style = narrator
}

thing = {
    id      = access_panel
    part_of = reactor_console            # destroyed with its parent
    name    = $thing_access_panel
}

thing = {
    id     = baked_potato
    in     = front_office
    name   = $thing_potato
    traits = { portable hot_potato }
}

armor = {
    id         = firefighter_gloves
    in         = storage
    name       = $thing_gloves
    traits     = { portable }
    protection = 0
    slot       = hands
}

# --- 9.2 A weapon, with a tooltip template ----------------------------------

weapon = {
    id           = plasma_cutter
    in           = storage
    name         = $thing_cutter
    traits       = { portable }
    damage       = "2d6+1"
    damage_types = { thermal ionising }      # flags<E>
    range        = 1
    weight       = 2.250

    # Styling within a template. A span runs until the next @style directive,
    # an @endstyle, or the end of the template. Line breaks are written
    # explicitly with \n rather than by wrapping the literal, which also
    # removes any question about whether the indentation is part of the text.
    tooltip = @style(tooltip_card) "[Name self]\n"
        "@style(stat_line)Damage: [DamageString(self)]\n"
        "@style(stat_line)Range: [self.range] m\n"
        "@style(flavour)Industrial. Not, strictly, a weapon."
}

thing = { id = ration_pack  in = storage  name = $thing_ration  traits = { portable } }
thing = { id = water_flask  in = storage  name = $thing_flask
          traits = { portable drinkable }  volume_ml = 500 }
thing = { id = vex_whisky   in = storage  name = $thing_whisky
          traits = { portable drinkable alcoholic }  volume_ml = 40 }
thing = { id = captains_log in = storage  name = $thing_log     traits = { portable } }
thing = { id = crowbar      in = storage  name = $thing_crowbar traits = { portable } }
thing = { id = lockpicks    in = ornate_box name = $thing_picks traits = { portable } }

# --- 9.4 Objects present in more than one place ------------ (spec §8.6) ---
# Presence is a SECOND relation, orthogonal to containment. These objects are
# not moved to the player's room on arrival; they are genuinely in several
# rooms, so no movement event fires, no save delta is written, and nothing has
# to be special-cased at each site that iterates room contents.

backdrop = {
    id         = the_sky
    name       = $thing_sky
    traits     = { scenery fixed_in_place }
    present_in = { antecourt }
}

# A query form, resolved at compile time into a concrete room set. `dynamic`
# would defer it to scope time and must be opted into explicitly.
backdrop = {
    id         = reactor_hum
    name       = $thing_hum
    traits     = { scenery fixed_in_place }
    present_in = { where = { in_sector = station_alpha }  dynamic = no }
}

# A two-sided object. State (`open`, `locked`) is declared on the object and
# stored ONCE, so the two sides cannot desynchronise; only presentation and
# direction are per-side facets. This door also spans two sectors, which makes
# it the transition trigger between them.
door = {
    id     = airlock_hatch
    traits = { openable lockable fixed_in_place }
    open   = no
    locked = yes

    side = { room = airlock
             direction   = out
             name        = $hatch_inner
             description = $hatch_inner_desc }

    side = { room = docking_gantry
             direction   = in
             name        = $hatch_outer }
}
thing = { id = blood_trail  in = corridor name = $thing_blood   traits = { fixed_in_place } }

# --- 9.3 People -------------------------------------------------------------

person = {
    id       = pc
    in       = your_cell
    name     = $person_pc
    traits   = { animate }
    strength = 12
    presence = 11
    combat_response = defend_self
}

person = {
    id       = companion_kira
    in       = wayfarer_bridge
    name     = $person_kira
    traits   = { animate }
    strength = 11
    presence = 14
    combat_response = join_ally
    portrait = "res/portraits/kira.png"
    portrait_mood = { angry = "res/portraits/kira_angry.png"
                      wry   = "res/portraits/kira_wry.png" }
    speaker_style = speaker_kira
}

person = {
    id       = quartermaster_vex
    in       = storage
    name     = $person_vex
    traits   = { animate }
    strength = 13
    presence = 12
    combat_response = observe
    portrait = "res/portraits/vex.png"
    portrait_mood = { angry = "res/portraits/vex_angry.png" }
    speaker_style = speaker_vex
}

person = {
    id       = station_janitor
    in       = corridor
    name     = $person_janitor
    traits   = { animate }
    # The deliberate joke: he does not care that people are fighting.
    # This is an authored choice, which is the point — the default is `flee`.
    combat_response = ignore
}

person = {
    id     = captain_reyes
    in     = command_bridge_stub
    name   = $person_reyes
    traits = { animate }
}

room = { id = command_bridge_stub  sector = station_alpha  name = $room_bridge
         exits = { out = control_room } }


# =============================================================================
#  10. ACTIONS                                            (spec §10, §11, §7.2)
# =============================================================================

action = {
    id = take
    # A list block of strings. `/` gives word-level alternatives;
    # `[something]` must resolve to an object in scope.
    match = { "get/take/grab [something]"
              "pick up [things]"
              "pick [things] up" }

    duration      = default
    advances_turn = on_success

    # Actions are a closed set, so their verb forms are enumerated rather than
    # derived. Five strings, exactly correct, and translated rather than
    # re-implemented for another language (spec §9.5.1).
    verb = { base = "take"  third = "takes"  past = "took"
             past_participle = "taken"  present_participle = "taking" }

    # Implicit AND, evaluated in source order, short-circuiting: the FIRST
    # failing condition supplies the message the player sees (spec §10.5.2).
    #
    # Each failureMsg sits on the NOT, not on the `carrying` block inside it.
    # A NOT fails when its contents SUCCEED, so the NOT is what failed; a
    # message on the inner block would describe the opposite of what happened
    # and would never be printed. The compiler rejects it (spec §10.5.1).
    restrictions = {
        # A localisation key: what a translator should see (spec §9.5).
        NOT = {
            carrying = { holder = actor  obj = noun }
            failureMsg = $already_holding
        }
        # An inline string: implicitly assigned a generated key at compile
        # time, so a game can be localised after the fact without the author
        # having restructured anything. Both forms are equally valid.
        NOT = {
            carrying = { holder = { of_class = person }  obj = noun }
            failureMsg = "I don't suppose [the HolderOf(noun)] would care for that."
        }
        NOT = {
            noun = { has_trait = fixed_in_place }
            failureMsg = $cant_take_fixed
        }
    }

    effects = {
        move = { obj = noun  to = actor  relation = carried }
    }

    successMsg = "Taken."
}

action = {
    id       = remove_from
    match    = { "take/get [something] from/off [something]"
                 "remove [something] from [something]" }
    restrictions = {
        containing = { holder = second  obj = noun }
        second = { open == yes  failureMsg = $closed_container }
    }
    effects    = { move = { obj = noun  to = actor  relation = carried } }
    successMsg = "You take [the noun] from [the second]."
}

action = {
    id       = open
    match    = { "open [something]"  "uncover/unwrap [something]" }
    verb     = { base = "open"  third = "opens"  past = "opened"
                 past_participle = "opened"  present_participle = "opening" }
    restrictions = {
        noun = { has_trait = openable  failureMsg = $not_openable }
    }
    effects    = { set = { target = noun  prop = open  value = yes } }
    successMsg = $opened_default
}

action = {
    id       = examine
    match    = { "x/examine/look at/read/check/describe [something]" }
    duration = 6                                # examining is quick
    effects  = { }                              # empty block: no world change
    successMsg = examine_message                # a text_or_script: names a script
}

# An out-of-world action: costs no time and runs no actor loop, so checking
# your inventory does not give the enemy a free swing.
action = {
    id            = inventory
    match         = { "i/inv/inventory"  "take inventory" }
    duration      = 0
    advances_turn = never
    effects       = { }
    successMsg    = inventory_message
}

action = {
    id       = pick_lock
    match    = { "pick [something] with [something preferably held]" }
    duration = 300                              # five minutes

    # Ordered most-specific first, since evaluation short-circuits and the
    # first failure is the one the player is told about.
    restrictions = {
        # A failing child of the implicit AND explains itself.
        noun = { has_trait = lockable  locked == yes  failureMsg = $not_locked }

        # An OR fails only when EVERY branch fails, so no single branch is
        # the reason and none of them can explain it. The message goes on
        # the OR itself; messages on the branches would be unreachable and
        # are a compile error (spec §10.5.1).
        OR = {
            carrying = { holder = actor  obj = lockpicks }
            carrying = { holder = actor  obj = plasma_cutter }
            failureMsg = $no_lockpicks
        }

        # Deliberately carries no message: it falls through to the action's
        # own failureMsg below (spec §10.5.3, step 4).
        actor = { presence >= 10 }
    }

    # The action-level fallback, covering any restriction that supplies no
    # message of its own. Without it, the `presence` check above would reach
    # the engine's generic string and the compiler would warn.
    failureMsg = $lock_too_fiddly

    effects = {
        set    = { target = noun  prop = locked  value = no }
        script = { fn = note_lock_picked }
    }
    successMsg = $lock_picked
}

# The broad-token pattern (spec §8.8.1). `drink [something]` parses anything,
# so DRINK LAPTOP produces a real in-world refusal rather than a parser error
# about a laptop the player can plainly see. The restriction then NARROWS the
# noun for everything after it, so `noun.volume_ml` below is statically legal
# without a second action, a class token, or a runtime check.
action = {
    id       = drink
    match    = { "drink/sip/swallow [something]" }
    verb     = { base = "drink"  third = "drinks"  past = "drank"
                 past_participle = "drunk"  present_participle = "drinking" }
    restrictions = {
        noun = { has_trait = drinkable
                 failureMsg = "[The noun] [is noun] not something you can drink." }
    }
    effects = {
        add_global       = { id = hydration  amount = noun.volume_ml }
        remove_from_play = { obj = noun }
    }
    successMsg = "You drink [the noun]."
}

# Where behaviour genuinely differs by class, that is a rule's job — not a
# second action with a narrower grammar token.
rule = {
    of_action  = drink
    when       = { noun = { has_trait = alcoholic } }
    effects    = @after { add_global = { id = intoxication  amount = 1 } }
    successMsg = @after "It burns pleasantly on the way down."
}

action = {
    id       = talk_to
    match    = { "talk to [someone]"  "greet [someone]" }
    duration = 0
    effects  = { enter_dialogue = { dialogue = auto  on_exit = resume } }
}


# =============================================================================
#  11. RULES                                              (spec §5.4, §11.2, §12.1)
# =============================================================================

# --- 11.1 The classic: taking a hot potato requires gloves ------------------

rule = {
    of_action = take
    when      = { noun = { of_class = thing  has_trait = hot_potato } }

    # An empty conditions block explicitly overrides (and so ignores) any
    # inherited conditions. See spec §5.4.2.
    conditions = { }

    # A non-empty restrictions block defaults to @after — evaluated in
    # addition to, and after, the action's own restrictions.
    restrictions = {
        wearing = {
            holder = actor
            obj    = firefighter_gloves
            failureMsg = "Taking [the noun] seems painful enough that "
                         "you don't dare attempt it."
        }
    }

    effects    = inherit                # do not change the action's effects
    successMsg = @before "You reach a gloved hand into the embers."
}

# --- 11.2 Action redirection (spec §11.2) -----------------------------------
# TAKE APPLE, when the apple is in a box, becomes REMOVE APPLE FROM BOX.

rule = {
    of_action = take
    when      = { noun = { has_trait = portable } }
    conditions = { noun = { held_by = { of_class = container } } }
    effects = @override {
        try_action = {
            action     = remove_from
            noun       = noun
            second     = HolderOf(noun)
            on_failure = abort            # abort | continue | succeed
            report     = yes              # yes | no | only_on_failure
            inherit_duration = no         # one round total, outer duration
        }
    }
}

# --- 11.3 Entering dialogue from an ordinary action (spec §11.3) ------------
# Examining the console drops the player into a "conversation" with it.

rule = {
    of_action = examine
    when      = { noun = { is = reactor_console } }
    effects = @override {
        enter_dialogue = {
            dialogue = reactor_console_interface
            node     = main_menu
            on_exit  = resume
        }
    }
}

# --- 11.3b Property access and narrowing ------------------- (spec §8.8) ---
# `noun.damage` is meaningless on a sandwich, so a read must be statically
# justified. Here `when` narrows noun to `weapon`, which makes the read legal
# in every later stage of the rule.

rule = {
    of_action  = examine
    when       = { noun = { of_class = weapon } }
    successMsg = "It is rated for [noun.damage] damage at [noun.range] m."
}

# Where narrowing is not available, `has_prop` is both a runtime test and a
# narrowing operator: the read after it is legal because the test guarantees it.
rule = {
    of_action = attack
    when      = { }
    restrictions = {
        second = { has_prop = damage
                   damage > 3
                   failureMsg = $too_feeble }
    }
}

# --- 11.3c Globals and collections ------------------ (spec §6.4, §6.5) ---

rule = {
    of_action = go
    when      = { noun = { is = antecourt } }
    conditions = {
        global = { alert_level == high }
        NOT    = { flag_set = captain_found }
    }
    effects = {
        add_global = { id = times_caught  amount = 1 }
        set_global = { id = last_accused  value = quartermaster_vex }
        list_add   = { collection = seen_endings  value = caught_ending }
        set_flag   = coolant_unlocked
    }
}

# --- 11.3d Testing a computed value ---------------------- (spec §10.6) ---
# Everything else in the condition language tests a value that already exists.
# `count_of` and `at` COMPUTE one, and `Key Op Value` has nowhere to put the
# result — `count_of = { ... } >= 2` has two operators and is ungrammatical.
# `compare` is the one place a computed value is produced and then tested.

rule = {
    of_action = go
    when      = { noun = { is = airlock } }
    conditions = {
        # One reader, then one or more `value` tests, implicitly ANDed.
        # A BARE name in an argument position is always a global (spec §6.6.1).
        compare = {
            count_of = seen_endings
            value >= 1
            value <= 3
        }
        # A DOTTED PATH is an object's property. The two forms are
        # distinguished syntactically, so an argument never changes meaning
        # depending on which block encloses it.
        compare = {
            count_of = navcomp.waypoints
            value >= 2
        }
        compare = {
            at    = { collection = navcomp.waypoints  index = 0 }
            value == docking_gantry
        }
    }
    effects = { set_flag = saw_the_manifest }
}

# Collection membership: `includes`, not `contains` — §10.4 already has
# `containing` for PHYSICAL containment, and the two are unrelated.
rule = {
    of_action = examine
    when      = { noun = { is = navcomp } }
    conditions = {
        includes = { collection = navcomp.waypoints  value = storage }
        NOT      = { is_empty = seen_endings }
    }
    effects = {
        list_remove = { collection = navcomp.waypoints  index = 0 }
        list_add    = { collection = seen_endings       value = navcomp_ending }
    }
}

rule = {
    of_action = attack
    when      = { }
    restrictions = {
        # A defaulting read: absence is acceptable here and yields 0.
        compare = {
            value_or = { datum = noun.damage  default = 0 }
            value > 3
            failureMsg = $too_feeble
        }
    }
}

rule = {
    of_action  = examine
    when       = { noun = { is = reactor_console } }
    conditions = {
        compare = {
            value_of = core_temp
            value >= max_reactor_temp        # operand may be a const
        }
    }
    effects = { set_global = { id = alert_level  value = elevated } }
}

# --- 11.3e Map access ------------------------------ (spec §6.6.1, §6.6.2) ---
# A segment following a map-typed segment is a KEY, not a property name. Here
# `exits` is map<direction, ref<room>>, so `north` is a key and the path yields
# a room reference. `location` is the acting actor's current room.

rule = {
    of_action  = examine
    when       = { noun = { is = blood_trail } }
    conditions = { location = { exits.north == corridor } }
    effects    = { set_flag = heard_vex_slip }
}

# A missing KEY yields `none` rather than raising, because "this room has no
# north exit" is ordinary rather than a defect. A missing PROPERTY still
# raises — the two look alike and differ deliberately (spec §6.6.2).
rule = {
    of_action  = go
    when       = { }
    conditions = { location = { exits.north == none } }
    effects    = { add_global = { id = times_caught  amount = 1 } }
}

# `includes` takes EITHER a value or a key — one question over two domains,
# with the argument name saying which (spec §6.5.1). The `key` form tests
# presence without reading, which is what to use before chaining, since
# `location.exits.north.name` raises if there is no north exit.
rule = {
    of_action  = look
    when       = { }
    conditions = {
        includes = { collection = location.exits  key = north }
        includes = { collection = npc_moods       value = hostile }
    }
    successMsg = @after "Something moves in the corridor to the north."
}

# `map_get` remains for keys that are COMPUTED, or whose type cannot be
# written as an identifier. `a.b` is just sugar for it.
rule = {
    of_action = go
    when      = { }
    conditions = {
        compare = {
            map_get = { collection = location.exits  key = south }
            value == your_cell
        }
    }
    effects = { set_flag = saw_the_manifest }
}

# --- 11.4 A fully scripted rule (spec §12.1) -------------------------------
# `when` stays declarative so the dispatch index can be built from it;
# everything else is one function returning ok / fail / pass.

rule = {
    of_action = take
    when      = { noun = { has_trait = cursed } }
    script    = handle_cursed_take
}

# --- 11.5 Annotations: priority, phase, debug, platform --------------------

rule = {
    of_action    = open
    when         = { noun = { has_trait = lockable } }
    restrictions = @before {
        noun = { locked == no  failureMsg = $its_locked }
    }
    effects   = inherit
    successMsg = inherit
}

rule = {
    of_action = go
    when      = { }
    conditions = @priority(50) { actor = { in_combat == yes } }
    restrictions = {
        script = { fn = can_disengage  failureMsg = $cant_flee_combat }
    }
}

# Development-only: stripped entirely from a release build.
rule = @debug {
    of_action = examine
    when      = { }
    # \[ and \] escape the template delimiters (spec §3.5, §9.1).
    successMsg = @after "\[debug: [DebugId(noun)]\]"
}

# Frontend-specific: a text-only fallback where there is no tooltip.
rule = @platform(glk, cli) {
    of_action  = examine
    when       = { noun = { of_class = weapon } }
    successMsg = @after "([DamageString(noun)], range [noun.range])"
}


# =============================================================================
#  12. TURN HOOKS                                         (proposal §7.1, §9.3)
# =============================================================================
# The turn is an initiative-ordered loop over actors. A ruleset registers at
# named points rather than replacing the sequence.

turn_hook = { id = ss_upkeep      phase = upkeep      priority = 100
              script = ss_tick_effects }

turn_hook = { id = ss_initiative  phase = initiative  priority = 100
              conditions = { any_actor = { in_combat == yes } }
              script = ss_roll_initiative }

turn_hook = { id = ss_reaction    phase = reaction    priority = 100
              script = ss_offer_reactions }

turn_hook = { id = ss_round_end   phase = round_end   priority = 100
              script = ss_end_of_round }

# The default combat response, dispatching on the property declared in §6.1.
turn_hook = { id = ss_combat_response  phase = round_start  priority = 90
              script = ss_dispatch_combat_response }


# =============================================================================
#  13. QUESTS                                                (proposal §9.4)
# =============================================================================

quest = {
    id       = find_the_captain
    title    = $q_find_captain_title
    summary  = $q_find_captain_summary
    category = main                       # main | side | faction | hidden

    stage = {
        id   = search_bridge
        text = $q_find_captain_s1
        complete_when = { visited = command_bridge_stub }
    }
    stage = {
        id   = follow_blood_trail
        text = $q_find_captain_s2
        complete_when = {
            OR = {
                examined = blood_trail
                script   = { fn = player_asked_about_captain }
            }
        }
    }
    stage = {
        id   = confront
        text = $q_find_captain_s3
        complete_when = { quest_flag = captain_confronted }
    }

    on_complete = {
        effects = {
            add      = { target = player  prop = xp  amount = 250 }
            set_flag = captain_found
        }
    }
    on_fail = {
        conditions = { dead = captain_reyes }
    }
}

# A hidden quest: tracked, but absent from the journal until revealed.
quest = {
    id       = the_real_employer
    title    = $q_employer_title
    summary  = $q_employer_summary
    category = hidden
    stage = { id = suspicion  text = $q_employer_s1
              complete_when = { COUNT_AT_LEAST = {
                  n = 2
                  flag_set = saw_the_manifest
                  flag_set = heard_vex_slip
                  examined = captains_log } } }
}


# =============================================================================
#  14. DIALOGUE                                       (proposal §11, §11.1.1)
# =============================================================================

dialogue = {
    id = vex_first_meeting

    # Everyone who can speak or be addressed. Participants may be in any
    # loaded sector, so a radio conversation uses the same construct.
    participant = { actor = quartermaster_vex  role = primary }
    participant = { actor = pc                 role = player }
    participant = { actor = companion_kira     role = companion  optional = yes }

    # Entry rules are tried in order; the first whose conditions hold wins.
    entry = { node = greeting  conditions = { NOT = { met = quartermaster_vex } } }
    entry = { node = greeting_familiar }

    node = {
        id      = greeting
        speaker = quartermaster_vex
        text    = $vex_greeting
        on_enter = { effects = { set_met = quartermaster_vex } }

        choice = { id   = ask_about_station
                   text = $vex_c_station
                   goto = station_info }

        choice = { id   = ask_about_captain
                   text = $vex_c_captain
                   conditions = { quest_state = { quest = find_the_captain
                                                  state = active } }
                   goto = captain_topic }

        # A skill check, resolved by the loaded ruleset. `show_difficulty`
        # gives the player the odds, cRPG-style.
        choice = { id   = intimidate
                   text = $vex_c_intimidate
                   check = { stat = presence  difficulty = 14 }
                   show_difficulty = yes
                   on_success = { goto = vex_cowed }
                   on_failure = { goto = vex_offended
                                  effects = { adjust_attitude = {
                                      npc = quartermaster_vex  amount = -20 } } } }

        # A line spoken by the companion rather than the PC. Still the
        # player's choice to deploy her, but it is her voice.
        choice = { id      = kira_vouches
                   speaker = companion_kira
                   text    = $kira_c_vouch
                   conditions = {
                       present  = companion_kira
                       attitude = { npc = companion_kira  toward = pc  at_least = 40 }
                   }
                   goto = vex_persuaded }

        choice = { id = leave  text = $c_leave  goto = END }
    }

    node = {
        id      = greeting_familiar
        speaker = quartermaster_vex
        text    = $vex_greeting_again
        choice  = { id = ask_about_station  text = $vex_c_station  goto = station_info }
        choice  = { id = leave              text = $c_leave        goto = END }
    }

    node = {
        id      = station_info
        speaker = quartermaster_vex
        text    = $vex_station_info
        once    = yes                     # greys out after use

        # An unprompted third-party line. `joins = yes` promotes Kira to an
        # active participant for the rest of the conversation, which is what
        # distinguishes an interjection from a bark.
        interjection = {
            speaker = companion_kira
            text    = $kira_i_station
            conditions = { present = companion_kira
                           NOT = { said_before = kira_i_station } }
            chance = 60
            joins  = yes
        }

        goto = greeting                   # hub-and-spoke
    }

    node = { id = captain_topic   speaker = quartermaster_vex
             text = $vex_captain_topic
             on_enter = { effects = { set_flag = heard_vex_slip } }
             goto = greeting }

    node = { id = vex_cowed       speaker = quartermaster_vex
             text = $vex_cowed     goto = greeting }

    node = { id = vex_offended    speaker = quartermaster_vex
             text = $vex_offended  goto = END }

    node = { id = vex_persuaded   speaker = quartermaster_vex
             text = $vex_persuaded goto = greeting }
}

# A "conversation" with an object, entered from the examine rule in §11.3.
# `speaker = none` would give a pure narrator voice; here the console has a
# speaker_name and portrait of its own.
dialogue = {
    id = reactor_console_interface

    participant = { actor = reactor_console  role = primary }
    participant = { actor = pc               role = player }

    entry = { node = main_menu }

    node = {
        id      = main_menu
        speaker = reactor_console
        text    = $console_main
        choice  = { id = read_status   text = $console_c_status   goto = status }
        choice  = { id = vent_coolant  text = $console_c_vent
                    conditions = { flag_set = coolant_unlocked }
                    goto = vent }
        choice  = { id = disconnect    text = $console_c_leave    goto = END }
    }

    node = { id = status  speaker = reactor_console  text = $console_status
             goto = main_menu }

    node = { id = vent    speaker = reactor_console  text = $console_vent
             on_enter = { effects = {
                 set_flag = coolant_vented
                 trigger  = { event = alarm_raised  in_sector = station_alpha } } }
             goto = END }
}


# =============================================================================
#  15. NPC BEHAVIOUR                                       (proposal §10.1)
# =============================================================================

# --- 15.1 Goals: multi-round plans, pushed by interrupts or schedules -------

goal_def = {
    id     = investigate
    params = { target urgency }

    step = { move_to = target  on_blocked = abandon }
    step = { do_action = { action = look } }
    step = { script = evaluate_findings }

    abandon_when = { OR = { in_combat == yes  urgency < 1 } }
}

goal_def = {
    id     = follow
    params = { target distance }
    step   = { move_to = target  on_blocked = wait }
    repeat = yes
}

# --- 15.2 Interrupts: event-driven reactions --------------------------------

rule = {
    of_event = alarm_raised
    when     = { self = { has_trait = animate } }
    conditions = { self = { in_sector = station_alpha } }
    effects  = {
        script = { fn = push_investigate_goal }
    }
}

# --- 15.3 Schedules: time-of-day defaults, catch-up-able --------------------

schedule = {
    of_npc = quartermaster_vex
    entry = { from = "06:00" to = "07:00"  location = your_cell     activity = idle_morning }
    entry = { from = "07:00" to = "12:00"  location = storage       activity = working }
    entry = { from = "12:00" to = "13:00"  location = front_office  activity = eating }
    entry = { from = "13:00" to = "19:00"  location = storage       activity = working }
    entry = { from = "19:00" to = "23:00"  location = corridor      activity = drinking
              conditions = { NOT = { faction_alert = high } } }
    entry = { from = "23:00" to = "06:00"  location = your_cell     activity = sleeping }
}

schedule = {
    of_npc = station_janitor
    entry = { from = "08:00" to = "18:00"  location = corridor  activity = mopping }
    entry = { from = "18:00" to = "08:00"  location = storage   activity = sleeping }
}

# --- 15.4 Barks: fire-and-forget lines, distinct from interjections ---------
# A bark changes nothing. An interjection changes the participant set.

bark_table = {
    of_npc = station_janitor
    line = { text = $janitor_bark_1  weight = 3 }
    line = { text = $janitor_bark_2  weight = 2
             conditions = { any_actor = { in_combat == yes } } }
    line = { text = $janitor_bark_3  weight = 1  once = yes }
}


# =============================================================================
#  16. THE PARTY                                            (proposal §9.5)
# =============================================================================

party = {
    id        = player_party
    viewpoint = pc                        # whose senses drive scope and description
    max_size  = 4

    member = { actor = pc  role = leader }

    member = {
        actor = companion_kira
        role  = companion
        # Control is per-member and per-context, and is separate from
        # viewpoint — so player-controlled companions do not require a
        # switchable viewpoint.
        control = { in_combat = player  out_of_combat = ai }
        follow  = { target = viewpoint  distance = same_room }
    }
}


# =============================================================================
#  16b. THE LEXICON                                            (spec §9.5.2)
# =============================================================================
# The core knows nothing about English. A language pack infers word forms from
# rules; this block overrides only the inferences that are wrong. Starforge
# reports every form it had to infer, so the guesses are reviewable and the
# rule table can never quietly become the specification (spec §9.5.4).

lexicon = {
    lang = en

    noun = { base = "knife"   plural = "knives" }
    noun = { base = "sheep"   plural = "sheep" }
    noun = { base = "hour"    article = "an" }     # phonetic, not orthographic
    noun = { base = "unicorn" article = "a" }      # ...and the converse

    # A verb used in author-written messages, outside the closed action set.
    verb = { base = "polish"  third = "polishes"  past = "polished"
             past_participle = "polished"  present_participle = "polishing" }
}


# =============================================================================
#  17. LOCALISATION                                             (spec §9.6)
# =============================================================================
# Inline strings elsewhere in this file are implicitly assigned generated keys
# at compile time, so a game can be localised after the fact. Keys that a
# translator should see are written explicitly here.

loc = {
    lang = en

    fmt_stardate       = "Stardate [stardate(now)]"

    # Rooms
    room_your_cell     = "Holding Cell"
    room_corridor      = "Spinal Corridor"
    room_control_room  = "Control Room"
    room_front_office  = "Front Office"
    room_storage       = "Storage Bay"
    room_antecourt     = "Antecourt"
    room_airlock       = "Airlock"
    room_gantry        = "Docking Gantry"
    room_wayfarer_bridge = "Wayfarer — Bridge"
    room_bridge        = "Command Bridge"

    # Sectors
    sector_station_alpha = "Kepler Station"
    sector_docking_ring  = "Docking Ring"
    sector_wayfarer      = "SS Wayfarer"
    sector_surface       = "Kepler IV — Surface"

    # Things
    thing_ornate_box   = "ornate box"
    thing_brass_key    = "brass key"
    thing_mess_table   = "mess table"
    thing_mug          = "tarnished mug"
    thing_console      = "reactor console"
    thing_access_panel = "access panel"
    thing_potato       = "baked potato"
    thing_gloves       = "firefighter's gloves"
    thing_cutter       = "plasma cutter"
    thing_ration       = "ration pack"
    thing_log          = "captain's log"
    thing_crowbar      = "crowbar"
    thing_picks        = "set of lockpicks"
    thing_plain_box    = "box"
    thing_navcomp      = "navigation computer"
    thing_flask        = "water flask"
    thing_whisky       = "bottle of whisky"
    too_feeble         = "That would barely scratch it."
    g_alert_level_doc  = "Station-wide alert state. Drives NPC schedules and "
                         "the quartermaster's willingness to talk."
    thing_sky          = "sky"
    thing_hum          = "reactor hum"
    hatch_inner        = "inner hatch"
    hatch_outer        = "outer hatch"
    hatch_inner_desc   = "A blast hatch, scuffed where a thousand boots have "
                         "crossed it."
    thing_blood        = "blood trail"

    # People
    person_pc          = "yourself"
    person_kira        = "Kira"
    person_vex         = "Quartermaster Vex"
    person_janitor     = "the janitor"
    person_reyes       = "Captain Reyes"
    console_speaker_name = "REACTOR CONSOLE"

    # Messages
    already_open       = "It is already open."
    opened_default     = "You open [the noun]."
    already_holding    = "You are already holding [the noun]."
    cant_take_fixed    = "[The noun] [is noun] fixed in place."
    closed_container   = "[The second] is closed."
    not_openable       = "[The noun] [is noun] not something you can open."
    its_locked         = "It's locked."
    not_locked         = "That isn't locked."
    no_lockpicks       = "You have nothing to pick a lock with."
    lock_too_fiddly    = "Your hands aren't steady enough for work this fine."
    lock_picked        = "The lock yields with a small, satisfying click."

    # Adaptive text: one template, correct for any actor (spec §9.5.3).
    #   player -> "You take the brass key."
    #   NPC    -> "Vex takes the brass key."
    took_it            = "[The actor] [verb(actor, take)] [the noun]."
    polished_it        = "[The actor] [verb(actor, polish)] [the noun] "
                         "with [possessive(actor)] sleeve."

    # The last resort in the failureMsg fallback chain (spec §10.5.3, step 5).
    # It is a loc key rather than a hard-coded string so a project can reword
    # it once — but reaching it in play means a restriction is missing a
    # message, which the compiler warns about.
    action_blocked_default = "You can't do that right now."
    cant_flee_combat   = "Not while [ThreatName()] has your attention."
    c_leave            = "(Leave.)"

    # A conditional fragment and a plural, in one template.
    inventory_header   = "You are carrying [number(count)] "
                         "[plural(count, \"item\", \"items\")]"
                         "[if carrying_nothing] — that is, nothing at all.[end]"

    # Quests
    q_find_captain_title   = "Find the Captain"
    q_find_captain_summary = "Reyes was aboard when the alarm sounded. Nobody has seen her since."
    q_find_captain_s1      = "Search the command bridge."
    q_find_captain_s2      = "Follow the blood trail."
    q_find_captain_s3      = "Confront what you find."
    q_employer_title       = "The Real Employer"
    q_employer_summary     = "Something about this contract does not add up."
    q_employer_s1          = "Gather what does not fit."

    # Dialogue
    vex_greeting       = "Vex looks up. \"You're the one from the cell. Walking around.\""
    vex_greeting_again = "\"You again.\""
    vex_c_station      = "\"What happened here?\""
    vex_c_captain      = "\"Where is Captain Reyes?\""
    vex_c_intimidate   = "\"You're going to tell me what I want to know.\""
    vex_station_info   = "\"Reactor scrammed. Then the doors locked. Then it got quiet.\""
    vex_captain_topic  = "\"Reyes? She went aft. That's all I — that's all I saw.\""
    vex_cowed          = "\"All right! All right.\""
    vex_offended       = "\"Get out of my bay.\""
    vex_persuaded      = "Vex glances at Kira, then relents. \"Fine. Fine.\""
    kira_c_vouch       = "Kira: \"He's with me, Vex.\""
    kira_i_station     = "Kira, quietly: \"He's leaving something out.\""

    console_main       = "REACTOR CONSOLE — DIAGNOSTIC MODE"
    console_c_status   = "Query core status."
    console_c_vent     = "Vent coolant to the ring."
    console_c_leave    = "Disconnect."
    console_status     = "CORE: SCRAMMED. COOLANT: NOMINAL. CREW: 1 REGISTERED ABOARD."
    console_vent       = "VENTING. STAND CLEAR."

    janitor_bark_1     = "The janitor works his mop in slow figure-eights."
    janitor_bark_2     = "The janitor sighs at the spreading stain and reaches for a second bucket."
    janitor_bark_3     = "\"Twenty-two years,\" the janitor says, to nobody. \"Twenty-two.\""

    # Schema documentation
    schema_loot_table_doc = "A weighted table of items produced when a container is looted."
    lib_starscape_name    = "Starscape"
}


# =============================================================================
#  18. SYNTAX CORNERS                                    (spec §3, §5, §6)
# =============================================================================
# Constructs with no natural home in a scenario, gathered so the corpus covers
# the whole specification.

# --- 18.1 Every operator ----------------------------------------------------

class_extension = {
    of_class = thing
    prop_def = { synonyms = list<identifier>  notes = string }

    synonyms  = { thing object }        # `=`  bind
    synonyms += { item }                # `+=` extend
    synonyms -= { object }              # `-=` reduce
    notes      = "plainly bound"        # `?=` was removed; see spec §6.3.1
}

# Comparison operators appear only in condition contexts.
rule = {
    of_action = take
    when      = { noun = { weight > 0.000 } }
    conditions = {
        actor = {
            strength >= 10
            strength <= 30
            presence != 0
        }
        noun = { weight < 50.000 }
        second = { open == yes }
    }
    restrictions = { }                  # empty block: explicit override
}

# --- 18.2 Every scalar kind -------------------------------------------------

class_extension = {
    of_class = thing
    prop_def = {
        an_identifier = identifier
        an_integer    = int
        a_decimal     = decimal
        a_float       = float
        a_string      = string
        a_text        = text
        a_bool        = bool
        a_ref         = ref<thing>
        an_enum       = enum<condition_enum>
        a_resource    = resource
        a_clock_time  = clock_time
        a_duration    = duration
        a_dice        = dice
        a_script      = script
        a_flag_set    = flags<damage_type_enum>
        a_list        = list<int>
        a_set         = set<identifier>
        a_map         = map<identifier, int>
    }

    an_identifier = some_symbol
    an_integer    = -42
    a_decimal     = -3.750
    a_float       = 0.333
    a_string      = "raw, never localised or interpolated"
    a_text        = $flavour_text_key
    a_bool        = yes
    a_ref         = none                # `none` clears a reference
    an_enum       = vacuum
    a_resource    = "res/audio/hum.ogg"
    a_clock_time  = "18:45:30"
    a_duration    = 90
    a_dice        = "1d20+4"
    a_script      = some_function_name
    a_flag_set    = { kinetic corrosive }
    a_list        = { 1 2 3 3 }         # duplicates permitted in a list
    a_set         = { alpha beta gamma }
    a_map         = { alpha = 1  beta = 2 }
}

# --- 18.3 Adjacent-literal concatenation, and every escape ------------------

loc = {
    lang = en

    flavour_text_key = "Concatenated across lines, "
                       "with no separator inserted between the parts."

    escapes_demo = "A quote: \" · a backslash: \\ · a newline: \n"
                   "a tab: \t · a bracket: \[ and \] · a dollar: \$ · an at: \@"
                   " · a unicode escape: é"

    # `#` inside a string is an ordinary character, not a comment.
    hash_demo = "Cell block #4 — not a comment."

    # A template with a conditional fragment, an else branch, and an
    # explicit tooltip span over text that is not a name.
    conditional_demo = "[if is_dark]You can't see much."
                       "[else]The bay is lit by a "
                       "[tip(plasma_cutter)]guttering blue glare[end]."
                       "[end]"
}

# --- 18.4 A key written as a string ----------------------------------------
# Permitted where the schema allows it, for keys needing characters outside
# the identifier set.

loc = {
    lang = en
    "key.with.unusual-characters" = "Legal, if rarely wanted."
}

# --- 18.5 `inherit`, and combination annotations ---------------------------

rule = {
    of_action    = examine
    when         = { noun = { of_class = weapon } }
    conditions   = inherit              # identical to omitting the statement
    restrictions = inherit
    effects      = inherit
    successMsg   = @after "It is heavier than it looks."
}

rule = {
    of_action  = examine
    when       = { noun = { of_class = armor } }
    successMsg = @before "You turn it over in your hands."
}

class_extension = {
    of_class = weapon
    prop_def = @merge { serial_number = string }
}

class_extension = {
    of_class = thing
    synonyms = @remove { thing }
}

# --- 18.6 Library metadata --------------------------------------------------
# Normally in the library's own tree; included here so the corpus covers it.

library = {
    id           = starscape
    version      = "1.0.0"
    display_name = $lib_starscape_name
    requires     = { stdlib >= "1.0.0" }
    uses_editor_feature = { rpg quests dialogue }
    provides_schema     = { stat_block combat_style loot_table }
}

# --- 18.7 An empty file is legal; so is an empty block ---------------------

action = {
    id       = wait
    match    = { "z/wait" }
    effects  = { }
    successMsg = "Time passes."
}

# --- 18.8 Superseding a declaration ------------------------ (spec §7.6) ---
# No declaration may be duplicated. `@replaces` is the deliberate form, and
# naming the SOURCE is what makes it useful: if the stdlib stopped shipping
# a `wait` action, or renamed it, this becomes a build failure rather than a
# second declaration that silently never takes effect.
#
# Replacement is total — nothing is merged from the original. Merging is what
# class_extension and schema_extension are for.
action = @replaces(stdlib) {
    id         = wait
    match      = { "wait for/until [text]" }
    effects    = { }
    successMsg = "You wait."
}

# =============================================================================
#  End of tour.star
# =============================================================================
