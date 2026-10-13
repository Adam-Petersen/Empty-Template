#include "types.h"
#include "divmod.h"

// Var functions
extern u16 load_var_value(u16 var_id);
extern u8 var_set_value(u16 var_id, u16 value);
extern u8 get_flag(u16 flag);
extern u16 randomNumber();
void givepokemon(u16 species, u8 level, u16 item);
void calculate_mon_stats(const u8* mon);
void heal_player_party();

u16 random(u16 count) {
    return randomNumber() % count;
}

// Party data
// The data for pokemon in the party is 100 bytes each, and thankfully the data substructure isn't encrypted since it's in RAM
const u8 *PLAYER_PARTY_PTR = (u8*) 0x02024284;
const u16 PARTY_MON_SIZE = 100;
const u16 PARTY_MON_SPECIES_OFFSET = 32;
const u16 PARTY_MON_MOVESET_OFFSET = 44;
const u16 PARTY_MON_EXPERIENCE_OFFSET = 36;
const u16 PARTY_MON_LEVEL_OFFSET = 84;

// Pokemon base data
// Each species has a block of 28 bytes, the growth rate is at offset 19
const u8 *POKEMON_BASE_DATA_PTR = (u8*) 0x099778E0;
const u16 POKEMON_BASE_DATA_SIZE = 28;
const u16 POKEMON_BASE_DATA_GROWTH_RATE_OFFSET = 19;

// Level up tables data
// There are 6 level up tables corresponding to growth rates 0-5, each has 256 entries of 4 bytes
const u8 *LEVEL_UP_TABLES_PTR = (u8*) 0x094C41FC;
const u16 LEVEL_UP_TABLE_SIZE = 1024;

// Pokemon moves data
const u8 *POKEMON_LEVELUP_MOVES_PTR = (u8*) 0x099B9E14;
const u16 POKEMON_LEVELUP_MOVE_ENTRY_SIZE = 4;

// Randomizer table
const u8* RANDOMIZER_TABLE_PTR = (u8*) 0x083759B0;
const u16 RANDOMIZER_TABLE_COUNT = 942;
const u8 RANDOMIZER_TABLE_ENTRY_SIZE = 8;
const u8 RANDOMIZER_GEN_OFFSET = 6;
const u8 RANDOMIZER_LEGENDARY_OFFSET = 4;
const u8 RANDOMIZER_TYPE1_OFFSET = 2;
const u8 RANDOMIZER_TYPE2_OFFSET = 3;
const u8 RANDOMIZER_IS_TRASH_OFFSET = 5;

u32* debug_xp_pointer = (u32*) 0x020242A8;

// GBA can't read halfwords starting at an odd address, so we need to build u16 manually in some cases
// It took me way too fucking long to figure out this was a thing
u16 build_u16(u8 b1, u8 b2) {
    return ((u16)b1) | (((u16)b2) << 8);
}

u8 move_already_learned(u8* party_member, u16 move_id) {
    for (u8 i = 0; i < 4; i++) {
        u16* current_move_ptr = (u16 *)(party_member + PARTY_MON_MOVESET_OFFSET + (i * 2));
        if (*current_move_ptr == move_id) {
            return true;
        }
    }
    return false;
}

// In addition to setting the level byte, we also need to set the experience to the minimum required for that level
void setPartyLevel(void)
{
    u16 desired_level = load_var_value(0x4000);

    for (u8 i = 0; i < 6; i++) {
        // Load party member and determine species
        const u8 *party_member = PLAYER_PARTY_PTR + (i * PARTY_MON_SIZE);
        u16 species = *(u16 *)(party_member + PARTY_MON_SPECIES_OFFSET);

        // Determine growth rate of the species
        const u8 *base_data_for_species = POKEMON_BASE_DATA_PTR + (species * POKEMON_BASE_DATA_SIZE);
        u8 growth_rate = *(u8 *)(base_data_for_species + POKEMON_BASE_DATA_GROWTH_RATE_OFFSET);

        // Determine new experience required for desired level
        const u8 *level_up_table = LEVEL_UP_TABLES_PTR + (growth_rate * LEVEL_UP_TABLE_SIZE);
        u32 new_experience = *(u32 *)(level_up_table + (desired_level * 4));

        // Set new level and experience values
        *(u8 *)(party_member + PARTY_MON_LEVEL_OFFSET) = (u8)desired_level;
        *(u32 *)(party_member + PARTY_MON_EXPERIENCE_OFFSET) = new_experience;

        calculate_mon_stats(party_member);

        // Delete any level-up moves beyond the new level
        const u32 *levelup_moves_entry = (u32*) (POKEMON_LEVELUP_MOVES_PTR + (species * POKEMON_LEVELUP_MOVE_ENTRY_SIZE));
        u8* first_learnable_move = (u8 *)(*levelup_moves_entry);
        u8* moves_ptr = (u8 *)(*levelup_moves_entry);
        u16 move_id = build_u16(*(moves_ptr), *(moves_ptr + 1));
        u8* last_learnable_move_ptr = (u8 *)(*levelup_moves_entry);
        u8 past_desired_level = false;

        // Determine last eligible move to learn
        while (move_id != 0) {
            u8 move_level = *(u8 *)(moves_ptr + 2);
            past_desired_level = past_desired_level || (move_level > desired_level);

            if (!past_desired_level) {
                last_learnable_move_ptr = moves_ptr;
            }

            moves_ptr = (u8*)(moves_ptr + 3);
            move_id = build_u16(*(moves_ptr), *(moves_ptr + 1));
        }

        for(u8 j = 0; j < 4; j++) {
            u16* current_move_ptr = (u16 *)(party_member + PARTY_MON_MOVESET_OFFSET + (j * 2));

            if (last_learnable_move_ptr < first_learnable_move) {
                last_learnable_move_ptr = NULL;
            }

            if (last_learnable_move_ptr == NULL) {
                // Delete move in this slot
                *current_move_ptr = 0;
                *(u8*)((((u8*) current_move_ptr) - (j * 2)) + 8 + j) = 0; 
            } else {
                // Overwrite move in this slot
                u16 new_move_id = build_u16(*(last_learnable_move_ptr), *(last_learnable_move_ptr + 1));
                *current_move_ptr = new_move_id;
                *(u8*)((((u8*) current_move_ptr) - (j * 2)) + 8 + j) = 5; 
            }

            if (last_learnable_move_ptr != NULL) {
                last_learnable_move_ptr = (u8 *)(last_learnable_move_ptr - 3);
            }
        }
    }

    // Recalc HP and PP
    heal_player_party();
}

