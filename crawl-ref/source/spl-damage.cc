/**
 * @file
 * @brief Damage-dealing spells not already handled elsewhere.
 *           Other targeted spells are covered in spl-zap.cc.
**/

#include "AppHdr.h"


#include "spl-damage.h"

#include <functional>

#include "act-iter.h"
#include "areas.h"
#include "attack.h"
#include "beam.h"
#include "cloud.h"
#include "colour.h"
#include "coordit.h"
#include "directn.h"
#include "english.h"
#include "env.h"
#include "fight.h"
#include "food.h"
#include "fprop.h"
#include "god-conduct.h"
#include "invent.h"
#include "items.h"
#include "los.h"
#include "losglobal.h"
#include "macro.h"
#include "message.h"
#include "misc.h"
#include "mon-behv.h"
#include "mon-death.h"
#include "mon-tentacle.h"
#include "mutation.h"
#include "ouch.h"
#include "player-stats.h"
#include "prompt.h"
#include "religion.h"
#include "shout.h"
#include "spl-summoning.h"
#include "spl-util.h"
#include "spl-zap.h"
#include "stepdown.h"
#include "stringutil.h"
#include "target.h"
#include "terrain.h"
#include "transform.h"
#include "unicode.h"
#include "viewchar.h"
#include "view.h"

void setup_fire_storm(const actor *source, int pow, bolt &beam)
{
    zappy(ZAP_FIRE_STORM, pow, source->is_monster(), beam);
    beam.ex_size      = 2 + (random2(1000) < pow);
    beam.source_id    = source->mid;
    // XXX: Should this be KILL_MON_MISSILE?
    beam.thrower      =
        source->is_player() ? KILL_YOU_MISSILE : KILL_MON;
    beam.aux_source.clear();
    beam.is_tracer    = false;
    beam.origin_spell = SPELL_FIRE_STORM;
}

spret cast_fire_storm(int pow, bolt &beam, bool fail)
{
    if (grid_distance(beam.target, beam.source) > beam.range)
    {
        mpr("That is beyond the maximum range.");
        return spret::abort;
    }

    if (cell_is_solid(beam.target))
    {
        const char *feat = feat_type_name(grd(beam.target));
        mprf("You can't place the storm on %s.", article_a(feat).c_str());
        return spret::abort;
    }

    setup_fire_storm(&you, pow, beam);

    bolt tempbeam = beam;
    tempbeam.ex_size = (pow > 76) ? 3 : 2;
    tempbeam.is_tracer = true;

    tempbeam.explode(false);
    if (tempbeam.beam_cancelled)
        return spret::abort;

    fail_check();

    beam.apply_beam_conducts();
    beam.refine_for_explosion();
    beam.explode(false);

    viewwindow();
    return spret::success;
}

// No setup/cast split here as monster hellfire is completely different.
// XXX make this not true
bool cast_smitey_hellfire(int pow, bolt &beam)
{
    beam.name              = "hellfire";
    beam.aux_source        = "hellfire";
    beam.ex_size           = 1;
    beam.flavour           = BEAM_DAMNATION;
    beam.real_flavour      = beam.flavour;
    beam.glyph             = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour            = LIGHTRED;
    beam.source_id         = MID_PLAYER;
    beam.thrower           = KILL_YOU;
    beam.obvious_effect    = false;
    beam.pierce            = false;
    beam.is_explosion      = true;
    beam.ench_power        = pow;      // used for radius
    beam.hit               = 20 + pow / 10;
    beam.damage            = calc_dice(6, 30 + pow);
    beam.attitude          = ATT_FRIENDLY;
    beam.friend_info.count = 0;
    beam.is_tracer         = true;

    beam.explode(false);

    if (beam.beam_cancelled)
    {
        canned_msg(MSG_OK);
        return false;
    }

    mpr("You call forth a pillar of hellfire!");

    beam.is_tracer = false;
    beam.in_explosion_phase = false;
    beam.explode(true);

    return true;
}

// todo: update this
string desc_chain_lightning_dam(int pow)
{
    // Damage is 5d(9.2 + pow / 30), but if lots of targets are around
    // it can hit the player precisely once at very low (e.g. 1) power
    // and deal 5 damage.
    int min = 5;

    // Max damage per bounce is 46 + pow / 6; in the worst case every other
    // bounce hits the player, losing 8 pow on the bounce away and 8 on the
    // bounce back for a total of 16; thus, for n bounces, it's:
    // (46 + pow/6) * n less 16/6 times the (n - 1)th triangular number.
    int n = (pow + 15) / 16;
    int max = (46 + (pow / 6)) * n - 4 * n * (n - 1) / 3;

    return make_stringf("%d-%d", min, max);
}

static bool _chain_lightning_harms(const actor *act)
{
    if (act->is_monster())
    {
        // Harmless to these monsters, so don't prompt about them.
        if (act->res_elec() >= 3
            || you.deity() == GOD_FEDHAS && fedhas_protects(*act->as_monster()))
        {
            return false;
        }
        return true;
    }
    return false;
}

spret cast_chain_spell(spell_type spell_cast, int pow,
                            const actor *caster, bool fail)
{
    fail_check();
    bolt beam;

    // initialise beam structure
    beam.source_id      = caster->mid;
    beam.thrower        = caster->is_player() ? KILL_YOU_MISSILE : KILL_MON_MISSILE;
    beam.range          = 8;
    beam.hit            = AUTOMATIC_HIT;
    beam.obvious_effect = true;
    beam.pierce         = false;       // since we want to stop at our target
    beam.is_explosion   = false;
    beam.is_tracer      = false;
    beam.origin_spell   = spell_cast;
    switch (spell_cast)
    {
    case SPELL_CHAIN_LIGHTNING:
        if (caster->is_player())
        {
            targeter_los hitfunc(&you, LOS_ARENA);
            if (stop_attack_prompt(hitfunc, "attack", _chain_lightning_harms))
                return spret::abort;
        }

        beam.name           = "lightning arc";
        beam.aux_source     = "chain lightning";
        beam.glyph          = dchar_glyph(DCHAR_FIRED_ZAP);
        beam.flavour        = BEAM_ELECTRICITY;
        beam.is_explosion   = true;
        break;
    case SPELL_CHAIN_OF_CHAOS:
        beam.name           = "arc of chaos";
        beam.aux_source     = "chain of chaos";
        beam.glyph          = dchar_glyph(DCHAR_FIRED_ZAP);
        beam.flavour        = BEAM_CHAOS;
        break;
    default:
        die("buggy chain spell %d cast", spell_cast);
        break;
    }

    if (const monster* mons = caster->as_monster())
        beam.source_name = mons->name(DESC_PLAIN, true);

    bool first = true;
    coord_def source = caster->pos();
    coord_def target;
    coord_def last_target = source;

    int bounces = div_rand_round(isqrt(max(pow, 1) * 100), 20) + random2(2);
    int power_decrement = pow / (bounces * 2);
    
    bool player_targetable = spell_cast != SPELL_CHAIN_LIGHTNING || !caster->is_player();

    for (int bounce = 0; bounce < bounces; bounce++)
    {       
        // infinity as far as this spell is concerned
        // (Range - 1) is used because the distance is randomised and
        // may be shifted by one.
        int min_dist = LOS_DEFAULT_RANGE - 1;

        int dist;
        int count = 0;

        target.x = -1;
        target.y = -1;

        for (monster_iterator mi; mi; ++mi)
        {
            if (invalid_monster(*mi))
                continue;

            // Don't arc to things we cannot hit.
            if (beam.ignores_monster(*mi))
                continue;

            coord_def mi_pos = mi->pos();
            dist = grid_distance(source, mi->pos());

            // check for the source of this arc
            if (!dist)
                continue;

            // don't allow bounce loops between 2 targets
            if (mi_pos == last_target)
                continue;

            // randomise distance (arcs don't care about a couple of feet)
            dist += (random2(3) - 1);

            // always ignore targets further than current one
            if (dist > min_dist)
                continue;

            if (!cell_see_cell(source, mi->pos(), LOS_SOLID)
                || !cell_see_cell(caster->pos(), mi->pos(), LOS_SOLID_SEE))
            {
                continue;
            }

            // check for actors along the arc path
            ray_def ray;
            if (!find_ray(source, mi->pos(), ray, opc_solid))
                continue;

            while (ray.advance())
                if (actor_at(ray.pos()))
                    break;

            if (ray.pos() != mi->pos())
                continue;

            count++;

            if (dist < min_dist)
            {
                // switch to looking for closer targets (but not always)
                if (!one_chance_in(10))
                {
                    min_dist = dist;
                    target = mi->pos();
                    count = 0;
                }
            }
            else if (target.x == -1 || one_chance_in(count))
            {
                // either first target, or new selected target at
                // min_dist == dist.
                target = mi->pos();
            }
        }

        // now check if the player is a target
        dist = grid_distance(source, you.pos());

        if (dist && player_targetable)  // player was not the source
        {
            // distance randomised (as above)
            dist += (random2(3) - 1);

            // select player if only, closest, or randomly selected
            if ((target.x == -1
                    || dist < min_dist
                    || (dist == min_dist && one_chance_in(count + 1)))
                && cell_see_cell(source, you.pos(), LOS_SOLID))
            {
                target = you.pos();
            }
        }

        const bool see_source = you.see_cell(source);
        const bool see_targ   = you.see_cell(target);

        if (target.x == -1)
        {
            if (see_source)
                mprf("The %s grounds out.", beam.name.c_str());

            break;
        }

        // Trying to limit message spamming here so we'll only mention
        // the thunder at the start or when it's out of LoS.
        switch (spell_cast)
        {
            case SPELL_CHAIN_LIGHTNING:
            {
                const char* msg = "You hear a mighty clap of thunder!";
                noisy(spell_effect_noise(SPELL_CHAIN_LIGHTNING), source,
                      (first || !see_source) ? msg : nullptr);
                break;
            }
            case SPELL_CHAIN_OF_CHAOS:
                if (first && see_source)
                    mpr("A swirling arc of seething chaos appears!");
                break;
            default:
                break;
        }
        first = false;

        if (see_source && !see_targ)
            mprf("The %s arcs out of your line of sight!", beam.name.c_str());
        else if (!see_source && see_targ)
            mprf("The %s suddenly appears!", beam.name.c_str());

        beam.source = source;
        beam.target = target;
        beam.draw_delay = 5;
        beam.explode_delay = 25;
        
        switch (spell_cast)
        {
            case SPELL_CHAIN_LIGHTNING:
                beam.colour = LIGHTBLUE;
                beam.damage = caster->is_player()
                    ? calc_dice(5, 18 + isqrt(max(pow, 1) * 80))
                    : calc_dice(5, 46 + pow / 6);
                break;
            case SPELL_CHAIN_OF_CHAOS:
                beam.colour       = ETC_RANDOM;
                beam.ench_power   = pow;
                beam.damage       = calc_dice(3, 5 + pow / 6);
                beam.real_flavour = BEAM_CHAOS;
                beam.flavour      = BEAM_CHAOS;
            default:
                break;
        }

        if (target == caster->pos())
        {
            beam.real_flavour = BEAM_VISUAL;
            beam.flavour      = BEAM_VISUAL;
        }
        beam.fire();
        
        last_target = source;
        source = target;
        pow -= power_decrement;
    }

    return spret::success;
}

/*
 * Handle the application of damage from a player spell that doesn't apply these
 * through struct bolt. This applies any Zin sancuary violation and can apply
 * god conducts as well.
 * @param mon          The monster.
 * @param damage       The damage to apply, if any. Regardless of damage done,
 *                     the monster will have death cleanup applied via
 *                     monster_die() if it's now dead.
 * @param flavour      The beam flavour of damage.
 * @param god_conducts If true, apply any god conducts. Some callers need to
 *                     apply effects prior to damage that might kill the
 *                     monster, hence handle conducts on their own.
*/
static void _player_hurt_monster(monster &mon, int damage, beam_type flavour,
                                 bool god_conducts = true)
{
    if (is_sanctuary(you.pos()) || is_sanctuary(mon.pos()))
        remove_sanctuary(true);

    if (god_conducts && you.deity() == GOD_FEDHAS && fedhas_neutralises(mon))
    {
        simple_god_message(" protects your plant from harm.", GOD_FEDHAS);
        return;
    }

    god_conduct_trigger conducts[3];
    if (god_conducts)
        set_attack_conducts(conducts, mon, you.can_see(mon));

    // Don't let monster::hurt() do death cleanup here. We're handling death
    // cleanup at the end to cover cases where we've done no damage and the
    // monster is dead from previous effects.
    if (damage)
        mon.hurt(&you, damage, flavour, KILLED_BY_BEAM, "", "", false);

    if (mon.alive())
    {
        behaviour_event(&mon, ME_WHACK, &you);

        if (damage && you.can_see(mon))
            print_wounds(mon);
    }
    else
        monster_die(mon, KILL_YOU, NON_MONSTER);
}

