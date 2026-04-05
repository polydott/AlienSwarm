#include "cbase.h"
#include "asw_input.h"
#include "iasw_client_vehicle.h" // asw
#include "iasw_client_aim_target.h" // asw
#include "c_asw_player.h" // asw
#include "c_asw_marine.h" // asw
#include "c_asw_marine_resource.h" // asw
#include "c_asw_game_resource.h" // asw
#include "c_asw_weapon.h"
#include "controller_focus.h"
#include "inputsystem/ButtonCode.h"
#include "kbutton.h"
#include "c_asw_order_arrow.h"
#include "vgui/asw_vgui_info_message.h"
#include "hltvcamera.h"
#include "iclientmode.h"
#include "prediction.h"
#include "checksum_md5.h"
#include "in_buttons.h"
#include "holdout_resupply_frame.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar asw_controls; // asw, whether to use swarm controls or not
ConVar joy_pan_camera("joy_pan_camera", "0", FCVAR_ARCHIVE);
ConVar asw_ground_secondary("asw_ground_secondary", "1", FCVAR_NONE, "Set to 1 to make marines aim grenades at the floor instead of firing them straight");


static  kbutton_t	in_holdorder;

// JOYPAD ADDED

// ===========
// IN_Joystick_Advanced_f
// ===========
void IN_Joystick_Advanced_f (void)
{
	::input->Joystick_Advanced( false );
#ifdef INFESTED_DLL	// asw - make sure our vgui joypad focus panel knows which buttons we're using for up/down/left/right
	ASW_UpdateControllerCodes();
#endif
}

extern ConVar sv_noclipduringpause;
extern ConVar in_forceuser;
extern int in_impulse[ MAX_SPLITSCREEN_PLAYERS ];
extern kbutton_t	in_attack;
extern kbutton_t	in_attack2;

static float s_fMarineDownTime = 0;
static int s_iMarineOrderingStartX = 0;
static int s_iMarineOrderingStartY = 0;
static Vector s_vecMarineOrderPos = vec3_origin;
CHandle<C_ASW_Marine> s_hOrderTarget = NULL;
ConVar asw_mouse_order_dist("asw_mouse_order_dist", "100", 0, "Minimum distance squared needed to move the cursor while holding down a marine number to order that marine to face that direction");

void GetVGUICursorPos( int& x, int& y );
void SetVGUICursorPos( int x, int y );

// ordering marines to hold a specific position/direction
void IN_HoldOrderDown()
{
	KeyDown(&in_holdorder, NULL);
	// if we don't have a marine to order, find one
	if (s_hOrderTarget.Get() == NULL)
	{
		GetVGUICursorPos(s_iMarineOrderingStartX, s_iMarineOrderingStartY);
		int x, y;
		engine->GetScreenSize( x, y );
		x = x >> 1;
		y = y >> 1;

		float mx, my;
		mx = s_iMarineOrderingStartX - x;
		my = s_iMarineOrderingStartY - y;
		float mx_ratio =((float) mx) / ((float) x);
		float my_ratio =((float) my) / ((float) y);
		HUDTraceToWorld(-mx_ratio * 0.5f, -my_ratio * 0.5f, s_vecMarineOrderPos);	// store the spot we'll send a marine to

		// find the marine nearest s_vecMarineOrderPos, biased against marines already holding a spot
		C_ASW_Player* pPlayer = C_ASW_Player::GetLocalASWPlayer();
		if (pPlayer)
		{
			// get the marine we're ordering
			C_ASW_Marine *pTarget = pPlayer->FindMarineToHoldOrder(s_vecMarineOrderPos);
			if (pTarget && pTarget->GetHealth() > 0)
			{
				s_hOrderTarget = pTarget;				
				ASWInput()->ASW_SetOrderingMarine(pTarget->entindex());
			}
		}
		C_ASW_Game_Resource *pGameResource = ASWGameResource();
		if (pGameResource && pGameResource->GetNumMarines(pPlayer, true) > 2)	// if we only have 2 marines selected, we can just fall through here and start ordering
			return;
	}

	if (s_fMarineDownTime == 0)
	{
		s_fMarineDownTime = gpGlobals->curtime;				
		GetVGUICursorPos(s_iMarineOrderingStartX, s_iMarineOrderingStartY);
		int x, y;
		engine->GetScreenSize( x, y );
		x = x >> 1;
		y = y >> 1;

		float mx, my;
		mx = s_iMarineOrderingStartX - x;
		my = s_iMarineOrderingStartY - y;
		float mx_ratio =((float) mx) / ((float) x);
		float my_ratio =((float) my) / ((float) y);			
		HUDTraceToWorld(-mx_ratio * 0.5f, -my_ratio * 0.5f, s_vecMarineOrderPos, true);	// store the spot we'll send a marine to

		// find the marine nearest s_vecMarineOrderPos, biased against marines already holding a spot
		C_ASW_Player* pPlayer = C_ASW_Player::GetLocalASWPlayer();
		if (pPlayer)
		{	
			C_ASW_Marine *pTarget = s_hOrderTarget.Get();
			if (pTarget && pTarget->GetHealth() > 0)
			{
				s_hOrderTarget = pTarget;
				// position this marine's arrow here
				if (pTarget->m_hOrderArrow.Get())
				{
					// work out yaw to my marine
					C_ASW_Marine *pMyMarine = pPlayer->GetMarine();
					float fYaw = 0;
					if (pMyMarine)
					{
						Vector diff = s_vecMarineOrderPos - pMyMarine->GetAbsOrigin();
						diff.z = 0;
						fYaw = UTIL_VecToYaw(diff);
					}
					pTarget->m_hOrderArrow->SetAbsOrigin(s_vecMarineOrderPos);
					pTarget->m_hOrderArrow->RemoveEffects(EF_NODRAW);
					pTarget->m_hOrderArrow->RefreshArrow();
					QAngle arrow_yaw(0, fYaw, 0);
					pTarget->m_hOrderArrow->SetAbsAngles(arrow_yaw);
				}
			}
		}
	}
}

