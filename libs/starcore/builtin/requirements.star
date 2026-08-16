# =============================================================================
#  What starcore depends on — spec §7.2.2
# =============================================================================
#
#  > If `starcore` requires something to be defined a particular way, it MUST
#  > assert that requirement rather than assume it will be met.
#
#  This file is that assertion, and it is the whole content of the anti-wart
#  rule. Every requirement has an id, so a failure is a named diagnostic at
#  load time rather than something that surfaces much later as a crash, or —
#  worse — as behaviour nobody can account for.
#
#  Requirements are checked against whatever has been loaded, so this file
#  guards the built-in set against its own edits as much as against a
#  library's. Delete a property from object.star and the build fails here,
#  by name, rather than in Phase 1 when something reads it.
#
#  `requires` takes one of:
#
#      form      a schema with this id exists, and is sealed
#      class     a class with this id exists, and is sealed
#      trait     a trait with this id exists, and is sealed
#      property  `subject` declares `member`, at `type` if one is given
#      parent    `subject` derives from `member`
# =============================================================================


# --- The forms of §7.2.4's first table ---------------------------------------

core_requirement = { id = form_class
                     requires = form  subject = class
                     doc = "The schema layer dispatches on the class of an object." }

core_requirement = { id = form_class_extension
                     requires = form  subject = class_extension
                     doc = "Adding to a core class is how a library extends the world model." }

core_requirement = { id = form_trait
                     requires = form  subject = trait
                     doc = "has_trait is a bitmask test in the hot path; the trait table is core's." }

core_requirement = { id = form_enum
                     requires = form  subject = enum
                     doc = "Enums are a declared type, so the schema layer resolves them itself." }

core_requirement = { id = form_global
                     requires = form  subject = global
                     doc = "Globals are the save-state layout (§6.4)." }

core_requirement = { id = form_const
                     requires = form  subject = const
                     doc = "Constants are folded at compile time and never enter the save state." }

core_requirement = { id = form_action
                     requires = form  subject = action
                     doc = "The turn sequence dispatches on actions." }

core_requirement = { id = form_rule
                     requires = form  subject = rule
                     doc = "Rules are indexed by action at build time; the index is core's." }

core_requirement = { id = form_turn_hook
                     requires = form  subject = turn_hook
                     doc = "Turn hooks run at fixed points core owns." }

core_requirement = { id = form_sector
                     requires = form  subject = sector
                     doc = "Sectors are the unit of residency and streaming." }

core_requirement = { id = form_project
                     requires = form  subject = project
                     doc = "The manifest determines load order (§13.2)." }

core_requirement = { id = form_library
                     requires = form  subject = library
                     doc = "Libraries are loaded in the order the manifest names them." }


# --- The root class, and the six slots of §8.1.1 -----------------------------

core_requirement = { id = class_object
                     requires = class  subject = starcore.object
                     doc = "Every world object is one. There is no way to declare a class outside this hierarchy." }

core_requirement = { id = object_holder
                     requires = property  subject = starcore.object  member = holder
                     type = ref<starcore.object>
                     doc = "The containment parent. held_by, carrying and containing are implemented against this slot." }

core_requirement = { id = object_relation
                     requires = property  subject = starcore.object  member = relation
                     type = enum<relation_enum>
                     doc = "How the parent link is held. The placement sugar of §8.5 expands to this slot and holder." }

core_requirement = { id = object_sector
                     requires = property  subject = starcore.object  member = sector
                     type = ref<sector>
                     doc = "Residency. The streaming system reads this directly." }

core_requirement = { id = object_present_in
                     requires = property  subject = starcore.object  member = present_in
                     type = set<ref<starcore.room>>
                     doc = "Presence, which is how a door is referable from both rooms it joins (§8.6)." }

core_requirement = { id = object_name
                     requires = property  subject = starcore.object  member = name
                     type = text
                     doc = "What the parser matches and the templates print." }

core_requirement = { id = object_synonyms
                     requires = property  subject = starcore.object  member = synonyms
                     type = list<identifier>
                     doc = "Additional parser names." }


# --- The two concepts an IF system is never without --------------------------
#
#  §7.2.4 marks the membership of this group [OPEN]: a narrower reading would
#  make both markers instead. They are asserted here rather than assumed
#  precisely so that the question stays answerable — if these move to markers,
#  this file is where the change is visible.

core_requirement = { id = class_room
                     requires = class  subject = starcore.room
                     doc = "Scope is computed from an actor's room, and the location slot resolves to one." }

core_requirement = { id = room_parent
                     requires = parent  subject = starcore.room  member = starcore.object
                     doc = "A room is an object; scope and containment both depend on it being one." }

core_requirement = { id = trait_actor
                     requires = trait  subject = starcore.actor
                     doc = "The actor loop iterates these, and busy_until lives on them." }

core_requirement = { id = actor_busy_until
                     requires = property  subject = starcore.actor  member = busy_until
                     type = int
                     doc = "When this actor is next free to act. The turn loop reads it every turn." }

core_requirement = { id = form_schema_extension
                     requires = form  subject = schema_extension
                     doc = "Adding keys to a form is the mechanism a library extends the schema layer with (§7.5)." }
