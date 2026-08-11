# EXPECT E-BLOCK-MIXED
# spec §8.6.2 rule 1 — an object present in several rooms may not be portable,
# because there is no sensible answer to where it went when taken. That rule
# needs the schema layer, so the checker cannot see it yet; this fixture pins
# the syntax it will be reported against and fails today on a shape error.
backdrop = {
    id         = takeable_sky
    traits     = { scenery portable }
    present_in = { antecourt  observation_deck = yes }
}
