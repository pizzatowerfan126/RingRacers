// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  p_inter.c
/// \brief Handling interactions (i.e., collisions)

#include "doomdef.h"
#include "i_system.h"
#include "am_map.h"
#include "g_game.h"
#include "m_random.h"
#include "p_local.h"
#include "s_sound.h"
#include "r_main.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "lua_hook.h"
#include "m_cond.h" // unlockables, emblems, etc
#include "p_setup.h"
#include "m_cheat.h" // objectplace
#include "m_misc.h"
#include "v_video.h" // video flags for CEchos
#include "f_finale.h"

// SRB2kart
#include "k_kart.h"
#include "k_battle.h"
#include "k_specialstage.h"
#include "k_pwrlv.h"
#include "k_grandprix.h"
#include "k_respawn.h"
#include "p_spec.h"
#include "k_objects.h"
#include "k_roulette.h"
#include "k_boss.h"
#include "k_hitlag.h"
#include "acs/interface.h"
#include "k_powerup.h"

// CTF player names
#define CTFTEAMCODE(pl) pl->ctfteam ? (pl->ctfteam == 1 ? "\x85" : "\x84") : ""
#define CTFTEAMENDCODE(pl) pl->ctfteam ? "\x80" : ""

void P_ForceFeed(const player_t *player, INT32 attack, INT32 fade, tic_t duration, INT32 period)
{
	BasicFF_t Basicfeed;
	if (!player)
		return;
	Basicfeed.Duration = (UINT32)(duration * (100L/TICRATE));
	Basicfeed.ForceX = Basicfeed.ForceY = 1;
	Basicfeed.Gain = 25000;
	Basicfeed.Magnitude = period*10;
	Basicfeed.player = player;
	/// \todo test FFB
	P_RampConstant(&Basicfeed, attack, fade);
}

void P_ForceConstant(const BasicFF_t *FFInfo)
{
	JoyFF_t ConstantQuake;
	if (!FFInfo || !FFInfo->player)
		return;
	ConstantQuake.ForceX    = FFInfo->ForceX;
	ConstantQuake.ForceY    = FFInfo->ForceY;
	ConstantQuake.Duration  = FFInfo->Duration;
	ConstantQuake.Gain      = FFInfo->Gain;
	ConstantQuake.Magnitude = FFInfo->Magnitude;
	if (FFInfo->player == &players[consoleplayer])
		I_Tactile(ConstantForce, &ConstantQuake);
	else if (splitscreen && FFInfo->player == &players[g_localplayers[1]])
		I_Tactile2(ConstantForce, &ConstantQuake);
	else if (splitscreen > 1 && FFInfo->player == &players[g_localplayers[2]])
		I_Tactile3(ConstantForce, &ConstantQuake);
	else if (splitscreen > 2 && FFInfo->player == &players[g_localplayers[3]])
		I_Tactile4(ConstantForce, &ConstantQuake);
}
void P_RampConstant(const BasicFF_t *FFInfo, INT32 Start, INT32 End)
{
	JoyFF_t RampQuake;
	if (!FFInfo || !FFInfo->player)
		return;
	RampQuake.ForceX    = FFInfo->ForceX;
	RampQuake.ForceY    = FFInfo->ForceY;
	RampQuake.Duration  = FFInfo->Duration;
	RampQuake.Gain      = FFInfo->Gain;
	RampQuake.Magnitude = FFInfo->Magnitude;
	RampQuake.Start     = Start;
	RampQuake.End       = End;
	if (FFInfo->player == &players[consoleplayer])
		I_Tactile(ConstantForce, &RampQuake);
	else if (splitscreen && FFInfo->player == &players[g_localplayers[1]])
		I_Tactile2(ConstantForce, &RampQuake);
	else if (splitscreen > 1 && FFInfo->player == &players[g_localplayers[2]])
		I_Tactile3(ConstantForce, &RampQuake);
	else if (splitscreen > 2 && FFInfo->player == &players[g_localplayers[3]])
		I_Tactile4(ConstantForce, &RampQuake);
}


//
// GET STUFF
//

//
// P_CanPickupItem
//
// Returns true if the player is in a state where they can pick up items.
//
boolean P_CanPickupItem(player_t *player, UINT8 weapon)
{
	if (player->exiting || mapreset || (player->pflags & PF_ELIMINATED))
		return false;

	// 0: Sphere/Ring
	// 1: Random Item / Capsule
	// 2: Eggbox
	// 3: Paperitem
	if (weapon)
	{
		// Item slot already taken up
		if (weapon == 2)
		{
			// Invulnerable
			if (player->flashing > 0)
				return false;

			// Already have fake
			if ((player->itemRoulette.active && player->itemRoulette.eggman) == true
				|| player->eggmanexplode)
				return false;
		}
		else
		{
			// Item-specific timer going off
			if (player->stealingtimer
				|| player->rocketsneakertimer
				|| player->eggmanexplode)
				return false;

			// Item slot already taken up
			if (player->itemRoulette.active == true
				|| player->ringboxdelay > 0
				|| (weapon != 3 && player->itemamount)
				|| (player->pflags & PF_ITEMOUT))
				return false;

			if (weapon == 3 && K_GetShieldFromItem(player->itemtype) != KSHIELD_NONE)
				return false; // No stacking shields!
		}
	}

	return true;
}

// Allow players to pick up only one pickup from each set of pickups.
// Anticheese pickup types are different than-P_CanPickupItem weapon, because that system is
// already slightly scary without introducing special cases for different types of the same pickup.
// 1 = floating item, 2 = perma ring, 3 = capsule
boolean P_IsPickupCheesy(player_t *player, UINT8 type)
{
	if (player->lastpickupdistance && player->lastpickuptype == type)
	{
		UINT32 distancedelta = min(player->distancetofinish - player->lastpickupdistance, player->lastpickupdistance - player->distancetofinish);
		if (distancedelta < 2500)
			return true;
	}
	return false;
}

void P_UpdateLastPickup(player_t *player, UINT8 type)
{
	player->lastpickuptype = type;
	player->lastpickupdistance = player->distancetofinish;
}

boolean P_CanPickupEmblem(player_t *player, INT32 emblemID)
{
	if (emblemID < 0 || emblemID >= MAXEMBLEMS)
	{
		// Invalid emblem ID, can't pickup.
		return false;
	}

	if (demo.playback)
	{
		// Never collect emblems in replays.
		return false;
	}

	if (player->bot)
	{
		// Your nefarious opponent puppy can't grab these for you.
		return false;
	}

	return true;
}

boolean P_EmblemWasCollected(INT32 emblemID)
{
	if (emblemID < 0 || emblemID >= numemblems)
	{
		// Invalid emblem ID, can't pickup.
		return true;
	}

	return gamedata->collected[emblemID];
}

static void P_ItemPop(mobj_t *actor)
{
	/*
	INT32 locvar1 = var1;

	if (LUA_CallAction(A_ITEMPOP, actor))
		return;

	if (!(actor->target && actor->target->player))
	{
		if (cht_debug && !(actor->target && actor->target->player))
			CONS_Printf("ERROR: Powerup has no target!\n");
		return;
	}
	*/

	Obj_SpawnItemDebrisEffects(actor, actor->target);

	if (!specialstageinfo.valid) // In Special, you'll respawn as a Ring Box (random-item.c), don't confuse the player.
		P_SetMobjState(actor, S_RINGBOX1);
	actor->extravalue1 = 0;

	// de-solidify
	actor->flags |= MF_NOCLIPTHING;

	// RF_DONTDRAW will flicker as the object's fuse gets
	// closer to running out (see P_FuseThink)
	actor->renderflags |= RF_DONTDRAW|RF_TRANS50;
	actor->color = SKINCOLOR_GREY;
	actor->colorized = true;

	/*
	if (locvar1 == 1)
	{
		P_GivePlayerSpheres(actor->target->player, actor->extravalue1);
	}
	else if (locvar1 == 0)
	{
		if (actor->extravalue1 >= TICRATE)
			K_StartItemRoulette(actor->target->player, false);
		else
			K_StartItemRoulette(actor->target->player, true);
	}
	*/

	// Here at mapload in battle?
	if (!(gametyperules & GTR_CIRCUIT) && (actor->flags2 & MF2_BOSSNOTRAP))
	{
		numgotboxes++;

		// do not flicker back in just yet, handled by
		// P_RespawnBattleBoxes eventually
		P_SetMobjState(actor, S_INVISIBLE);
	}
}

/** Takes action based on a ::MF_SPECIAL thing touched by a player.
  * Actually, this just checks a few things (heights, toucher->player, no
  * objectplace, no dead or disappearing things)
  *
  * The special thing may be collected and disappear, or a sound may play, or
  * both.
  *
  * \param special     The special thing.
  * \param toucher     The player's mobj.
  * \param heightcheck Whether or not to make sure the player and the object
  *                    are actually touching.
  */