static counted_monster_list _counted_monster_list_from_vector(
    vector<monster *> affected_monsters)
{
    counted_monster_list mons;
    for (auto mon : affected_monsters)
        mons.add(mon);
    return mons;
}

static bool _drain_lifeable(const actor* agent, const actor* act)
{
    if (act->res_negative_energy() >= 3)
        return false;

    if (!agent)
        return true;

    const monster* mons = agent->as_monster();
    const monster* m = act->as_monster();

    return !(agent->is_player() && act->wont_attack()
             || mons && act->is_player() && mons->wont_attack()
             || mons && m && mons_atts_aligned(mons->attitude, m->attitude));
}

static void _los_spell_pre_damage_monsters(const actor* agent,
                                           vector<monster *> affected_monsters,
                                           const char *verb)
{
    // Filter out affected monsters that we don't know for sure are there
    vector<monster*> seen_monsters;
    for (monster *mon : affected_monsters)
        if (you.can_see(*mon))
            seen_monsters.push_back(mon);

    if (!seen_monsters.empty())
    {
        counted_monster_list mons_list =
            _counted_monster_list_from_vector(seen_monsters);
        const string message = make_stringf("%s %s %s.",
                mons_list.describe(DESC_THE, true).c_str(),
                conjugate_verb("be", mons_list.count() > 1).c_str(), verb);
        if (strwidth(message) < get_number_of_cols() - 2)
            mpr(message);
        else
        {
            // Exclamation mark to suggest that a lot of creatures were
            // affected.
            mprf("The monsters around %s are %s!",
                agent && agent->is_monster() && you.can_see(*agent)
                ? agent->as_monster()->name(DESC_THE).c_str()
                : "you", verb);
        }
    }
}

static int _los_spell_damage_player(const actor* agent, bolt &beam,
                                    bool actual)
{
    int hurted = actual ? beam.damage.roll()
                        // Monsters use the average for foe calculations.
                        : (1 + beam.damage.num * beam.damage.size) / 2;
    hurted = check_your_resists(hurted, beam.flavour, beam.name, 0,
            // Drain life doesn't apply drain effects.
            actual && beam.origin_spell != SPELL_DRAIN_LIFE);
    if (actual && hurted > 0)
    {
        if (beam.origin_spell == SPELL_OZOCUBUS_REFRIGERATION)
            mpr("You feel very cold.");

        if (agent && !agent->is_player())
        {
            ouch(hurted, KILLED_BY_BEAM, agent->mid,
                 make_stringf("by %s", beam.name.c_str()).c_str(), true,
                 agent->as_monster()->name(DESC_A).c_str());
            you.expose_to_element(beam.flavour, 5);
        }
        // -harm from player casting Ozo's Refridge.
        else if (beam.origin_spell == SPELL_OZOCUBUS_REFRIGERATION)
        {
            ouch(hurted, KILLED_BY_FREEZING);
            you.expose_to_element(beam.flavour, 5);
            you.increase_duration(DUR_NO_POTIONS, 7 + random2(9), 15);
        }
    }

    return hurted;
}

static int _los_spell_damage_monster(const actor* agent, monster &target,
                                     bolt &beam, bool actual, bool wounds)
{

    beam.thrower = (agent && agent->is_player()) ? KILL_YOU :
                    agent                        ? KILL_MON
                                                 : KILL_MISC;

    // Set conducts here. The monster needs to be alive when this is done, and
    // mons_adjust_flavoured() could kill it.
    god_conduct_trigger conducts[3];
    if (YOU_KILL(beam.thrower))
        set_attack_conducts(conducts, target, you.can_see(target));

    int hurted = actual ? beam.damage.roll()
                        // Monsters use the average for foe calculations.
                        : (1 + beam.damage.num * beam.damage.size) / 2;
    hurted = mons_adjust_flavoured(&target, beam, hurted,
                 // Drain life doesn't apply drain effects.
                 actual && beam.origin_spell != SPELL_DRAIN_LIFE);
    dprf("damage done: %d", hurted);

    if (actual)
    {
        if (YOU_KILL(beam.thrower))
            _player_hurt_monster(target, hurted, beam.flavour, false);
        else if (hurted)
            target.hurt(agent, hurted, beam.flavour);

        // Cold-blooded creatures can be slowed.
        if (beam.origin_spell == SPELL_OZOCUBUS_REFRIGERATION
            && target.alive())
        {
            target.expose_to_element(beam.flavour, 5);
        }
    }

    // So that summons don't restore HP.
    if (beam.origin_spell == SPELL_DRAIN_LIFE && target.is_summoned())
        return 0;

    return hurted;
}


static spret _cast_los_attack_spell(spell_type spell, int pow,
                                         const actor* agent, actor* defender,
                                         bool actual, bool fail,
                                         int* damage_done)
{
    const monster* mons = agent ? agent->as_monster() : nullptr;

    const zap_type zap = spell_to_zap(spell);
    if (zap == NUM_ZAPS)
        return spret::abort;

    bolt beam;
    zappy(zap, pow, mons, beam);
    beam.source_id = agent ? agent->mid : MID_NOBODY;
    beam.foe_ratio = 80;

    const char *player_msg = nullptr, *global_msg = nullptr,
               *mons_vis_msg = nullptr, *mons_invis_msg = nullptr,
               *verb = nullptr, *prompt_verb = nullptr;
    bool (*vulnerable)(const actor *, const actor *) = nullptr;

    switch (spell)
    {
        case SPELL_OZOCUBUS_REFRIGERATION:
            player_msg = "The heat is drained from your surroundings.";
            global_msg = "Something drains the heat from around you.";
            mons_vis_msg = " drains the heat from the surrounding"
                           " environment!";
            mons_invis_msg = "The ambient heat is drained!";
            verb = "frozen";
            prompt_verb = "refrigerate";
            vulnerable = [](const actor *caster, const actor *act) {
                return act->is_player() || act->res_cold() < 3
                       && !(caster->deity() == GOD_FEDHAS
                            && fedhas_protects(*act->as_monster()));
            };
            break;

        case SPELL_DRAIN_LIFE:
            player_msg = "You draw life from your surroundings.";
            global_msg = "Something draws the life force from your"
                         " surroundings.";
            mons_vis_msg = " draws from the surrounding life force!";
            mons_invis_msg = "The surrounding life force dissipates!";
            verb = "drained of life";
            prompt_verb = "drain life";
            vulnerable = &_drain_lifeable;
            break;

        case SPELL_SONIC_WAVE:
            player_msg = "You send a blast of sound all around you.";
            global_msg = "Something sends a blast of sound all around you.";
            mons_vis_msg = " sends a blast of sound all around you!";
            mons_invis_msg = "Sound blasts the surrounding area!";
            verb = "blasted";
            // prompt_verb = "sing" The singing sword prompts in melee-attack
            vulnerable = [](const actor *caster, const actor *act) {
                return act != caster
                       && !(caster->deity() == GOD_FEDHAS
                            && fedhas_protects(*act->as_monster()));
            };
            break;

        default:
            return spret::abort;
    }

    auto vul_hitfunc = [vulnerable](const actor *act) -> bool
    {
        return (*vulnerable)(&you, act);
    };

    if (agent && agent->is_player())
    {
        ASSERT(actual);

        targeter_los hitfunc(&you, LOS_NO_TRANS);
        // Singing Sword's spell shouldn't give a prompt at this time.
        if (spell != SPELL_SONIC_WAVE)
        {
            if (stop_attack_prompt(hitfunc, prompt_verb, vul_hitfunc))
                return spret::abort;

            fail_check();
        }

        mpr(player_msg);
        flash_view_delay(UA_PLAYER, beam.colour, 300, &hitfunc);
    }
    else if (actual)
    {
        if (!agent)
            mpr(global_msg);
        else if (you.can_see(*agent))
            simple_monster_message(*mons, mons_vis_msg);
        else if (you.see_cell(agent->pos()))
            mpr(mons_invis_msg);

        if (!agent || you.see_cell(agent->pos()))
            flash_view_delay(UA_MONSTER, beam.colour, 300);
    }

    bool affects_you = false;
    vector<monster *> affected_monsters;

    for (actor_near_iterator ai((agent ? agent : &you)->pos(), LOS_NO_TRANS);
         ai; ++ai)
    {
        if ((*vulnerable)(agent, *ai))
        {
            if (ai->is_player())
                affects_you = true;
            else
                affected_monsters.push_back(ai->as_monster());
        }
    }

    const int avg_damage = (1 + beam.damage.num * beam.damage.size) / 2;
    int total_damage = 0;
    // XXX: This ordering is kind of broken; it's to preserve the message
    // order from the original behaviour in the case of refrigerate.
    if (affects_you)
    {
        total_damage = _los_spell_damage_player(agent, beam, actual);
        if (!actual && mons)
        {
            if (mons->wont_attack())
            {
                beam.friend_info.count++;
                beam.friend_info.power +=
                    (you.get_experience_level() * total_damage / avg_damage);
            }
            else
            {
                beam.foe_info.count++;
                beam.foe_info.power +=
                    (you.get_experience_level() * total_damage / avg_damage);
            }
        }
    }

    if (actual && !affected_monsters.empty())
        _los_spell_pre_damage_monsters(agent, affected_monsters, verb);

    for (auto m : affected_monsters)
    {
        // Watch out for invalidation. Example: Ozocubu's refrigeration on
        // a bunch of ballistomycete spores that blow each other up.
        if (!m->alive())
            continue;

        int this_damage = _los_spell_damage_monster(agent, *m, beam, actual,
                                                    m != defender);
        total_damage += this_damage;

        if (!actual && mons)
        {
            if (mons_atts_aligned(m->attitude, mons->attitude))
            {
                beam.friend_info.count++;
                beam.friend_info.power +=
                    (m->get_hit_dice() * this_damage / avg_damage);
            }
            else
            {
                beam.foe_info.count++;
                beam.foe_info.power +=
                    (m->get_hit_dice() * this_damage / avg_damage);
            }
        }
    }

    if (damage_done)
        *damage_done = total_damage;

    if (actual)
        return spret::success;
    return mons_should_fire(beam) ? spret::success : spret::abort;
}

spret trace_los_attack_spell(spell_type spell, int pow, const actor* agent)
{
    return _cast_los_attack_spell(spell, pow, agent, nullptr, false, false,
                                  nullptr);
}

spret fire_los_attack_spell(spell_type spell, int pow, const actor* agent,
                                 actor *defender, bool fail, int* damage_done)
{
    return _cast_los_attack_spell(spell, pow, agent, defender, true, fail,
                                  damage_done);
}

spret vampiric_drain(int pow, monster* mons, bool fail)
{
    const bool observable = mons && mons->observable();
    if (!mons
        || mons->submerged()
        || !observable && !actor_is_susceptible_to_vampirism(*mons))
    {
        fail_check();

        canned_msg(MSG_NOTHING_CLOSE_ENOUGH);
        // Cost to disallow freely locating invisible/submerged
        // monsters.
        return spret::success;
    }

    if (observable &&
            (!actor_is_susceptible_to_vampirism(*mons) || mons->res_negative_energy() >= 3))
    {
        mpr("You can't drain life from that!");
        return spret::abort;
    }

    if (stop_attack_prompt(mons, false, you.pos()))
    {
        canned_msg(MSG_OK);
        return spret::abort;
    }

    fail_check();

    if (!mons->alive())
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return spret::success;
    }

    int hp_gain = 2 + random2avg(pow, 2)/2;
    hp_gain = resist_adjust_damage(mons, BEAM_NEG, hp_gain);
    hp_gain = min(mons->hit_points, hp_gain);

    if (!hp_gain)
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return spret::success;
    }

    _player_hurt_monster(*mons, hp_gain, BEAM_NEG);

    hp_gain = div_rand_round(hp_gain*2, 3);

    if (hp_gain && !you.duration[DUR_DEATHS_DOOR] && you.species != SP_DEEP_DWARF)
    {
        mpr("You feel life coursing into your body.");
        inc_hp(hp_gain);
        
        if (one_chance_in(10))
        {
            mprf(MSGCH_WARN, "The foreign life energy weakens you!");
            lose_stat(STAT_STR, 1);
        }
    }

    return spret::success;
}