void calcEligiblePokemon() {
    u8 allow_legendaries = get_flag(0x00B2);
    u8 monotype_enabled = get_flag(0x00B3);
    u8 monotype = (u8) load_var_value(0x408E);
    u8 trash_enabled = get_flag(0x00B4);
    u8 regional_preference_enabled = get_flag(0x00B5);
    u8 current_gen = (u8) load_var_value(0x408F);

    u16 outside_regional_count = 0;
    u16 regional_count = 0;

    for(u16 i = 0; i < RANDOMIZER_TABLE_COUNT * RANDOMIZER_TABLE_ENTRY_SIZE; i += RANDOMIZER_TABLE_ENTRY_SIZE) {
        u8* entry = (u8*) (RANDOMIZER_TABLE_PTR + i);
        u8 entry_gen = *(u8*) (entry + RANDOMIZER_GEN_OFFSET);
        u8 entry_is_legendary = *(u16*) (entry + RANDOMIZER_LEGENDARY_OFFSET);
        u8 entry_type1 = *(u8*) (entry + RANDOMIZER_TYPE1_OFFSET);
        u8 entry_type2 = *(u8*) (entry + RANDOMIZER_TYPE2_OFFSET);
        u8 entry_is_trash = *(u8*) (entry + RANDOMIZER_IS_TRASH_OFFSET);

        u8 allowed = (allow_legendaries || !entry_is_legendary) &&
                     (!monotype_enabled || monotype == entry_type1 || monotype == entry_type2) &&
                     (!trash_enabled || entry_is_trash);

        if (allowed) {
            if (regional_preference_enabled && current_gen == entry_gen) {
                regional_count += 1;
            } else {
                outside_regional_count += 1;
            }
        }
    }

    var_set_value(0x4090, outside_regional_count);
    var_set_value(0x4091, regional_count);
}

void getRandomPokemon() {
    u16 outside_regional_count = load_var_value(0x4090);
    u16 regional_count = load_var_value(0x4091);
    u8 allow_legendaries = get_flag(0x00B2);
    u8 monotype_enabled = get_flag(0x00B3);
    u8 monotype = (u8) load_var_value(0x408E);
    u8 trash_enabled = get_flag(0x00B4);
    u8 regional_preference_enabled = get_flag(0x00B5);
    u8 current_gen = (u8) load_var_value(0x408F);

    u16 random_count;
    u8 use_regional_mon = regional_preference_enabled && regional_count > 0 && random(2);

    if (use_regional_mon) {
        random_count = regional_count;
    } else {
        random_count = outside_regional_count;
    }

    u16 random_mon_ind = random(random_count);
    u16 eligible_count = 0;
    u16 first_eligible_species = 0;

    for(u16 i = 0; i < RANDOMIZER_TABLE_COUNT * RANDOMIZER_TABLE_ENTRY_SIZE; i += RANDOMIZER_TABLE_ENTRY_SIZE) {
        u8* entry = (u8*) (RANDOMIZER_TABLE_PTR + i);
        u16 species_id = *(u16*) (entry);
        u8 entry_gen = *(u8*) (entry + RANDOMIZER_GEN_OFFSET);
        u8 entry_is_legendary = *(u8*) (entry + RANDOMIZER_LEGENDARY_OFFSET);
        u8 entry_type1 = *(u8*) (entry + RANDOMIZER_TYPE1_OFFSET);
        u8 entry_type2 = *(u8*) (entry + RANDOMIZER_TYPE2_OFFSET);
        u8 entry_is_trash = *(u8*) (entry + RANDOMIZER_IS_TRASH_OFFSET);

        u8 allowed = (allow_legendaries || !entry_is_legendary) &&
                     (!monotype_enabled || monotype == entry_type1 || monotype == entry_type2) &&
                     (!trash_enabled || entry_is_trash) &&
                     (!regional_preference_enabled || (use_regional_mon && current_gen == entry_gen) || (!use_regional_mon && current_gen != entry_gen));
        
        if (allowed) {
            if (eligible_count == 0) {
                first_eligible_species = species_id;
            }

            if (random_mon_ind == eligible_count) {
                u8 is_duplicate = false;

                // Check that party doesn't already contain this species
                for(u16 j = 0; j < 6; j++) {
                    u16 party_species_id = *(u16*) (PLAYER_PARTY_PTR + (PARTY_MON_SIZE * j) + PARTY_MON_SPECIES_OFFSET);

                    if (species_id == party_species_id) {
                        is_duplicate = true;
                        break;
                    }
                }

                if (is_duplicate) {
                    continue;
                }

                // return species_id;
                var_set_value(0x800D, species_id);
                return;
            }

            eligible_count += 1;
        }
    }

    // Use first eligible as failsafe if nothing got retured
    // return first_eligible_species;
    var_set_value(0x800D, first_eligible_species);
    return;
}

void applyAnchorStone() {
    
}