void P_TouchSpecialThing(mobj_t *special, mobj_t *toucher, boolean heightcheck)
{
	player_t *player;

	if (objectplacing)
		return;

	I_Assert(special != NULL);
	I_Assert(toucher != NULL);

	// Dead thing touching.
	// Can happen with a sliding player corpse.
	if (toucher->health <= 0)
		return;
	if (special->health <= 0)
		return;

	if (heightcheck)
	{
		fixed_t toucher_bottom = toucher->z;
		fixed_t special_bottom = special->z;

		if (toucher->flags & MF_PICKUPFROMBELOW)
			toucher_bottom -= toucher->height;

		if (special->flags & MF_PICKUPFROMBELOW)
			special_bottom -= special->height;

		if (toucher->momz < 0) {
			if (toucher_bottom + toucher->momz > special->z + special->height)
				return;
		} else if (toucher_bottom > special->z + special->height)
			return;
		if (toucher->momz > 0) {
			if (toucher->z + toucher->height + toucher->momz < special_bottom)
				return;
		} else if (toucher->z + toucher->height < special_bottom)
			return;
	}

	player = toucher->player;
	I_Assert(player != NULL); // Only players can touch stuff!

	if (player->spectator)
		return;

	// Ignore multihits in "ouchie" mode
	if (special->flags & (MF_ENEMY|MF_BOSS) && special->flags2 & MF2_FRET)
		return;

	if (LUA_HookTouchSpecial(special, toucher) || P_MobjWasRemoved(special))
		return;

	if ((special->flags & (MF_ENEMY|MF_BOSS)) && !(special->flags & MF_MISSILE))
	{
		////////////////////////////////////////////////////////
		/////ENEMIES & BOSSES!!/////////////////////////////////
		////////////////////////////////////////////////////////

		P_DamageMobj(toucher, special, special, 1, DMG_NORMAL);
		return;
	}
	else
	{
	// We now identify by object type, not sprite! Tails 04-11-2001
	switch (special->type)
	{
		case MT_MEMENTOSTP:	 // Mementos teleport
			// Teleport player to the other teleporter (special->target). We'll assume there's always only ever 2.
			if (!special->target)
				return;	// foolproof crash prevention check!!!!!

			P_SetOrigin(player->mo, special->target->x, special->target->y, special->target->z + (48<<FRACBITS));
			player->mo->angle = special->target->angle;
			P_SetObjectMomZ(player->mo, 12<<FRACBITS, false);
			P_InstaThrust(player->mo, player->mo->angle, 20<<FRACBITS);
			return;
		case MT_FLOATINGITEM: // SRB2Kart
			if (special->threshold >= FIRSTPOWERUP)
			{
				if (P_PlayerInPain(player))
					return;

				K_GivePowerUp(player, special->threshold, special->movecount);
			}
			else
			{
				if (!P_CanPickupItem(player, 3) || (player->itemamount && player->itemtype != special->threshold))
					return;

				player->itemtype = special->threshold;
				if ((UINT16)(player->itemamount) + special->movecount > 255)
					player->itemamount = 255;
				else
					player->itemamount += special->movecount;
			}

			S_StartSound(special, special->info->deathsound);

			P_SetTarget(&special->tracer, toucher);
			special->flags2 |= MF2_NIGHTSPULL;
			special->destscale = mapobjectscale>>4;
			special->scalespeed <<= 1;

			special->flags &= ~MF_SPECIAL;
			return;
		case MT_RANDOMITEM:
			UINT8 cheesetype = (special->flags2 & MF2_AMBUSH) ? 2 : 1;

			if (!P_CanPickupItem(player, 1))
				return;
			if (P_IsPickupCheesy(player, cheesetype))
				return;

			special->momx = special->momy = special->momz = 0;
			P_SetTarget(&special->target, toucher);
			P_UpdateLastPickup(player, cheesetype);
			// P_KillMobj(special, toucher, toucher, DMG_NORMAL);
			if (special->extravalue1 >= RINGBOX_TIME)
				K_StartItemRoulette(player, false);
			else
				K_StartItemRoulette(player, true);
			P_ItemPop(special);
			special->fuse = TICRATE;
			return;
		case MT_SPHEREBOX:
			if (!P_CanPickupItem(player, 0))
				return;

			special->momx = special->momy = special->momz = 0;
			P_SetTarget(&special->target, toucher);
			// P_KillMobj(special, toucher, toucher, DMG_NORMAL);
			P_ItemPop(special);
			P_GivePlayerSpheres(player, special->extravalue2);
			return;
		case MT_ITEMCAPSULE:
			if (special->scale < special->extravalue1) // don't break it while it's respawning
				return;

			switch (special->threshold)
			{
				case KITEM_SPB:
					if (K_IsSPBInGame()) // don't spawn a second SPB
						return;
					break;
				case KITEM_SUPERRING:
					if (player->pflags & PF_RINGLOCK) // no cheaty rings
						return;
					break;
				default:
					if (!P_CanPickupItem(player, 1))
						return;
					if (P_IsPickupCheesy(player, 3))
						return;
					break;
			}

			// Ring Capsules shouldn't affect pickup cheese, they're just used as condensed ground-ring placements.
			if (special->threshold != KITEM_SUPERRING)
				P_UpdateLastPickup(player, 3);

			S_StartSound(toucher, special->info->deathsound);
			P_KillMobj(special, toucher, toucher, DMG_NORMAL);
			return;
		case MT_KARMAHITBOX:
			if (!special->target->player)
				return;
			if (player == special->target->player)
				return;
			if (special->target->player->exiting || player->exiting)
				return;

			if (P_PlayerInPain(special->target->player))
				return;

			if (special->target->player->karmadelay > 0)
				return;

			{
				mobj_t *boom;

				if (P_DamageMobj(toucher, special, special->target, 1, DMG_KARMA) == false)
				{
					return;
				}

				boom = P_SpawnMobj(special->target->x, special->target->y, special->target->z, MT_BOOMEXPLODE);

				boom->scale = special->target->scale;
				boom->destscale = special->target->scale;
				boom->momz = 5*FRACUNIT;

				if (special->target->color)
					boom->color = special->target->color;
				else
					boom->color = SKINCOLOR_KETCHUP;

				S_StartSound(boom, special->info->attacksound);

				special->target->player->karthud[khud_yougotem] = 2*TICRATE;
				special->target->player->karmadelay = comebacktime;
			}
			return;
		case MT_DUELBOMB:
			{
				Obj_DuelBombTouch(special, toucher);
				return;
			}
		case MT_EMERALD:
			if (!P_CanPickupItem(player, 0) || P_PlayerInPain(player))
				return;

			if (special->threshold > 0)
				return;

			if (toucher->hitlag > 0)
				return;

			// Emerald will now orbit the player

			{
				const tic_t orbit = 2*TICRATE;
				Obj_BeginEmeraldOrbit(special, toucher, toucher->radius, orbit, orbit * 20);
			}

			return;
		case MT_SPECIAL_UFO:
			if (Obj_UFOEmeraldCollect(special, toucher) == false)
			{
				return;
			}

			break;
		/*
		case MT_EERIEFOG:
			special->frame &= ~FF_TRANS80;
			special->frame |= FF_TRANS90;
			return;
		*/
		case MT_SMK_MOLE:
			if (special->target && !P_MobjWasRemoved(special->target))
				return;

			if (special->health <= 0 || toucher->health <= 0)
				return;

			if (!player->mo || player->spectator)
				return;

			// kill
			if (player->invincibilitytimer > 0
				|| K_IsBigger(toucher, special) == true
				|| player->flamedash > 0)
			{
				P_KillMobj(special, toucher, toucher, DMG_NORMAL);
				return;
			}

			// no interaction
			if (player->flashing > 0 || player->hyudorotimer > 0 || P_PlayerInPain(player))
				return;

			// attach to player!
			P_SetTarget(&special->target, toucher);
			S_StartSound(special, sfx_s1a2);
			return;
		case MT_CDUFO: // SRB2kart
			if (special->fuse || !P_CanPickupItem(player, 1))
				return;

			K_StartItemRoulette(player, false);

			// Karma fireworks
			/*for (i = 0; i < 5; i++)
			{
				mobj_t *firework = P_SpawnMobj(special->x, special->y, special->z, MT_KARMAFIREWORK);
				firework->momx = toucher->momx;
				firework->momy = toucher->momy;
				firework->momz = toucher->momz;
				P_Thrust(firework, FixedAngle((72*i)<<FRACBITS), P_RandomRange(PR_ITEM_DEBRIS, 1,8)*special->scale);
				P_SetObjectMomZ(firework, P_RandomRange(PR_ITEM_DEBRIS, 1,8)*special->scale, false);
				firework->color = toucher->color;
			}*/

			K_SetHitLagForObjects(special, toucher, toucher, 2, true);

			break;

		case MT_BALLOON: // SRB2kart
			P_SetObjectMomZ(toucher, 20<<FRACBITS, false);
			break;

		case MT_BUBBLESHIELDTRAP:
			if ((special->target == toucher || special->target == toucher->target) && (special->threshold > 0))
				return;

			if (special->tracer && !P_MobjWasRemoved(special->tracer))
				return;

			if (special->health <= 0 || toucher->health <= 0)
				return;

			if (!player->mo || player->spectator)
				return;

			// attach to player!
			P_SetTarget(&special->tracer, toucher);
			toucher->flags |= MF_NOGRAVITY;
			toucher->momz = (8*toucher->scale) * P_MobjFlip(toucher);
			S_StartSound(toucher, sfx_s1b2);
			return;

		case MT_HYUDORO:
			Obj_HyudoroCollide(special, toucher);
			return;

		case MT_RING:
		case MT_FLINGRING:
			if (special->extravalue1)
				return;

			// No picking up rings while SPB is targetting you
			if (player->pflags & PF_RINGLOCK)
				return;

			// Don't immediately pick up spilled rings
			if (special->threshold > 0 || P_PlayerInPain(player) || player->spindash) // player->spindash: Otherwise, players can pick up rings that are thrown out of them from invinc spindash penalty
				return;

			if (!(P_CanPickupItem(player, 0)))
				return;

			// Reached the cap, don't waste 'em!
			if (RINGTOTAL(player) >= 20)
				return;

			special->momx = special->momy = special->momz = 0;

			special->extravalue1 = 1; // Ring collect animation timer
			special->angle = R_PointToAngle2(toucher->x, toucher->y, special->x, special->y); // animation angle
			P_SetTarget(&special->target, toucher); // toucher for thinker
			player->pickuprings++;

			return;

		case MT_BLUESPHERE:
			if (!(P_CanPickupItem(player, 0)))
				return;

			P_GivePlayerSpheres(player, 1);
			break;

		// Secret emblem thingy
		case MT_EMBLEM:
			{
				if (!P_CanPickupEmblem(player, special->health - 1))
					return;

				if (!P_IsLocalPlayer(player))
				{
					// Must be party.
					return;
				}

				if (!gamedata->collected[special->health-1])
				{
					gamedata->collected[special->health-1] = true;
					if (!M_UpdateUnlockablesAndExtraEmblems(true, true))
						S_StartSound(NULL, sfx_ncitem);
					gamedata->deferredsave = true;
				}

				// Don't delete the object, just fade it.
				return;
			}

		case MT_SPRAYCAN:
			{
				if (demo.playback)
				{
					// Never collect emblems in replays.
					return;
				}

				if (player->bot)
				{
					// Your nefarious opponent puppy can't grab these for you.
					return;
				}

				if (!P_IsLocalPlayer(player))
				{
					// Must be party.
					return;
				}

				// See also P_SprayCanInit
				UINT16 can_id = mapheaderinfo[gamemap-1]->cache_spraycan;

				if (can_id < gamedata->numspraycans)
				{
					// Assigned to this level, has been grabbed
					return;
				}
				// Prevent footguns - these won't persist when custom levels are unloaded
				else if (gamemap-1 < basenummapheaders)
				{
					// Unassigned, get the next grabbable colour
					can_id = gamedata->gotspraycans;
				}

				if (can_id >= gamedata->numspraycans)
				{
					// We've exhausted all the spraycans to grab.
					return;
				}

				if (gamedata->spraycans[can_id].map >= nummapheaders)
				{
					gamedata->spraycans[can_id].map = gamemap-1;
					mapheaderinfo[gamemap-1]->cache_spraycan = can_id;

					gamedata->gotspraycans++;

					if (!M_UpdateUnlockablesAndExtraEmblems(true, true))
						S_StartSound(NULL, sfx_ncitem);
					gamedata->deferredsave = true;
				}

				// Don't delete the object, just fade it.
				P_SprayCanInit(special);
				return;
			}

		// CTF Flags
		case MT_REDFLAG:
		case MT_BLUEFLAG:
			return;

		case MT_CHEATCHECK:
			P_TouchCheatcheck(special, player, special->thing_args[1]);
			return;

		case MT_BIGTUMBLEWEED:
		case MT_LITTLETUMBLEWEED:
			if (toucher->momx || toucher->momy)
			{
				special->momx = toucher->momx;
				special->momy = toucher->momy;
				special->momz = P_AproxDistance(toucher->momx, toucher->momy)/4;

				if (toucher->momz > 0)
					special->momz += toucher->momz/8;

				P_SetMobjState(special, special->info->seestate);
			}
			return;

		case MT_WATERDROP:
			if (special->state == &states[special->info->spawnstate])
			{
				special->z = toucher->z+toucher->height-FixedMul(8*FRACUNIT, special->scale);
				special->momz = 0;
				special->flags |= MF_NOGRAVITY;
				P_SetMobjState (special, special->info->deathstate);
				S_StartSound (special, special->info->deathsound+(P_RandomKey(PR_DECORATION, special->info->mass)));
			}
			return;

		case MT_LOOPENDPOINT:
			Obj_LoopEndpointCollide(special, toucher);
			return;

		case MT_RINGSHOOTER:
			Obj_PlayerUsedRingShooter(special, player);
			return;

		case MT_SUPER_FLICKY:
			Obj_SuperFlickyPlayerCollide(special, toucher);
			return;

		case MT_DASHRING:
		case MT_RAINBOWDASHRING:
			Obj_DashRingTouch(special, player);
			return;

		default: // SOC or script pickup
			P_SetTarget(&special->target, toucher);
			break;
		}
	}

	S_StartSound(toucher, special->info->deathsound); // was NULL, but changed to player so you could hear others pick up rings
	P_KillMobj(special, NULL, toucher, DMG_NORMAL);
	special->shadowscale = 0;
}