dice_def freeze_damage(int pow)
{
    return dice_def(1, 3 + pow / 3);
}

spret cast_freeze(int pow, monster* mons, bool fail)
{
    if (!mons || mons->submerged())
    {
        fail_check();
        canned_msg(MSG_NOTHING_CLOSE_ENOUGH);
        // If there's no monster there, you still pay the costs in
        // order to prevent locating invisible/submerged monsters.
        return spret::success;
    }

    const bool observable = mons && mons->observable();
    if (mons->res_cold() >= 3 && observable)
    {
        mpr("That target is immune!");
        return spret::abort;
    }

    if (stop_attack_prompt(mons, false, you.pos()))
    {
        canned_msg(MSG_OK);
        return spret::abort;
    }

    fail_check();

    // Set conducts here. The monster needs to be alive when this is done, and
    // mons_adjust_flavoured() could kill it.
    god_conduct_trigger conducts[3];
    set_attack_conducts(conducts, *mons);

    bolt beam;
    beam.flavour = BEAM_COLD;
    beam.thrower = KILL_YOU;

    const int orig_hurted = freeze_damage(pow).roll();
    int hurted = mons_adjust_flavoured(mons, beam, orig_hurted);
    mprf("You freeze %s%s%s",
         mons->name(DESC_THE).c_str(),
         hurted ? "" : " but do no damage",
         attack_strength_punctuation(hurted).c_str());

    _player_hurt_monster(*mons, hurted, beam.flavour, false);

    if (mons->alive())
        mons->expose_to_element(BEAM_COLD, orig_hurted);

    return spret::success;
}

spret cast_airstrike(int pow, const dist &beam, bool fail)
{
    if (cell_is_solid(beam.target))
    {
        canned_msg(MSG_UNTHINKING_ACT);
        return spret::abort;
    }

    monster* mons = monster_at(beam.target);
    if (!mons || mons->submerged())
    {
        fail_check();
        canned_msg(MSG_SPELL_FIZZLES);
        return spret::success; // still losing a turn
    }

    if (stop_attack_prompt(mons, false, you.pos()))
        return spret::abort;
    fail_check();

    god_conduct_trigger conducts[3];
    set_attack_conducts(conducts, *mons, you.can_see(*mons));

    noisy(spell_effect_noise(SPELL_AIRSTRIKE), beam.target);

    bolt pbeam;
    pbeam.flavour = BEAM_AIR;

    int hurted = 8 + random2(2 + div_rand_round(pow, 7));
#ifdef DEBUG_DIAGNOSTICS
    const int preac = hurted;
#endif
    hurted = mons->apply_ac(mons->beam_resists(pbeam, hurted, false));
    dprf("preac: %d, postac: %d", preac, hurted);

    mprf("The air twists around and %sstrikes %s%s%s",
         mons->airborne() ? "violently " : "",
         mons->name(DESC_THE).c_str(),
         hurted ? "" : " but does no damage",
         attack_strength_punctuation(hurted).c_str());
    _player_hurt_monster(*mons, hurted, pbeam.flavour);

    return spret::success;
}

// Here begin the actual spells:
static int _shatter_mon_dice(const monster *mon)
{
    if (!mon)
        return 3;

    // Removed a lot of silly monsters down here... people, just because
    // it says ice, rock, or iron in the name doesn't mean it's actually
    // made out of the substance. - bwr
    switch (mon->type)
    {
    // Double damage to stone, metal and crystal.
    case MONS_EARTH_ELEMENTAL:
    case MONS_ROCKSLIME:
    case MONS_USHABTI:
    case MONS_STATUE:
    case MONS_GARGOYLE:
    case MONS_IRON_ELEMENTAL:
    case MONS_IRON_GOLEM:
    case MONS_PEACEKEEPER:
    case MONS_WAR_GARGOYLE:
    case MONS_ANCIENT_AUTOMATON:
    case MONS_SALTLING:
    case MONS_CRYSTAL_GUARDIAN:
    case MONS_OBSIDIAN_STATUE:
    case MONS_ORANGE_STATUE:
    case MONS_ROXANNE:
        return 6;

    default:
        if (mon->is_insubstantial())
            return 0;
        if (mon->petrifying() || mon->petrified())
            return 6; // reduced later by petrification's damage reduction
        else if (mon->is_skeletal() || mon->is_icy())
            return 6;
        else if (mon->airborne() || mons_is_slime(*mon))
            return 1;
        // Normal damage to everything else.
        else
            return 3;
    }
}

dice_def shatter_damage(int pow, monster *mon)
{
    return dice_def(_shatter_mon_dice(mon), 5 + pow / 3);
}

static int _shatter_monsters(coord_def where, int pow, actor *agent)
{
    monster* mon = monster_at(where);

    if (!mon || !mon->alive() || mon == agent)
        return 0;

    const dice_def dam_dice = shatter_damage(pow, mon);
    int base_dmg = dam_dice.roll();
    int damage = max(0, base_dmg - random2(mon->armour_class()));

    if (agent->is_player() && base_dmg)
        _player_hurt_monster(*mon, damage, BEAM_MMISSILE);
    else if (damage)
        mon->hurt(agent, damage);

    return damage;
}

static int _shatter_walls(coord_def where, int pow, actor *agent)
{
    int chance = 0;

    // if not in-bounds then we can't really shatter it -- bwr
    if (!in_bounds(where))
        return 0;

    if (env.markers.property_at(where, MAT_ANY, "veto_shatter") == "veto")
        return 0;

    const dungeon_feature_type grid = grd(where);

    switch (grid)
    {
    case DNGN_CLOSED_DOOR:
    case DNGN_CLOSED_CLEAR_DOOR:
    case DNGN_RUNED_DOOR:
    case DNGN_RUNED_CLEAR_DOOR:
    case DNGN_OPEN_DOOR:
    case DNGN_OPEN_CLEAR_DOOR:
    case DNGN_SEALED_DOOR:
    case DNGN_SEALED_CLEAR_DOOR:
    case DNGN_GRATE:
    case DNGN_ORCISH_IDOL:
    case DNGN_GRANITE_STATUE:
        chance = 100;
        break;

    case DNGN_METAL_WALL:
        chance = 15;
        break;

    case DNGN_CLEAR_STONE_WALL:
    case DNGN_STONE_WALL:
        chance = 25;
        break;

    case DNGN_CLEAR_ROCK_WALL:
    case DNGN_ROCK_WALL:
    case DNGN_SLIMY_WALL:
    case DNGN_CRYSTAL_WALL:
        chance = 33;
        break;
    case DNGN_TREE:
        if (agent->deity() == GOD_FEDHAS)
            return 0;
        chance = 33;
        break;

    default:
        break;
    }        

    if (x_chance_in_y(min((chance*pow)/110, chance + pow - 100), 100))
    {
        noisy(spell_effect_noise(SPELL_SHATTER), where);

        destroy_wall(where);

        return 1;
    }

    return 0;
}

static int _shatter_player_dice()
{
    if (you.is_insubstantial())
        return 1;
    if (you.petrified() || you.petrifying())
        return 6; // reduced later by petrification's damage reduction
    else if (you.form == transformation::statue
             || you.form == transformation::ice_beast
             || you.species == SP_GARGOYLE)
        return 6;
    else if (you.airborne())
        return 1;
    else
        return 3;
}

/**
 * Is this a valid target for shatter?
 *
 * @param act     The actor being considered
 * @return        Whether the actor will take damage from shatter.
 */
static bool _shatterable(const actor *act)
{
    if (act->is_player())
        return _shatter_player_dice();
    return _shatter_mon_dice(act->as_monster());
}

spret cast_shatter(int pow, bool fail)
{
    targeter_los hitfunc(&you, LOS_ARENA);
    if (stop_attack_prompt(hitfunc, "attack", _shatterable))
        return spret::abort;

    fail_check();
    const bool silence = silenced(you.pos());

    if (silence)
        mpr("The dungeon shakes!");
    else
    {
        noisy(spell_effect_noise(SPELL_SHATTER), you.pos());
        mprf(MSGCH_SOUND, "The dungeon rumbles!");
    }

    run_animation(ANIMATION_SHAKE_VIEWPORT, UA_PLAYER);

    int dest = 0;
    for (distance_iterator di(you.pos(), true, true, LOS_RADIUS); di; ++di)
    {
        // goes from the center out, so newly dug walls recurse
        if (!cell_see_cell(you.pos(), *di, LOS_SOLID))
            continue;

        _shatter_monsters(*di, pow, &you);
        dest += _shatter_walls(*di, pow, &you);
    }

    if (dest && !silence)
        mprf(MSGCH_SOUND, "Ka-crash!");

    return spret::success;
}

static int _shatter_player(int pow, actor *wielder, bool devastator = false)
{
    if (wielder->is_player())
        return 0;

    dice_def dam_dice(_shatter_player_dice(), 5 + pow / 3);

    int damage = max(0, dam_dice.roll() - random2(you.armour_class()));

    if (damage > 0)
    {
        mprf(damage > 15 ? "You shudder from the earth-shattering force%s"
                        : "You shudder%s",
             attack_strength_punctuation(damage).c_str());
        if (devastator)
            ouch(damage, KILLED_BY_MONSTER, wielder->mid);
        else
            ouch(damage, KILLED_BY_BEAM, wielder->mid, "by Shatter");
    }

    return damage;
}

bool mons_shatter(monster* caster, bool actual)
{
    const bool silence = silenced(caster->pos());
    int foes = 0;

    if (actual)
    {
        if (silence)
        {
            mprf("The dungeon shakes around %s!",
                 caster->name(DESC_THE).c_str());
        }
        else
        {
            noisy(spell_effect_noise(SPELL_SHATTER), caster->pos(), caster->mid);
            mprf(MSGCH_SOUND, "The dungeon rumbles around %s!",
                 caster->name(DESC_THE).c_str());
        }
    }

    int pow = 5 + div_rand_round(caster->get_hit_dice() * 9, 2);

    int dest = 0;
    for (distance_iterator di(caster->pos(), true, true, LOS_RADIUS); di; ++di)
    {
        // goes from the center out, so newly dug walls recurse
        if (!cell_see_cell(caster->pos(), *di, LOS_SOLID))
            continue;

        if (actual)
        {
            _shatter_monsters(*di, pow, caster);
            if (*di == you.pos())
                _shatter_player(pow, caster);
            dest += _shatter_walls(*di, pow, caster);
        }
        else
        {
            if (you.pos() == *di)
                foes -= _shatter_player_dice();
            if (const monster *victim = monster_at(*di))
            {
                dprf("[%s]", victim->name(DESC_PLAIN, true).c_str());
                foes += _shatter_mon_dice(victim)
                     * (victim->wont_attack() ? -1 : 1);
            }
        }
    }

    if (dest && !silence)
        mprf(MSGCH_SOUND, "Ka-crash!");

    if (actual)
        run_animation(ANIMATION_SHAKE_VIEWPORT, UA_MONSTER);

    if (!caster->wont_attack())
        foes *= -1;

    if (!actual)
        dprf("Shatter foe HD: %d", foes);

    return foes > 0; // doesn't matter if actual
}

void shillelagh(actor *wielder, coord_def where, int pow)
{
    bolt beam;
    beam.name = "shillelagh";
    beam.flavour = BEAM_VISUAL;
    beam.set_agent(wielder);
    beam.colour = BROWN;
    beam.glyph = dchar_glyph(DCHAR_EXPLOSION);
    beam.range = 1;
    beam.ex_size = 1;
    beam.is_explosion = true;
    beam.source = wielder->pos();
    beam.target = where;
    beam.hit = AUTOMATIC_HIT;
    beam.loudness = 7;
    beam.explode();

    counted_monster_list affected_monsters;
    for (adjacent_iterator ai(where, false); ai; ++ai)
    {
        monster *mon = monster_at(*ai);
        if (!mon || !mon->alive() || mon->submerged()
            || mon->is_insubstantial() || !you.can_see(*mon)
            || mon == wielder)
        {
            continue;
        }
        affected_monsters.add(mon);
    }
    if (!affected_monsters.empty())
    {
        const string message =
            make_stringf("%s shudder%s.",
                         affected_monsters.describe().c_str(),
                         affected_monsters.count() == 1? "s" : "");
        if (strwidth(message) < get_number_of_cols() - 2)
            mpr(message);
        else
            mpr("There is a shattering impact!");
    }

    // need to do this again to do the actual damage
    for (adjacent_iterator ai(where, false); ai; ++ai)
        _shatter_monsters(*ai, pow, wielder);

    if ((you.pos() - wielder->pos()).rdist() <= 1 && in_bounds(you.pos()))
        _shatter_player(pow / 2, wielder, true);
}

