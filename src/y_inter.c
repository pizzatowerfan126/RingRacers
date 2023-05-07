// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2004-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  y_inter.c
/// \brief Tally screens, or "Intermissions" as they were formally called in Doom

#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "f_finale.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "i_net.h"
#include "i_video.h"
#include "p_tick.h"
#include "r_defs.h"
#include "r_skins.h"
#include "s_sound.h"
#include "st_stuff.h"
#include "v_video.h"
#include "w_wad.h"
#include "y_inter.h"
#include "z_zone.h"
#include "k_menu.h"
#include "m_misc.h"
#include "i_system.h"
#include "p_setup.h"

#include "r_local.h"
#include "p_local.h"

#include "m_cond.h" // condition sets
#include "lua_hook.h" // IntermissionThinker hook

#include "lua_hud.h"
#include "lua_hudlib_drawlist.h"

#include "m_random.h" // M_RandomKey
#include "g_input.h" // G_PlayerInputDown
#include "k_hud.h" // K_DrawMapThumbnail
#include "k_battle.h"
#include "k_boss.h"
#include "k_pwrlv.h"
#include "k_grandprix.h"
#include "k_color.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

typedef struct
{
	char patch[9];
	 INT32 points;
	UINT8 display;
} y_bonus_t;

typedef struct
{
	INT32 *character[MAXPLAYERS]; // Character #
	UINT16 *color[MAXPLAYERS]; // Color #
	SINT8 num[MAXPLAYERS]; // Player #
	char *name[MAXPLAYERS]; // Player's name

	UINT8 numplayers; // Number of players being displayed

	char headerstring[64]; // holds levelnames up to 64 characters

	// SRB2kart
	INT16 increase[MAXPLAYERS]; // how much did the score increase by?
	UINT8 jitter[MAXPLAYERS]; // wiggle

	UINT32 val[MAXPLAYERS]; // Gametype-specific value
	UINT8 pos[MAXPLAYERS]; // player positions. used for ties

	boolean rankingsmode; // rankings mode
	boolean gotthrough; // show "got through"
	boolean encore; // encore mode
} y_data;

static y_data data;

// graphics
static patch_t *bgpatch = NULL;     // INTERSCR
static patch_t *widebgpatch = NULL;
static patch_t *bgtile = NULL;      // SPECTILE/SRB2BACK
static patch_t *interpic = NULL;    // custom picture defined in map header

static INT32 timer;
static INT32 powertype = PWRLV_DISABLED;

static INT32 intertic;
static INT32 endtic = -1;
static INT32 sorttic = -1;
static INT32 replayprompttic;

// FUCK YOU
static fixed_t mqscroll = 0;
static fixed_t chkscroll = 0;

intertype_t intertype = int_none;

static huddrawlist_h luahuddrawlist_intermission;

static void Y_UnloadData(void);

//
// SRB2Kart - Y_CalculateMatchData and ancillary functions
//
static void Y_CompareTime(INT32 i)
{
	UINT32 val = ((players[i].pflags & PF_NOCONTEST || players[i].realtime == UINT32_MAX)
		? (UINT32_MAX-1) : players[i].realtime);

	if (!(val < data.val[data.numplayers]))
		return;

	data.val[data.numplayers] = val;
	data.num[data.numplayers] = i;
}

static void Y_CompareScore(INT32 i)
{
	UINT32 val = ((players[i].pflags & PF_NOCONTEST)
			? (UINT32_MAX-1) : players[i].roundscore);

	if (!(data.val[data.numplayers] == UINT32_MAX
	|| (!(players[i].pflags & PF_NOCONTEST) && val > data.val[data.numplayers])))
		return;

	data.val[data.numplayers] = val;
	data.num[data.numplayers] = i;
}

static void Y_CompareRank(INT32 i)
{
	INT16 increase = ((data.increase[i] == INT16_MIN) ? 0 : data.increase[i]);
	UINT32 score = players[i].score;

	if (powertype != PWRLV_DISABLED)
	{
		score = clientpowerlevels[i][powertype];
	}

	if (!(data.val[data.numplayers] == UINT32_MAX || (score - increase) > data.val[data.numplayers]))
		return;

	data.val[data.numplayers] = (score - increase);
	data.num[data.numplayers] = i;
}