/** Saves a player's level progress at a Cheat Check
  *
  * \param post The Cheat Check to trigger
  * \param player The player that should receive the cheatcheck
  * \param snaptopost If true, the respawn point will use the cheatcheck's position, otherwise player x/y and star post z
  */
void P_TouchCheatcheck(mobj_t *post, player_t *player, boolean snaptopost)
{
	mobj_t *toucher = player->mo;

	(void)snaptopost;

	// Player must have touched all previous cheatchecks
	if (post->health - player->cheatchecknum > 1)
	{
		if (!player->checkskip)
			S_StartSound(toucher, sfx_lose);
		player->checkskip = 3;
		return;
	}

	// With the parameter + angle setup, we can go up to 1365 star posts. Who needs that many?
	if (post->health > 1365)
	{
		CONS_Debug(DBG_GAMELOGIC, "Bad Cheatcheck Number!\n");
		return;
	}

	if (player->cheatchecknum >= post->health)
		return; // Already hit this post

	player->cheatchecknum = post->health;
}

static void P_AddBrokenPrison(mobj_t *target, mobj_t *source)
{
	(void)target;

	if (!battleprisons)
		return;

	if ((gametyperules & GTR_POINTLIMIT) && (source && source->player))
	{
		/*mobj_t * ring;
		for (i = 0; i < 2; i++)
		{
			dir += (ANGLE_MAX/3);
			ring = P_SpawnMobj(target->x, target->y, target->z, MT_RING);
			ring->angle = dir;
			P_InstaThrust(ring, dir, 16*ring->scale);
			ring->momz = 8 * target->scale * P_MobjFlip(target);
			P_SetTarget(&ring->tracer, source);
			source->player->pickuprings++;
		}*/

		P_AddPlayerScore(source->player, 1);
		K_SpawnBattlePoints(source->player, NULL, 1);
	}

	if (++numtargets >= maptargets)
	{
		P_DoAllPlayersExit(0, (grandprixinfo.gp == true));
	}
	else
	{
		S_StartSound(NULL, sfx_s221);
		if (timelimitintics)
		{
			extratimeintics += 10*TICRATE;
			secretextratime = TICRATE/2;
		}
	}
}

/** Checks if the level timer is over the timelimit and the round should end,
  * unless you are in overtime. In which case leveltime may stretch out beyond
  * timelimitintics and overtime's status will be checked here each tick.
  *
  * \sa cv_timelimit, P_CheckPointLimit, P_UpdateSpecials
  */
void P_CheckTimeLimit(void)
{
	if (exitcountdown)
		return;

	if (!timelimitintics)
		return;

	if (leveltime < starttime)
	{
		if (secretextratime)
			secretextratime--;
		return;
	}

	if (leveltime < (timelimitintics + starttime))
	{
		if (secretextratime)
		{
			secretextratime--;
			timelimitintics++;
		}
		else if (extratimeintics)
		{
			timelimitintics++;
			if (leveltime & 1)
				;
			else
			{
				if (extratimeintics > 20)
				{
					extratimeintics -= 20;
					timelimitintics += 20;
				}
				else
				{
					timelimitintics += extratimeintics;
					extratimeintics = 0;
				}
				S_StartSound(NULL, sfx_ptally);
			}
		}
		else
		{
			if (timelimitintics + starttime - leveltime <= 3*TICRATE)
			{
				if (((timelimitintics + starttime - leveltime) % TICRATE) == 0)
					S_StartSound(NULL, sfx_s3ka7);
			}
		}
		return;
	}

	if (gameaction == ga_completed)
		return;

	if ((grandprixinfo.gp == false) && (cv_overtime.value) && (gametyperules & GTR_OVERTIME))
	{
#ifndef TESTOVERTIMEINFREEPLAY
		UINT8 i;
		boolean foundone = false; // Overtime is used for closing off down to a specific item.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (foundone)
			{
#endif
				// Initiate the kill zone
				if (!battleovertime.enabled)
				{
					thinker_t *th;
					mobj_t *center = NULL;

					fixed_t rx, ry;

					for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
					{
						mobj_t *thismo;

						if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
							continue;

						thismo = (mobj_t *)th;

						if (thismo->type == MT_OVERTIME_CENTER)
						{
							center = thismo;
							break;
						}
					}

					if (center == NULL || P_MobjWasRemoved(center))
					{
						CONS_Alert(CONS_WARNING, "No center point for overtime!\n");

						battleovertime.x = 0;
						battleovertime.y = 0;
						battleovertime.z = 0;
					}
					else
					{
						battleovertime.x = center->x;
						battleovertime.y = center->y;
						battleovertime.z = center->z;
					}

					// Get largest radius from center point to minimap edges

					rx = max(
							abs(battleovertime.x - (minimapinfo.min_x * FRACUNIT)),
							abs(battleovertime.x - (minimapinfo.max_x * FRACUNIT))
					);

					ry = max(
							abs(battleovertime.y - (minimapinfo.min_y * FRACUNIT)),
							abs(battleovertime.y - (minimapinfo.max_y * FRACUNIT))
					);

					battleovertime.initial_radius = min(
							max(max(rx, ry), 4096 * mapobjectscale),
							// Prevent overflow in K_RunBattleOvertime
							FixedDiv(INT32_MAX, M_PI_FIXED) / 2
					);

					battleovertime.radius = battleovertime.initial_radius;

					battleovertime.enabled = 1;

					S_StartSound(NULL, sfx_kc47);
				}

				return;
#ifndef TESTOVERTIMEINFREEPLAY
			}
			else
				foundone = true;
		}
#endif
	}

	P_DoAllPlayersExit(0, false);
}

/** Checks if a player's score is over the pointlimit and the round should end.
  *
  * \sa cv_pointlimit, P_CheckTimeLimit, P_UpdateSpecials
  */
void P_CheckPointLimit(void)
{
	INT32 i;

	if (exitcountdown)
		return;

	if (!K_CanChangeRules(true))
		return;

	if (!g_pointlimit)
		return;

	if (!(gametyperules & GTR_POINTLIMIT))
		return;

	if (battleprisons)
		return;

	// This will be handled by P_KillPlayer
	if (gametyperules & GTR_BUMPERS)
		return;

	// pointlimit is nonzero, check if it's been reached by this player
	if (G_GametypeHasTeams())
	{
		// Just check both teams
		if (g_pointlimit <= redscore || g_pointlimit <= bluescore)
		{
			if (server)
				SendNetXCmd(XD_EXITLEVEL, NULL, 0);
		}
	}
	else
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (g_pointlimit <= players[i].roundscore)
			{
				P_DoAllPlayersExit(0, false);

				/*if (server)
					SendNetXCmd(XD_EXITLEVEL, NULL, 0);*/
				return; // good thing we're leaving the function immediately instead of letting the loop get mangled!
			}
		}
	}
}

// Checks whether or not to end a race netgame.
boolean P_CheckRacers(void)
{
	const boolean griefed = (spectateGriefed > 0);

	boolean eliminateLast = cv_karteliminatelast.value;
	boolean allHumansDone = true;
	//boolean allBotsDone = true;

	UINT8 numPlaying = 0;
	UINT8 numExiting = 0;
	UINT8 numHumans = 0;
	UINT8 numBots = 0;

	UINT8 i;

	// Check if all the players in the race have finished. If so, end the level.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator || (players[i].lives <= 0 && !players[i].exiting))
		{
			// Y'all aren't even playing
			continue;
		}

		numPlaying++;

		if (players[i].bot)
		{
			numBots++;
		}
		else
		{
			numHumans++;
		}

		if (players[i].exiting || (players[i].pflags & PF_NOCONTEST))
		{
			numExiting++;
		}
		else
		{
			if (players[i].bot)
			{
				//allBotsDone = false;
			}
			else
			{
				allHumansDone = false;
			}
		}
	}

	if (numPlaying <= 1 || specialstageinfo.valid == true)
	{
		// Never do this without enough players.
		eliminateLast = false;
	}
	else
	{
		if (griefed == true)
		{
			// Don't do this if someone spectated
			eliminateLast = false;
		}
		else if (grandprixinfo.gp == true)
		{
			// Always do this in GP
			eliminateLast = true;
		}
	}

	if (eliminateLast == true && (numExiting >= numPlaying-1))
	{
		// Everyone's done playing but one guy apparently.
		// Just kill everyone who is still playing.

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator || players[i].lives <= 0)
			{
				// Y'all aren't even playing
				continue;
			}

			if (players[i].exiting || (players[i].pflags & PF_NOCONTEST))
			{
				// You're done, you're free to go.
				continue;
			}

			P_DoTimeOver(&players[i]);
		}

		// Everyone should be done playing at this point now.
		racecountdown = 0;
		return true;
	}

	if (numHumans > 0 && allHumansDone == true)
	{
		// There might be bots that are still going,
		// but all of the humans are done, so we can exit now.
		racecountdown = 0;
		return true;
	}

	// SO, we're not done playing.
	// Let's see if it's time to start the death counter!

	if (racecountdown == 0 && K_Cooperative() == false)
	{
		// If the winners are all done, then start the death timer.
		UINT8 winningPos = max(1, numPlaying / 2);

		if (numPlaying % 2) // Any remainder? Then round up.
		{
			winningPos++;
		}

		if (numExiting >= winningPos)
		{
			tic_t countdown = 30*TICRATE; // 30 seconds left to finish, get going!

			if (K_CanChangeRules(true) == true)
			{
				// Custom timer
				countdown = cv_countdowntime.value * TICRATE;
			}

			racecountdown = countdown + 1;
		}
	}

	// We're still playing, but no one else is,
	// so we need to reset spectator griefing.
	if (numPlaying <= 1)
	{
		spectateGriefed = 0;
	}

	// We are still having fun and playing the game :)
	return false;
}