/**
 * Is it OK for the player to cast Irradiate right now, or will they end up
 * injuring a monster they didn't mean to?
 *
 * @return  true if it's ok to go ahead with the spell; false if the player
 *          wants to abort.
 */
static bool _irradiate_is_safe()
{
    for (adjacent_iterator ai(you.pos()); ai; ++ai)
    {
        const monster *mon = monster_at(*ai);
        if (!mon)
            continue;

        if (you.deity() == GOD_FEDHAS && fedhas_protects(*mon))
            continue;

        if (stop_attack_prompt(mon, false, you.pos()))
            return false;
    }

    return true;
}

dice_def irradiate_damage(int pow)
{
    const int dice = 6;
    const int max_dam = 18 + div_rand_round(pow*2, 3);
    return calc_dice(dice, max_dam);
}

/**
 * Irradiate the given cell. (Per the spell.)
 *
 * @param where     The cell in question.
 * @param pow       The power with which the spell is being cast.
 * @param agent     The agent (player or monster) doing the irradiating.
 */
static int _irradiate_cell(coord_def where, int pow, actor *agent)
{
    monster *mons = monster_at(where);
    if (!mons || !mons->alive())
        return 0; // XXX: handle damaging the player for mons casts...?

    const dice_def dam_dice = irradiate_damage(pow);
    const int base_dam = dam_dice.roll();
    const int dam = mons->apply_ac(base_dam);
    mprf("%s is blasted with magical radiation%s",
         mons->name(DESC_THE).c_str(),
         attack_strength_punctuation(dam).c_str());

    if (agent->deity() == GOD_FEDHAS && fedhas_protects(*mons))
    {
        simple_god_message(
                    make_stringf(" protects %s plant from harm.",
                        agent->is_player() ? "your" : "a").c_str(),
                    GOD_FEDHAS);
        return 0;
    }

    if (agent->is_player())
        _player_hurt_monster(*mons, dam, BEAM_MMISSILE);
    else if (dam)
        mons->hurt(agent, dam, BEAM_MMISSILE);

    if (mons->alive())
        mons->malmutate("");

    return dam;
}

/**
 * Attempt to cast the spell "Irradiate", damaging & deforming enemies around
 * the player.
 *
 * @param pow   The power at which the spell is being cast.
 * @param who   The actor doing the irradiating.
 * @param fail  Whether the player has failed to cast the spell.
 * @return      spret::abort if the player changed their mind about casting after
 *              realizing they would hit an ally; spret::fail if they failed the
 *              cast chance; spret::success otherwise.
 */
spret cast_irradiate(int powc, actor* who, bool fail)
{
    if (!_irradiate_is_safe())
        return spret::abort;

    fail_check();

    ASSERT(who);
    if (who->is_player())
        mpr("You erupt in a fountain of uncontrolled magic!");
    else
    {
        simple_monster_message(*who->as_monster(),
                               " erupts in a fountain of uncontrolled magic!");
    }

    bolt beam;
    beam.name = "irradiate";
    beam.flavour = BEAM_VISUAL;
    beam.set_agent(&you);
    beam.colour = ETC_MUTAGENIC;
    beam.glyph = dchar_glyph(DCHAR_EXPLOSION);
    beam.range = 1;
    beam.ex_size = 1;
    beam.is_explosion = true;
    beam.explode_delay = beam.explode_delay * 3 / 2;
    beam.source = you.pos();
    beam.target = you.pos();
    beam.hit = AUTOMATIC_HIT;
    beam.loudness = 0;
    beam.explode(true, true);

    apply_random_around_square([powc, who] (coord_def where) {
        return _irradiate_cell(where, powc, who);
    }, who->pos(), true, 8);

    if (who->is_player())
    {
        contaminate_player(700 + random2(300));
        mprf(MSGCH_WARN, "Your magic feels %stainted.",
             you.duration[DUR_SAP_MAGIC] ? "more " : "");
        you.increase_duration(DUR_SAP_MAGIC, random_range(8, 12), 50);
    }
    return spret::success;
}

// How much work can we consider we'll have done by igniting a cloud here?
// Considers a cloud under a susceptible ally bad, a cloud under a a susceptible
// enemy good, and other clouds relatively unimportant.
static int _ignite_tracer_cloud_value(coord_def where, actor *agent)
{
    actor* act = actor_at(where);
    if (act)
    {
        const int dam = actor_cloud_immune(*act, CLOUD_FIRE)
                        ? 0
                        : resist_adjust_damage(act, BEAM_FIRE, 40);

        if (agent->deity() == GOD_FEDHAS && agent->is_player()
            && !act->is_player() && fedhas_protects(*act->as_monster()))
        {
            return 0;
        }

        return mons_aligned(act, agent) ? -dam : dam;
    }
    // We've done something, but its value is indeterminate
    else
        return 1;
}

/**
 * Turn poisonous clouds in the given tile into flame clouds, by the power of
 * Ignite Poison.
 *
 * @param where     The tile in question.
 * @param pow       The power with which Ignite Poison is being cast.
 *                  If -1, this indicates the spell is a test-run 'tracer'.
 * @param agent     The caster of Ignite Poison.
 * @return          If we're just running a tracer, return the expected 'value'
 *                  of creating fire clouds in the given location (could be
 *                  negative if there are allies there).
 *                  If it's not a tracer, return 1 if a flame cloud is created
 *                  and 0 otherwise.
 */
static int _ignite_poison_clouds(coord_def where, int pow, actor *agent)
{
    const bool tracer = (pow == -1);  // Only testing damage, not dealing it

    cloud_struct* cloud = cloud_at(where);
    if (!cloud)
        return false;

    if (cloud->type != CLOUD_MEPHITIC && cloud->type != CLOUD_POISON)
        return false;

    if (tracer)
    {
        const int value = _ignite_tracer_cloud_value(where, agent);
        // Player doesn't care about magnitude.
        return agent && agent->is_player() ? sgn(value) : value;
    }

    cloud->type = CLOUD_FIRE;
    cloud->decay = 30 + random2(20 + pow); // from 3-5 turns to 3-15 turns
    cloud->whose = agent->kill_alignment();
    cloud->killer = agent->is_player() ? KILL_YOU_MISSILE : KILL_MON_MISSILE;
    cloud->source = agent->mid;
    return true;
}

/**
 * Burn poisoned monsters in the given tile, removing their poison state &
 * damaging them.
 *
 * @param where     The tile in question.
 * @param pow       The power with which Ignite Poison is being cast.
 *                  If -1, this indicates the spell is a test-run 'tracer'.
 * @param agent     The caster of Ignite Poison.
 * @return          If we're just running a tracer, return the expected damage
 *                  of burning the monster in the given location (could be
 *                  negative if there are allies there).
 *                  If it's not a tracer, return 1 if damage is caused & 0
 *                  otherwise.
 */
static int _ignite_poison_monsters(coord_def where, int pow, actor *agent)
{
    bolt beam;
    beam.flavour = BEAM_FIRE;   // This is dumb, only used for adjust!

    const bool tracer = (pow == -1);  // Only testing damage, not dealing it
    if (tracer)                       // Give some fake damage to test resists
        pow = 100;

    // If a monster casts Ignite Poison, it can't hit itself.
    // This doesn't apply to the other functions: it can ignite
    // clouds where it's standing!

    monster* mon = monster_at(where);
    if (invalid_monster(mon) || mon == agent)
        return 0;

    // how poisoned is the victim?
    const mon_enchant ench = mon->get_ench(ENCH_POISON);
    const int pois_str = ench.ench == ENCH_NONE ? 0 : ench.degree;

    // poison currently does roughly 6 damage per degree (over its duration)
    // do roughly 1.5x to 4.5x that much, scaling with spellpower
    const dice_def dam_dice(pois_str * 2, 9 + div_rand_round(pow * 18, 100));

    const int base_dam = dam_dice.roll();
    const int damage = mons_adjust_flavoured(mon, beam, base_dam, false);
    if (damage <= 0)
        return 0;

    if (agent && agent->deity() == GOD_FEDHAS && fedhas_protects(*mon))
    {
        if (!tracer)
        {
            simple_god_message(
                        make_stringf(" protects %s plant from harm.",
                            agent->is_player() ? "your" : "a").c_str(),
                        GOD_FEDHAS);
        }
        return 0;
    }

    mon->expose_to_element(BEAM_FIRE, damage);

    if (tracer)
    {
        // players don't care about magnitude, just care if enemies exist
        if (agent && agent->is_player())
            return mons_aligned(mon, agent) ? -1 : 1;
        return mons_aligned(mon, agent) ? -1 * damage : damage;
    }

    if (you.see_cell(mon->pos()))
    {
        mprf("%s seems to burn from within%s",
             mon->name(DESC_THE).c_str(),
             attack_strength_punctuation(damage).c_str());
    }

    dprf("Dice: %dd%d; Damage: %d", dam_dice.num, dam_dice.size, damage);

    mon->hurt(agent, damage);

    if (mon->alive())
    {
        behaviour_event(mon, ME_WHACK, agent);

        // Monster survived, remove any poison.
        mon->del_ench(ENCH_POISON, true); // suppress spam
        print_wounds(*mon);
    }

    return 1;
}

/**
 * Burn poisoned players in the given tile, removing their poison state &
 * damaging them.
 *
 * @param where     The tile in question.
 * @param pow       The power with which Ignite Poison is being cast.
 *                  If -1, this indicates the spell is a test-run 'tracer'.
 * @param agent     The caster of Ignite Poison.
 * @return          If we're just running a tracer, return the expected damage
 *                  of burning the player in the given location (could be
 *                  negative if the player is an ally).
 *                  If it's not a tracer, return 1 if damage is caused & 0
 *                  otherwise.
 */

static int _ignite_poison_player(coord_def where, int pow, actor *agent)
{
    if (agent->is_player() || where != you.pos())
        return 0;

    const bool tracer = (pow == -1);  // Only testing damage, not dealing it
    if (tracer)                       // Give some fake damage to test resists
        pow = 100;

    // Step down heavily beyond light poisoning (or we could easily one-shot a heavily poisoned character)
    const int pois_str = stepdown((double)you.duration[DUR_POISONING] / 5000,
                                  2.25);
    if (!pois_str)
        return 0;

    const int base_dam = roll_dice(pois_str, 5 + pow/7);
    const int damage = resist_adjust_damage(&you, BEAM_FIRE, base_dam);

    if (tracer)
        return mons_aligned(&you, agent) ? -1 * damage : damage;

    const int resist = player_res_fire();
    if (resist > 0)
        mpr("You feel like your blood is boiling!");
    else if (resist < 0)
        mpr("The poison in your system burns terribly!");
    else
        mpr("The poison in your system burns!");

    ouch(damage, KILLED_BY_BEAM, agent->mid,
         "by burning poison", you.can_see(*agent),
         agent->as_monster()->name(DESC_A, true).c_str());
    if (damage > 0)
        you.expose_to_element(BEAM_FIRE, 2);

    mprf(MSGCH_RECOVERY, "You are no longer poisoned.");
    you.duration[DUR_POISONING] = 0;

    return damage ? 1 : 0;
}

/**
 * Would casting Ignite Poison possibly harm one of the player's allies in the
 * given cell?
 *
 * @param  where    The cell in question.
 * @return          1 if there's potential harm, 0 otherwise.
 */
static int _ignite_ally_harm(const coord_def &where)
{
    if (where == you.pos())
        return 0; // you're not your own ally!
    // (prevents issues with duplicate prompts when standing in an igniteable
    // cloud)

    return (_ignite_poison_clouds(where, -1, &you) < 0)   ? 1 :
           (_ignite_poison_monsters(where, -1, &you) < 0) ? 1 :
            0;
}

/**
 * Let the player choose to abort a casting of ignite poison, if it seems
 * like a bad idea. (If they'd ignite themself.)
 *
 * @return      Whether the player chose to abort the casting.
 */
