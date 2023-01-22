/// \file  menus/options-profiles-edit-1.c
/// \brief Profile Editor

#include "../k_menu.h"
#include "../s_sound.h"

menuitem_t OPTIONS_EditProfile[] = {
	{IT_STRING | IT_CVAR | IT_CV_STRING, "Profile Name", "6-character long name to identify this Profile.",
		NULL, {.cvar = &cv_dummyprofilename}, 0, 0},

	{IT_STRING | IT_CVAR | IT_CV_STRING, "Player Name", "Name displayed online when using this Profile.",
	NULL, {.cvar = &cv_dummyprofileplayername}, 0, 0},

	{IT_STRING | IT_CALL, "Character", "Default character and color for this Profile.",
		NULL, {.routine = M_CharacterSelect}, 0, 0},

	{IT_STRING | IT_CALL, "Controls", "Select the button mappings for this Profile.",
	NULL, {.routine = M_ProfileDeviceSelect}, 0, 0},

	{IT_STRING | IT_CALL, "Confirm", "Confirm changes.",
	NULL, {.routine = M_ConfirmProfile}, 0, 0},

};

menu_t OPTIONS_EditProfileDef = {
	sizeof (OPTIONS_EditProfile) / sizeof (menuitem_t),
	&OPTIONS_ProfilesDef,
	0,
	OPTIONS_EditProfile,
	32, 80,
	SKINCOLOR_ULTRAMARINE, 0,
	2, 5,
	M_DrawEditProfile,
	M_HandleProfileEdit,
	NULL,
	NULL,
	M_ProfileEditInputs,
};

// Returns true if the profile can be saved, false otherwise. Also starts messages if necessary.
static boolean M_ProfileEditEnd(const UINT8 pid)
{
	UINT8 i;

	// Guest profile, you can't edit that one!
	if (optionsmenu.profilen == 0)
	{
		S_StartSound(NULL, sfx_s3k7b);
		M_StartMessage(M_GetText("Guest profile cannot be edited.\nCreate a new profile instead."), NULL, MM_NOTHING);
		M_SetMenuDelay(pid);
		return false;
	}

	// check if some profiles have the same name
	for (i = 0; i < PR_GetNumProfiles(); i++)
	{
		profile_t *check = PR_GetProfile(i);

		// For obvious reasons don't check if our name is the same as our name....
		if (check != optionsmenu.profile)
		{
			if (!(strcmp(optionsmenu.profile->profilename, check->profilename)))
			{
				S_StartSound(NULL, sfx_s3k7b);
				M_StartMessage(M_GetText("Another profile uses the same name.\nThis must be changed to be able to save."), NULL, MM_NOTHING);
				M_SetMenuDelay(pid);
				return false;
			}
		}
	}

	return true;
}

static void M_ProfileEditExit(void)
{
	optionsmenu.toptx = 160;
	optionsmenu.topty = 35;
	optionsmenu.resetprofile = true;	// Reset profile after the transition is done.

	PR_SaveProfiles();					// save profiles after we do that.
}

// For profile edit, just make sure going back resets the card to its position, the rest is taken care of automatically.
boolean M_ProfileEditInputs(INT32 ch)
{

	(void) ch;
	const UINT8 pid = 0;

	if (M_MenuBackPressed(pid))
	{
		if (M_ProfileEditEnd(pid))
		{
			M_ProfileEditExit();
			if (cv_currprofile.value == -1)
				M_SetupNextMenu(&MAIN_ProfilesDef, false);
			else
				M_GoBack(0);
			M_SetMenuDelay(pid);
		}
		return true;
	}
	else if (M_MenuConfirmPressed(pid))
	{
		if (currentMenu->menuitems[itemOn].status & IT_TRANSTEXT)
			return true;	// No.
	}

	return false;
}

// Handle some actions in profile editing
void M_HandleProfileEdit(void)
{
	// Always copy the profile name and player name in the profile.
	if (optionsmenu.profile)
	{
		// Copy the first 6 chars for profile name
		if (strlen(cv_dummyprofilename.string))
		{
			char *s;
			// convert dummyprofilename to uppercase
			strncpy(optionsmenu.profile->profilename, cv_dummyprofilename.string, PROFILENAMELEN);
			s = optionsmenu.profile->profilename;
			while (*s)
			{
				*s = toupper(*s);
				s++;
			}
		}

		if (strlen(cv_dummyprofileplayername.string))
			strncpy(optionsmenu.profile->playername, cv_dummyprofileplayername.string, MAXPLAYERNAME);
	}

	M_OptionsTick();	//  Has to be afterwards because this can unset optionsmenu.profile
}

// Confirm Profile edi via button.
void M_ConfirmProfile(INT32 choice)
{
	const UINT8 pid = 0;
	(void) choice;

	if (M_ProfileEditEnd(pid))
	{
		if (cv_currprofile.value > -1)
		{
			M_ProfileEditExit();
			M_GoBack(0);
			M_SetMenuDelay(pid);
		}
		else
		{
			M_StartMessage(M_GetText("Are you sure you wish to\nselect this profile?\n\nPress (A) to confirm or (B) to cancel"), FUNCPTRCAST(M_FirstPickProfile), MM_YESNO);
			M_SetMenuDelay(pid);
		}
	}
	return;
}

// Prompt a device selection window (just tap any button on the device you want)
void M_ProfileDeviceSelect(INT32 choice)
{
	(void)choice;

	// While we're here, setup the incoming controls menu to reset the scroll & bind status:
	optionsmenu.controlscroll = 0;
	optionsmenu.bindcontrol = 0;
	optionsmenu.bindtimer = 0;

	optionsmenu.lastkey = 0;
	optionsmenu.keyheldfor = 0;

	optionsmenu.contx = optionsmenu.tcontx = controlleroffsets[gc_a][0];
	optionsmenu.conty = optionsmenu.tconty = controlleroffsets[gc_a][1];

	M_SetupNextMenu(&OPTIONS_ProfileControlsDef, false);	// Don't set device here anymore.
}