/** Kills an object.
  *
  * \param target    The victim.
  * \param inflictor The attack weapon. May be NULL (environmental damage).
  * \param source    The attacker. May be NULL.
  * \param damagetype The type of damage dealt that killed the target. If bit 7 (0x80) was set, this was an instant-death.
  * \todo Cleanup, refactor, split up.
  * \sa P_DamageMobj
  */
void P_KillMobj(mobj_t *target, mobj_t *inflictor, mobj_t *source, UINT8 damagetype)
{
	mobj_t *mo;

	if (target->flags & (MF_ENEMY|MF_BOSS))
		target->momx = target->momy = target->momz = 0;

	// SRB2kart
	if (target->type != MT_PLAYER && !(target->flags & MF_MONITOR)
		 && !(target->type == MT_ORBINAUT || target->type == MT_ORBINAUT_SHIELD
		 || target->type == MT_JAWZ || target->type == MT_JAWZ_SHIELD
		 || target->type == MT_BANANA || target->type == MT_BANANA_SHIELD
		 || target->type == MT_DROPTARGET || target->type == MT_DROPTARGET_SHIELD
		 || target->type == MT_EGGMANITEM || target->type == MT_EGGMANITEM_SHIELD
		 || target->type == MT_BALLHOG || target->type == MT_SPB
		 || target->type == MT_GACHABOM)) // kart dead items
		target->flags |= MF_NOGRAVITY; // Don't drop Tails 03-08-2000
	else
		target->flags &= ~MF_NOGRAVITY; // lose it if you for whatever reason have it, I'm looking at you shields
	//

	if (target->flags2 & MF2_NIGHTSPULL)
	{
		P_SetTarget(&target->tracer, NULL);
		target->movefactor = 0; // reset NightsItemChase timer
	}

	// dead target is no more shootable
	target->flags &= ~(MF_SHOOTABLE|MF_FLOAT|MF_SPECIAL);
	target->flags2 &= ~(MF2_SKULLFLY|MF2_NIGHTSPULL);
	target->health = 0; // This makes it easy to check if something's dead elsewhere.

	if (target->type != MT_BATTLEBUMPER)
	{
		target->shadowscale = 0;
	}

	if (LUA_HookMobjDeath(target, inflictor, source, damagetype) || P_MobjWasRemoved(target))
		return;

	P_ActivateThingSpecial(target, source);

	//K_SetHitLagForObjects(target, inflictor, source, MAXHITLAGTICS, true);

	// SRB2kart
	// I wish I knew a better way to do this
	if (target->target && target->target->player && target->target->player->mo)
	{
		if ((target->target->player->pflags & PF_EGGMANOUT) && target->type == MT_EGGMANITEM_SHIELD)
			target->target->player->pflags &= ~PF_EGGMANOUT;

		if (target->target->player->pflags & PF_ITEMOUT)
		{
			if ((target->type == MT_BANANA_SHIELD && target->target->player->itemtype == KITEM_BANANA) // trail items
				|| (target->type == MT_SSMINE_SHIELD && target->target->player->itemtype == KITEM_MINE)
				|| (target->type == MT_DROPTARGET_SHIELD && target->target->player->itemtype == KITEM_DROPTARGET)
				|| (target->type == MT_SINK_SHIELD && target->target->player->itemtype == KITEM_KITCHENSINK))
			{
				if (target->movedir != 0 && target->movedir < (UINT16)target->target->player->itemamount)
				{
					if (target->target->hnext)
						K_KillBananaChain(target->target->hnext, inflictor, source);
					target->target->player->itemamount = 0;
				}
				else if (target->target->player->itemamount)
					target->target->player->itemamount--;
			}
			else if ((target->type == MT_ORBINAUT_SHIELD && target->target->player->itemtype == KITEM_ORBINAUT) // orbit items
				|| (target->type == MT_JAWZ_SHIELD && target->target->player->itemtype == KITEM_JAWZ))
			{
				if (target->target->player->itemamount)
					target->target->player->itemamount--;
				if (target->lastlook != 0)
				{
					K_RepairOrbitChain(target);
				}
			}

			if (!target->target->player->itemamount)
				target->target->player->pflags &= ~PF_ITEMOUT;

			if (target->target->hnext == target)
				P_SetTarget(&target->target->hnext, NULL);
		}
	}
	// Above block does not clean up rocket sneakers when a player dies, so we need to do it here target->target is null when using rocket sneakers
	if (target->player)
		K_DropRocketSneaker(target->player);

	// Let EVERYONE know what happened to a player! 01-29-2002 Tails
	if (target->player && !target->player->spectator)
	{
		if (metalrecording) // Ack! Metal Sonic shouldn't die! Cut the tape, end recording!
			G_StopMetalRecording(true);

		target->renderflags &= ~RF_DONTDRAW;
	}

	// if killed by a player
	if (source && source->player)
	{
		if (target->flags & MF_MONITOR || target->type == MT_RANDOMITEM)
		{
			P_SetTarget(&target->target, source);

			if (!(gametyperules & GTR_CIRCUIT))
			{
				target->fuse = 2;
			}
			else
			{
				target->fuse = 2*TICRATE + 2;
			}
		}
	}

	// if a player avatar dies...
	if (target->player)
	{
		UINT8 i;

		target->flags &= ~(MF_SOLID|MF_SHOOTABLE); // does not block
		P_UnsetThingPosition(target);
		target->flags |= MF_NOBLOCKMAP|MF_NOCLIP|MF_NOCLIPHEIGHT|MF_NOGRAVITY;
		P_SetThingPosition(target);
		target->standingslope = NULL;
		target->terrain = NULL;
		target->pmomz = 0;

		target->player->playerstate = PST_DEAD;

		// respawn from where you died
		target->player->respawn.pointx = target->x;
		target->player->respawn.pointy = target->y;
		target->player->respawn.pointz = target->z;

		if (target->player == &players[consoleplayer])
		{
			// don't die in auto map,
			// switch view prior to dying
			if (automapactive)
				AM_Stop();
		}

		//added : 22-02-98: recenter view for next life...
		for (i = 0; i <= r_splitscreen; i++)
		{
			if (target->player == &players[displayplayers[i]])
			{
				localaiming[i] = 0;
				break;
			}
		}

		if (gametyperules & GTR_BUMPERS)
		{
			K_CheckBumpers();

			if (target->player->roundscore > 1)
				target->player->roundscore -= 2;
			else
				target->player->roundscore = 0;
		}

		target->player->trickpanel = 0;

		ACS_RunPlayerDeathScript(target->player);
	}

	if (source && target && target->player && source->player)
		P_PlayVictorySound(source); // Killer laughs at you. LAUGHS! BWAHAHAHA!

	// Other death animation effects
	switch(target->type)
	{
		case MT_BOUNCEPICKUP:
		case MT_RAILPICKUP:
		case MT_AUTOPICKUP:
		case MT_EXPLODEPICKUP:
		case MT_SCATTERPICKUP:
		case MT_GRENADEPICKUP:
			P_SetObjectMomZ(target, FRACUNIT, false);
			target->fuse = target->info->damage;
			break;

		case MT_BUGGLE:
			if (inflictor && inflictor->player // did a player kill you? Spawn relative to the player so they're bound to get it
			&& P_AproxDistance(inflictor->x - target->x, inflictor->y - target->y) <= inflictor->radius + target->radius + FixedMul(8*FRACUNIT, inflictor->scale) // close enough?
			&& inflictor->z <= target->z + target->height + FixedMul(8*FRACUNIT, inflictor->scale)
			&& inflictor->z + inflictor->height >= target->z - FixedMul(8*FRACUNIT, inflictor->scale))
				mo = P_SpawnMobj(inflictor->x + inflictor->momx, inflictor->y + inflictor->momy, inflictor->z + (inflictor->height / 2) + inflictor->momz, MT_EXTRALARGEBUBBLE);
			else
				mo = P_SpawnMobj(target->x, target->y, target->z, MT_EXTRALARGEBUBBLE);
			mo->destscale = target->scale;
			P_SetScale(mo, mo->destscale);
			P_SetMobjState(mo, mo->info->raisestate);
			break;

		case MT_YELLOWSHELL:
			P_SpawnMobjFromMobj(target, 0, 0, 0, MT_YELLOWSPRING);
			break;

		case MT_CRAWLACOMMANDER:
			target->momx = target->momy = target->momz = 0;
			break;

		case MT_CRUSHSTACEAN:
			if (target->tracer)
			{
				mobj_t *chain = target->tracer->target, *chainnext;
				while (chain)
				{
					chainnext = chain->target;
					P_RemoveMobj(chain);
					chain = chainnext;
				}
				S_StopSound(target->tracer);
				P_KillMobj(target->tracer, inflictor, source, damagetype);
			}
			break;

		case MT_BANPYURA:
			if (target->tracer)
			{
				S_StopSound(target->tracer);
				P_KillMobj(target->tracer, inflictor, source, damagetype);
			}
			break;

		case MT_EGGSHIELD:
			P_SetObjectMomZ(target, 4*target->scale, false);
			P_InstaThrust(target, target->angle, 3*target->scale);
			target->flags = (target->flags|MF_NOCLIPHEIGHT) & ~MF_NOGRAVITY;
			break;

		case MT_DRAGONBOMBER:
			{
				mobj_t *segment = target;
				while (segment->tracer != NULL)
				{
					P_KillMobj(segment->tracer, NULL, NULL, DMG_NORMAL);
					segment = segment->tracer;
				}
				break;
			}

		case MT_EGGMOBILE3:
			{
				mobj_t *mo2;
				thinker_t *th;
				UINT32 i = 0; // to check how many clones we've removed

				// scan the thinkers to make sure all the old pinch dummies are gone on death
				for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
				{
					if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
						continue;

					mo = (mobj_t *)th;
					if (mo->type != (mobjtype_t)target->info->mass)
						continue;
					if (mo->tracer != target)
						continue;

					P_KillMobj(mo, inflictor, source, damagetype);
					mo->destscale = mo->scale/8;
					mo->scalespeed = (mo->scale - mo->destscale)/(2*TICRATE);
					mo->momz = mo->info->speed;
					mo->angle = FixedAngle((P_RandomKey(PR_UNDEFINED, 36)*10)<<FRACBITS);

					mo2 = P_SpawnMobjFromMobj(mo, 0, 0, 0, MT_BOSSJUNK);
					mo2->angle = mo->angle;
					P_SetMobjState(mo2, S_BOSSSEBH2);

					if (++i == 2) // we've already removed 2 of these, let's stop now
						break;
					else
						S_StartSound(mo, mo->info->deathsound); // done once to prevent sound stacking
				}
			}
			break;

		case MT_BIGMINE:
			if (inflictor)
			{
				fixed_t dx = target->x - inflictor->x, dy = target->y - inflictor->y, dz = target->z - inflictor->z;
				fixed_t dm = FixedHypot(dz, FixedHypot(dy, dx));
				target->momx = FixedDiv(FixedDiv(dx, dm), dm)*512;
				target->momy = FixedDiv(FixedDiv(dy, dm), dm)*512;
				target->momz = FixedDiv(FixedDiv(dz, dm), dm)*512;
			}
			if (source)
				P_SetTarget(&target->tracer, source);
			break;

		case MT_BLASTEXECUTOR:
			if (target->spawnpoint)
				P_LinedefExecute(target->spawnpoint->angle, (source ? source : inflictor), target->subsector->sector);
			break;

		case MT_SPINBOBERT:
			if (target->hnext)
				P_KillMobj(target->hnext, inflictor, source, damagetype);
			if (target->hprev)
				P_KillMobj(target->hprev, inflictor, source, damagetype);
			break;

		case MT_EGGTRAP:
			// Time for birdies! Yaaaaaaaay!
			target->fuse = TICRATE;
			break;

		case MT_MINECART:
			A_Scream(target);
			target->momx = target->momy = target->momz = 0;
			if (target->target && target->target->health)
				P_KillMobj(target->target, target, source, DMG_NORMAL);
			break;

		case MT_PLAYER:
			if (damagetype != DMG_SPECTATOR)
			{
				angle_t flingAngle;
				mobj_t *kart;

				target->fuse = TICRATE*3; // timer before mobj disappears from view (even if not an actual player)
				target->momx = target->momy = target->momz = 0;

				kart = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_KART_LEFTOVER);

				if (kart && !P_MobjWasRemoved(kart))
				{
					kart->angle = target->angle;
					kart->color = target->color;
					kart->hitlag = target->hitlag;
					kart->eflags |= MFE_DAMAGEHITLAG;
					P_SetObjectMomZ(kart, 6*FRACUNIT, false);
					kart->extravalue1 = target->player->kartweight;

					// Copy interp data
					kart->old_angle = target->old_angle;
					kart->old_x = target->old_x;
					kart->old_y = target->old_y;
					kart->old_z = target->old_z;

					if (target->player->pflags & PF_NOCONTEST)
						P_SetTarget(&target->tracer, kart);

					kart->fuse = 5*TICRATE;
				}

				if (source && !P_MobjWasRemoved(source))
				{
					flingAngle = R_PointToAngle2(
						source->x - source->momx, source->y - source->momy,
						target->x, target->y
					);
				}
				else
				{
					flingAngle = target->angle + ANGLE_180;

					if (P_RandomByte(PR_ITEM_RINGS) & 1)
					{
						flingAngle -= ANGLE_45;
					}
					else
					{
						flingAngle += ANGLE_45;
					}
				}

				P_InstaThrust(target, flingAngle, 14 * target->scale);
				P_SetObjectMomZ(target, 14*FRACUNIT, false);

				P_PlayDeathSound(target);
			}

			// Prisons Free Play: don't eliminate P1 for
			// spectating. Because in Free Play, this player
			// can enter the game again, and these flags would
			// make them intangible.
			if (K_Cooperative() && !target->player->spectator)
			{
				target->player->pflags |= PF_ELIMINATED;

				if (!target->player->exiting)
				{
					target->player->pflags |= PF_NOCONTEST;
					K_InitPlayerTally(target->player);
				}
			}
			break;

		case MT_METALSONIC_RACE:
			target->fuse = TICRATE*3;
			target->momx = target->momy = target->momz = 0;
			P_SetObjectMomZ(target, 14*FRACUNIT, false);
			target->flags = (target->flags & ~MF_NOGRAVITY)|(MF_NOCLIP|MF_NOCLIPTHING);
			break;

		// SRB2Kart:
		case MT_SMK_ICEBLOCK:
			{
				mobj_t *cur = target->hnext;
				while (cur && !P_MobjWasRemoved(cur))
				{
					P_SetMobjState(cur, S_SMK_ICEBLOCK2);
					cur = cur->hnext;
				}
				target->fuse = 10;
				S_StartSound(target, sfx_s3k80);
			}
			break;

		case MT_ITEMCAPSULE:
		{
			UINT8 i;
			mobj_t *attacker = inflictor ? inflictor : source;
			mobj_t *part = target->hnext;
			angle_t angle = FixedAngle(360*P_RandomFixed(PR_ITEM_DEBRIS));
			INT16 spacing = (target->radius >> 1) / target->scale;

			// set respawn fuse
			if (K_CapsuleTimeAttackRules() == true) // no respawns
				;
			else if (target->threshold == KITEM_SUPERRING)
				target->fuse = 20*TICRATE;
			else
				target->fuse = 40*TICRATE;

			// burst effects
			for (i = 0; i < 2; i++)
			{
				mobj_t *blast = P_SpawnMobjFromMobj(target, 0, 0, target->info->height >> 1, MT_BATTLEBUMPER_BLAST);
				blast->angle = angle + i*ANGLE_90;
				P_SetScale(blast, 2*blast->scale/3);
				blast->destscale = 2*blast->scale;
			}

			// dust effects
			for (i = 0; i < 10; i++)
			{
				mobj_t *puff = P_SpawnMobjFromMobj(
					target,
					P_RandomRange(PR_ITEM_DEBRIS, -spacing, spacing) * FRACUNIT,
					P_RandomRange(PR_ITEM_DEBRIS, -spacing, spacing) * FRACUNIT,
					P_RandomRange(PR_ITEM_DEBRIS, 0, 4*spacing) * FRACUNIT,
					MT_SPINDASHDUST
				);

				P_SetScale(puff, (puff->destscale *= 2));
				puff->momz = puff->scale * P_MobjFlip(puff);

				P_Thrust(puff, R_PointToAngle2(target->x, target->y, puff->x, puff->y), 3*puff->scale);
				if (attacker)
				{
					puff->momx += attacker->momx;
					puff->momy += attacker->momy;
					puff->momz += attacker->momz;
				}
			}

			// remove inside item
			if (target->tracer && !P_MobjWasRemoved(target->tracer))
				P_RemoveMobj(target->tracer);

			// bust capsule caps
			while (part && !P_MobjWasRemoved(part))
			{
				P_InstaThrust(part, part->angle + ANGLE_90, 6 * part->target->scale);
				P_SetObjectMomZ(part, 6 * FRACUNIT, false);
				part->fuse = TICRATE/2;
				part->flags &= ~MF_NOGRAVITY;

				if (attacker)
				{
					part->momx += attacker->momx;
					part->momy += attacker->momy;
					part->momz += attacker->momz;
				}
				part = part->hnext;
			}

			// give the player an item!
			if (source && source->player)
			{
				player_t *player = source->player;

				// special behavior for ring capsules
				if (target->threshold == KITEM_SUPERRING)
				{
					K_AwardPlayerRings(player, 5 * target->movecount, true);
					break;
				}

				// special behavior for SPB capsules
				if (target->threshold == KITEM_SPB)
				{
					K_ThrowKartItem(player, true, MT_SPB, 1, 0, 0);
					break;
				}

				if (target->threshold < 1 || target->threshold >= NUMKARTITEMS) // bruh moment prevention
				{
					player->itemtype = KITEM_SAD;
					player->itemamount = 1;
				}
				else
				{
					player->itemtype = target->threshold;
					if (K_GetShieldFromItem(player->itemtype) != KSHIELD_NONE) // never give more than 1 shield
						player->itemamount = 1;
					else
						player->itemamount = max(1, target->movecount);
				}
				player->karthud[khud_itemblink] = TICRATE;
				player->karthud[khud_itemblinkmode] = 0;
				K_StopRoulette(&player->itemRoulette);
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolf);
			}
			break;
		}

		case MT_BATTLECAPSULE:
			{
				mobj_t *cur;
				angle_t dir = 0;

				target->fuse = 16;
				target->flags |= MF_NOCLIP|MF_NOCLIPTHING;

				if (inflictor)
				{
					dir = R_PointToAngle2(inflictor->x, inflictor->y, target->x, target->y);
					P_Thrust(target, dir, P_AproxDistance(inflictor->momx, inflictor->momy)/12);
				}
				else if (source)
					dir = R_PointToAngle2(source->x, source->y, target->x, target->y);

				target->momz += 8 * target->scale * P_MobjFlip(target);
				target->flags &= ~MF_NOGRAVITY;

				cur = target->hnext;

				while (cur && !P_MobjWasRemoved(cur))
				{
					cur->momx = target->momx;
					cur->momy = target->momy;
					cur->momz = target->momz;

					// Shoot every piece outward
					if (!(cur->x == target->x && cur->y == target->y))
					{
						P_Thrust(cur,
							R_PointToAngle2(target->x, target->y, cur->x, cur->y),
							R_PointToDist2(target->x, target->y, cur->x, cur->y) / 12
						);
					}

					cur->flags &= ~MF_NOGRAVITY;
					cur->tics = TICRATE;
					cur->frame &= ~FF_ANIMATE; // Stop animating the propellers

					cur->hitlag = target->hitlag;
					cur->eflags |= MFE_DAMAGEHITLAG;

					cur = cur->hnext;
				}

				S_StartSound(target, sfx_mbs60);

				P_AddBrokenPrison(target, source);
			}
			break;

		case MT_CDUFO:
			S_StartSound(inflictor, sfx_mbs60);

			target->momz = -(3*mapobjectscale)/2;
			target->fuse = 2*TICRATE;

			P_AddBrokenPrison(target, source);
			break;

		case MT_BATTLEBUMPER:
			{
				mobj_t *owner = target->target;
				mobj_t *overlay;

				S_StartSound(target, sfx_kc52);
				target->flags &= ~MF_NOGRAVITY;

				target->destscale = (3 * target->destscale) / 2;
				target->scalespeed = FRACUNIT/100;

				if (owner && !P_MobjWasRemoved(owner))
				{
					P_Thrust(target, R_PointToAngle2(owner->x, owner->y, target->x, target->y), 4 * target->scale);
				}

				target->momz += (24 * target->scale) * P_MobjFlip(target);
				target->fuse = 8;

				overlay = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_OVERLAY);

				P_SetTarget(&target->tracer, overlay);
				P_SetTarget(&overlay->target, target);

				overlay->color = target->color;
				P_SetMobjState(overlay, S_INVISIBLE);
			}
			break;

		case MT_DROPTARGET:
		case MT_DROPTARGET_SHIELD:
			target->fuse = 1;
			break;

		case MT_BANANA:
		case MT_BANANA_SHIELD:
		{
			const UINT8 numParticles = 8;
			const angle_t diff = ANGLE_MAX / numParticles;
			UINT8 i;

			for (i = 0; i < numParticles; i++)
			{
				mobj_t *spark = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_BANANA_SPARK);
				spark->angle = (diff * i) - (diff / 2);

				if (inflictor != NULL && P_MobjWasRemoved(inflictor) == false)
				{
					spark->angle += K_MomentumAngle(inflictor);
					spark->momx += inflictor->momx / 2;
					spark->momy += inflictor->momy / 2;
					spark->momz += inflictor->momz / 2;
				}

				P_SetObjectMomZ(spark, (12 + P_RandomRange(PR_DECORATION, -4, 4)) * FRACUNIT, true);
				P_Thrust(spark, spark->angle, (12 + P_RandomRange(PR_DECORATION, -4, 4)) * spark->scale);
			}
			break;
		}

		case MT_MONITOR:
			Obj_MonitorOnDeath(target);
			break;
		case MT_BATTLEUFO:
			Obj_BattleUFODeath(target);
			break;
		default:
			break;
	}

	if ((target->type == MT_JAWZ || target->type == MT_JAWZ_SHIELD) && !(target->flags2 & MF2_AMBUSH))
	{
		target->z += P_MobjFlip(target)*20*target->scale;
	}

	// kill tracer
	if (target->type == MT_FROGGER)
	{
		if (target->tracer && !P_MobjWasRemoved(target->tracer))
			P_KillMobj(target->tracer, inflictor, source, DMG_NORMAL);
	}

	if (target->type == MT_FROGGER || target->type == MT_ROBRA_HEAD || target->type == MT_BLUEROBRA_HEAD) // clean hnext list
	{
		mobj_t *cur = target->hnext;
		while (cur && !P_MobjWasRemoved(cur))
		{
			P_KillMobj(cur, inflictor, source, DMG_NORMAL);
			cur = cur->hnext;
		}
	}

	// Bounce up on death
	if (target->type == MT_SMK_PIPE || target->type == MT_SMK_MOLE || target->type == MT_SMK_THWOMP)
	{
		target->flags &= (~MF_NOGRAVITY);

		if (target->eflags & MFE_VERTICALFLIP)
			target->z -= target->height;
		else
			target->z += target->height;

		S_StartSound(target, target->info->deathsound);

		P_SetObjectMomZ(target, 8<<FRACBITS, false);
		if (inflictor)
			P_InstaThrust(target, R_PointToAngle2(inflictor->x, inflictor->y, target->x, target->y)+ANGLE_90, 16<<FRACBITS);
	}

	// Final state setting - do something instead of P_SetMobjState;
	// Final state setting - do something instead of P_SetMobjState;
	if (target->type == MT_SPIKE && target->info->deathstate != S_NULL)
	{
		const angle_t ang = ((inflictor) ? inflictor->angle : 0) + ANGLE_90;
		const fixed_t scale = target->scale;
		const fixed_t xoffs = P_ReturnThrustX(target, ang, 8*scale), yoffs = P_ReturnThrustY(target, ang, 8*scale);
		const UINT16 flip = (target->eflags & MFE_VERTICALFLIP);
		mobj_t *chunk;
		fixed_t momz;

		S_StartSound(target, target->info->deathsound);

		if (target->info->xdeathstate != S_NULL)
		{
			momz = 6*scale;
			if (flip)
				momz *= -1;
#define makechunk(angtweak, xmov, ymov) \
			chunk = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_SPIKE);\
			P_SetMobjState(chunk, target->info->xdeathstate);\
			chunk->health = 0;\
			chunk->angle = angtweak;\
			P_UnsetThingPosition(chunk);\
			chunk->flags = MF_NOCLIP;\
			chunk->x += xmov;\
			chunk->y += ymov;\
			P_SetThingPosition(chunk);\
			P_InstaThrust(chunk,chunk->angle, 4*scale);\
			chunk->momz = momz

			makechunk(ang + ANGLE_180, -xoffs, -yoffs);
			makechunk(ang, xoffs, yoffs);

#undef makechunk
		}

		momz = 7*scale;
		if (flip)
			momz *= -1;

		chunk = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_SPIKE);
		P_SetMobjState(chunk, target->info->deathstate);
		chunk->health = 0;
		chunk->angle = ang + ANGLE_180;
		P_UnsetThingPosition(chunk);
		chunk->flags = MF_NOCLIP;
		chunk->x -= xoffs;
		chunk->y -= yoffs;
		if (flip)
			chunk->z -= 12*scale;
		else
			chunk->z += 12*scale;
		P_SetThingPosition(chunk);
		P_InstaThrust(chunk, chunk->angle, 2*scale);
		chunk->momz = momz;

		P_SetMobjState(target, target->info->deathstate);
		target->health = 0;
		target->angle = ang;
		P_UnsetThingPosition(target);
		target->flags = MF_NOCLIP;
		target->x += xoffs;
		target->y += yoffs;
		target->z = chunk->z;
		P_SetThingPosition(target);
		P_InstaThrust(target, target->angle, 2*scale);
		target->momz = momz;
	}
	else if (target->type == MT_WALLSPIKE && target->info->deathstate != S_NULL)
	{
		const angle_t ang = (/*(inflictor) ? inflictor->angle : */target->angle) + ANGLE_90;
		const fixed_t scale = target->scale;
		const fixed_t xoffs = P_ReturnThrustX(target, ang, 8*scale), yoffs = P_ReturnThrustY(target, ang, 8*scale), forwardxoffs = P_ReturnThrustX(target, target->angle, 7*scale), forwardyoffs = P_ReturnThrustY(target, target->angle, 7*scale);
		const UINT16 flip = (target->eflags & MFE_VERTICALFLIP);
		mobj_t *chunk;
		boolean sprflip;

		S_StartSound(target, target->info->deathsound);
		if (!P_MobjWasRemoved(target->tracer))
			P_RemoveMobj(target->tracer);

		if (target->info->xdeathstate != S_NULL)
		{
			sprflip = P_RandomChance(PR_DECORATION, FRACUNIT/2);

#define makechunk(angtweak, xmov, ymov) \
			chunk = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_WALLSPIKE);\
			P_SetMobjState(chunk, target->info->xdeathstate);\
			chunk->health = 0;\
			chunk->angle = target->angle;\
			P_UnsetThingPosition(chunk);\
			chunk->flags = MF_NOCLIP;\
			chunk->x += xmov - forwardxoffs;\
			chunk->y += ymov - forwardyoffs;\
			P_SetThingPosition(chunk);\
			P_InstaThrust(chunk, angtweak, 4*scale);\
			chunk->momz = P_RandomRange(PR_DECORATION, 5, 7)*scale;\
			if (flip)\
				chunk->momz *= -1;\
			if (sprflip)\
				chunk->frame |= FF_VERTICALFLIP

			makechunk(ang + ANGLE_180, -xoffs, -yoffs);
			sprflip = !sprflip;
			makechunk(ang, xoffs, yoffs);

#undef makechunk
		}

		sprflip = P_RandomChance(PR_DECORATION, FRACUNIT/2);

		chunk = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_WALLSPIKE);

		P_SetMobjState(chunk, target->info->deathstate);
		chunk->health = 0;
		chunk->angle = target->angle;
		P_UnsetThingPosition(chunk);
		chunk->flags = MF_NOCLIP;
		chunk->x += forwardxoffs - xoffs;
		chunk->y += forwardyoffs - yoffs;
		P_SetThingPosition(chunk);
		P_InstaThrust(chunk, ang + ANGLE_180, 2*scale);
		chunk->momz = P_RandomRange(PR_DECORATION, 5, 7)*scale;
		if (flip)
			chunk->momz *= -1;
		if (sprflip)
			chunk->frame |= FF_VERTICALFLIP;

		P_SetMobjState(target, target->info->deathstate);
		target->health = 0;
		P_UnsetThingPosition(target);
		target->flags = MF_NOCLIP;
		target->x += forwardxoffs + xoffs;
		target->y += forwardyoffs + yoffs;
		P_SetThingPosition(target);
		P_InstaThrust(target, ang, 2*scale);
		target->momz = P_RandomRange(PR_DECORATION, 5, 7)*scale;
		if (flip)
			target->momz *= -1;
		if (!sprflip)
			target->frame |= FF_VERTICALFLIP;
	}
	else if (target->player)
	{
		P_SetPlayerMobjState(target, target->info->deathstate);
	}
	else