static void Y_CalculateMatchData(UINT8 rankingsmode, void (*comparison)(INT32))
{
	INT32 i, j;
	boolean completed[MAXPLAYERS];
	INT32 numplayersingame = 0;

	// Initialize variables
	if (rankingsmode > 1)
		;
	else if ((data.rankingsmode = (boolean)rankingsmode))
	{
		sprintf(data.headerstring, "Total Rankings");
		data.gotthrough = false;
	}
	else
	{
		UINT8 whiteplayer = demo.playback ? displayplayers[0] : consoleplayer;

		data.headerstring[0] = '\0';
		data.gotthrough = false;

		if (whiteplayer < MAXPLAYERS
			&& playeringame[whiteplayer]
			&& players[whiteplayer].spectator == false
		)
		{
			if (!(players[whiteplayer].pflags & PF_NOCONTEST))
			{
				data.gotthrough = true;

				if (players[whiteplayer].skin < numskins)
				{
					snprintf(data.headerstring,
						sizeof data.headerstring,
						"%s",
						skins[players[whiteplayer].skin].realname);
				}
			}
			else
			{
				snprintf(data.headerstring,
					sizeof data.headerstring,
					"NO CONTEST...");
			}
		}
		else
		{
			if (bossinfo.valid == true && bossinfo.enemyname)
			{
				snprintf(data.headerstring,
					sizeof data.headerstring,
					"%s",
					bossinfo.enemyname);
			}
			else if (battleprisons == true)
			{
				snprintf(data.headerstring,
					sizeof data.headerstring,
					"PRISON BREAK");
			}
			else
			{
				snprintf(data.headerstring,
					sizeof data.headerstring,
					"%s STAGE",
					gametypes[gametype]->name);
			}
		}

		data.headerstring[sizeof data.headerstring - 1] = '\0';

		data.encore = encoremode;

		memset(data.jitter, 0, sizeof (data.jitter));
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		data.val[i] = UINT32_MAX;

		if (!playeringame[i] || players[i].spectator)
		{
			data.increase[i] = INT16_MIN;
			continue;
		}

		if (!rankingsmode)
			data.increase[i] = INT16_MIN;

		numplayersingame++;
	}

	memset(data.color, 0, sizeof (data.color));
	memset(data.character, 0, sizeof (data.character));
	memset(completed, 0, sizeof (completed));
	data.numplayers = 0;

	for (j = 0; j < numplayersingame; j++)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator || completed[i])
				continue;

			comparison(i);
		}

		i = data.num[data.numplayers];

		completed[i] = true;

		data.color[data.numplayers] = &players[i].skincolor;
		data.character[data.numplayers] = &players[i].skin;
		data.name[data.numplayers] = player_names[i];

		if (data.numplayers && (data.val[data.numplayers] == data.val[data.numplayers-1]))
		{
			data.pos[data.numplayers] = data.pos[data.numplayers-1];
		}
		else
		{
			data.pos[data.numplayers] = data.numplayers+1;
		}

		if (!rankingsmode)
		{
			if ((powertype == PWRLV_DISABLED)
				&& !(players[i].pflags & PF_NOCONTEST)
				&& (data.pos[data.numplayers] < (numplayersingame + spectateGriefed)))
			{
				// Online rank is handled further below in this file.
				data.increase[i] = K_CalculateGPRankPoints(data.pos[data.numplayers], numplayersingame + spectateGriefed);
				players[i].score += data.increase[i];
			}

			if (demo.recording)
			{
				G_WriteStanding(
					data.pos[data.numplayers],
					data.name[data.numplayers],
					*data.character[data.numplayers],
					*data.color[data.numplayers],
					data.val[data.numplayers]
				);
			}
		}

		data.numplayers++;
	}
}

typedef enum
{
	BPP_AHEAD,
	BPP_DONE,
	BPP_MAIN,
	BPP_SHADOW = BPP_MAIN,
	BPP_MAX
} bottomprogressionpatch_t;