static bool maybe_abort_ignite()
{
    string prompt = "You are standing ";

    // XXX XXX XXX major code duplication (ChrisOelmueller)
    if (const cloud_struct* cloud = cloud_at(you.pos()))
    {
        if ((cloud->type == CLOUD_MEPHITIC || cloud->type == CLOUD_POISON)
            && !actor_cloud_immune(you, CLOUD_FIRE))
        {
            prompt += "in a cloud of ";
            prompt += cloud->cloud_name(true);
            prompt += "! Ignite poison anyway?";
            if (!yesno(prompt.c_str(), false, 'n'))
                return true;
        }
    }

    if (apply_area_visible(_ignite_ally_harm, you.pos()) > 0)
    {
        return !yesno("You might harm nearby allies! Ignite poison anyway?",
                      false, 'n');
    }

    return false;
}

/**
 * Does Ignite Poison affect the given creature?
 *
 * @param act       The creature in question.
 * @return          Whether Ignite Poison can directly damage the given
 *                  creature (not counting clouds).
 */
bool ignite_poison_affects(const actor* act)
{
    if (act->is_player())
        return you.duration[DUR_POISONING];
    return act->as_monster()->has_ench(ENCH_POISON);
}

/**
 * Cast the spell Ignite Poison, burning poisoned creatures and poisonous
 * clouds in LOS.
 *
 * @param agent         The spell's caster.
 * @param pow           The power with which the spell is being cast.
 * @param fail          If it's a player spell, whether the spell fail chance
 *                      was hit (whether the spell will fail as soon as the
 *                      player chooses not to abort the casting)
 * @param mon_tracer    Whether the 'casting' is just a tracer (a check to see
 *                      if it's worth actually casting)
 * @return              If it's a tracer, spret::success if the spell should
 *                      be cast & spret::abort otherwise.
 *                      If it's a real spell, spret::abort if the player chose
 *                      to abort the spell, spret::fail if they failed the cast
 *                      chance, and spret::success otherwise.
 */
spret cast_ignite_poison(actor* agent, int pow, bool fail, bool tracer)
{
    if (tracer)
    {
        // Estimate how much useful effect we'd get if we cast the spell now
        const int work = apply_area_visible([agent] (coord_def where) {
            return _ignite_poison_clouds(where, -1, agent)
                 + _ignite_poison_monsters(where, -1, agent)
                 + _ignite_poison_player(where, -1, agent);
        }, agent->pos());

        return work > 0 ? spret::success : spret::abort;
    }

    if (agent->is_player())
    {
        if (maybe_abort_ignite())
        {
            canned_msg(MSG_OK);
            return spret::abort;
        }
        fail_check();
    }

    targeter_los hitfunc(agent, LOS_NO_TRANS);
    flash_view_delay(
        agent->is_player()
            ? UA_PLAYER
            : UA_MONSTER,
        RED, 100, &hitfunc);

    mprf("%s %s the poison in %s surroundings!", agent->name(DESC_THE).c_str(),
         agent->conj_verb("ignite").c_str(),
         agent->pronoun(PRONOUN_POSSESSIVE).c_str());

    // this could conceivably cause crashes if the player dies midway through
    // maybe split it up...?
    apply_area_visible([pow, agent] (coord_def where) {
        _ignite_poison_clouds(where, pow, agent);
        _ignite_poison_monsters(where, pow, agent);
        // Only relevant if a monster is casting this spell
        // (never hurts the caster)
        _ignite_poison_player(where, pow, agent);
        return 0; // ignored
    }, agent->pos());

    return spret::success;
}

static void _ignition_square(const actor *agent, bolt beam, coord_def square, bool center)
{
    // HACK: bypass visual effect
    beam.target = square;
    beam.in_explosion_phase = true;
    beam.explosion_affect_cell(square);
    if (center)
        noisy(spell_effect_noise(SPELL_IGNITION),square);
}

spret cast_ignition(const actor *agent, int pow, bool fail)
{
    ASSERT(agent->is_player());

    fail_check();

    targeter_los hitfunc(agent, LOS_NO_TRANS);

    // Ignition affects squares that had hostile monsters on them at the time
    // of casting. This way nothing bad happens when monsters die halfway
    // through the spell.
    vector<coord_def> blast_sources;

    for (actor_near_iterator ai(agent->pos(), LOS_NO_TRANS);
         ai; ++ai)
    {
        if (ai->is_monster()
            && !ai->as_monster()->wont_attack()
            && !mons_is_firewood(*ai->as_monster())
            && !mons_is_tentacle_segment(ai->as_monster()->type)
            && !mons_is_projectile(*ai->as_monster()))
        {
            blast_sources.push_back(ai->position);
        }
    }

    if (blast_sources.empty())
        canned_msg(MSG_NOTHING_HAPPENS);
    else
    {
        mpr("The air bursts into flame!");

        vector<coord_def> blast_adjacents;

        // Used to draw explosion cells
        bolt beam_visual;
        beam_visual.set_agent(agent);
        beam_visual.flavour       = BEAM_VISUAL;
        beam_visual.glyph         = dchar_glyph(DCHAR_FIRED_BURST);
        beam_visual.colour        = RED;
        beam_visual.ex_size       = 1;
        beam_visual.is_explosion  = true;

        // Used to deal damage; invisible
        bolt beam_actual;
        zappy(ZAP_IGNITION, pow, false, beam_actual);
        beam_actual.set_agent(agent);
        beam_actual.ex_size       = 0;
        beam_actual.origin_spell  = SPELL_IGNITION;
        beam_actual.apply_beam_conducts();

#ifdef DEBUG_DIAGNOSTICS
        dprf(DIAG_BEAM, "ignition dam=%dd%d",
             beam_actual.damage.num, beam_actual.damage.size);
#endif

        // Fake "shaped" radius 1 explosions (skipping squares with friends).
        for (coord_def pos : blast_sources)
        {
            for (adjacent_iterator ai(pos); ai; ++ai)
            {
                if (cell_is_solid(*ai))
                    continue;

                actor *act = actor_at(*ai);

                // Friendly creature, don't blast this square.
                if (act && (act == agent
                            || (act->is_monster()
                                && act->as_monster()->wont_attack())))
                {
                    continue;
                }

                blast_adjacents.push_back(*ai);
                beam_visual.explosion_draw_cell(*ai);
            }
            beam_visual.explosion_draw_cell(pos);
        }
        update_screen();
        scaled_delay(50);

        // Real explosions on each individual square.
        for (coord_def pos : blast_sources)
            _ignition_square(agent, beam_actual, pos, true);
        for (coord_def pos : blast_adjacents)
            _ignition_square(agent, beam_actual, pos, false);
    }

    return spret::success;
}

static int _discharge_monsters(const coord_def &where, int pow,
                               const actor &agent, int recursions_left)
{
    actor* victim = actor_at(where);

    if (!victim || !victim->alive())
        return 0;

    int damage = 1 + div_rand_round(pow, 7);

    bolt beam;
    beam.flavour    = BEAM_ELECTRICITY; // used for mons_adjust_flavoured
    beam.glyph      = dchar_glyph(DCHAR_FIRED_ZAP);
    beam.colour     = LIGHTBLUE;
#ifdef USE_TILE
    beam.tile_beam  = -1;
#endif
    beam.draw_delay = 0;

    dprf("Static discharge on (%d,%d) pow: %d", where.x, where.y, pow);
    if (victim->is_player() || victim->res_elec() <= 0)
        beam.draw(where);

    if (victim->is_player())
    {
        damage = max(1, damage-1);
        dprf("You: static discharge damage: %d", damage);
        damage = check_your_resists(damage, BEAM_ELECTRICITY,
                                    "static discharge");
        mprf("You are struck by an arc of lightning%s",
             attack_strength_punctuation(damage).c_str());
        ouch(damage, KILLED_BY_BEAM, agent.mid, "by static electricity", true,
             agent.is_player() ? "you" : agent.name(DESC_A).c_str());
        if (damage > 0)
            victim->expose_to_element(BEAM_ELECTRICITY, 2);
    }
    // rEelec monsters don't allow arcs to continue.
    else if (victim->res_elec() > 0)
        return 0;
    else if (agent.deity() == GOD_FEDHAS
             && fedhas_protects(*victim->as_monster()))
    {
        simple_god_message(
                    make_stringf(" protects %s plant from harm.",
                        agent.is_player() ? "your" : "a").c_str(),
                    GOD_FEDHAS);
        return 0;
    }
    else
    {
        monster* mons = victim->as_monster();

        // We need to initialize these before the monster has died.
        god_conduct_trigger conducts[3];
        if (agent.is_player())
            set_attack_conducts(conducts, *mons, you.can_see(*mons));

        dprf("%s: static discharge damage: %d",
             mons->name(DESC_PLAIN, true).c_str(), damage);
        damage = mons_adjust_flavoured(mons, beam, damage);
        mprf("%s is struck by an arc of lightning%s",
                mons->name(DESC_THE).c_str(),
                attack_strength_punctuation(damage).c_str());

        if (agent.is_player())
            _player_hurt_monster(*mons, damage, beam.flavour, false);
        else if (damage)
            mons->hurt(agent.as_monster(), damage);
    }

    // Recursion to give us chain-lightning -- bwr
    // Low power slight chance added for low power characters -- bwr
    if (recursions_left > 0)
    {
        pow /= 2;
        damage += apply_random_around_square([pow, &agent, recursions_left] (coord_def where2) {
            return _discharge_monsters(where2, pow, agent, recursions_left-1);
        }, where, true, 2);
    }

    return damage;
}

bool safe_discharge(coord_def where, vector<const actor *> &exclude)
{
    for (adjacent_iterator ai(where); ai; ++ai)
    {
        const actor *act = actor_at(*ai);
        if (!act)
            continue;

        if (find(exclude.begin(), exclude.end(), act) == exclude.end())
        {
            if (act->is_monster())
            {
                // Harmless to these monsters, so don't prompt about them.
                if (act->res_elec() > 0
                    || you.deity() == GOD_FEDHAS
                       && fedhas_protects(*act->as_monster()))
                {
                    continue;
                }

                if (stop_attack_prompt(act->as_monster(), false, where))
                    return false;
            }
            // Don't prompt for the player, but always continue arcing.

            exclude.push_back(act);
            if (!safe_discharge(act->pos(), exclude))
                return false;
        }
    }

    return true;
}

spret cast_discharge(int pow, const actor &agent, bool fail, bool prompt)
{
    vector<const actor *> exclude;
    if (agent.is_player() && prompt && !safe_discharge(you.pos(), exclude))
        return spret::abort;

    fail_check();

    int num_targs = 2;
    int recursions = 1;
    const int dam =
        apply_random_around_square([pow, &agent, recursions] (coord_def target) {
            return _discharge_monsters(target, pow, agent, recursions);
        }, agent.pos(), true, num_targs);

    dprf("Damage: %d", dam);

    if (dam > 0)
        scaled_delay(100);
    else
    {
        if (coinflip())
            mpr("The air crackles with electrical energy.");
        else
        {
            const bool plural = coinflip();
            mprf("%s blue arc%s ground%s harmlessly.",
                 plural ? "Some" : "A",
                 plural ? "s" : "",
                 plural ? " themselves" : "s itself");
        }
    }
    return spret::success;
}