#ifdef DEBUG_NULL_DEATHSTATE
		P_SetMobjState(target, S_NULL);
#else
		P_SetMobjState(target, target->info->deathstate);
#endif

	/** \note For player, the above is redundant because of P_SetMobjState (target, S_PLAY_DIE1)
	   in P_DamageMobj()
	   Graue 12-22-2003 */
}

static boolean P_PlayerHitsPlayer(mobj_t *target, mobj_t *inflictor, mobj_t *source, INT32 damage, UINT8 damagetype)
{
	(void)inflictor;
	(void)damage;

	// SRB2Kart: We want to hurt ourselves, so it's now DMG_CANTHURTSELF
	if (damagetype & DMG_CANTHURTSELF)
	{
		// You can't kill yourself, idiot...
		if (source == target)
			return false;

		if (G_GametypeHasTeams())
		{
			// Don't hurt your team, either!
			if (source->player->ctfteam == target->player->ctfteam)
				return false;
		}
	}

	return true;
}

static boolean P_KillPlayer(player_t *player, mobj_t *inflictor, mobj_t *source, UINT8 type)
{
	if (type == DMG_SPECTATOR && (G_GametypeHasTeams() || G_GametypeHasSpectators()))
	{
		P_SetPlayerSpectator(player-players);
	}
	else
	{
		if (player->respawn.state != RESPAWNST_NONE)
		{
			K_DoInstashield(player);
			return false;
		}

		if (player->exiting == false && specialstageinfo.valid == true)
		{
			if (type == DMG_DEATHPIT)
			{
				HU_DoTitlecardCEcho(player, "FALL OUT!", false);
			}

			// This must be done before the condition to set
			// destscale = 1, so any special stage death
			// shrinks the player to a speck.
			P_DoPlayerExit(player, PF_NOCONTEST);
		}

		if (player->exiting && type == DMG_DEATHPIT)
		{
			// If the player already finished the race, and
			// they fall into a death pit afterward, their
			// body shrinks into nothingness.
			player->mo->destscale = 1;
			player->mo->flags |= MF_NOCLIPTHING;

			return false;
		}

		if (modeattacking & ATTACKING_SPB)
		{
			// Death in SPB Attack is an instant loss.
			P_DoPlayerExit(player, PF_NOCONTEST);
		}
	}

	switch (type)
	{
		case DMG_DEATHPIT:
			// Fell off the stage
			if (player->roundconditions.fell_off == false)
			{
				player->roundconditions.fell_off = true;
				player->roundconditions.checkthisframe = true;
			}

			if (gametyperules & GTR_BUMPERS)
			{
				player->mo->health--;
			}

			if (player->mo->health <= 0)
			{
				return true;
			}

			// Quick respawn; does not kill
			return K_DoIngameRespawn(player), false;

		case DMG_SPECTATOR:
			// disappearifies, but still gotta put items back in play
			break;

		default:
			// Everything else REALLY kills
			if (leveltime < starttime)
			{
				K_DoFault(player);
			}
			break;
	}

	if (player->spectator == false)
	{
		UINT32 skinflags = (demo.playback)
			? demo.skinlist[demo.currentskinid[(player-players)]].flags
			: skins[player->skin].flags;

		if (skinflags & SF_IRONMAN)
		{
			player->mo->skin = &skins[player->skin];
			player->charflags = skinflags;
			K_SpawnMagicianParticles(player->mo, 5);
			S_StartSound(player->mo, sfx_slip);
		}

		player->mo->renderflags &= ~RF_DONTDRAW;
	}

	K_DropEmeraldsFromPlayer(player, player->emeralds);
	K_SetHitLagForObjects(player->mo, inflictor, source, MAXHITLAGTICS, true);

	player->carry = CR_NONE;

	K_KartResetPlayerColor(player);

	P_ResetPlayer(player);

	P_SetPlayerMobjState(player->mo, player->mo->info->deathstate);

	if (type == DMG_TIMEOVER)
	{
		if (gametyperules & GTR_CIRCUIT)
		{
			mobj_t *boom;

			player->mo->flags |= (MF_NOGRAVITY|MF_NOCLIP);
			player->mo->renderflags |= RF_DONTDRAW;

			boom = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_FZEROBOOM);
			boom->scale = player->mo->scale;
			boom->angle = player->mo->angle;
			P_SetTarget(&boom->target, player->mo);
		}

		player->pflags |= PF_ELIMINATED;
	}

	return true;
}

