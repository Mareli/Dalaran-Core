/*
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "npc_pet_dk_".
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "CombatAI.h"
#include "Cell.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"

enum DeathKnightSpells
{
    SPELL_DK_SUMMON_GARGOYLE_1      = 49206,
    SPELL_DK_SUMMON_GARGOYLE_2      = 50514,
    SPELL_DK_DISMISS_GARGOYLE       = 50515,
    SPELL_DK_SANCTUARY              = 54661
};

class npc_pet_dk_ebon_gargoyle : public CreatureScript
{
    public:
        npc_pet_dk_ebon_gargoyle() : CreatureScript("npc_pet_dk_ebon_gargoyle") { }

        struct npc_pet_dk_ebon_gargoyleAI : CasterAI
        {
            npc_pet_dk_ebon_gargoyleAI(Creature* creature) : CasterAI(creature)
            {
                Initialize();
            }

            void Initialize()
            {
                // Not needed to be despawned now
                _despawnTimer = 0;
            }

            void InitializeAI() override
            {
                Initialize();

                CasterAI::InitializeAI();
                ObjectGuid ownerGuid = me->GetOwnerGUID();
                if (!ownerGuid)
                    return;

                // Find victim of Summon Gargoyle spell
                std::list<Unit*> targets;
                Trinity::AnyUnfriendlyUnitInObjectRangeCheck u_check(me, me, 30.0f);
                Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(me, targets, u_check);
                me->VisitNearbyObject(30.0f, searcher);
                for (std::list<Unit*>::const_iterator iter = targets.begin(); iter != targets.end(); ++iter)
                    if ((*iter)->HasAura(SPELL_DK_SUMMON_GARGOYLE_1, ownerGuid))
                    {
                        me->Attack((*iter), false);
                        break;
                    }
            }

            void JustDied(Unit* /*killer*/) override
            {
                // Stop Feeding Gargoyle when it dies
                if (Unit* owner = me->GetOwner())
                    owner->RemoveAurasDueToSpell(SPELL_DK_SUMMON_GARGOYLE_2);
            }

            // Fly away when dismissed
            void SpellHit(Unit* source, SpellInfo const* spell) override
            {
                if (spell->Id != SPELL_DK_DISMISS_GARGOYLE || !me->IsAlive())
                    return;

                Unit* owner = me->GetOwner();
                if (!owner || owner != source)
                    return;

                // Stop Fighting
                me->ApplyModFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE, true);

                // Sanctuary
                me->CastSpell(me, SPELL_DK_SANCTUARY, true);
                me->SetReactState(REACT_PASSIVE);

                //! HACK: Creature's can't have MOVEMENTFLAG_FLYING
                // Fly Away
                me->SetCanFly(true);
                me->SetSpeed(MOVE_FLIGHT, 0.75f, true);
                me->SetSpeed(MOVE_RUN, 0.75f, true);
                float x = me->GetPositionX() + 20 * std::cos(me->GetOrientation());
                float y = me->GetPositionY() + 20 * std::sin(me->GetOrientation());
                float z = me->GetPositionZ() + 40;
                me->GetMotionMaster()->Clear(false);
                me->GetMotionMaster()->MovePoint(0, x, y, z);

                // Despawn as soon as possible
                _despawnTimer = 4 * IN_MILLISECONDS;
            }

            void UpdateAI(uint32 diff) override
            {
                if (_despawnTimer > 0)
                {
                    if (_despawnTimer > diff)
                        _despawnTimer -= diff;
                    else
                        me->DespawnOrUnsummon();
                    return;
                }

                CasterAI::UpdateAI(diff);
            }

        private:
           uint32 _despawnTimer;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_pet_dk_ebon_gargoyleAI(creature);
        }
};

class spell_dk_avoidance_passive : public SpellScriptLoader
{
public:
	spell_dk_avoidance_passive() : SpellScriptLoader("spell_dk_avoidance_passive") { }

	class spell_dk_avoidance_passive_AuraScript : public AuraScript
	{
		PrepareAuraScript(spell_dk_avoidance_passive_AuraScript);

		bool Load() override
		{
			if (!GetCaster() || !GetCaster()->GetOwner() || GetCaster()->GetOwner()->GetTypeId() != TYPEID_PLAYER)
				return false;
			return true;
		}