void IN_HoldOrderUp()
{
	// if we don't have a marine to order yet, skip
	if (s_hOrderTarget.Get() == NULL || s_fMarineDownTime == 0)		
	{
		return;
	}		

	KeyUp(&in_holdorder, NULL);
	C_ASW_Player* pPlayer = C_ASW_Player::GetLocalASWPlayer();
	if ( pPlayer )	
	{
		// find how much the mouse has moved
		int cx, cy;
		GetVGUICursorPos(cx, cy);
		int dx = cx - s_iMarineOrderingStartX;
		int dy = cy - s_iMarineOrderingStartY;
		float dist_sqr = dx * dx + dy * dy;
		s_fMarineDownTime = 0;
		float fScreenScale = ScreenWidth() / 1024.0f;
		int iMinPixels = asw_mouse_order_dist.GetFloat() * fScreenScale;
		float fYaw = 0;
		if (dist_sqr > iMinPixels)
		{
			// order that marine to face this way
			Vector vecOrderDir(dx, dy, 0);
			fYaw = -UTIL_VecToYaw(vecOrderDir);
		}
		else if ( pPlayer->GetMarine() )
		{
			// otherwise order the marine to face away from current marine
			Vector diff = s_vecMarineOrderPos - pPlayer->GetMarine()->GetAbsOrigin();
			diff.z = 0;
			fYaw = UTIL_VecToYaw(diff);
		}
		char buffer[64];
		Q_snprintf(buffer, sizeof(buffer), "cl_marineface %d %f %d %d %d", ASWInput()->ASW_GetOrderingMarine(), fYaw,
			int(s_vecMarineOrderPos.x), int(s_vecMarineOrderPos.y), int(s_vecMarineOrderPos.z));

		engine->ClientCmd(buffer);
		s_hOrderTarget = NULL;
		ASWInput()->ASW_SetOrderingMarine(0);
	}
}

void UpdateOrderArrow()
{
	if (!(in_holdorder.GetPerUser().state & 1))
		return;
	C_ASW_Player* pPlayer = C_ASW_Player::GetLocalASWPlayer();
	if (!pPlayer || !pPlayer->GetMarine() || s_hOrderTarget.Get() == NULL || s_fMarineDownTime == 0)
		return;
	// find how much the mouse has moved
	int cx, cy;
	GetVGUICursorPos(cx, cy);
	int dx = cx - s_iMarineOrderingStartX;
	int dy = cy - s_iMarineOrderingStartY;
	float dist_sqr = dx * dx + dy * dy;

	float fScreenScale = ScreenWidth() / 1024.0f;
	int iMinPixels = asw_mouse_order_dist.GetFloat() * fScreenScale;
	float fYaw = 0;
	if (dist_sqr > iMinPixels)
	{
		// order that marine to face this way
		Vector vecOrderDir(dx, dy, 0);
		fYaw = -UTIL_VecToYaw(vecOrderDir);
	}
	else
	{
		// otherwise order the marine to face away from current marine
		Vector diff = s_vecMarineOrderPos - pPlayer->GetMarine()->GetAbsOrigin();
		diff.z = 0;
		fYaw = UTIL_VecToYaw(diff);
	}

	if (s_hOrderTarget->m_hOrderArrow.Get())
	{		
		QAngle arrow_yaw(0, fYaw, 0);
		s_hOrderTarget->m_hOrderArrow->SetAbsAngles(arrow_yaw);
		s_hOrderTarget->m_hOrderArrow->RefreshArrow();
	}
}