bool setup_fragmentation_beam(bolt &beam, int pow, const actor *caster,
                              const coord_def target, bool quiet,
                              const char **what, bool &should_destroy_wall,
                              bool &hole)
{
    beam.flavour     = BEAM_FRAG;
    beam.glyph       = dchar_glyph(DCHAR_FIRED_BURST);
    beam.source_id   = caster->mid;
    beam.thrower     = caster->is_player() ? KILL_YOU : KILL_MON;
    beam.ex_size     = 1;
    beam.source      = you.pos();
    beam.hit         = AUTOMATIC_HIT;

    beam.source_name = caster->name(DESC_PLAIN, true);
    beam.aux_source = "by Lee's Rapid Deconstruction"; // for direct attack

    beam.target = target;

    // Number of dice vary from 2-4.
    beam.damage = dice_def(0, 5 + pow / 5);

    monster* mon = monster_at(target);
    const dungeon_feature_type grid = grd(target);

    if (target == you.pos())
    {
        const bool petrified = (you.petrified() || you.petrifying());

        if (you.form == transformation::statue || you.species == SP_GARGOYLE)
        {
            beam.name       = "blast of rock fragments";
            beam.colour     = BROWN;
            beam.damage.num = you.form == transformation::statue ? 3 : 2;
            return true;
        }
        else if (petrified)
        {
            beam.name       = "blast of petrified fragments";
            beam.colour     = mons_class_colour(player_mons(true));
            beam.damage.num = 3;
            return true;
        }
        else if (you.form == transformation::ice_beast) // blast of ice
        {
            beam.name       = "icy blast";
            beam.colour     = WHITE;
            beam.damage.num = 3;
            beam.flavour    = BEAM_ICE;
            return true;
        }
    }
    else if (mon && (caster->is_monster() || (you.can_see(*mon))))
    {
        switch (mon->type)
        {
        case MONS_TOENAIL_GOLEM:
            beam.name       = "blast of toenail fragments";
            beam.colour     = RED;
            beam.damage.num = 3;
            break;

        case MONS_IRON_ELEMENTAL:
        case MONS_IRON_GOLEM:
        case MONS_PEACEKEEPER:
        case MONS_WAR_GARGOYLE:
        case MONS_ANCIENT_AUTOMATON:
            beam.name       = "blast of metal fragments";
            beam.colour     = CYAN;
            beam.damage.num = 4;
            break;

        case MONS_EARTH_ELEMENTAL:
        case MONS_ROCKSLIME:
        case MONS_USHABTI:
        case MONS_STATUE:
        case MONS_GARGOYLE:
            beam.name       = "blast of rock fragments";
            beam.colour     = BROWN;
            beam.damage.num = 3;
            break;

        case MONS_SALTLING:
            beam.name       = "blast of salt crystal fragments";
            beam.colour     = WHITE;
            beam.damage.num = 3;
            break;

        case MONS_OBSIDIAN_STATUE:
        case MONS_ORANGE_STATUE:
        case MONS_CRYSTAL_GUARDIAN:
        case MONS_ROXANNE:
            beam.ex_size    = 2;
            beam.damage.num = 4;
            if (mon->type == MONS_OBSIDIAN_STATUE)
            {
                beam.name       = "blast of obsidian shards";
                beam.colour     = DARKGREY;
            }
            else if (mon->type == MONS_ORANGE_STATUE)
            {
                beam.name       = "blast of orange crystal shards";
                beam.colour     = LIGHTRED;
            }
            else if (mon->type == MONS_CRYSTAL_GUARDIAN)
            {
                beam.name       = "blast of crystal shards";
                beam.colour     = GREEN;
            }
            else
            {
                beam.name       = "blast of sapphire shards";
                beam.colour     = BLUE;
            }
            break;

        default:
            const bool petrified = (mon->petrified() || mon->petrifying());

            // Petrifying or petrified monsters can be exploded.
            if (petrified)
            {
                monster_info minfo(mon);
                beam.name       = "blast of petrified fragments";
                beam.colour     = minfo.colour();
                beam.damage.num = 3;
                break;
            }
            else if (mon->is_icy()) // blast of ice
            {
                beam.name       = "icy blast";
                beam.colour     = WHITE;
                beam.damage.num = 3;
                beam.flavour    = BEAM_ICE;
                break;
            }
            else if (mon->is_skeletal()) // blast of bone
            {
                beam.name   = "blast of bone shards";
                beam.colour = LIGHTGREY;
                beam.damage.num = 3;
                break;
            }
            // Targeted monster not shatterable, try the terrain instead.
            goto do_terrain;
        }

        beam.aux_source = beam.name;

        // Got a target, let's blow it up.
        return true;
    }

  do_terrain:
    switch (grid)
    {
    // Stone and rock terrain
    case DNGN_ORCISH_IDOL:
        if (what && *what == nullptr)
            *what = "stone idol";
        // fall-through
    case DNGN_ROCK_WALL:
    case DNGN_SLIMY_WALL:
    case DNGN_STONE_WALL:
    case DNGN_CLEAR_ROCK_WALL:
    case DNGN_CLEAR_STONE_WALL:
        if (what && *what == nullptr)
            *what = "wall";
        // fall-through
    case DNGN_GRANITE_STATUE:
        if (what && *what == nullptr)
            *what = "statue";

        beam.name       = "blast of rock fragments";
        beam.damage.num = 3;

        if (grid == DNGN_ORCISH_IDOL
            || grid == DNGN_GRANITE_STATUE
            || pow >= 35 && (grid == DNGN_ROCK_WALL
                             || grid == DNGN_SLIMY_WALL
                             || grid == DNGN_CLEAR_ROCK_WALL)
               && one_chance_in(3)
            || pow >= 75 && (grid == DNGN_STONE_WALL
                             || grid == DNGN_CLEAR_STONE_WALL)
               && one_chance_in(5))
        {
            should_destroy_wall = true;
        }
        break;

    // Metal -- small but nasty explosion
    case DNGN_METAL_WALL:
        if (what)
            *what = "metal wall";
        // fall-through
    case DNGN_GRATE:
        if (what && *what == nullptr)
            *what = "iron grate";
        beam.name       = "blast of metal fragments";
        beam.damage.num = 4;

        if (grid == DNGN_GRATE)
        {
            should_destroy_wall = true;
        }
        break;

    // Crystal
    case DNGN_CRYSTAL_WALL:       // crystal -- large & nasty explosion
        if (what)
            *what = "crystal wall";
        beam.ex_size    = 2;
        beam.name       = "blast of crystal shards";
        beam.damage.num = 4;

        if (one_chance_in(3))
            should_destroy_wall = true;
        break;

    // Stone arches and doors
    case DNGN_OPEN_DOOR:
    case DNGN_OPEN_CLEAR_DOOR:
    case DNGN_CLOSED_DOOR:
    case DNGN_CLOSED_CLEAR_DOOR:
    case DNGN_RUNED_DOOR:
    case DNGN_RUNED_CLEAR_DOOR:
    case DNGN_SEALED_DOOR:
    case DNGN_SEALED_CLEAR_DOOR:
        if (what)
            *what = "door";
        should_destroy_wall = true;
        // fall-through
    case DNGN_STONE_ARCH:
        if (what && *what == nullptr)
            *what = "stone arch";
        hole            = false;  // to hit monsters standing on doors
        beam.name       = "blast of rock fragments";
        beam.damage.num = 3;
        break;

    default:
        // Couldn't find a monster or wall to shatter - abort casting!
        if (caster->is_player() && !quiet)
            mpr("You can't deconstruct that!");
        return false;
    }

    // If it was recoloured, use that colour instead.
    if (env.grid_colours(target))
        beam.colour = env.grid_colours(target);
    else
    {
        beam.colour = element_colour(get_feature_def(grid).colour(),
                                     false, target);
    }

    beam.aux_source = beam.name;

    return true;
}

spret cast_fragmentation(int pow, const actor *caster,
                              const coord_def target, bool fail)
{
    bool should_destroy_wall = false;
    bool hole                = true;
    const char *what         = nullptr;

    bolt beam;

    // should_destroy_wall is an output argument.
    if (!setup_fragmentation_beam(beam, pow, caster, target, false, &what,
                                  should_destroy_wall, hole))
    {
        return spret::abort;
    }

    if (caster->is_player())
    {
        bolt tempbeam;
        bool temp;
        setup_fragmentation_beam(tempbeam, pow, caster, target, true, nullptr,
                                 temp, temp);
        tempbeam.is_tracer = true;
        tempbeam.explode(false);
        if (tempbeam.beam_cancelled)
        {
            canned_msg(MSG_OK);
            return spret::abort;
        }
    }

    fail_check();

    if (what != nullptr) // Terrain explodes.
    {
        if (you.see_cell(target))
            mprf("The %s shatters!", what);
        if (should_destroy_wall)
            destroy_wall(target);
    }
    else if (target == you.pos()) // You explode.
    {
        const int dam = beam.damage.roll();
        mprf("You shatter%s", attack_strength_punctuation(dam).c_str());

        ouch(dam, KILLED_BY_BEAM, caster->mid,
             "by Lee's Rapid Deconstruction", true,
             caster->is_player() ? "you"
                                 : caster->name(DESC_A).c_str());
    }
    else // Monster explodes.
    {
        // Checks by setup_fragmentation_beam() must guarantee that we have a
        // monster.
        monster* mon = monster_at(target);
        ASSERT(mon);

        const int dam = beam.damage.roll();
        if (you.see_cell(target))
        {
            mprf("%s shatters%s", mon->name(DESC_THE).c_str(),
                 attack_strength_punctuation(dam).c_str());
        }

        if (caster->is_player())
            _player_hurt_monster(*mon, dam, BEAM_DISINTEGRATION);
        else if (dam)
            mon->hurt(caster, dam, BEAM_DISINTEGRATION);
    }

    beam.explode(true, hole);

    return spret::success;
}

spret cast_sandblast(int pow, bolt &beam, bool fail)
{
    item_def *stone = nullptr;
    int num_stones = 0;
    for (item_def& i : you.inv)
    {
        if (i.is_type(OBJ_MISSILES, MI_STONE)
            && check_warning_inscriptions(i, OPER_DESTROY))
        {
            num_stones += i.quantity / 10;
            stone = &i;
        }
    }

    if (num_stones == 0)
    {
        mpr("You don't have enough stones to cast with.");
        return spret::abort;
    }

    zap_type zap = ZAP_SANDBLAST;
    const spret ret = zapping(zap, pow, beam, true, nullptr, fail);

    if (ret == spret::success)
    {
        if (dec_inv_item_quantity(letter_to_index(stone->slot), 10)
            || stone->quantity < 10)
            mpr("You now have insufficient stones remaining to "
                "continue to cast sandblast.");
        else
            mprf_nocap("%s", stone->name(DESC_INVENTORY).c_str());
    }

    return ret;
}

static bool _elec_not_immune(const actor *act)
{
    return act->res_elec() < 3 && !(you_worship(GOD_FEDHAS)
                                    && fedhas_protects(*act->as_monster()));
}

coord_def get_thunderbolt_last_aim(actor *caster)
{
    const int &last_turn = caster->props[THUNDERBOLT_LAST_KEY].get_int();
    const coord_def &last_aim = caster->props[THUNDERBOLT_AIM_KEY].get_coord();

    // check against you.pos() in case the player has moved instantaneously,
    // via mesmerise, wjc, etc. In principle, this should probably also
    // record and check the player's location on their last cast.
    if (last_turn && last_turn + 1 == you.num_turns && last_aim != you.pos())
        return last_aim;
    else
        return coord_def();
}

static void _set_thundervolt_last_aim(actor *caster, coord_def aim)
{
    int &last_turn = caster->props[THUNDERBOLT_LAST_KEY].get_int();
    coord_def &last_aim = caster->props[THUNDERBOLT_AIM_KEY].get_coord();

    last_turn = you.num_turns;
    last_aim = aim;
}

spret cast_thunderbolt(actor *caster, int pow, coord_def aim, bool fail)
{
    coord_def prev = get_thunderbolt_last_aim(caster);
    targeter_thunderbolt hitfunc(caster, spell_range(SPELL_THUNDERBOLT, pow), prev);
    hitfunc.set_aim(aim);

    if (caster->is_player()
        && stop_attack_prompt(hitfunc, "zap", _elec_not_immune))
    {
        return spret::abort;
    }

    fail_check();

    bolt beam;
    beam.name              = "thunderbolt";
    beam.aux_source        = "lightning rod";
    beam.flavour           = BEAM_ELECTRICITY;
    beam.glyph             = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour            = LIGHTCYAN;
    beam.range             = 1;
    beam.hit               = AUTOMATIC_HIT;
    beam.ac_rule           = AC_PROPORTIONAL;
    beam.set_agent(caster);
#ifdef USE_TILE
    beam.tile_beam = -1;
#endif
    beam.draw_delay = 0;

    for (const auto &entry : hitfunc.zapped)
    {
        if (entry.second <= 0)
            continue;

        beam.draw(entry.first);
    }

    scaled_delay(200);

    beam.glyph = 0; // FIXME: a hack to avoid "appears out of thin air"

    for (const auto &entry : hitfunc.zapped)
    {
        if (entry.second <= 0)
            continue;

        // beams are incredibly spammy in debug mode
        if (!actor_at(entry.first))
            continue;

        int arc = hitfunc.arc_length[entry.first.distance_from(hitfunc.origin)];
        ASSERT(arc > 0);
        dprf("at distance %d, arc length is %d",
             entry.first.distance_from(hitfunc.origin), arc);
        beam.source = beam.target = entry.first;
        beam.source.x -= sgn(beam.source.x - hitfunc.origin.x);
        beam.source.y -= sgn(beam.source.y - hitfunc.origin.y);
        beam.damage = dice_def(4, div_rand_round(pow * 7, 8*(arc + 4)));
        beam.fire();
    }

    _set_thundervolt_last_aim(caster, aim);
    return spret::success;
}