		void CalculateAvoidanceAmount(AuraEffect const* /* aurEff */, int32& amount, bool& /*canBeRecalculated*/)
		{
			if (Unit* pet = GetUnitOwner())
			{
				if (Unit* owner = pet->GetOwner())
				{
					// Army of the dead ghoul
					if (pet->GetEntry() == 24207)
						amount = -90;
					// Night of the dead
					else if (owner->HasSpell(55620))
						amount = -45;
					else if (owner->HasSpell(55623))
						amount = -90;
				}
			}
		}

		void Register() override
		{
			DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_dk_avoidance_passive_AuraScript::CalculateAvoidanceAmount, EFFECT_0, SPELL_AURA_MOD_CREATURE_AOE_DAMAGE_AVOIDANCE);
		}
	};

	AuraScript* GetAuraScript() const override
	{
		return new spell_dk_avoidance_passive_AuraScript();
	}
};

class npc_pet_dk_dancing_rune_weapon : public CreatureScript
{
public:
	npc_pet_dk_dancing_rune_weapon() : CreatureScript("npc_pet_dk_dancing_rune_weapon") { }

	struct npc_pet_dk_dancing_rune_weaponAI : public ScriptedAI
	{
	private:
		uint32 _swingTimer;

	public:
		npc_pet_dk_dancing_rune_weaponAI(Creature* creature) : ScriptedAI(creature)
		{
			_swingTimer = 3500;    // Ready to swing as soon as it spawns
		}

		void Reset() override
		{
			if (Unit* owner = me->GetOwner())
			{
				// Apply auras if the death knight has them
				if (owner->HasAura(59921))  // Frost Fever (passive)
					me->AddAura(59921, me);
				if (owner->HasAura(59879))  // Blood Plague (passive)
					me->AddAura(59879, me);
				if (owner->HasAura(59327))  // Glyph of Rune Tap
					me->AddAura(59327, me);

				// Synchronize weapon's health to get proper combat log healing effects
				me->SetMaxHealth(owner->GetMaxHealth());
				me->SetHealth(owner->GetMaxHealth());

				// All threat is redirected to the death knight.
				// The rune weapon should never be attacked by players and NPCs alike
				me->SetRedirectThreat(owner->GetGUID(), 100);
			}
			else
				me->DespawnOrUnsummon();
		}


		void AttackStart(Unit* who) override
		{
			if (who)
				me->GetMotionMaster()->MoveChase(who);
		}

		void UpdateAI(uint32 diff) override
		{
			_swingTimer += diff;
			if (_swingTimer >= 3500)
			{
				if (Unit* owner = me->GetOwner())
				{
					if (me->GetVictim() == NULL)
						if (Unit* target = ObjectAccessor::GetUnit(*owner, owner->GetTarget()))
							me->Attack(target, false);
					if (me->GetVictim() == NULL) return;    // Prevent crash

					CalcDamageInfo damageInfo;
					owner->CalculateMeleeDamage(me->GetVictim(), 0, &damageInfo, BASE_ATTACK);
					damageInfo.attacker = me;
					damageInfo.damage = damageInfo.damage / 2;

					me->DealDamageMods(me->GetVictim(), damageInfo.damage, &damageInfo.absorb);
					me->SendAttackStateUpdate(&damageInfo);
					me->ProcDamageAndSpell(damageInfo.target, damageInfo.procAttacker, damageInfo.procVictim, damageInfo.procEx, damageInfo.damage, damageInfo.attackType);
					me->DealMeleeDamage(&damageInfo, true);
				}
				_swingTimer = 0;
			}

			// Do not call base to prevent weapon's own autoattack that deals less than 5 damage
		}
	};

	CreatureAI* GetAI(Creature* creature) const override
	{
		return new npc_pet_dk_dancing_rune_weaponAI(creature);
	}
};

class npc_pet_dk_guardian : public CreatureScript
{
public:
    npc_pet_dk_guardian() : CreatureScript("npc_pet_dk_guardian") { }

    struct npc_pet_dk_guardianAI : public AggressorAI
    {
        npc_pet_dk_guardianAI(Creature* creature) : AggressorAI(creature) { }

        bool CanAIAttack(Unit const* target) const override
        {
            if (!target)
                return false;
            Unit* owner = me->GetOwner();
            if (owner && !target->IsInCombatWith(owner))
                return false;
            return AggressorAI::CanAIAttack(target);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_pet_dk_guardianAI(creature);
    }
};

void AddSC_deathknight_pet_scripts()
{
    new npc_pet_dk_ebon_gargoyle();
	new spell_dk_avoidance_passive();
	new npc_pet_dk_dancing_rune_weapon();
    new npc_pet_dk_guardian();
}