static void AddTimesHit(player_t *player)
{
	const INT32 oldtimeshit = player->timeshit;

	player->timeshit++;

	// overflow prevention
	if (player->timeshit < oldtimeshit)
	{
		player->timeshit = oldtimeshit;
	}
}

static void AddNullHitlag(player_t *player, tic_t oldHitlag)
{
	if (player == NULL)
	{
		return;
	}

	// Hitlag from what would normally be damage but the
	// player was invulnerable.
	//
	// If we're constantly getting hit the same number of
	// times, we're probably standing on a damage floor.
	//
	// Checking if we're hit more than before ensures that:
	//
	// 1) repeating damage doesn't count
	// 2) new damage sources still count

	if (player->timeshit <= player->timeshitprev)
	{
		player->nullHitlag += (player->mo->hitlag - oldHitlag);
	}
}

/** Damages an object, which may or may not be a player.
  * For melee attacks, source and inflictor are the same.
  *
  * \param target     The object being damaged.
  * \param inflictor  The thing that caused the damage: creature, missile,
  *                   gargoyle, and so forth. Can be NULL in the case of
  *                   environmental damage, such as slime or crushing.
  * \param source     The creature or person responsible. For example, if a
  *                   player is hit by a ring, the player who shot it. In some
  *                   cases, the target will go after this object after
  *                   receiving damage. This can be NULL.
  * \param damage     Amount of damage to be dealt.
  * \param damagetype Type of damage to be dealt. If bit 7 (0x80) is set, this is an instant-kill.
  * \return True if the target sustained damage, otherwise false.
  * \todo Clean up this mess, split into multiple functions.
  * \sa P_KillMobj
  */