//
// Y_IntermissionDrawer
//
// Called by D_Display. Nothing is modified here; all it does is draw. (SRB2Kart: er, about that...)
// Neat concept, huh?
//
void Y_IntermissionDrawer(void)
{

// dummy ALL OF THIS SHIT out, we're gonna be starting over.
#if 0
	INT32 i, whiteplayer = MAXPLAYERS, x = 4, hilicol = highlightflags;

	// If we early return, skip drawing the 3D scene (software buffer) so it doesn't clobber the frame for the wipe
	g_wipeskiprender = true;

	if (intertype == int_none || rendermode == render_none)
		return;

	g_wipeskiprender = false;

	// the merge was kind of a mess, how does this work -- toast 171021
	{
		M_DrawMenuBackground();
	}

	if (renderisnewtic)
	{
		LUA_HUD_ClearDrawList(luahuddrawlist_intermission);
		LUA_HookHUD(luahuddrawlist_intermission, HUD_HOOK(intermission));
	}
	LUA_HUD_DrawList(luahuddrawlist_intermission);

	if (!LUA_HudEnabled(hud_intermissiontally))
		goto skiptallydrawer;

	if (!r_splitscreen)
		whiteplayer = demo.playback ? displayplayers[0] : consoleplayer;

	if (sorttic != -1 && intertic > sorttic)
	{
		INT32 count = (intertic - sorttic);

		if (count < 8)
			x -= ((count * vid.width) / (8 * vid.dupx));
		else if (count == 8)
			goto skiptallydrawer;
		else if (count < 16)
			x += (((16 - count) * vid.width) / (8 * vid.dupx));
	}

	if (intertype == int_time || intertype == int_score)
	{
#define NUMFORNEWCOLUMN 8
		INT32 y = 41, gutter = ((data.numplayers > NUMFORNEWCOLUMN) ? 0 : (BASEVIDWIDTH/2));
		INT32 dupadjust = (vid.width/vid.dupx), duptweak = (dupadjust - BASEVIDWIDTH)/2;
		const char *timeheader;
		int y2;

		if (data.rankingsmode)
		{
			if (powertype == PWRLV_DISABLED)
			{
				timeheader = "RANK";
			}
			else
			{
				timeheader = "PWR.LV";
			}
		}
		else
		{
			switch (intertype)
			{
				case int_score:
					timeheader = "SCORE";
					break;
				default:
					timeheader = "TIME";
					break;
			}
		}

		// draw the level name
		V_DrawCenteredString(-4 + x + BASEVIDWIDTH/2, 12, 0, data.levelstring);
		V_DrawFill((x-3) - duptweak, 34, dupadjust-2, 1, 0);

		if (data.encore)
			V_DrawCenteredString(-4 + x + BASEVIDWIDTH/2, 12-8, hilicol, "ENCORE MODE");

		if (data.numplayers > NUMFORNEWCOLUMN)
		{
			V_DrawFill(x+156, 24, 1, 158, 0);
			V_DrawFill((x-3) - duptweak, 182, dupadjust-2, 1, 0);

			V_DrawCenteredString(x+6+(BASEVIDWIDTH/2), 24, hilicol, "#");
			V_DrawString(x+36+(BASEVIDWIDTH/2), 24, hilicol, "NAME");

			V_DrawRightAlignedString(x+152, 24, hilicol, timeheader);
		}

		V_DrawCenteredString(x+6, 24, hilicol, "#");
		V_DrawString(x+36, 24, hilicol, "NAME");

		V_DrawRightAlignedString(x+(BASEVIDWIDTH/2)+152, 24, hilicol, timeheader);

		for (i = 0; i < data.numplayers; i++)
		{
			boolean dojitter = data.jitter[data.num[i]];
			data.jitter[data.num[i]] = 0;

			if (data.num[i] != MAXPLAYERS && playeringame[data.num[i]] && !players[data.num[i]].spectator)
			{
				char strtime[MAXPLAYERNAME+1];

				if (dojitter)
					y--;

				V_DrawCenteredString(x+6, y, 0, va("%d", data.pos[i]));

				if (data.color[i])
				{
					UINT8 *colormap = R_GetTranslationColormap(*data.character[i], *data.color[i], GTC_CACHE);
					V_DrawMappedPatch(x+16, y-4, 0, faceprefix[*data.character[i]][FACE_RANK], colormap);
				}

				if (data.num[i] == whiteplayer)
				{
					UINT8 cursorframe = (intertic / 4) % 8;
					V_DrawScaledPatch(x+16, y-4, 0, W_CachePatchName(va("K_CHILI%d", cursorframe+1), PU_CACHE));
				}

				if ((players[data.num[i]].pflags & PF_NOCONTEST) && players[data.num[i]].bot)
				{
					// RETIRED!!
					V_DrawScaledPatch(x+12, y-7, 0, W_CachePatchName("K_NOBLNS", PU_CACHE));
				}

				STRBUFCPY(strtime, data.name[i]);

				y2 = y;

				if ((netgame || (demo.playback && demo.netgame)) && playerconsole[data.num[i]] == 0 && server_lagless && !players[data.num[i]].bot)
				{
					static int alagles_timer = 0;
					patch_t *alagles;

					y2 = ( y - 4 );

					V_DrawScaledPatch(x + 36, y2, 0, W_CachePatchName(va("BLAGLES%d", (intertic / 3) % 6), PU_CACHE));
					// every 70 tics
					if (( leveltime % 70 ) == 0)
					{
						alagles_timer = 9;
					}
					if (alagles_timer > 0)
					{
						alagles = W_CachePatchName(va("ALAGLES%d", alagles_timer), PU_CACHE);
						V_DrawScaledPatch(x + 36, y2, 0, alagles);
						if (( leveltime % 2 ) == 0)
							alagles_timer--;
					}
					else
					{
						alagles = W_CachePatchName("ALAGLES0", PU_CACHE);
						V_DrawScaledPatch(x + 36, y2, 0, alagles);
					}

					y2 += SHORT (alagles->height) + 1;
				}

				if (data.numplayers > NUMFORNEWCOLUMN)
					V_DrawThinString(x+36, y2-1, ((data.num[i] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, strtime);
				else
					V_DrawString(x+36, y2, ((data.num[i] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE, strtime);

				if (data.rankingsmode)
				{
					if (powertype != PWRLV_DISABLED && !clientpowerlevels[data.num[i]][powertype])
					{
						// No power level (guests)
						STRBUFCPY(strtime, "----");
					}
					else
					{
						if (data.increase[data.num[i]] != INT16_MIN)
						{
							snprintf(strtime, sizeof strtime, "(%d)", data.increase[data.num[i]]);

							if (data.numplayers > NUMFORNEWCOLUMN)
								V_DrawRightAlignedThinString(x+133+gutter, y-1, V_6WIDTHSPACE, strtime);
							else
								V_DrawRightAlignedString(x+118+gutter, y, 0, strtime);
						}

						snprintf(strtime, sizeof strtime, "%d", data.val[i]);
					}

					if (data.numplayers > NUMFORNEWCOLUMN)
						V_DrawRightAlignedThinString(x+152+gutter, y-1, V_6WIDTHSPACE, strtime);
					else
						V_DrawRightAlignedString(x+152+gutter, y, 0, strtime);
				}
				else
				{
					if (data.val[i] == (UINT32_MAX-1))
						V_DrawRightAlignedThinString(x+152+gutter, y-1, (data.numplayers > NUMFORNEWCOLUMN ? V_6WIDTHSPACE : 0), "NO CONTEST.");
					else
					{
						if (intertype == int_time)
						{
							snprintf(strtime, sizeof strtime, "%i'%02i\"%02i", G_TicsToMinutes(data.val[i], true),
							G_TicsToSeconds(data.val[i]), G_TicsToCentiseconds(data.val[i]));
							strtime[sizeof strtime - 1] = '\0';

							if (data.numplayers > NUMFORNEWCOLUMN)
								V_DrawRightAlignedThinString(x+152+gutter, y-1, V_6WIDTHSPACE, strtime);
							else
								V_DrawRightAlignedString(x+152+gutter, y, 0, strtime);
						}
						else
						{
							if (data.numplayers > NUMFORNEWCOLUMN)
								V_DrawRightAlignedThinString(x+152+gutter, y-1, V_6WIDTHSPACE, va("%i", data.val[i]));
							else
								V_DrawRightAlignedString(x+152+gutter, y, 0, va("%i", data.val[i]));
						}
					}
				}

				if (dojitter)
					y++;
			}
			else
				data.num[i] = MAXPLAYERS; // this should be the only field setting in this function

			y += 18;

			if (i == NUMFORNEWCOLUMN-1)
			{
				y = 41;
				x += BASEVIDWIDTH/2;
			}
#undef NUMFORNEWCOLUMN
		}
	}

skiptallydrawer:
	if (!LUA_HudEnabled(hud_intermissionmessages))
		return;

	if (timer)
	{
		if (netgame || demo.netgame)
		{
			char *string;
			INT32 tickdown = (timer+1)/TICRATE;

			if (demo.playback)
				string = va("Replay ends in %d", tickdown);
			else if ((nextmapoverride != 0)
			|| (roundqueue.size > 0 && roundqueue.position < roundqueue.size))
				string = va("Next starts in %d", tickdown);
			else
				string = va("%s starts in %d", cv_advancemap.string, tickdown);

			V_DrawCenteredString(BASEVIDWIDTH/2, 188, hilicol, string);

			if (speedscramble != -1 && speedscramble != gamespeed)
			{
				V_DrawCenteredString(BASEVIDWIDTH/2, BASEVIDHEIGHT-24, hilicol|V_ALLOWLOWERCASE|V_SNAPTOBOTTOM,
					va(M_GetText("Next race will be %s Speed!"), kartspeed_cons_t[1+speedscramble].strvalue));
			}
		}
	}

	M_DrawMenuForeground();
#endif

	// INFO SEGMENT
	// Numbers are V_DrawRightAlignedThinString WITH v_6widthspace as flags
	// resbar 1 (48,82)  5 (176, 82)
	// 2 (48, 96)

	//player icon 1 (55,79) 2 (55,93) 5 (183,79)

	// If we early return, skip drawing the 3D scene (software buffer) so it doesn't clobber the frame for the wipe
	g_wipeskiprender = true;

	if (intertype == int_none || rendermode == render_none)
		return;

	g_wipeskiprender = false;

	UINT8 i = 0;
	fixed_t x, y, xoffset = 0;

	INT32 hilicol = highlightflags;
	INT32 whiteplayer = MAXPLAYERS;

	if (!r_splitscreen)
		whiteplayer = demo.playback ? displayplayers[0] : consoleplayer;

	// Patches

	patch_t *gthro = W_CachePatchName("R_GTHRO", PU_PATCH); // GOT THROUGH ROUND
	patch_t *resbar = W_CachePatchName("R_RESBAR", PU_PATCH); // Results bars for players

	// Header bar
	patch_t *rtpbr = W_CachePatchName("R_RTPBR", PU_PATCH);

	// Checker scroll
	patch_t *rbgchk = W_CachePatchName("R_RBGCHK", PU_PATCH);

	// Scrolling marquee
	patch_t *rrmq = W_CachePatchName("R_RRMQ", PU_PATCH);

	// Blending mask for the background
	patch_t *mask = W_CachePatchName("R_MASK", PU_PATCH);

	fixed_t mqloop = SHORT(rrmq->width)*FRACUNIT;
	fixed_t chkloop = SHORT(rbgchk->width)*FRACUNIT;

	UINT8 *bgcolor = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_INTERMISSION, GTC_CACHE);

	// Draw the background
	K_DrawMapThumbnail(0, 0, BASEVIDWIDTH<<FRACBITS, (data.encore ? V_FLIP : 0), prevmap, bgcolor);
	
	// Draw a mask over the BG to get the correct colorization
	V_DrawMappedPatch(0, 0, V_ADD|V_TRANSLUCENT, mask, NULL);
	
	// Draw the marquee (scroll pending)
	//V_DrawMappedPatch(0, 154, V_SUBTRACT, rrmq, NULL);
	
	// Draw the checker pattern (scroll pending)
	//V_DrawMappedPatch(0, 0, V_SUBTRACT, rbgchk, NULL);
	
	for (x = -mqscroll; x < (BASEVIDWIDTH * FRACUNIT); x += mqloop)
	{
		V_DrawFixedPatch(x, 154<<FRACBITS, FRACUNIT, V_SUBTRACT, rrmq, NULL);
	}

	V_DrawFixedPatch(chkscroll, 0, FRACUNIT, V_SUBTRACT, rbgchk, NULL);
	V_DrawFixedPatch(chkscroll - chkloop, 0, FRACUNIT, V_SUBTRACT, rbgchk, NULL);

	// Animate scrolling elements if relevant
	if (!paused && !P_AutoPause())
	{
		mqscroll += renderdeltatics;
		if (mqscroll > mqloop)
			mqscroll %= mqloop;

		chkscroll += renderdeltatics;
		if (chkscroll > chkloop)
			chkscroll %= chkloop;
	}

	if (renderisnewtic)
	{
		LUA_HUD_ClearDrawList(luahuddrawlist_intermission);
		LUA_HookHUD(luahuddrawlist_intermission, HUD_HOOK(intermission));
	}
	LUA_HUD_DrawList(luahuddrawlist_intermission);

	if (!LUA_HudEnabled(hud_intermissiontally))
		goto skiptallydrawer;

	if (sorttic != -1 && intertic > sorttic)
	{
		INT32 count = (intertic - sorttic);

		if (count < 8)
			xoffset = -((count * vid.width) / (8 * vid.dupx));
		else if (count == 8)
			goto skiptallydrawer;
		else if (count < 16)
			xoffset = (((16 - count) * vid.width) / (8 * vid.dupx));
	}

	// Draw the header bar
	{
		V_DrawMappedPatch(20 + xoffset, 24, 0, rtpbr, NULL);

		if (data.gotthrough)
		{
			// Draw "GOT THROUGH ROUND"
			V_DrawMappedPatch(50 + xoffset, 42, 0, gthro, NULL);

			// Draw round numbers
			if (roundqueue.roundnum > 0 && !(grandprixinfo.gp == true && grandprixinfo.eventmode != GPEVENT_NONE))
			{
				patch_t *roundpatch =
					W_CachePatchName(
						va("TT_RND%d", roundqueue.roundnum),
						PU_PATCH
					);
				V_DrawMappedPatch(240 + xoffset, 39, 0, roundpatch, NULL);
			}

			// Draw the player's name
			V_DrawTitleCardString(51 + xoffset, 7, data.headerstring, V_6WIDTHSPACE, false, 0, 0);
		}
		else
		{
			V_DrawTitleCardString(51 + xoffset, 17, data.headerstring, V_6WIDTHSPACE, false, 0, 0);
		}
	}

	{
		SINT8 yspacing = 14;
		fixed_t heightcount = (data.numplayers - 1);
		fixed_t returny;

		boolean verticalresults = (data.numplayers < 4);

		if (verticalresults)
		{
			x = (BASEVIDWIDTH/2) - 61;
		}
		else
		{
			x = 29 + xoffset;
			heightcount /= 2;
		}

		if (data.numplayers > 10)
		{
			yspacing--;
		}
		else if (data.numplayers <= 6)
		{
			yspacing++;
			if (verticalresults)
			{
				yspacing++;
			}
		}

		y = returny = 106 - (heightcount * yspacing)/2;

		for (i = 0; i < data.numplayers; i++)
		{
			boolean dojitter = data.jitter[data.num[i]] > 0;
			data.jitter[data.num[i]] = 0;

			if (data.num[i] == MAXPLAYERS)
				;
			else if (!playeringame[data.num[i]] || players[data.num[i]].spectator == true)
				data.num[i] = MAXPLAYERS; // this should be the only field setting in this function
			else
			{
				char strtime[MAXPLAYERNAME+1];

				if (dojitter)
					y--;

				V_DrawMappedPatch(x, y, 0, resbar, NULL);

				V_DrawRightAlignedThinString(x+13, y-2, 0, va("%d", data.pos[i]));

				if (data.color[i])
				{
					UINT8 *charcolormap;
					if (data.rankingsmode == 0 && (players[data.num[i]].pflags & PF_NOCONTEST) && players[data.num[i]].bot)
					{
						// RETIRED !!
						charcolormap = R_GetTranslationColormap(TC_DEFAULT, *data.color[i], GTC_CACHE);
						V_DrawMappedPatch(x+14, y-5, 0, W_CachePatchName("MINIDEAD", PU_CACHE), charcolormap);
					}
					else
					{
						charcolormap = R_GetTranslationColormap(*data.character[i], *data.color[i], GTC_CACHE);
						V_DrawMappedPatch(x+14, y-5, 0, faceprefix[*data.character[i]][FACE_MINIMAP], charcolormap);
					}
				}

				STRBUFCPY(strtime, data.name[i]);

/*				y2 = y;

				if ((netgame || (demo.playback && demo.netgame)) && playerconsole[data.num[i]] == 0 && server_lagless && !players[data.num[i]].bot)
				{
					static UINT8 alagles_timer = 0;
					patch_t *alagles;

					y2 = ( y - 4 );

					V_DrawScaledPatch(x + 36, y2, 0, W_CachePatchName(va("BLAGLES%d", (intertic / 3) % 6), PU_CACHE));
					// every 70 tics
					if (( leveltime % 70 ) == 0)
					{
						alagles_timer = 9;
					}
					if (alagles_timer > 0)
					{
						alagles = W_CachePatchName(va("ALAGLES%d", alagles_timer), PU_CACHE);
						V_DrawScaledPatch(x + 36, y2, 0, alagles);
						if (( leveltime % 2 ) == 0)
							alagles_timer--;
					}
					else
					{
						alagles = W_CachePatchName("ALAGLES0", PU_CACHE);
						V_DrawScaledPatch(x + 36, y2, 0, alagles);
					}

					y2 += SHORT (alagles->height) + 1;
				}*/

				V_DrawThinString(x+27, y-2, ((data.num[i] == whiteplayer) ? hilicol : 0)|V_ALLOWLOWERCASE|V_6WIDTHSPACE, strtime);

				strtime[0] = '\0';

				if (data.rankingsmode)
				{
					if (powertype != PWRLV_DISABLED && !clientpowerlevels[data.num[i]][powertype])
					{
						// No power level (guests)
						STRBUFCPY(strtime, "----");
					}
					else
					{
						/*if (data.increase[data.num[i]] != INT16_MIN)
						{
							snprintf(strtime, sizeof strtime, " (%d)", data.increase[data.num[i]]);

							V_DrawThinString(x+118, y-2, V_6WIDTHSPACE, strtime);
						}*/

						snprintf(strtime, sizeof strtime, "%d", data.val[i]);
					}
				}
				else
				{
					if (data.val[i] == (UINT32_MAX-1))
						STRBUFCPY(strtime, "RETIRED.");
					else
					{
						if (intertype == int_time)
						{
							snprintf(strtime, sizeof strtime, "%i'%02i\"%02i", G_TicsToMinutes(data.val[i], true),
							G_TicsToSeconds(data.val[i]), G_TicsToCentiseconds(data.val[i]));
							strtime[sizeof strtime - 1] = '\0';
						}
						else
						{
							snprintf(strtime, sizeof strtime, "%d", data.val[i]);
						}
					}
				}

				V_DrawRightAlignedThinString(x+118, y-2, V_6WIDTHSPACE, strtime);

				if (dojitter)
					y++;
			}

			y += yspacing;

			if (verticalresults == false && i == (data.numplayers-1)/2)
			{
				x = 169 + xoffset;
				y = returny;
			}
		}
	}

	// Draw bottom (and top) pieces
skiptallydrawer:
	{
		if ((modeattacking == ATTACKING_NONE) && (demo.recording || demo.savemode == DSM_SAVED) && !demo.playback)
		{
			switch (demo.savemode)
			{
				case DSM_NOTSAVING: 
				{
					INT32 buttonx = BASEVIDWIDTH;
					INT32 buttony = 2;
					
					K_drawButtonAnim(buttonx - 76, buttony, V_SNAPTOTOP|V_SNAPTORIGHT, kp_button_b[1], replayprompttic);
					V_DrawRightAlignedThinString(buttonx - 55, buttony, V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|V_6WIDTHSPACE|hilicol, "or");
					K_drawButtonAnim(buttonx - 55, buttony, V_SNAPTOTOP|V_SNAPTORIGHT, kp_button_x[1], replayprompttic);
					V_DrawRightAlignedThinString(buttonx - 2, buttony, V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|V_6WIDTHSPACE|hilicol, "Save replay");
					break;	
				}
				case DSM_SAVED:
					V_DrawRightAlignedThinString(BASEVIDWIDTH - 2, 2, V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|V_6WIDTHSPACE|hilicol, "Replay saved!");
					break;

				case DSM_TITLEENTRY:
					ST_DrawDemoTitleEntry();
					break;

				default: // Don't render any text here
					break;
			}
		}
	}

	if (!LUA_HudEnabled(hud_intermissionmessages))
		return;

	if (roundqueue.size > 0)
	{
		UINT8 *greymap = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_GREY, GTC_CACHE);

		// Background pieces
		patch_t *queuebg_flat = W_CachePatchName("R_RMBG1", PU_PATCH);
		patch_t *queuebg_upwa = W_CachePatchName("R_RMBG2", PU_PATCH);
		patch_t *queuebg_down = W_CachePatchName("R_RMBG3", PU_PATCH);
		//patch_t *queuebg_prize = W_CachePatchName("R_RMBG4", PU_PATCH);

		// Progression lines
		patch_t *line_upwa[BPP_MAX];
		patch_t *line_down[BPP_MAX];
//		patch_t *line_flat[BPP_MAX];

		line_upwa[BPP_AHEAD] = W_CachePatchName("R_RRMLN1", PU_PATCH);
		line_upwa[BPP_DONE] = W_CachePatchName("R_RRMLN3", PU_PATCH);
		line_upwa[BPP_SHADOW] = W_CachePatchName("R_RRMLS1", PU_PATCH);

		line_down[BPP_AHEAD] = W_CachePatchName("R_RRMLN2", PU_PATCH);
		line_down[BPP_DONE] = W_CachePatchName("R_RRMLN4", PU_PATCH);
		line_down[BPP_SHADOW] = W_CachePatchName("R_RRMLS2", PU_PATCH);
/*
		line_flat[BPP_AHEAD] = W_CachePatchName("R_RRMLN5", PU_PATCH);
		line_flat[BPP_DONE] = W_CachePatchName("R_RRMLN6", PU_PATCH);
		line_flat[BPP_SHADOW] = W_CachePatchName("R_RRMLS3", PU_PATCH);
*/
		// Progress markers
		patch_t *rpmark = W_CachePatchName("R_RPMARK", PU_PATCH);
		patch_t *level_dot[BPP_MAIN];
		patch_t *capsu_dot[BPP_MAIN];
		patch_t *prize_dot[BPP_MAIN];

		level_dot[BPP_AHEAD] = W_CachePatchName("R_RRMRK2", PU_PATCH);
		level_dot[BPP_DONE] = W_CachePatchName("R_RRMRK1", PU_PATCH);

		capsu_dot[BPP_AHEAD] = W_CachePatchName("R_RRMRK3", PU_PATCH);
		capsu_dot[BPP_DONE] = W_CachePatchName("R_RRMRK5", PU_PATCH);

		prize_dot[BPP_AHEAD] = W_CachePatchName("R_RRMRK4", PU_PATCH);
		prize_dot[BPP_DONE] = W_CachePatchName("R_RRMRK6", PU_PATCH);

		UINT8 *colormap = NULL, *oppositemap = NULL;
		UINT8 pskin = MAXSKINS;
		UINT16 pcolor = SKINCOLOR_NONE;

		if (whiteplayer < MAXPLAYERS
			&& playeringame[whiteplayer]
			&& players[whiteplayer].spectator == false
			&& players[whiteplayer].skin < numskins
			&& players[whiteplayer].skincolor < numskincolors
		)
		{
			pskin = players[whiteplayer].skin;
			pcolor = players[whiteplayer].skincolor;
		}

		if (pcolor != SKINCOLOR_NONE)
		{
			colormap = R_GetTranslationColormap(TC_DEFAULT, pcolor, GTC_CACHE);
			oppositemap = R_GetTranslationColormap(TC_DEFAULT, skincolors[pcolor].invcolor, GTC_CACHE);
		}

		//x = 24;
		x = (BASEVIDWIDTH - 24*(roundqueue.size - 1)) / 2;

		// Fill in background to left edge of screen
		fixed_t xiter = x;
		while (xiter > 0)
		{
			xiter -= 24;
			V_DrawMappedPatch(xiter, 167, 0, queuebg_flat, greymap);
		}

		for (i = 0; i < roundqueue.size; i++)
		{
			// Draw the background, and grab the appropriate line, to the right of the dot
			patch_t **choose_line = NULL;

			if (i & 1)
			{
				y = 171;

				V_DrawMappedPatch(x, 167, 0, queuebg_down, greymap);

				if (i+1 != roundqueue.size) // no more line?
				{
					choose_line = line_down;
				}
			}
			else
			{
				y = 179;

				if (i+1 != roundqueue.size) // no more line?
				{
					V_DrawMappedPatch(x, 167, 0, queuebg_upwa, greymap);

					choose_line = line_upwa;
				}
				else
				{
					V_DrawMappedPatch(x, 167, 0, queuebg_flat, greymap);
				}
			}

			// Draw the line to the right of the dot (if valid)
			if (choose_line != NULL)
			{
				V_DrawMappedPatch(
					x - 1, 178,
					0,
					choose_line[BPP_SHADOW],
					NULL
				);

				V_DrawMappedPatch(
					x - 1, 179,
					0,
					choose_line[roundqueue.position > i+1 ? BPP_DONE : BPP_AHEAD],
					roundqueue.position > i+1 ? colormap : NULL
				);
			}

			// Now draw the dot
			patch_t **chose_dot = NULL;

			if (roundqueue.entries[i].rankrestricted == true)
			{
				chose_dot = prize_dot;
			}
			else if (grandprixinfo.gp == true
				&& roundqueue.entries[i].gametype != roundqueue.entries[0].gametype
			)
			{
				chose_dot = capsu_dot;
			}
			else
			{
				chose_dot = level_dot;
			}

			if (chose_dot)
			{
				V_DrawMappedPatch(
					x - 8, y,
					0,
					chose_dot[roundqueue.position >= i+1 ? BPP_DONE : BPP_AHEAD],
					roundqueue.position == i+1 ? oppositemap : colormap
				);
			}

			// Draw the player position through the round queue!
			if (roundqueue.position == i+1)
			{
				// Draw outline for rank icon
				V_DrawMappedPatch(
					x - 10, y - 14,
					0, rpmark,
					NULL
				);

				if (pskin < numskins
					&& pcolor != SKINCOLOR_NONE
				)
				{
					// Draw the player's rank icon
					V_DrawMappedPatch(
						x - 9, y - 13,
						0,
						faceprefix[pskin][FACE_RANK],
						R_GetTranslationColormap(pskin, pcolor, GTC_CACHE)
					);
				}
			}

			x += 24;
		}

		// Fill in background to right edge of screen
		xiter = x;
		while (xiter < BASEVIDWIDTH)
		{
			V_DrawMappedPatch(xiter, 167, 0, queuebg_flat, greymap);
			xiter += 24;
		}

/*
		V_DrawMappedPatch(253, 167, 0, queuebg_flat, greymap);
		V_DrawMappedPatch(277, 167, 0, queuebg_prize, greymap);
		V_DrawMappedPatch(301, 167, 0, queuebg_flat, greymap);

		// haha funny 54-part progress bar
		// i am a dumbass and there is probably a better way to do this
		for (UINT16 x2 = 172; x2 < 284; x2 += 2)
		{
			// does not account for colormap since at the moment that will never be seen
			V_DrawMappedPatch(x2, 177, 0, line_flat[BPP_SHADOW], NULL);
			V_DrawMappedPatch(x2, 179, 0, line_flat[BPP_AHEAD], NULL);
		}
*/
	}
}

//
// Y_Ticker
//
// Manages fake score tally for single player end of act, and decides when intermission is over.
//
void Y_Ticker(void)
{
	if (intertype == int_none)
		return;

	if (demo.recording)
	{
		if (demo.savemode == DSM_NOTSAVING)
		{
			replayprompttic++;
			G_CheckDemoTitleEntry();
		} 
			
		if (demo.savemode == DSM_WILLSAVE || demo.savemode == DSM_WILLAUTOSAVE)
			G_SaveDemo();
	}

	// Check for pause or menu up in single player
	if (paused || P_AutoPause())
		return;

	LUA_HOOK(IntermissionThinker);

	intertic++;

	// Team scramble code for team match and CTF.
	// Don't do this if we're going to automatically scramble teams next round.
	/*if (G_GametypeHasTeams() && cv_teamscramble.value && !cv_scrambleonchange.value && server)
	{
		// If we run out of time in intermission, the beauty is that
		// the P_Ticker() team scramble code will pick it up.
		if ((intertic % (TICRATE/7)) == 0)
			P_DoTeamscrambling();
	}*/

	if ((timer && !--timer)
		|| (intertic == endtic))
	{
		Y_EndIntermission();
		G_AfterIntermission();
		return;
	}

	if (intertic < TICRATE || intertic & 1 || endtic != -1)
		return;

	if (intertype == int_time || intertype == int_score)
	{
		{
			if (!data.rankingsmode && sorttic != -1 && (intertic >= sorttic + 8))
			{
				// Anything with post-intermission consequences here should also occur in Y_EndIntermission.
				K_RetireBots();
				Y_CalculateMatchData(1, Y_CompareRank);
			}

			if (data.rankingsmode && intertic > sorttic+16+(2*TICRATE))
			{
				INT32 q=0,r=0;
				boolean kaching = true;

				for (q = 0; q < data.numplayers; q++)
				{
					if (data.num[q] == MAXPLAYERS
						|| !data.increase[data.num[q]]
						|| data.increase[data.num[q]] == INT16_MIN)
					{
						continue;
					}

					r++;
					data.jitter[data.num[q]] = 1;

					if (powertype != PWRLV_DISABLED)
					{
						// Power Levels
						if (abs(data.increase[data.num[q]]) < 10)
						{
							// Not a lot of point increase left, just set to 0 instantly
							data.increase[data.num[q]] = 0;
						}
						else
						{
							SINT8 remove = 0; // default (should not happen)

							if (data.increase[data.num[q]] < 0)
								remove = -10;
							else if (data.increase[data.num[q]] > 0)
								remove = 10;

							// Remove 10 points at a time
							data.increase[data.num[q]] -= remove;

							// Still not zero, no kaching yet
							if (data.increase[data.num[q]] != 0)
								kaching = false;
						}
					}
					else
					{
						// Basic bitch points
						if (data.increase[data.num[q]])
						{
							if (--data.increase[data.num[q]])
								kaching = false;
						}
					}
				}

				if (r)
				{
					S_StartSound(NULL, (kaching ? sfx_chchng : sfx_ptally));
					Y_CalculateMatchData(2, Y_CompareRank);
				}
				/*else -- This is how to define an endtic, but we currently use timer for both SP and MP.
					endtic = intertic + 3*TICRATE;*/
			}
		}
	}
}

//
// Y_DetermineIntermissionType
//
// Determines the intermission type from the current gametype.
//
void Y_DetermineIntermissionType(void)
{
	// no intermission for GP events
	if (grandprixinfo.gp == true && grandprixinfo.eventmode != GPEVENT_NONE)
	{
		intertype = int_none;
		return;
	}

	// set initially
	intertype = gametypes[gametype]->intermission;

	// special cases
	if (intertype == int_scoreortimeattack)
	{
		UINT8 i = 0, nump = 0;
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;
			nump++;
		}
		intertype = (nump < 2 ? int_time : int_score);
	}
}

//
// Y_StartIntermission
//
// Called by G_DoCompleted. Sets up data for intermission drawer/ticker.
//
void Y_StartIntermission(void)
{
	UINT8 i = 0, nump = 0;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		nump++;
	}

	intertic = -1;

#ifdef PARANOIA
	if (endtic != -1)
		I_Error("endtic is dirty");
#endif

	// set player Power Level type
	powertype = K_UsingPowerLevels();

	// determine the tic the intermission ends
	// Technically cv_inttime is saved to demos... but this permits having extremely long timers for post-netgame chatting without stranding you on the intermission in netreplays.
	if (!K_CanChangeRules(false))
	{
		timer = 10*TICRATE;
	}
	else
	{
		timer = cv_inttime.value*TICRATE;
	}

	// determine the tic everybody's scores/PWR starts getting sorted
	sorttic = -1;
	if (!timer)
	{
		// Prevent a weird bug
		timer = 1;
	}
	else if (nump < 2 && !netgame)
	{
		// No PWR/global score, skip it
		timer /= 2;
	}
	else
	{
		// Minimum two seconds for match results, then two second slideover approx halfway through
		sorttic = max((timer/2) - 2*TICRATE, 2*TICRATE);
	}

	// We couldn't display the intermission even if we wanted to.
	// But we still need to give the players their score bonuses, dummy.
	//if (dedicated) return;

	// This should always exist, but just in case...
	if (prevmap >= nummapheaders || !mapheaderinfo[prevmap])
		I_Error("Y_StartIntermission: Internal map ID %d not found (nummapheaders = %d)", prevmap, nummapheaders);

	if (timer > 1 && musiccountdown == 0)
		S_ChangeMusicInternal("racent", true); // loop it

	S_ShowMusicCredit(); // Always call

	switch (intertype)
	{
		case int_score:
		{
			// Calculate who won
			Y_CalculateMatchData(0, Y_CompareScore);
			break;
		}
		case int_time:
		{
			// Calculate who won
			Y_CalculateMatchData(0, Y_CompareTime);
			break;
		}

		case int_none:
		default:
			break;
	}

	LUA_HUD_DestroyDrawList(luahuddrawlist_intermission);
	luahuddrawlist_intermission = LUA_HUD_CreateDrawList();

	if (powertype != PWRLV_DISABLED)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			// Kind of a hack to do this here,
			// but couldn't think of a better way.
			data.increase[i] = K_FinalPowerIncrement(
				&players[i],
				clientpowerlevels[i][powertype],
				clientPowerAdd[i]
			);
		}

		K_CashInPowerLevels();
	}

	if (roundqueue.size > 0 && roundqueue.position == roundqueue.size)
	{
		Automate_Run(AEV_QUEUEEND);
	}

	Automate_Run(AEV_INTERMISSIONSTART);
	bgpatch = W_CachePatchName("MENUBG", PU_STATIC);
	widebgpatch = W_CachePatchName("WEIRDRES", PU_STATIC);

	M_UpdateMenuBGImage(true);
}

// ======

//
// Y_EndIntermission
//
void Y_EndIntermission(void)
{
	if (!data.rankingsmode)
	{
		K_RetireBots();
	}

	Y_UnloadData();

	endtic = -1;
	sorttic = -1;
	intertype = int_none;
}

#define UNLOAD(x) if (x) {Patch_Free(x);} x = NULL;
#define CLEANUP(x) x = NULL;

//
// Y_UnloadData
//
static void Y_UnloadData(void)
{
	// In hardware mode, don't Z_ChangeTag a pointer returned by W_CachePatchName().
	// It doesn't work and is unnecessary.
	if (rendermode != render_soft)
		return;

	// unload the background patches
	UNLOAD(bgpatch);
	UNLOAD(widebgpatch);
	UNLOAD(bgtile);
	UNLOAD(interpic);
}