// Find an enemy who would suffer from Awaken Forest.
actor* forest_near_enemy(const actor *mon)
{
    const coord_def pos = mon->pos();

    for (radius_iterator ri(pos, LOS_NO_TRANS); ri; ++ri)
    {
        actor* foe = actor_at(*ri);
        if (!foe || mons_aligned(foe, mon))
            continue;

        for (adjacent_iterator ai(*ri); ai; ++ai)
            if (feat_is_tree(grd(*ai)) && cell_see_cell(pos, *ai, LOS_DEFAULT))
                return foe;
    }

    return nullptr;
}

// Print a message only if you can see any affected trees.
void forest_message(const coord_def pos, const string &msg, msg_channel_type ch)
{
    for (radius_iterator ri(pos, LOS_DEFAULT); ri; ++ri)
        if (feat_is_tree(grd(*ri))
            && cell_see_cell(you.pos(), *ri, LOS_DEFAULT))
        {
            mprf(ch, "%s", msg.c_str());
            return;
        }
}

void forest_damage(const actor *mon)
{
    const coord_def pos = mon->pos();
    const int hd = mon->get_hit_dice();

    if (one_chance_in(4))
    {
        forest_message(pos, random_choose(
            "The trees move their gnarly branches around.",
            "You feel roots moving beneath the ground.",
            "Branches wave dangerously above you.",
            "Trunks creak and shift.",
            "Tree limbs sway around you."), MSGCH_TALK_VISUAL);
    }

    for (radius_iterator ri(pos, LOS_NO_TRANS); ri; ++ri)
    {
        actor* foe = actor_at(*ri);
        if (!foe || mons_aligned(foe, mon))
            continue;

        if (is_sanctuary(foe->pos()))
            continue;

        for (adjacent_iterator ai(*ri); ai; ++ai)
            if (feat_is_tree(grd(*ai)) && cell_see_cell(pos, *ai, LOS_NO_TRANS))
            {
                int dmg = 0;
                string msg;

                if (!apply_chunked_AC(1, foe->evasion(EV_IGNORE_NONE, mon)))
                {
                    msg = random_choose(
                            "@foe@ @is@ waved at by a branch",
                            "A tree reaches out but misses @foe@",
                            "A root lunges up near @foe@");
                }
                else if (!(dmg = foe->apply_ac(hd + random2(hd), hd * 2 - 1,
                                               AC_PROPORTIONAL)))
                {
                    msg = random_choose(
                            "@foe@ @is@ scraped by a branch",
                            "A tree reaches out and scrapes @foe@",
                            "A root barely touches @foe@ from below");
                    if (foe->is_monster())
                        behaviour_event(foe->as_monster(), ME_WHACK);
                }
                else
                {
                    msg = random_choose(
                        "@foe@ @is@ hit by a branch",
                        "A tree reaches out and hits @foe@",
                        "A root smacks @foe@ from below");
                    if (foe->is_monster())
                        behaviour_event(foe->as_monster(), ME_WHACK);
                }

                msg = replace_all(replace_all(msg,
                    "@foe@", foe->name(DESC_THE)),
                    "@is@", foe->conj_verb("be"))
                    + attack_strength_punctuation(dmg);
                if (you.see_cell(foe->pos()))
                    mpr(msg);

                if (dmg <= 0)
                    break;

                foe->hurt(mon, dmg, BEAM_MISSILE, KILLED_BY_BEAM, "",
                          "by angry trees");

                break;
            }
    }
}

vector<bolt> get_spray_rays(const actor *caster, coord_def aim, int range,
                            int max_rays, int max_spacing)
{
    coord_def aim_dir = (caster->pos() - aim).sgn();

    int num_targets = 0;
    vector<bolt> beams;

    bolt base_beam;

    base_beam.set_agent(caster);
    base_beam.attitude = caster->is_player() ? ATT_FRIENDLY
                                             : caster->as_monster()->attitude;
    base_beam.is_tracer = true;
    base_beam.is_targeting = true;
    base_beam.dont_stop_player = true;
    base_beam.friend_info.dont_stop = true;
    base_beam.foe_info.dont_stop = true;
    base_beam.range = range;
    base_beam.source = caster->pos();
    base_beam.target = aim;

    bolt center_beam = base_beam;
    center_beam.hit = AUTOMATIC_HIT;
    center_beam.fire();
    center_beam.target = center_beam.path_taken.back();
    center_beam.hit = 1;
    center_beam.fire();
    center_beam.is_tracer = false;
    center_beam.dont_stop_player = false;
    center_beam.foe_info.dont_stop = false;
    center_beam.friend_info.dont_stop = false;
    // Prevent self-hits, specifically when you aim at an adjacent wall.
    if (center_beam.path_taken.back() != caster->pos())
        beams.push_back(center_beam);

    for (distance_iterator di(aim, false, false, max_spacing); di; ++di)
    {
        if (monster_at(*di))
        {
            coord_def delta = caster->pos() - *di;

            //Don't aim secondary rays at friendlies
            if (mons_aligned(caster, monster_at(*di)))
                continue;

            if (!caster->can_see(*monster_at(*di)))
                continue;

            //Don't try to aim at a target if it's out of range
            if (delta.rdist() > range)
                continue;

            //Don't try to aim at targets in the opposite direction of main aim
            if (abs(aim_dir.x - delta.sgn().x) + abs(aim_dir.y - delta.sgn().y) >= 2)
                continue;

            //Test if this beam stops at a location used by any prior beam
            bolt testbeam = base_beam;
            testbeam.target = *di;
            testbeam.hit = AUTOMATIC_HIT;
            testbeam.fire();
            bool duplicate = false;

            for (const bolt &beam : beams)
            {
                if (testbeam.path_taken.back() == beam.target)
                {
                    duplicate = true;
                    continue;
                }
            }
            if (!duplicate)
            {
                bolt tempbeam = base_beam;
                tempbeam.target = testbeam.path_taken.back();
                tempbeam.fire();
                tempbeam.is_tracer = false;
                tempbeam.is_targeting = false;
                tempbeam.dont_stop_player = false;
                tempbeam.foe_info.dont_stop = false;
                tempbeam.friend_info.dont_stop = false;
                beams.push_back(tempbeam);
                num_targets++;
            }

            if (num_targets == max_rays - 1)
              break;
        }
    }

    return beams;
}

static bool _dazzle_can_hit(const actor *act)
{
    if (act->is_monster())
    {
        const monster* mons = act->as_monster();
        bolt testbeam;
        testbeam.thrower = KILL_YOU;
        zappy(ZAP_DAZZLING_SPRAY, 100, false, testbeam);

        return !testbeam.ignores_monster(mons);
    }
    else
        return false;
}

spret cast_dazzling_spray(int pow, coord_def aim, bool fail)
{
    int range = spell_range(SPELL_DAZZLING_SPRAY, pow);

    targeter_spray hitfunc(&you, range, ZAP_DAZZLING_SPRAY);
    hitfunc.set_aim(aim);
    if (stop_attack_prompt(hitfunc, "fire towards", _dazzle_can_hit))
        return spret::abort;

    fail_check();

    if (hitfunc.beams.size() == 0)
    {
        mpr("You can't see any targets in that direction!");
        return spret::abort;
    }

    for (bolt &beam : hitfunc.beams)
    {
        zappy(ZAP_DAZZLING_SPRAY, pow, false, beam);
        beam.fire();
    }

    return spret::success;
}

static bool _toxic_can_affect(const actor *act)
{
    return act->res_poison() < 2;
}

spret cast_toxic_radiance(actor *agent, int pow, bool fail, bool mon_tracer)
{
    if (agent->is_player())
    {
        targeter_los hitfunc(&you, LOS_NO_TRANS);
        {
            if (stop_attack_prompt(hitfunc, "poison", _toxic_can_affect))
                return spret::abort;
        }
        fail_check();

        if (!you.duration[DUR_TOXIC_RADIANCE])
            mpr("You begin to radiate toxic energy.");
        else
            mpr("Your toxic radiance grows in intensity.");

        you.increase_duration(DUR_TOXIC_RADIANCE, 2 + random2(pow/20), 15);
        toxic_radiance_effect(&you, 10, true);

        flash_view_delay(UA_PLAYER, GREEN, 300, &hitfunc);

        return spret::success;
    }
    else if (mon_tracer)
    {
        for (actor_near_iterator ai(agent->pos(), LOS_NO_TRANS); ai; ++ai)
        {
            if (!_toxic_can_affect(*ai) || mons_aligned(agent, *ai))
                continue;
            else
                return spret::success;
        }

        // Didn't find any susceptible targets
        return spret::abort;
    }
    else
    {
        monster* mon_agent = agent->as_monster();
        simple_monster_message(*mon_agent,
                               " begins to radiate toxic energy.");

        mon_agent->add_ench(mon_enchant(ENCH_TOXIC_RADIANCE, 1, mon_agent,
                                        (4 + random2avg(pow/15, 2)) * BASELINE_DELAY));
        toxic_radiance_effect(agent, 10);

        targeter_los hitfunc(mon_agent, LOS_NO_TRANS);
        flash_view_delay(UA_MONSTER, GREEN, 300, &hitfunc);

        return spret::success;
    }
}

/*
 * Attempt to poison all monsters in line of sight of the caster.
 *
 * @param agent   The caster.
 * @param mult    A number to multiply the damage by.
 *                This is the time taken for the player's action in auts,
 *                or 10 if the spell was cast this turn.
 * @param on_cast Whether the spell was cast this turn. This only matters
 *                if the player cast the spell. If true, we trigger conducts
 *                if the player hurts allies; if false, we don't, to avoid
 *                the player being accidentally put under penance.
 *                Defaults to false.
 */
void toxic_radiance_effect(actor* agent, int mult, bool on_cast)
{
    int pow;
    if (agent->is_player())
        pow = calc_spell_power(SPELL_OLGREBS_TOXIC_RADIANCE, true);
    else
        pow = agent->as_monster()->get_hit_dice() * 8;

    bool break_sanctuary = (agent->is_player() && is_sanctuary(you.pos()));

    for (actor_near_iterator ai(agent->pos(), LOS_NO_TRANS); ai; ++ai)
    {
        if (!_toxic_can_affect(*ai))
            continue;

        // Monsters can skip hurting friendlies
        if (agent->is_monster() && mons_aligned(agent, *ai))
            continue;

        if (ai->is_player())
        {
            // We're affected only if we're not the agent.
            if (!agent->is_player())
            {
                int dam = roll_dice(2, 1 + div_rand_round(pow, 20)) * div_rand_round(mult, BASELINE_DELAY);
                poison_player(dam, agent->name(DESC_A), "toxic radiance", false);
            }
        }
        else
        {
            // We need to deal with conducts before damaging the monster,
            // because otherwise friendly monsters that are one-shot won't
            // trigger conducts. Only trigger conducts on the turn the player
            // casts the spell (see PR #999).
            if (on_cast && agent->is_player())
            {
                god_conduct_trigger conducts[3];
                set_attack_conducts(conducts, *ai->as_monster());
                if (is_sanctuary(ai->pos()))
                    break_sanctuary = true;
            }

            if (ai->alive())
            {
                behaviour_event(ai->as_monster(), ME_ANNOY, agent, agent->pos());
                
                int HD = ai->get_hit_dice();
                int rpois = max(0, ai->res_poison());
                int power = (pow * mult) / (rpois + 1);
                int pois_amount = div_rand_round(power, HD * 16 * BASELINE_DELAY);
                if(pois_amount)
                    poison_monster(ai->as_monster(), agent, pois_amount);
            }
        }
    }

    if (break_sanctuary)
        remove_sanctuary(true);
}