boolean P_DamageMobj(mobj_t *target, mobj_t *inflictor, mobj_t *source, INT32 damage, UINT8 damagetype)
{
	player_t *player;
	player_t *playerInflictor;
	boolean force = false;
	boolean spbpop = false;

	INT32 laglength = 6;

	if (objectplacing)
		return false;

	if (target->health <= 0)
		return false;

	// Spectator handling
	if (damagetype != DMG_SPECTATOR && target->player && target->player->spectator)
		return false;

	if (source && source->player && source->player->spectator)
		return false;

	if (((damagetype & DMG_TYPEMASK) == DMG_STING)
	|| ((inflictor && !P_MobjWasRemoved(inflictor)) && inflictor->type == MT_BANANA && inflictor->health <= 1))
	{
		laglength = 2;
	}
	else if (target->type == MT_DROPTARGET || target->type == MT_DROPTARGET_SHIELD)
	{
		laglength = 0; // handled elsewhere
	}

	switch (target->type)
	{
		case MT_MONITOR:
			damage = Obj_MonitorGetDamage(target, inflictor, damagetype);
			Obj_MonitorOnDamage(target, inflictor, damage);
			break;
		case MT_CDUFO:
			// Make it possible to pick them up during race
			if (inflictor->type == MT_ORBINAUT_SHIELD || inflictor->type == MT_JAWZ_SHIELD)
				return false;
			break;

		case MT_SPB:
			spbpop = (damagetype & DMG_TYPEMASK) == DMG_VOLTAGE;
			if (spbpop && source && source->player
				&& source->player->roundconditions.spb_neuter == false)
			{
				source->player->roundconditions.spb_neuter = true;
				source->player->roundconditions.checkthisframe = true;
			}
			break;

		default:
			break;
	}

	// Everything above here can't be forced.
	if (!metalrecording)
	{
		UINT8 shouldForce = LUA_HookShouldDamage(target, inflictor, source, damage, damagetype);
		if (P_MobjWasRemoved(target))
			return (shouldForce == 1); // mobj was removed
		if (shouldForce == 1)
			force = true;
		else if (shouldForce == 2)
			return false;
	}

	if (!force)
	{
		if (!spbpop)
		{
			if (!(target->flags & MF_SHOOTABLE))
				return false; // shouldn't happen...
		}
	}

	if (target->flags2 & MF2_SKULLFLY)
		target->momx = target->momy = target->momz = 0;

	if (target->flags & (MF_ENEMY|MF_BOSS))
	{
		if (!force && target->flags2 & MF2_FRET) // Currently flashing from being hit
			return false;

		if (LUA_HookMobjDamage(target, inflictor, source, damage, damagetype) || P_MobjWasRemoved(target))
			return true;

		if (target->health > 1)
			target->flags2 |= MF2_FRET;
	}

	player = target->player;
	playerInflictor = inflictor ? inflictor->player : NULL;

	if (playerInflictor)
	{
		AddTimesHit(playerInflictor);
	}

	if (player) // Player is the target
	{
		AddTimesHit(player);

		if (player->pflags & PF_GODMODE)
			return false;

		if (!force)
		{
			// Player hits another player
			if (source && source->player)
			{
				if (!P_PlayerHitsPlayer(target, inflictor, source, damage, damagetype))
					return false;
			}
		}

		if (inflictor && source && source->player)
		{
			if (source->player->roundconditions.hit_midair == false
				&& K_IsMissileOrKartItem(source)
				&& target->player->airtime > TICRATE/2
				&& source->player->airtime > TICRATE/2)
			{
				source->player->roundconditions.hit_midair = true;
				source->player->roundconditions.checkthisframe = true;
			}
		}

		// Instant-Death
		if ((damagetype & DMG_DEATHMASK))
		{
			if (!P_KillPlayer(player, inflictor, source, damagetype))
				return false;
		}
		else if (LUA_HookMobjDamage(target, inflictor, source, damage, damagetype))
		{
			return true;
		}
		else
		{
			UINT8 type = (damagetype & DMG_TYPEMASK);
			const boolean hardhit = (type == DMG_EXPLODE || type == DMG_KARMA || type == DMG_TUMBLE); // This damage type can do evil stuff like ALWAYS combo
			INT16 ringburst = 5;

			// Check if the player is allowed to be damaged!
			// If not, then spawn the instashield effect instead.
			if (!force)
			{
				boolean invincible = true;
				boolean clash = false;
				sfxenum_t sfx = sfx_None;

				if (!(gametyperules & GTR_BUMPERS))
				{
					if (damagetype & DMG_STEAL)
					{
						// Gametype does not have bumpers, steal damage is intended to not do anything
						// (No instashield is intentional)
						return false;
					}
				}

				if (player->invincibilitytimer > 0)
				{
					sfx = sfx_invind;
				}
				else if (K_IsBigger(target, inflictor) == true)
				{
					sfx = sfx_grownd;
				}
				else if (K_PlayerGuard(player))
				{
					sfx = sfx_s3k3a;
					clash = true;
				}
				else if (player->hyudorotimer > 0)
					;
				else
				{
					invincible = false;
				}

				if (invincible && type != DMG_STUMBLE && type != DMG_WHUMBLE)
				{
					const INT32 oldHitlag = target->hitlag;
					const INT32 oldHitlagInflictor = inflictor ? inflictor->hitlag : 0;

					// Damage during hitlag should be a no-op
					// for invincibility states because there
					// are no flashing tics. If the damage is
					// from a constant source, a deadlock
					// would occur.

					if (target->eflags & MFE_PAUSED)
					{
						player->timeshit--; // doesn't count

						if (playerInflictor)
						{
							playerInflictor->timeshit--;
						}

						return false;
					}

					laglength = max(laglength / 2, 1);
					K_SetHitLagForObjects(target, inflictor, source, laglength, false);

					AddNullHitlag(player, oldHitlag);
					AddNullHitlag(playerInflictor, oldHitlagInflictor);

					if (player->timeshit > player->timeshitprev)
					{
						S_StartSound(target, sfx);
					}

					if (clash)
					{
						player->spheres = max(player->spheres - 10, 0);

						if (inflictor)
						{
							K_DoPowerClash(target, inflictor);

							if (inflictor->type == MT_SUPER_FLICKY)
							{
								Obj_BlockSuperFlicky(inflictor);
							}
						}
						else if (source)
							K_DoPowerClash(target, source);
					}

					// Full invulnerability
					K_DoInstashield(player);
					return false;
				}

				{
					// Check if we should allow wombo combos (hard hits by default, inverted by the presence of DMG_WOMBO).
					boolean allowcombo = ((hardhit || (type == DMG_STUMBLE || type == DMG_WHUMBLE)) == !(damagetype & DMG_WOMBO));

					// Tumble/stumble is a special case.
					if (type == DMG_TUMBLE)
					{
						// don't allow constant combo
						if (player->tumbleBounces == 1 && (P_MobjFlip(target)*target->momz > 0))
							allowcombo = false;
					}
					else if (type == DMG_STUMBLE || type == DMG_WHUMBLE)
					{
						// don't allow constant combo
						if (player->tumbleBounces == TUMBLEBOUNCES-1 && (P_MobjFlip(target)*target->momz > 0))
						{
							if (type == DMG_STUMBLE)
								return false; // No-sell strings of stumble

							allowcombo = false;
						}
					}

					if (allowcombo == false && (target->eflags & MFE_PAUSED))
					{
						return false;
					}

					// DMG_EXPLODE excluded from flashtic checks to prevent dodging eggbox/SPB with weak spinout
					if ((target->hitlag == 0 || allowcombo == false) && player->flashing > 0 && type != DMG_EXPLODE && type != DMG_STUMBLE && type != DMG_WHUMBLE)
					{
						// Post-hit invincibility
						K_DoInstashield(player);
						return false;
					}
					else if (target->flags2 & MF2_ALREADYHIT) // do not deal extra damage in the same tic
					{
						K_SetHitLagForObjects(target, inflictor, source, laglength, true);
						return false;
					}
				}
			}

			if (gametyperules & GTR_BUMPERS)
			{
				if (damagetype & DMG_STEAL)
				{
					// Steals 2 bumpers
					damage = 2;
				}
			}
			else
			{
				// Do not die from damage outside of bumpers health system
				damage = 0;
			}

			// Sting and stumble shouldn't be rewarding Battle hits.
			if (type == DMG_STING || type == DMG_STUMBLE)
			{
				damage = 0;
			}
			else
			{
				// We successfully damaged them! Give 'em some bumpers!

				if (source && source != player->mo && source->player)
				{
					// Extend the invincibility if the hit was a direct hit.
					if (inflictor == source && source->player->invincibilitytimer &&
							!K_PowerUpRemaining(source->player, POWERUP_SMONITOR))
					{
						tic_t kinvextend;

						if (gametyperules & GTR_CLOSERPLAYERS)
							kinvextend = 2*TICRATE;
						else
							kinvextend = 5*TICRATE;

						source->player->invincibilitytimer += kinvextend;
					}

					K_TryHurtSoundExchange(target, source);

					if (K_Cooperative() == false)
					{
						K_BattleAwardHit(source->player, player, inflictor, damage);
					}

					if (K_Bumpers(source->player) < K_StartingBumperCount() || (damagetype & DMG_STEAL))
					{
						K_TakeBumpersFromPlayer(source->player, player, damage);
					}

					if (damagetype & DMG_STEAL)
					{
						// Give them ALL of your emeralds instantly :)
						source->player->emeralds |= player->emeralds;
						player->emeralds = 0;
						K_CheckEmeralds(source->player);
					}

					/* Drop "shield" immediately on contact. */
					if (source->player->curshield == KSHIELD_TOP)
					{
						Obj_GardenTopDestroy(source->player);
					}
				}

				if (!(damagetype & DMG_STEAL))
				{
					// Drop all of your emeralds
					K_DropEmeraldsFromPlayer(player, player->emeralds);
				}
			}

			player->sneakertimer = player->numsneakers = 0;
			player->driftboost = player->strongdriftboost = 0;
			player->gateBoost = 0;
			player->fastfall = 0;
			player->ringboost = 0;
			player->glanceDir = 0;
			player->pflags &= ~PF_GAINAX;

			if (player->spectator == false && !(player->charflags & SF_IRONMAN))
			{
				UINT32 skinflags = (demo.playback)
					? demo.skinlist[demo.currentskinid[(player-players)]].flags
					: skins[player->skin].flags;

				if (skinflags & SF_IRONMAN)
				{
					player->mo->skin = &skins[player->skin];
					player->charflags = skinflags;
					K_SpawnMagicianParticles(player->mo, 5);
				}
			}

			if (player->rings <= -20)
			{
				player->markedfordeath = true;
				damagetype = DMG_TUMBLE;
				type = DMG_TUMBLE;
				P_StartQuakeFromMobj(5, 32 * player->mo->scale, 512 * player->mo->scale, player->mo);
				//P_KillPlayer(player, inflictor, source, damagetype);
			}

			switch (type)
			{
				case DMG_STING:
					K_DebtStingPlayer(player, source);
					K_KartPainEnergyFling(player);
					ringburst = 0;
					break;
				case DMG_STUMBLE:
				case DMG_WHUMBLE:
					K_StumblePlayer(player);
					ringburst = 0;
					break;
				case DMG_TUMBLE:
					K_TumblePlayer(player, inflictor, source);
					ringburst = 10;
					break;
				case DMG_EXPLODE:
				case DMG_KARMA:
					ringburst = K_ExplodePlayer(player, inflictor, source);
					break;
				case DMG_WIPEOUT:
					K_SpinPlayer(player, inflictor, source, KSPIN_WIPEOUT);
					K_KartPainEnergyFling(player);
					break;
				case DMG_VOLTAGE:
				case DMG_NORMAL:
				default:
					K_SpinPlayer(player, inflictor, source, KSPIN_SPINOUT);
					break;
			}

			if (type != DMG_STUMBLE && type != DMG_WHUMBLE)
			{
				if (type != DMG_STING)
					player->flashing = K_GetKartFlashing(player);

				P_PlayRinglossSound(target);
				P_PlayerRingBurst(player, ringburst);

				K_PopPlayerShield(player);
				player->instashield = 15;

				K_PlayPainSound(target, source);
			}

			if (gametyperules & GTR_BUMPERS)
				player->spheres = min(player->spheres + 5, 40);

			if ((hardhit == true) || cv_kartdebughuddrop.value)
			{
				K_DropItems(player);
			}
			else
			{
				K_DropHnextList(player);
			}

			if (inflictor && !P_MobjWasRemoved(inflictor) && inflictor->type == MT_BANANA)
			{
				player->flipDI = true;
			}
		}
	}
	else
	{
		if (target->type == MT_SPECIAL_UFO)
		{
			return Obj_SpecialUFODamage(target, inflictor, source, damagetype);
		}

		if (damagetype & DMG_STEAL)
		{
			// Not a player, steal damage is intended to not do anything
			return false;
		}
	}

	// do the damage
	if (damagetype & DMG_DEATHMASK)
		target->health = 0;
	else
		target->health -= damage;

	if (source && source->player && target)
		G_GhostAddHit((INT32) (source->player - players), target);

	K_SetHitLagForObjects(target, inflictor, source, laglength, true);

	target->flags2 |= MF2_ALREADYHIT;

	if (target->health <= 0)
	{
		P_KillMobj(target, inflictor, source, damagetype);
		return true;
	}

	//K_SetHitLagForObjects(target, inflictor, source, laglength, true);

	if (!player)
	{
		P_SetMobjState(target, target->info->painstate);

		if (!P_MobjWasRemoved(target))
		{
			// if not intent on another player,
			// chase after this one
			P_SetTarget(&target->target, source);
		}
	}

	return true;
}