// order marine nearest the cursor to follow

void asw_OrderMarinesFollowf()
{	
	// if we order nearby marines, clear any specific marine ordering we're about to give
	if (s_hOrderTarget.Get())
	{
		s_hOrderTarget = NULL;
		ASWInput()->ASW_SetOrderingMarine(0);
	}
	// send follow order
	engine->ClientCmd("cl_orderfollow");
}
ConCommand OrderMarinesFollow( "asw_OrderMarinesFollow", asw_OrderMarinesFollowf, "Orders nearest marine to follow", 0);

void asw_OrderMarinesHoldf()
{	
	// if we order nearby marines, clear any specific marine ordering we're about to give
	if (s_hOrderTarget.Get())
	{
		s_hOrderTarget = NULL;
		ASWInput()->ASW_SetOrderingMarine(0);
	}
	// send follow order
	engine->ClientCmd("cl_orderhold");
}
ConCommand OrderMarinesHold( "asw_OrderMarinesHold", asw_OrderMarinesHoldf, "Orders nearby marines to hold position", 0);



/*
============
KeyEvent

Return 1 to allow engine to process the key, otherwise, act on it as needed
============
*/
int CASWInput::KeyEvent( int down, ButtonCode_t code, const char *pszCurrentBinding )
{
	// JOYPAD ADDED
	// asw - grab joypad presses here
	if ( code >= JOYSTICK_FIRST && code <= KEY_XSTICK2_UP && GetControllerFocus() )
	{
		if (down != 0)
		{
			if ( GetControllerFocus()->OnControllerButtonPressed( code ) )
			{
				return 0;
			}
		}
		else
		{
			if ( GetControllerFocus()->OnControllerButtonReleased( code ) )
			{
				return 0;
			}
		}
	}

	// notify ingame VGUI panels of mouse clicks
	if ( code == MOUSE_LEFT )
	{
		if ( g_IngamePanelManager.SendMouseClick( false, down ? true : false ) )
			return false;
	}
	else if ( code == MOUSE_RIGHT )
	{
		if ( g_IngamePanelManager.SendMouseClick( true, down ? true : false ) )
			return false;
	}

	// use key: if we have any info messages up, close them and leave as that's our keypress used
	if (down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+use" ) == 0 && CASW_VGUI_Info_Message::CloseInfoMessage())
		return false;

	return CInput::KeyEvent( down, code, pszCurrentBinding );
}

void CASWInput::ExtraMouseSample( float frametime, bool active )
{
	ASSERT_LOCAL_PLAYER_RESOLVABLE();
	int nSlot = GET_ACTIVE_SPLITSCREEN_SLOT();

	static CUserCmd dummy[ MAX_SPLITSCREEN_PLAYERS ];
	CUserCmd *cmd = &dummy[ nSlot ];

	cmd->Reset();


	QAngle viewangles;

	if ( active )
	{
		// Determine view angles
		AdjustAngles ( nSlot, frametime );


		// asw - is this needed?
		// Retreive view angles from engine ( could have been changed in AdjustAngles above )
		engine->GetViewAngles( viewangles );
		// Use new view angles if alive, otherwise user last angles we stored off.
		VectorCopy( viewangles, cmd->viewangles );
		VectorCopy( viewangles, GetPerUser().m_angPreviousViewAngles );
		
		// Determine sideways movement
		ComputeSideMove( nSlot, cmd );

		// Determine vertical movement
		ComputeUpwardMove( nSlot, cmd );

		// Determine forward movement
		ComputeForwardMove( nSlot, cmd );

		// Scale based on holding speed key or having too fast of a velocity based on client maximum
		//  speed.
		ScaleMovements( cmd );

		// Allow mice and other controllers to add their inputs
		ControllerMove( nSlot, frametime, cmd );
	}

	// Retreive view angles from engine ( could have been set in IN_AdjustAngles above )
	engine->GetViewAngles( viewangles );

	// Set button and flag bits, don't blow away state
	cmd->buttons = GetButtonBits( false );

	// Use new view angles if alive, otherwise user last angles we stored off.
	VectorCopy( viewangles, cmd->viewangles );
	VectorCopy( viewangles, GetPerUser().m_angPreviousViewAngles );

	// Let the move manager override anything it wants to.
	if ( GetClientMode()->CreateMove( frametime, cmd ) )
	{
		// Get current view angles after the client mode tweaks with it
		engine->SetViewAngles( cmd->viewangles );
		prediction->SetLocalViewAngles( cmd->viewangles );
	}
}

void CASWInput::CreateMove( int sequence_number, float input_sample_frametime, bool active )
{	
	ASSERT_LOCAL_PLAYER_RESOLVABLE();
	int nSlot = GET_ACTIVE_SPLITSCREEN_SLOT();

	CUserCmd *cmd = &GetPerUser(nSlot).m_pCommands[ sequence_number % MULTIPLAYER_BACKUP];
	CVerifiedUserCmd *pVerified = &GetPerUser(nSlot).m_pVerifiedCommands[ sequence_number % MULTIPLAYER_BACKUP];

	cmd->Reset();

	cmd->command_number = sequence_number;
	cmd->tick_count = gpGlobals->tickcount;

	QAngle viewangles;

	if ( active || sv_noclipduringpause.GetInt() )
	{
		if ( engine->GetActiveSplitScreenPlayerSlot() == in_forceuser.GetInt() )
		{
			// Determine view angles
			AdjustAngles ( nSlot, input_sample_frametime );


			// asw - is this needed?
			// Retreive view angles from engine ( could have been changed in AdjustAngles above )
			engine->GetViewAngles( viewangles );
			// Use new view angles if alive, otherwise user last angles we stored off.
			VectorCopy( viewangles, cmd->viewangles );
			VectorCopy( viewangles, GetPerUser( nSlot ).m_angPreviousViewAngles );

			// Determine sideways movement
			ComputeSideMove( nSlot, cmd );

			// Determine vertical movement
			ComputeUpwardMove( nSlot, cmd );

			// Determine forward movement
			ComputeForwardMove( nSlot, cmd );

			// Scale based on holding speed key or having too fast of a velocity based on client maximum
			//  speed.
			ScaleMovements( cmd );

			if ( CASW_VGUI_Info_Message::HasInfoMessageOpen() || Holdout_Resupply_Frame::HasResupplyFrameOpen() )
			{
				cmd->forwardmove = 0;
				cmd->sidemove = 0;
				cmd->upmove = 0;
			}
		}

		// Allow mice and other controllers to add their inputs
		ControllerMove( nSlot, input_sample_frametime, cmd );
	}

	// Retreive view angles from engine ( could have been set in IN_AdjustAngles above )
	engine->GetViewAngles( viewangles );

	cmd->impulse = in_impulse[ nSlot ];
	in_impulse[ nSlot ] = 0;

	// Latch and clear weapon selection
	if ( GetPerUser().m_hSelectedWeapon != NULL )
	{
		C_BaseCombatWeapon *weapon = GetPerUser().m_hSelectedWeapon;

		cmd->weaponselect = weapon->entindex();

		// Always clear weapon selection
		GetPerUser().m_hSelectedWeapon = NULL;
	}

	// store the currently selected marine in the weapon subtype
	C_ASW_Player* pPlayer = C_ASW_Player::GetLocalASWPlayer();
	C_ASW_Marine* pMarine = pPlayer->GetMarine();
	if ( ASWGameResource() && pPlayer && pMarine && pPlayer->GetMarine()->GetMarineResource() )
	{
		cmd->weaponsubtype = ASWGameResource()->GetMarineResourceIndex( pMarine->GetMarineResource() );

		// get light at the current marine
		//Vector pos = pPlayer->GetMarine()->GetAbsOrigin() + Vector(0, 0, 40);
		//Vector col = engine->GetLightForPoint( pos, true );		
		//cmd->light_level = (byte) clamp( ( 255.0f * ( col.x + col.y + col.z ) ) / 3.0f, 0.0f, 254.0f );
	}
	else
	{
		cmd->weaponsubtype = 0;
		//cmd->light_level = 255;
	}

	// Set button and flag bits
	cmd->buttons = GetButtonBits( true );

	// Using joystick?
	if ( in_joystick.GetInt() )
	{
		if ( cmd->forwardmove > 0 )
		{
			cmd->buttons |= IN_FORWARD;
		}
		else if ( cmd->forwardmove < 0 )
		{
			cmd->buttons |= IN_BACK;
		}
	}

	// asw - alter view angles for this move if it's one where we're firing off a ground grenade
	if ( asw_ground_secondary.GetBool() && cmd->buttons & IN_ATTACK2 )
	{
		ASW_AdjustViewAngleForGroundShooting(viewangles);
	}

	// Use new view angles if alive, otherwise user last angles we stored off.
	VectorCopy( viewangles, cmd->viewangles );
	VectorCopy( viewangles, GetPerUser().m_angPreviousViewAngles );

	// Let the move manager override anything it wants to.
	if ( GetClientMode()->CreateMove( input_sample_frametime, cmd ) )
	{
		// Get current view angles after the client mode tweaks with it
		engine->SetViewAngles( cmd->viewangles );
	}

	GetPerUser().m_flLastForwardMove = cmd->forwardmove;

	cmd->random_seed = MD5_PseudoRandom( sequence_number ) & 0x7fffffff;

	HLTVCamera()->CreateMove( cmd );

	if ( pPlayer )
	{
		cmd->crosshairtrace = ASWInput()->GetCrosshairTracePos();
	}
	else
	{
		cmd->crosshairtrace = vec3_origin;
	}
	cmd->crosshair_entity = GetHighlightEntity() ? GetHighlightEntity()->entindex() : 0;

	cmd->forced_action = pMarine ? pMarine->GetForcedActionRequest() : 0;
	cmd->sync_kill_ent = 0;

	C_ASW_Weapon *pWeapon = pMarine ? pMarine->GetActiveASWWeapon() : NULL;
	if ( pWeapon )
	{
		pWeapon->CheckSyncKill( cmd->forced_action, cmd->sync_kill_ent );
	}

	pVerified->m_cmd = *cmd;
	pVerified->m_crc = cmd->GetChecksum();
}

// asw

bool CASWInput::ASWWriteVehicleMessage( bf_write *buf )
{
	int startbit = buf->GetNumBitsWritten();

	if (!PlayerDriving())
		return false;

	C_ASW_Player* pPlayer = C_ASW_Player::GetLocalASWPlayer();
	if (!pPlayer || !pPlayer->GetMarine() || !pPlayer->GetMarine()->GetClientsideVehicle())
		return false;

	C_BaseAnimating *pAnimating = dynamic_cast<C_BaseAnimating*>(pPlayer->GetMarine()->GetClientsideVehicle()->GetEntity());
	if (!pAnimating)
		return false;

	// = static_cast<C_BaseAnimating*>(s_pCVehicle->GetVehicleEnt());
	buf->WriteBitVec3Coord(pAnimating->GetAbsOrigin());
	buf->WriteBitAngles(pAnimating->GetAbsAngles());
	//todo: velocity?

	// poseparams
	//Msg(" Client params: ");
	for (int i=0;i<12;i++)
	{
		buf->WriteBitFloat(pAnimating->GetPoseParameterRaw(i));
		//Msg("%f ", pAnimating->GetPoseParameter(i));
	}
	//Msg("\n");

	if ( buf->IsOverflowed() )
	{
		int endbit = buf->GetNumBitsWritten();

		Msg( "WARNING! ASW Vehicle packet buffer overflow, last cmd was %i bits long\n",
			endbit - startbit );

		return false;
	}

	return true;
}

int CASWInput::GetButtonBits( bool bResetState )
{
	int bits = CInput::GetButtonBits( bResetState );
	
	// if player is pressing their melee key, do a simple check for contact
	/*
	if ( bits & MELEE_BUTTON )
	{
		C_ASW_Marine *pMarine = C_ASW_Marine::GetLocalMarine();
		if ( pMarine && !pMarine->m_bMeleeMadeContact && pMarine->GetCurrentMeleeAttack() )
		{
			if ( !asw_melee_require_contact.GetBool() || pMarine->GetCurrentMeleeAttack()->CheckContact( pMarine ) )
			{
				bits |= IN_MELEE_CONTACT;
			}
		}
	}
	*/
	return bits;
}

static ConCommand startholdorder("+holdorder", IN_HoldOrderDown);
static ConCommand endholdorder("-holdorder", IN_HoldOrderUp);

void CASWInput::Init_All( void )
{
	CInput::Init_All();
	m_iOrderingMarine = 0;

	if ( IsX360() )
	{
		EngageControllerMode();
	}
}

// was used by joypad to stop turning the marine when firing
bool CASWInput::IsAttacking( void )
{
	return (( in_attack.GetPerUser().state & 1 ) || ( in_attack2.GetPerUser().state & 1 ));
}