spret cast_searing_ray(int pow, bolt &beam, bool fail)
{
    const spret ret = zapping(ZAP_SEARING_RAY_I, pow, beam, true, nullptr,
                                   fail);

    if (ret == spret::success)
    {
        // Special value, used to avoid terminating ray immediately, since we
        // took a non-wait action on this turn (ie: casting it)
        you.attribute[ATTR_SEARING_RAY] = -1;
        you.props["searing_ray_target"].get_coord() = beam.target;
        you.props["searing_ray_aimed_at_spot"].get_bool() = beam.aimed_at_spot;
        string msg = "(Press <w>%</w> to maintain the ray.)";
        insert_commands(msg, { CMD_WAIT });
        mpr(msg);
    }

    return ret;
}

void handle_searing_ray()
{
    ASSERT_RANGE(you.attribute[ATTR_SEARING_RAY], 1, 4);

    // All of these effects interrupt a channeled ray
    if (you.confused() || you.berserk())
    {
        end_searing_ray();
        return;
    }

    int ray_power_level = min(2, (you.attribute[ATTR_SEARING_RAY]-1));
    int required_mp = ray_power_level >= 2 ? 2 : 1;

    if (!enough_mp(required_mp, true))
    {
        mpr("Without enough magic to sustain it, your searing ray dissipates.");
        end_searing_ray();
        return;
    }

    const zap_type zap = zap_type(ZAP_SEARING_RAY_I + ray_power_level);
    const int pow = calc_spell_power(SPELL_SEARING_RAY, true);

    bolt beam;
    beam.thrower = KILL_YOU_MISSILE;
    beam.range   = calc_spell_range(SPELL_SEARING_RAY, pow);
    beam.source  = you.pos();
    beam.target  = you.props["searing_ray_target"].get_coord();
    beam.aimed_at_spot = you.props["searing_ray_aimed_at_spot"].get_bool();

    // If friendlies have moved into the beam path, give a chance to abort
    if (!player_tracer(zap, pow, beam))
    {
        mpr("You stop channeling your searing ray.");
        end_searing_ray();
        return;
    }

    zappy(zap, pow, false, beam);

    aim_battlesphere(&you, SPELL_SEARING_RAY, pow, beam);
    beam.fire();
    trigger_battlesphere(&you, beam);

    dec_mp(required_mp);

    if (you.attribute[ATTR_SEARING_RAY] < 3)
        you.attribute[ATTR_SEARING_RAY]++;
}

void end_searing_ray()
{
    you.attribute[ATTR_SEARING_RAY] = 0;
    you.props.erase("searing_ray_target");
    you.props.erase("searing_ray_aimed_at_spot");
}

/**
 * Can a casting of Glaciate by the player injure the given creature?
 *
 * @param victim        The potential victim.
 * @return              Whether Glaciate can harm that victim.
 *                      (False for IOODs or friendly battlespheres.)
 */
static bool _player_glaciate_affects(const actor *victim)
{
    // TODO: deduplicate this with beam::ignores
    if (!victim)
        return false;

    const monster* mon = victim->as_monster();
    if (!mon) // player
        return true;

    return !mons_is_projectile(*mon)
            && (!mons_is_avatar(mon->type) || !mons_aligned(&you, mon));
}

spret cast_glaciate(actor *caster, int pow, coord_def aim, bool fail)
{
    const int range = spell_range(SPELL_GLACIATE, pow);
    targeter_cone hitfunc(caster, range);
    hitfunc.set_aim(aim);

    if (caster->is_player()
        && stop_attack_prompt(hitfunc, "glaciate", _player_glaciate_affects))
    {
        return spret::abort;
    }

    fail_check();

    bolt beam;
    beam.name              = "great icy blast";
    beam.aux_source        = "great icy blast";
    beam.flavour           = BEAM_ICE;
    beam.glyph             = dchar_glyph(DCHAR_EXPLOSION);
    beam.colour            = WHITE;
    beam.range             = 1;
    beam.hit               = AUTOMATIC_HIT;
    beam.source_id         = caster->mid;
    beam.hit_verb          = "engulfs";
    beam.origin_spell      = SPELL_GLACIATE;
    beam.set_agent(caster);
#ifdef USE_TILE
    beam.tile_beam = -1;
#endif
    beam.draw_delay = 0;

    for (int i = 1; i <= range; i++)
    {
        for (const auto &entry : hitfunc.sweep[i])
        {
            if (entry.second <= 0)
                continue;

            beam.draw(entry.first);
        }
        scaled_delay(25);
    }

    scaled_delay(100);

    if (you.can_see(*caster) || caster->is_player())
    {
        mprf("%s %s a mighty blast of ice!",
             caster->name(DESC_THE).c_str(),
             caster->conj_verb("conjure").c_str());
    }

    beam.glyph = 0;

    for (int i = 1; i <= range; i++)
    {
        for (const auto &entry : hitfunc.sweep[i])
        {
            if (entry.second <= 0)
                continue;

            const int eff_range = max(3, (6 * i / LOS_DEFAULT_RANGE));

            // At or within range 3, this is equivalent to the old Ice Storm
            // damage.
            beam.damage =
                caster->is_player()
                    ? calc_dice(7, (66 + 3 * pow) / eff_range)
                    : calc_dice(10, (54 + 3 * pow / 2) / eff_range);

            if (actor_at(entry.first))
            {
                beam.source = beam.target = entry.first;
                beam.source.x -= sgn(beam.source.x - hitfunc.origin.x);
                beam.source.y -= sgn(beam.source.y - hitfunc.origin.y);
                beam.fire();
            }
            place_cloud(CLOUD_COLD, entry.first,
                        (18 + random2avg(45,2)) / eff_range, caster);
        }
    }

    noisy(spell_effect_noise(SPELL_GLACIATE), hitfunc.origin);

    return spret::success;
}

spret cast_random_bolt(int pow, bolt& beam, bool fail)
{
    // Need to use a 'generic' tracer regardless of the actual beam type,
    // to account for the possibility of both bouncing and irresistible damage
    // (even though only one of these two ever occurs on the same bolt type).
    bolt tracer = beam;
    if (!player_tracer(ZAP_RANDOM_BOLT_TRACER, 200, tracer))
        return spret::abort;

    fail_check();

    zap_type zap = random_choose(ZAP_BOLT_OF_FIRE,
                                 ZAP_BOLT_OF_COLD,
                                 ZAP_VENOM_BOLT,
                                 ZAP_BOLT_OF_DRAINING,
                                 ZAP_QUICKSILVER_BOLT,
                                 ZAP_CRYSTAL_BOLT,
                                 ZAP_LIGHTNING_BOLT,
                                 ZAP_CORROSIVE_BOLT);
    beam.origin_spell = SPELL_NO_SPELL; // let zapping reset this
    zapping(zap, pow * 7 / 6 + 15, beam, false);

    return spret::success;
}

size_t shotgun_beam_count(int pow)
{
    return 1 + stepdown((pow - 5) / 3, 5, ROUND_CLOSE);
}

spret cast_scattershot(const actor *caster, int pow, const coord_def &pos,
                            bool fail)
{
    const size_t range = spell_range(SPELL_SCATTERSHOT, pow);
    const size_t beam_count = shotgun_beam_count(pow);

    targeter_shotgun hitfunc(caster, beam_count, range);

    hitfunc.set_aim(pos);

    if (caster->is_player())
    {
        if (stop_attack_prompt(hitfunc, "scattershot"))
            return spret::abort;
    }

    fail_check();

    bolt beam;
    beam.thrower = (caster && caster->is_player()) ? KILL_YOU :
                   (caster)                        ? KILL_MON
                                                   : KILL_MISC;
    beam.range       = range;
    beam.source      = caster->pos();
    beam.source_id   = caster->mid;
    beam.source_name = caster->name(DESC_PLAIN, true);
    zappy(ZAP_SCATTERSHOT, pow, false, beam);
    beam.aux_source  = beam.name;
    beam.draw_delay = 5;   // in ms, 1/3 the default

    if (!caster->is_player())
        beam.damage   = dice_def(3, 4 + (pow / 18));

    // Choose a random number of 'pellets' to fire for each beam in the spread.
    // total pellets has O(beam_count^2)
    vector<size_t> pellets;
    pellets.resize(beam_count);
    for (size_t i = 0; i < beam_count; i++)
        pellets[random2avg(beam_count, 3)]++;

    map<mid_t, int> hit_count;

    // for each beam of pellets...
    for (size_t i = 0; i < beam_count; i++)
    {
        // find the beam's path.
        ray_def ray = hitfunc.rays[i];
        for (size_t j = 0; j < range; j++)
            ray.advance();

        // fire the beam once per pellet.
        for (size_t j = 0; j < pellets[i]; j++)
        {
            bolt tempbeam = beam;
            tempbeam.draw_delay = 0;
            tempbeam.target = ray.pos();
            tempbeam.fire();
            scaled_delay(5);
            for (auto it : tempbeam.hit_count)
               hit_count[it.first] += it.second;
        }
    }

    for (auto it : hit_count)
    {
        if (it.first == MID_PLAYER)
            continue;

        monster* mons = monster_by_mid(it.first);
        if (!mons || !mons->alive() || !you.can_see(*mons))
            continue;

        print_wounds(*mons);
    }

    return spret::success;
}

size_t icicle_burst_count(int pow)
{
    return (3 + div_rand_round(pow, 13));
}

spret cast_icicle_burst(const actor *caster, int pow, const coord_def &pos,
                            bool fail)
{
    const size_t range = spell_range(SPELL_ICICLE_BURST, pow);
    const size_t beam_count = icicle_burst_count(pow);

    targeter_shotgun hitfunc(caster, beam_count, range);

    hitfunc.set_aim(pos);

    if (caster->is_player())
    {
        if (stop_attack_prompt(hitfunc, "icicle burst"))
            return spret::abort;
    }

    fail_check();

    bolt beam;
    beam.thrower = (caster && caster->is_player()) ? KILL_YOU :
                   (caster)                        ? KILL_MON
                                                   : KILL_MISC;
    beam.range       = range;
    beam.source      = caster->pos();
    beam.source_id   = caster->mid;
    beam.source_name = caster->name(DESC_PLAIN, true);
    zappy(ZAP_ICICLE_BURST, pow, false, beam);
    beam.aux_source  = beam.name;
    beam.draw_delay = 5;   // in ms, 1/3 the default

    int die_size = 8 + div_rand_round(pow, 10);
    beam.damage = dice_def(2, die_size);
    beam.hit = 5;

    // Choose a random number of 'pellets' to fire for each beam in the spread.
    // total pellets has O(beam_count^2)
    vector<size_t> pellets;
    pellets.resize(beam_count);
    for (size_t i = 0; i < beam_count; i++)
        pellets[random2avg(beam_count, 3)]++;

    map<mid_t, int> hit_count;

    // for each beam of pellets...
    for (size_t i = 0; i < beam_count; i++)
    {
        // find the beam's path.
        ray_def ray = hitfunc.rays[i];
        for (size_t j = 0; j < range; j++)
            ray.advance();

        // fire the beam once per pellet.
        for (size_t j = 0; j < pellets[i]; j++)
        {
            bolt tempbeam = beam;
            tempbeam.draw_delay = 0;
            tempbeam.target = ray.pos();
            tempbeam.fire();
            scaled_delay(5);
            for (auto it : tempbeam.hit_count)
               hit_count[it.first] += it.second;
        }
    }

    for (auto it : hit_count)
    {
        if (it.first == MID_PLAYER)
            continue;

        monster* mons = monster_by_mid(it.first);
        if (!mons || !mons->alive() || !you.can_see(*mons))
            continue;

        print_wounds(*mons);
    }

    return spret::success;
}

static void _setup_borgnjors_vile_clutch(bolt &beam, int pow)
{
    beam.name         = "vile clutch";
    beam.aux_source   = "vile_clutch";
    beam.flavour      = BEAM_VILE_CLUTCH;
    beam.glyph        = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour       = GREEN;
    beam.source_id    = MID_PLAYER;
    beam.thrower      = KILL_YOU;
    beam.is_explosion = true;
    beam.ex_size      = 1;
    beam.ench_power   = pow;
    beam.origin_spell = SPELL_BORGNJORS_VILE_CLUTCH;
}

spret cast_borgnjors_vile_clutch(int pow, bolt &beam, bool fail)
{
    if (cell_is_solid(beam.target))
    {
        canned_msg(MSG_SOMETHING_IN_WAY);
        return spret::abort;
    }

    fail_check();

    _setup_borgnjors_vile_clutch(beam, pow);
    mpr("Decaying hands burst forth from the earth!");
    beam.explode();

    return spret::success;
}