static void P_FlingBurst
(		player_t *player,
		angle_t fa,
		mobjtype_t objType,
		tic_t objFuse,
		fixed_t objScale,
		INT32 i)
{
	mobj_t *mo;
	fixed_t ns;
	fixed_t momxy = 5<<FRACBITS, momz = 12<<FRACBITS; // base horizonal/vertical thrusts
	INT32 mx = (i + 1) >> 1;

	mo = P_SpawnMobjFromMobj(player->mo, 0, 0, 0, objType);

	mo->threshold = 10; // not useful for spikes
	mo->fuse = objFuse;
	P_SetTarget(&mo->target, player->mo);

	// We want everything from P_SpawnMobjFromMobj except scale.
	objScale = FixedMul(objScale, FixedDiv(mapobjectscale, player->mo->scale));

	if (objScale != FRACUNIT)
	{
		P_SetScale(mo, FixedMul(objScale, mo->scale));
		mo->destscale = mo->scale;
	}

	/*
	0: 0
	1: 1 = (1+1)/2 = 1
	2: 1 = (2+1)/2 = 1
	3: 2 = (3+1)/2 = 2
	4: 2 = (4+1)/2 = 2
	5: 3 = (4+1)/2 = 2
	 */
	// Angle / height offset changes every other ring
	momxy -= mx * FRACUNIT;
	momz += mx * (2<<FRACBITS);

	if (i & 1)
		fa += ANGLE_180;

	ns = FixedMul(momxy, player->mo->scale);
	mo->momx = (mo->target->momx/2) + FixedMul(FINECOSINE(fa>>ANGLETOFINESHIFT), ns);
	mo->momy = (mo->target->momy/2) + FixedMul(FINESINE(fa>>ANGLETOFINESHIFT), ns);

	ns = FixedMul(momz, player->mo->scale);
	mo->momz = (mo->target->momz/2) + ((ns) * P_MobjFlip(mo));
}

/** Spills an injured player's rings.
  *
  * \param player    The player who is losing rings.
  * \param num_rings Number of rings lost. A maximum of 20 rings will be
  *                  spawned.
  * \sa P_PlayerFlagBurst
  */
void P_PlayerRingBurst(player_t *player, INT32 num_rings)
{
	INT32 num_fling_rings;
	INT32 i;
	angle_t fa;

	// Rings shouldn't be in Battle!
	if (gametyperules & GTR_SPHERES)
		return;

	// Better safe than sorry.
	if (!player)
		return;

	// Have a shield? You get hit, but don't lose your rings!
	if (K_GetShieldFromItem(player->itemtype) != KSHIELD_NONE)
		return;

	// 20 is the maximum number of rings that can be taken from you at once - half the span of your counter
	if (num_rings > 20)
		num_rings = 20;
	else if (num_rings <= 0)
		return;

	num_rings = -P_GivePlayerRings(player, -num_rings);
	num_fling_rings = num_rings+min(0, player->rings);

	// determine first angle
	fa = player->mo->angle + ((P_RandomByte(PR_ITEM_RINGS) & 1) ? -ANGLE_90 : ANGLE_90);

	for (i = 0; i < num_fling_rings; i++)
	{
		P_FlingBurst(player, fa, MT_FLINGRING, 60*TICRATE, FRACUNIT, i);
	}

	while (i < num_rings)
	{
		P_FlingBurst(player, fa, MT_DEBTSPIKE, 0, 3 * FRACUNIT / 2, i++);
	}
}
