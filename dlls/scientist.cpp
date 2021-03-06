/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   This source code contains proprietary and confidential information of
*   Valve LLC and its suppliers.  Access to this code is restricted to
*   persons who have executed a written SDK license with Valve.  Any access,
*   use or distribution of this code by or to any unlicensed person is illegal.
*
****/
//=========================================================
// human scientist (passive lab worker)
//=========================================================

#include	"extdll.h"
#include	"util.h"
#include	"cbase.h"
#include	"monsters.h"
#include	"talkmonster.h"
#include	"schedule.h"
#include	"scripted.h"
#include	"animation.h"
#include	"soundent.h"
#include	"mod_features.h"

#define		NUM_SCIENTIST_HEADS		4 // four heads available for scientist model, used when randoming a scientist head

// used for body change when scientist uses the needle
#if FEATURE_OPFOR_SCIENTIST_BODIES
#define		NUM_SCIENTIST_BODIES		6
#else
#define		NUM_SCIENTIST_BODIES 4
#endif

#define SF_SCI_DONT_STOP_FOLLOWING SF_MONSTER_SPECIAL_FLAG
#define SF_SCI_SITTING_DONT_DROP SF_MONSTER_SPECIAL_FLAG // Don't drop to the floor. We can re-use the same value as sitting scientists can't follow

enum
{
	HEAD_GLASSES = 0,
	HEAD_EINSTEIN = 1,
	HEAD_LUTHER = 2,
	HEAD_SLICK = 3,
	HEAD_EINSTEIN_WITH_BOOK = 4,
	HEAD_SLICK_WITH_STICK = 5
};

enum
{
	SCHED_HIDE = LAST_TALKMONSTER_SCHEDULE + 1,
	SCHED_FEAR,
	SCHED_PANIC,
	SCHED_STARTLE,
	SCHED_TARGET_CHASE_SCARED,
	SCHED_TARGET_FACE_SCARED,
	SCHED_HEAL
};

enum
{
	TASK_SAY_HEAL = LAST_TALKMONSTER_TASK + 1,
	TASK_HEAL,
	TASK_SAY_FEAR,
	TASK_RUN_PATH_SCARED,
	TASK_SCREAM,
	TASK_RANDOM_SCREAM,
	TASK_MOVE_TO_TARGET_RANGE_SCARED,
	TASK_DRAW_NEEDLE,
	TASK_PUTAWAY_NEEDLE
};

//=========================================================
// Monster's Anim Events Go Here
//=========================================================
#define		SCIENTIST_AE_HEAL	( 1 )
#define		SCIENTIST_AE_NEEDLEON	( 2 )
#define		SCIENTIST_AE_NEEDLEOFF	( 3 )

//=======================================================
// Scientist
//=======================================================
class CScientist : public CTalkMonster
{
public:
	void Spawn( void );
	void Precache( void );

	void SetYawSpeed( void );
	int DefaultClassify( void );
	const char* DefaultDisplayName() { return "Scientist"; }
	void HandleAnimEvent( MonsterEvent_t *pEvent );
	void RunTask( Task_t *pTask );
	void StartTask( Task_t *pTask );
	int DefaultToleranceLevel() { return TOLERANCE_ZERO; }
	void SetActivity( Activity newActivity );
	Activity GetStoppedActivity( void );
	int DefaultISoundMask( void );
	void DeclineFollowing( CBaseEntity* pCaller );

	float CoverRadius( void ) { return 1200; }		// Need more room for cover because scientists want to get far away!
	BOOL DisregardEnemy( CBaseEntity *pEnemy ) { return !pEnemy->IsAlive() || ( gpGlobals->time - m_fearTime ) > 15; }

	virtual BOOL	CanHeal( void );
	void StartFollowingHealTarget(CBaseEntity* pTarget);
	bool ReadyToHeal();
	void Heal( void );
	void Scream( void );

	// Override these to set behavior
	Schedule_t *GetScheduleOfType( int Type );
	Schedule_t *GetSchedule( void );
	MONSTERSTATE GetIdealState( void );

	void DeathSound( void );
	void PainSound( void );

	void TalkInit( void );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	CUSTOM_SCHEDULES

	void ReportAIState(ALERT_TYPE level);

protected:
	void SciSpawnHelper(const char* modelName, float health, int headCount = NUM_SCIENTIST_HEADS);
	void PrecacheSounds();

	virtual const char* HealSentence() { return "SC_HEAL"; }
	virtual const char* ScreamSentence() { return "SC_SCREAM"; }
	virtual const char* FearSentence() { return "SC_FEAR"; }
	virtual const char* PlayerFearSentence() { return "SC_PLFEAR"; }

	float m_painTime;
	float m_healTime;
	float m_fearTime;
};

LINK_ENTITY_TO_CLASS( monster_scientist, CScientist )

TYPEDESCRIPTION	CScientist::m_SaveData[] =
{
	DEFINE_FIELD( CScientist, m_painTime, FIELD_TIME ),
	DEFINE_FIELD( CScientist, m_healTime, FIELD_TIME ),
	DEFINE_FIELD( CScientist, m_fearTime, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CScientist, CTalkMonster )

//=========================================================
// AI Schedules Specific to this monster
//=========================================================

Task_t tlFollowScared[] =
{
	{ TASK_SET_FAIL_SCHEDULE, (float)SCHED_TARGET_CHASE },// If you fail, follow normally
	{ TASK_MOVE_TO_TARGET_RANGE_SCARED, 128.0f },	// Move within 128 of target ent (client)
	//{ TASK_SET_SCHEDULE, (float)SCHED_TARGET_FACE_SCARED },
};

Schedule_t slFollowScared[] =
{
	{
		tlFollowScared,
		ARRAYSIZE( tlFollowScared ),
		bits_COND_NEW_ENEMY |
		bits_COND_HEAR_SOUND |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE,
		bits_SOUND_DANGER,
		"FollowScared"
	},
};

Task_t tlFaceTargetScared[] =
{
	{ TASK_FACE_TARGET, 0.0f },
	{ TASK_SET_ACTIVITY, (float)ACT_CROUCHIDLE },
	{ TASK_SET_SCHEDULE, (float)SCHED_TARGET_CHASE_SCARED },
};

Schedule_t slFaceTargetScared[] =
{
	{
		tlFaceTargetScared,
		ARRAYSIZE( tlFaceTargetScared ),
		bits_COND_HEAR_SOUND |
		bits_COND_NEW_ENEMY,
		bits_SOUND_DANGER,
		"FaceTargetScared"
	},
};

Task_t tlStopFollowing[] =
{
	{ TASK_CANT_FOLLOW, 0.0f },
};

Schedule_t slStopFollowing[] =
{
	{
		tlStopFollowing,
		ARRAYSIZE( tlStopFollowing ),
		0,
		0,
		"StopFollowing"
	},
};

Task_t tlHeal[] =
{
	{ TASK_CATCH_WITH_TARGET_RANGE, 50.0f },	// Move within 50 of target ent (client)
	{ TASK_FACE_IDEAL, 0.0f },
	{ TASK_SAY_HEAL, 0.0f },
	{ TASK_DRAW_NEEDLE, 0.0f },			// Whip out the needle
	{ TASK_SET_FAIL_SCHEDULE, (float)SCHED_HEAL },	// If you fail, catch up with that guy! (change this to put syringe away and then chase)
	{ TASK_HEAL, 0.0f },	// Put it in the target
	{ TASK_CLEAR_FAIL_SCHEDULE, 0.0f },
	{ TASK_PUTAWAY_NEEDLE, 0.0f },			// Put away the needle
};

Schedule_t slHeal[] =
{
	{
		tlHeal,
		ARRAYSIZE( tlHeal ),
		bits_COND_LIGHT_DAMAGE|
		bits_COND_HEAVY_DAMAGE|
		bits_COND_HEAR_SOUND|
		bits_COND_NEW_ENEMY,
		bits_SOUND_DANGER,
		"Heal"
	},
};

Task_t tlSciFaceTarget[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_FACE_TARGET, 0.0f },
	{ TASK_SET_ACTIVITY, (float)ACT_IDLE },
	{ TASK_SET_SCHEDULE, (float)SCHED_TARGET_CHASE },
};

Schedule_t slSciFaceTarget[] =
{
	{
		tlSciFaceTarget,
		ARRAYSIZE( tlSciFaceTarget ),
		bits_COND_CLIENT_PUSH |
		bits_COND_NEW_ENEMY |
		bits_COND_HEAR_SOUND,
		bits_SOUND_COMBAT |
		bits_SOUND_DANGER,
		"Sci FaceTarget"
	},
};

Task_t tlSciPanic[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_FACE_ENEMY, 0.0f },
	{ TASK_SCREAM, 0.0f },
	{ TASK_PLAY_SEQUENCE_FACE_ENEMY, (float)ACT_EXCITED },	// This is really fear-stricken excitement
	{ TASK_SET_ACTIVITY, (float)ACT_IDLE },
};

Schedule_t slSciPanic[] =
{
	{
		tlSciPanic,
		ARRAYSIZE( tlSciPanic ),
		bits_COND_HEAR_SOUND,
		bits_SOUND_DANGER,
		"SciPanic"
	},
};

Task_t tlIdleSciStand[] =
{
	{ TASK_STOP_MOVING, 0 },
	{ TASK_SET_ACTIVITY, (float)ACT_IDLE },
	{ TASK_WAIT, 2.0f }, // repick IDLESTAND every two seconds.
	{ TASK_TLK_HEADRESET, (float)0 }, // reset head position
};

Schedule_t slIdleSciStand[] =
{
	{
		tlIdleSciStand,
		ARRAYSIZE( tlIdleSciStand ),
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_HEAR_SOUND |
		bits_COND_SMELL |
		bits_COND_CLIENT_PUSH |
		bits_COND_PROVOKED,
		bits_SOUND_COMBAT |// sound flags
		//bits_SOUND_PLAYER |
		//bits_SOUND_WORLD |
		bits_SOUND_DANGER |
		bits_SOUND_MEAT |// scents
		bits_SOUND_CARCASS |
		bits_SOUND_GARBAGE,
		"IdleSciStand"

	},
};

Task_t tlScientistCover[] =
{
	{ TASK_SET_FAIL_SCHEDULE, (float)SCHED_PANIC },		// If you fail, just panic!
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_FIND_RUN_AWAY_FROM_ENEMY, 0.0f },
	{ TASK_RUN_PATH_SCARED, 0.0f },
	{ TASK_TURN_LEFT, 179.0f },
	{ TASK_SET_SCHEDULE, (float)SCHED_HIDE },
};

Schedule_t slScientistCover[] =
{
	{
		tlScientistCover,
		ARRAYSIZE( tlScientistCover ),
		bits_COND_NEW_ENEMY|
		bits_COND_HEAR_SOUND,
		bits_SOUND_DANGER,
		"ScientistCover"
	},
};

Task_t tlScientistHide[] =
{
	{ TASK_SET_FAIL_SCHEDULE, (float)SCHED_PANIC },		// If you fail, just panic!
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_PLAY_SEQUENCE, (float)ACT_CROUCH },
	{ TASK_SET_ACTIVITY, (float)ACT_CROUCHIDLE },	// FIXME: This looks lame
	{ TASK_WAIT_RANDOM, 10.0f },
};

Schedule_t slScientistHide[] =
{
	{
		tlScientistHide,
		ARRAYSIZE( tlScientistHide ),
		bits_COND_NEW_ENEMY |
		bits_COND_HEAR_SOUND |
		bits_COND_SEE_ENEMY |
		bits_COND_SEE_HATE |
		bits_COND_SEE_FEAR |
		bits_COND_SEE_DISLIKE,
		bits_SOUND_DANGER,
		"ScientistHide"
	},
};

Task_t tlScientistStartle[] =
{
	{ TASK_SET_FAIL_SCHEDULE, (float)SCHED_PANIC },		// If you fail, just panic!
	{ TASK_RANDOM_SCREAM, 0.3f },				// Scream 30% of the time
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_PLAY_SEQUENCE_FACE_ENEMY, (float)ACT_CROUCH },
	{ TASK_RANDOM_SCREAM, 0.1f },				// Scream again 10% of the time
	{ TASK_PLAY_SEQUENCE_FACE_ENEMY, (float)ACT_CROUCHIDLE },
	{ TASK_WAIT_RANDOM, 1.0f },
};

Schedule_t slScientistStartle[] =
{
	{
		tlScientistStartle,
		ARRAYSIZE( tlScientistStartle ),
		bits_COND_NEW_ENEMY |
		bits_COND_SEE_ENEMY |
		bits_COND_SEE_HATE |
		bits_COND_SEE_FEAR |
		bits_COND_SEE_DISLIKE|
		bits_COND_HEAR_SOUND,
		bits_SOUND_DANGER,
		"ScientistStartle"
	},
};

Task_t tlFear[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_FACE_ENEMY, 0.0f },
	{ TASK_SAY_FEAR, 0.0f },
	//{ TASK_PLAY_SEQUENCE, (float)ACT_FEAR_DISPLAY },
};

Schedule_t slFear[] =
{
	{
		tlFear,
		ARRAYSIZE( tlFear ),
		bits_COND_NEW_ENEMY,
		0,
		"Fear"
	},
};

Task_t	tlDisarmNeedle[] =
{
	{ TASK_PUTAWAY_NEEDLE,		(float)0	},			// Put away the needle
};

Schedule_t	slDisarmNeedle[] =
{
	{
		tlDisarmNeedle,
		ARRAYSIZE ( tlDisarmNeedle ),
		0,	// Don't interrupt or he'll end up running around with a needle all the time
		0,
		"DisarmNeedle"
	},
};

DEFINE_CUSTOM_SCHEDULES( CScientist )
{
	slSciFaceTarget,
	slFear,
	slScientistCover,
	slScientistHide,
	slScientistStartle,
	slHeal,
	slStopFollowing,
	slSciPanic,
	slFollowScared,
	slFaceTargetScared,
	slDisarmNeedle,
};

IMPLEMENT_CUSTOM_SCHEDULES( CScientist, CTalkMonster )

void CScientist::DeclineFollowing(CBaseEntity *pCaller )
{
	Talk( 10 );
	CTalkMonster::DeclineFollowing(pCaller);
}

void CScientist::Scream( void )
{
	if( FOkToSpeak(SPEAK_DISREGARD_ENEMY|SPEAK_DISREGARD_OTHER_SPEAKING) )
	{
		m_hTalkTarget = m_hEnemy;
		PlaySentence( ScreamSentence(), RANDOM_FLOAT( 3.0f, 6.0f ), VOL_NORM, ATTN_NORM );
	}
}

Activity CScientist::GetStoppedActivity( void )
{ 
	if( m_hEnemy != 0 ) 
		return ACT_EXCITED;
	return CTalkMonster::GetStoppedActivity();
}

void CScientist::StartTask( Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_SAY_HEAL:
		if (m_hTargetEnt == 0)
			TaskFail("no target ent");
		else if (m_hTargetEnt->pev->deadflag != DEAD_NO)
		{
			// The guy we wanted to heal is dying or just died. Probably a good place for some scared sentence?
			TaskFail("target ent is dead");
		}
		else
		{
			if (!InScriptedSentence())
			{
				m_hTalkTarget = m_hTargetEnt;
				PlaySentence( HealSentence(), 2, VOL_NORM, ATTN_IDLE );
			}
			TaskComplete();
		}
		break;
	case TASK_SCREAM:
		Scream();
		TaskComplete();
		break;
	case TASK_RANDOM_SCREAM:
		if( RANDOM_FLOAT( 0.0f, 1.0f ) < pTask->flData )
			Scream();
		TaskComplete();
		break;
	case TASK_SAY_FEAR:
		if( FOkToSpeak(SPEAK_DISREGARD_ENEMY) )
		{
			Talk( 2 );
			m_hTalkTarget = m_hEnemy;

			//The enemy can be null here. - Solokiller
			//Discovered while testing the barnacle grapple on headcrabs with scientists in view.
			if( m_hEnemy != 0 && m_hEnemy->IsPlayer() )
				PlaySentence( PlayerFearSentence(), 5, VOL_NORM, ATTN_NORM );
			else
				PlaySentence( FearSentence(), 5, VOL_NORM, ATTN_NORM );
		}
		TaskComplete();
		break;
	case TASK_HEAL:
		m_IdealActivity = ACT_MELEE_ATTACK1;
		break;
	case TASK_RUN_PATH_SCARED:
		m_movementActivity = ACT_RUN_SCARED;
		break;
	case TASK_MOVE_TO_TARGET_RANGE_SCARED:
		{
			if( m_hTargetEnt == 0 )
				TaskFail("no target ent");
			else
			{
				if( ( m_hTargetEnt->pev->origin - pev->origin ).Length() < 1.0f )
				{
					TaskComplete();
				}
				else
				{
					m_vecMoveGoal = m_hTargetEnt->pev->origin;
					if( !MoveToTarget( ACT_WALK_SCARED, 0.5 ) )
						TaskFail("can't build path to target");
				}
			}
		}
		break;
	case TASK_DRAW_NEEDLE:
		if (pev->body >= NUM_SCIENTIST_BODIES)
		{
			TaskComplete();
		}
		else
		{
			m_IdealActivity = ACT_ARM;
		}
		break;
	case TASK_PUTAWAY_NEEDLE:
		if (pev->body >= NUM_SCIENTIST_BODIES)
		{
			m_IdealActivity = ACT_DISARM;
		}
		else
		{
			TaskComplete();
		}
		break;
	default:
		CTalkMonster::StartTask( pTask );
		break;
	}
}

void CScientist::RunTask( Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_RUN_PATH_SCARED:
		if( MovementIsComplete() )
			TaskComplete();
		if( RANDOM_LONG( 0, 31 ) < 8 )
			Scream();
		break;
	case TASK_MOVE_TO_TARGET_RANGE_SCARED:
		{
			if( RANDOM_LONG( 0, 63 ) < 8 )
				Scream();

			if( m_hTargetEnt == 0 )
			{
				TaskFail("no target ent");
			}
			else if( m_hEnemy == 0 )
			{
				TaskFail("no enemy");
			}
			else
			{
				float distance;

				distance = ( m_vecMoveGoal - pev->origin ).Length2D();
				// Re-evaluate when you think your finished, or the target has moved too far
				if( ( distance < pTask->flData ) || ( m_vecMoveGoal - m_hTargetEnt->pev->origin ).Length() > pTask->flData * 0.5f )
				{
					m_vecMoveGoal = m_hTargetEnt->pev->origin;
					distance = ( m_vecMoveGoal - pev->origin ).Length2D();
					FRefreshRoute();
				}

				// Set the appropriate activity based on an overlapping range
				// overlap the range to prevent oscillation
				if( distance < pTask->flData )
				{
					TaskComplete();
					RouteClear();		// Stop moving
				}
				else if( distance < 190 && m_movementActivity != ACT_WALK_SCARED )
					m_movementActivity = ACT_WALK_SCARED;
				else if( distance >= 270 && m_movementActivity != ACT_RUN_SCARED )
					m_movementActivity = ACT_RUN_SCARED;
			}
		}
		break;
	case TASK_HEAL:
		if( m_fSequenceFinished )
		{
			TaskComplete();
		}
		else
		{
			if( TargetDistance() > 90 )
			{
				TaskFail("target ent is too far");
			}
			if (m_hTargetEnt != 0)
			{
				pev->ideal_yaw = UTIL_VecToYaw( m_hTargetEnt->pev->origin - pev->origin );
				ChangeYaw( pev->yaw_speed );
			}
		}
		break;
	case TASK_DRAW_NEEDLE:
	case TASK_PUTAWAY_NEEDLE:
		{
			CBaseEntity *pTarget = m_hTargetEnt;
			if( pTarget )
			{
				pev->ideal_yaw = UTIL_VecToYaw( pTarget->pev->origin - pev->origin );
				ChangeYaw( pev->yaw_speed );
			}
			if( m_fSequenceFinished )
				TaskComplete();
		}
	default:
		CTalkMonster::RunTask( pTask );
		break;
	}
}

//=========================================================
// Classify - indicates this monster's place in the 
// relationship table.
//=========================================================
int CScientist::DefaultClassify( void )
{
	return CLASS_HUMAN_PASSIVE;
}

//=========================================================
// SetYawSpeed - allows each sequence to have a different
// turn rate associated with it.
//=========================================================
void CScientist::SetYawSpeed( void )
{
	int ys;

	ys = 90;

	switch( m_Activity )
	{
	case ACT_IDLE:
		ys = 120;
		break;
	case ACT_WALK:
		ys = 180;
		break;
	case ACT_RUN:
		ys = 150;
		break;
	case ACT_TURN_LEFT:
	case ACT_TURN_RIGHT:
		ys = 120;
		break;
	default:
		break;
	}

	pev->yaw_speed = ys;
}

//=========================================================
// HandleAnimEvent - catches the monster-specific messages
// that occur when tagged animation frames are played.
//=========================================================
void CScientist::HandleAnimEvent( MonsterEvent_t *pEvent )
{
	switch( pEvent->event )
	{		
	case SCIENTIST_AE_HEAL:		// Heal my target (if within range)
		Heal();
		break;
	case SCIENTIST_AE_NEEDLEON:
		{
			int oldBody = pev->body;
			pev->body = ( oldBody % NUM_SCIENTIST_BODIES ) + NUM_SCIENTIST_BODIES * 1;
		}
		break;
	case SCIENTIST_AE_NEEDLEOFF:
		{
			int oldBody = pev->body;
			pev->body = ( oldBody % NUM_SCIENTIST_BODIES ) + NUM_SCIENTIST_BODIES * 0;
		}
		break;
	default:
		CTalkMonster::HandleAnimEvent( pEvent );
	}
}

//=========================================================
// Spawn
//=========================================================
void CScientist::SciSpawnHelper(const char* modelName, float health, int headCount)
{
	// We need to set it before precache so the right voice will be chosen
	if( pev->body == -1 )
	{
		// -1 chooses a random head
		pev->body = RANDOM_LONG( 0, headCount - 1 );// pick a head, any head
	}

	Precache();

	SetMyModel( modelName );
	SetMySize( DefaultMinHullSize(), DefaultMaxHullSize() );

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	SetMyBloodColor( BLOOD_COLOR_RED );
	SetMyHealth( health );
	pev->view_ofs = Vector( 0, 0, 50 );// position of the eyes relative to monster's origin.
	m_flFieldOfView = VIEW_FIELD_WIDE; // NOTE: we need a wide field of view so scientists will notice player and say hello
	m_MonsterState = MONSTERSTATE_NONE;

	//m_flDistTooFar = 256.0;

	m_afCapability = bits_CAP_HEAR | bits_CAP_TURN_HEAD | bits_CAP_OPEN_DOORS | bits_CAP_AUTO_DOORS | bits_CAP_USE;
}

void CScientist::Spawn()
{
	SciSpawnHelper("models/scientist.mdl", gSkillData.scientistHealth);

	// White hands
	pev->skin = 0;

	// Luther is black, make his hands black
	if( pev->body == HEAD_LUTHER )
		pev->skin = 1;

	TalkMonsterInit();
}

//=========================================================
// Precache - precaches all resources this monster needs
//=========================================================
void CScientist::Precache( void )
{
	PrecacheMyModel( "models/scientist.mdl" );
	PrecacheSounds();
	PRECACHE_SOUND( "items/medshot4.wav" );

	// every new scientist must call this, otherwise
	// when a level is loaded, nobody will talk (time is reset to 0)
	TalkInit();

	CTalkMonster::Precache();
}

void CScientist::PrecacheSounds()
{
	PRECACHE_SOUND( "scientist/sci_pain1.wav" );
	PRECACHE_SOUND( "scientist/sci_pain2.wav" );
	PRECACHE_SOUND( "scientist/sci_pain3.wav" );
	PRECACHE_SOUND( "scientist/sci_pain4.wav" );
	PRECACHE_SOUND( "scientist/sci_pain5.wav" );
}

// Init talk data
void CScientist::TalkInit()
{
	CTalkMonster::TalkInit();

	// scientists speach group names (group names are in sentences.txt)

	m_szGrp[TLK_ANSWER] = "SC_ANSWER";
	m_szGrp[TLK_QUESTION] = "SC_QUESTION";
	m_szGrp[TLK_IDLE] = "SC_IDLE";
	m_szGrp[TLK_STARE] = "SC_STARE";
	m_szGrp[TLK_USE] = "SC_OK";
	m_szGrp[TLK_UNUSE] = "SC_WAIT";
	m_szGrp[TLK_DECLINE] = "SC_POK";
	m_szGrp[TLK_STOP] = "SC_STOP";
	m_szGrp[TLK_NOSHOOT] = "SC_SCARED";
	m_szGrp[TLK_HELLO] = "SC_HELLO";

	m_szGrp[TLK_PLHURT1] = "!SC_CUREA";
	m_szGrp[TLK_PLHURT2] = "!SC_CUREB"; 
	m_szGrp[TLK_PLHURT3] = "!SC_CUREC";

	m_szGrp[TLK_PHELLO] = "SC_PHELLO";
	m_szGrp[TLK_PIDLE] = "SC_PIDLE";
	m_szGrp[TLK_PQUESTION] = "SC_PQUEST";
	m_szGrp[TLK_SMELL] = "SC_SMELL";

	m_szGrp[TLK_WOUND] = "SC_WOUND";
	m_szGrp[TLK_MORTAL] = "SC_MORTAL";

	m_szGrp[TLK_SHOT] = NULL;
	m_szGrp[TLK_MAD] = NULL;

	// get voice for head
	switch( pev->body % NUM_SCIENTIST_BODIES )
	{
	default:
	case HEAD_GLASSES:
		m_voicePitch = 105;
		break;	//glasses
	case HEAD_EINSTEIN:
	case HEAD_EINSTEIN_WITH_BOOK:
		m_voicePitch = 100;
		break;	//einstein
	case HEAD_LUTHER:
		m_voicePitch = 95;
		break;	//luther
	case HEAD_SLICK:
	case HEAD_SLICK_WITH_STICK:
		m_voicePitch = 100;
		break;	//slick
	}
}

//=========================================================
// ISoundMask - returns a bit mask indicating which types
// of sounds this monster regards. In the base class implementation,
// monsters care about all sounds, but no scents.
//=========================================================
int CScientist::DefaultISoundMask( void )
{
	return bits_SOUND_WORLD |
			bits_SOUND_COMBAT |
			bits_SOUND_CARCASS |
			bits_SOUND_MEAT |
			bits_SOUND_DANGER |
			bits_SOUND_PLAYER;
}

//=========================================================
// PainSound
//=========================================================
void CScientist::PainSound( void )
{
	if( gpGlobals->time < m_painTime )
		return;

	m_painTime = gpGlobals->time + RANDOM_FLOAT( 0.5f, 0.75f );

	switch( RANDOM_LONG( 0, 4 ) )
	{
	case 0:
		EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "scientist/sci_pain1.wav", 1, ATTN_NORM, 0, GetVoicePitch() );
		break;
	case 1:
		EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "scientist/sci_pain2.wav", 1, ATTN_NORM, 0, GetVoicePitch() );
		break;
	case 2:
		EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "scientist/sci_pain3.wav", 1, ATTN_NORM, 0, GetVoicePitch() );
		break;
	case 3:
		EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "scientist/sci_pain4.wav", 1, ATTN_NORM, 0, GetVoicePitch() );
		break;
	case 4:
		EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "scientist/sci_pain5.wav", 1, ATTN_NORM, 0, GetVoicePitch() );
		break;
	}
}

//=========================================================
// DeathSound 
//=========================================================
void CScientist::DeathSound( void )
{
	PainSound();
}

void CScientist::SetActivity( Activity newActivity )
{
	int iSequence;

	iSequence = LookupActivity( newActivity );

	// Set to the desired anim, or default anim if the desired is not present
	if( iSequence == ACTIVITY_NOT_AVAILABLE )
		newActivity = ACT_IDLE;
	CTalkMonster::SetActivity( newActivity );
}

Schedule_t *CScientist::GetScheduleOfType( int Type )
{
	switch( Type )
	{
	// Hook these to make a looping schedule
	case SCHED_TARGET_FACE:
		return slSciFaceTarget;
	case SCHED_TARGET_CHASE:
		if (FBitSet(pev->spawnflags, SF_SCI_DONT_STOP_FOLLOWING))
			return CTalkMonster::GetScheduleOfType(SCHED_FOLLOW);
		else
			return CTalkMonster::GetScheduleOfType(SCHED_FOLLOW_FALLIBLE);
	case SCHED_CANT_FOLLOW:
		return slStopFollowing;
	case SCHED_PANIC:
		return slSciPanic;
	case SCHED_TARGET_CHASE_SCARED:
		return slFollowScared;
	case SCHED_TARGET_FACE_SCARED:
		return slFaceTargetScared;
	case SCHED_HIDE:
		return slScientistHide;
	case SCHED_STARTLE:
		return slScientistStartle;
	case SCHED_FEAR:
		return slFear;
	case SCHED_HEAL:
		if (CanHeal())
			return slHeal;
		else
			return slDisarmNeedle;
	}

	return CTalkMonster::GetScheduleOfType( Type );
}

Schedule_t *CScientist::GetSchedule( void )
{
	// so we don't keep calling through the EHANDLE stuff
	CBaseEntity *pEnemy = m_hEnemy;

	CSound *pSound = NULL;
	if( HasConditions( bits_COND_HEAR_SOUND ) )
	{
		pSound = PBestSound();

		ASSERT( pSound != NULL );
		if( pSound && ( pSound->m_iType & bits_SOUND_DANGER ) )
		{
			Scream();
			return GetScheduleOfType( SCHED_TAKE_COVER_FROM_BEST_SOUND );
		}
	}

	const bool wantsToDisarm = (pev->body >= NUM_SCIENTIST_BODIES);

	switch( m_MonsterState )
	{
	case MONSTERSTATE_ALERT:
	case MONSTERSTATE_IDLE:
	case MONSTERSTATE_HUNT:
		if( pEnemy )
		{
			if( HasConditions( bits_COND_SEE_ENEMY ) )
				m_fearTime = gpGlobals->time;
			else if( DisregardEnemy( pEnemy ) )		// After 15 seconds of being hidden, return to alert
			{
				m_hEnemy = 0;
				pEnemy = 0;
			}
		}

		if( HasConditions( bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE ) )
		{
			// flinch if hurt
			return GetScheduleOfType( SCHED_SMALL_FLINCH );
		}

		// Cower when you hear something scary
		if( HasConditions( bits_COND_HEAR_SOUND ) )
		{
			if( pSound )
			{
				if( pSound->m_iType & ( bits_SOUND_DANGER | bits_SOUND_COMBAT ) )
				{
					if( gpGlobals->time - m_fearTime > 3 )	// Only cower every 3 seconds or so
					{
						m_fearTime = gpGlobals->time;		// Update last fear
						return GetScheduleOfType( SCHED_STARTLE );	// This will just duck for a second
					}
				}
			}
		}

		// Behavior for following the player
		if( m_hTargetEnt != 0 && FollowedPlayer() == m_hTargetEnt )
		{
			if( !FollowedPlayer()->IsAlive() )
			{
				// UNDONE: Comment about the recently dead player here?
				StopFollowing( FALSE, false );
				break;
			}

			int relationship = R_NO;

			// Nothing scary, just me and the player
			if( pEnemy != NULL )
				relationship = IRelationship( pEnemy );

			// UNDONE: Model fear properly, fix R_FR and add multiple levels of fear
			if( relationship == R_NO )
			{
				// If I'm already close enough to my target
				if( TargetDistance() <= 128 )
				{
					if( CanHeal() )	// Heal opportunistically
						return GetScheduleOfType( SCHED_HEAL );
					if( HasConditions( bits_COND_CLIENT_PUSH ) )	// Player wants me to move
						return GetScheduleOfType( SCHED_MOVE_AWAY_FOLLOW );
				}
				if (wantsToDisarm)
					return slDisarmNeedle;
				return GetScheduleOfType( SCHED_TARGET_FACE );	// Just face and follow.
			}
			else	// UNDONE: When afraid, scientist won't move out of your way.  Keep This?  If not, write move away scared
			{
				if( HasConditions( bits_COND_NEW_ENEMY ) ) // I just saw something new and scary, react
					return GetScheduleOfType( SCHED_FEAR );					// React to something scary
				return GetScheduleOfType( SCHED_TARGET_FACE_SCARED );	// face and follow, but I'm scared!
			}
		}
		// was called by other ally
		else if ( CanHeal() )
		{
			return GetScheduleOfType( SCHED_HEAL );
		}

		if( HasConditions( bits_COND_CLIENT_PUSH ) )	// Player wants me to move
			return GetScheduleOfType( SCHED_MOVE_AWAY );

		if (wantsToDisarm)
			return slDisarmNeedle;

		// try to say something about smells
		TrySmellTalk();
		break;
	case MONSTERSTATE_COMBAT:
		if( HasConditions( bits_COND_NEW_ENEMY ) )
			return slFear;					// Point and scream!
		if( HasConditions( bits_COND_SEE_ENEMY ) )
			return slScientistCover;		// Take Cover

		if( HasConditions( bits_COND_HEAR_SOUND ) )
			return GetScheduleOfType( SCHED_TAKE_COVER_FROM_BEST_SOUND );	// Cower and panic from the scary sound!

		return slScientistCover;			// Run & Cower
		break;
	default:
		break;
	}
	
	return CTalkMonster::GetSchedule();
}

MONSTERSTATE CScientist::GetIdealState( void )
{
	switch( m_MonsterState )
	{
	case MONSTERSTATE_ALERT:
	case MONSTERSTATE_IDLE:
	case MONSTERSTATE_HUNT:
		if( HasConditions( bits_COND_NEW_ENEMY ) )
		{
			if( IsFollowingPlayer() )
			{
				int relationship = IRelationship( m_hEnemy );
				if( relationship != R_FR || ( relationship < R_HT && !HasConditions( bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE ) ) )
				{
					// Don't go to combat if you're following the player
					m_IdealMonsterState = MONSTERSTATE_ALERT;
					return m_IdealMonsterState;
				}
				StopFollowing( TRUE );
			}
		}
		else if( HasConditions( bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE ) )
		{
			// Stop following if you take damage
			if( IsFollowingPlayer() )
				StopFollowing( TRUE );
		}
		break;
	case MONSTERSTATE_COMBAT:
		{
			CBaseEntity *pEnemy = m_hEnemy;
			if( pEnemy != NULL )
			{
				if( DisregardEnemy( pEnemy ) )		// After 15 seconds of being hidden, return to alert
				{
					// Strip enemy when going to alert
					m_IdealMonsterState = MONSTERSTATE_ALERT;
					m_hEnemy = 0;
					return m_IdealMonsterState;
				}

				// Follow if only scared a little
				if( m_hTargetEnt != 0 && FollowedPlayer() == m_hTargetEnt )
				{
					m_IdealMonsterState = MONSTERSTATE_ALERT;
					return m_IdealMonsterState;
				}

				if( HasConditions( bits_COND_SEE_ENEMY ) )
				{
					m_fearTime = gpGlobals->time;
					m_IdealMonsterState = MONSTERSTATE_COMBAT;
					return m_IdealMonsterState;
				}
			}
		}
		break;
	default:
		break;
	}

	return CTalkMonster::GetIdealState();
}

BOOL CScientist::CanHeal( void )
{ 
	if( ( m_healTime > gpGlobals->time ) || ( m_hTargetEnt == 0 ) || ( !m_hTargetEnt->IsAlive() ) ||
			( m_hTargetEnt->IsPlayer() ? m_hTargetEnt->pev->health > ( m_hTargetEnt->pev->max_health * 0.5f ) :
			  m_hTargetEnt->pev->health >= m_hTargetEnt->pev->max_health ) )
		return FALSE;

	return TRUE;
}

void CScientist::StartFollowingHealTarget(CBaseEntity *pTarget)
{
	StopScript();

	m_hTargetEnt = pTarget;
	ClearConditions( bits_COND_CLIENT_PUSH );
	ClearSchedule();
	ALERT(at_aiconsole, "Scientist started to follow injured %s\n", STRING(pTarget->pev->classname));
}

bool CScientist::ReadyToHeal()
{
	return AbleToFollow() && ( m_healTime <= gpGlobals->time ) && m_pSchedule != slHeal;
}

void CScientist::Heal( void )
{
	if( !CanHeal() )
		return;

	Vector target = m_hTargetEnt->pev->origin - pev->origin;
	if( target.Length() > 100.0f )
		return;

	m_hTargetEnt->TakeHealth(this, gSkillData.scientistHeal, DMG_GENERIC );
	EMIT_SOUND( ENT( pev ), CHAN_WEAPON, "items/medshot4.wav", 0.75, ATTN_NORM );

	// Don't heal again for 1 minute
	m_healTime = gpGlobals->time + gSkillData.scientistHealTime;
}

void CScientist::ReportAIState(ALERT_TYPE level)
{
	CTalkMonster::ReportAIState(level);
	if (m_healTime <= gpGlobals->time)
	{
		ALERT(level, "Can heal now. ");
	}
	else
	{
		ALERT(level, "Can heal in %3.1f seconds. ", (double)(m_healTime - gpGlobals->time));
	}
}

//=========================================================
// Dead Scientist PROP
//=========================================================
class CDeadScientist : public CDeadMonster
{
public:
	void Spawn( void );
	int	DefaultClassify ( void ) { return	CLASS_HUMAN_PASSIVE; }

	const char* getPos(int pos) const;
	static const char *m_szPoses[7];
};
const char *CDeadScientist::m_szPoses[] = { "lying_on_back", "lying_on_stomach", "dead_sitting", "dead_hang", "dead_table1", "dead_table2", "dead_table3" };

const char* CDeadScientist::getPos(int pos) const
{
	return m_szPoses[pos % ARRAYSIZE(m_szPoses)];
}

LINK_ENTITY_TO_CLASS( monster_scientist_dead, CDeadScientist )

//
// ********** DeadScientist SPAWN **********
//
void CDeadScientist :: Spawn( )
{
	SpawnHelper("models/scientist.mdl");

	if ( pev->body == -1 )
	{// -1 chooses a random head
		pev->body = RANDOM_LONG(0, NUM_SCIENTIST_HEADS-1);// pick a head, any head
	}
	// Luther is black, make his hands black
	if ( pev->body == HEAD_LUTHER )
		pev->skin = 1;
	else
		pev->skin = 0;

	//	pev->skin += 2; // use bloody skin -- UNDONE: Turn this back on when we have a bloody skin again!
	MonsterInitDead();
}

//=========================================================
// Sitting Scientist PROP
//=========================================================
class CSittingScientist : public CScientist // kdb: changed from public CBaseMonster so he can speak
{
public:
	void Spawn( void );
	void Precache( void );

	void EXPORT SittingThink( void );
	int DefaultClassify( void );
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	virtual bool SetAnswerQuestion( CTalkMonster *pSpeaker );

	virtual int DefaultSizeForGrapple() { return GRAPPLE_FIXED; }
	Vector DefaultMinHullSize() { return Vector(-14.0f, -14.0f, 0.0f); }
	Vector DefaultMaxHullSize() { return Vector(14.0f, 14.0f, 36.0f); }

	int FIdleSpeak( void );
	int m_baseSequence;	
	int m_headTurn;
	float m_flResponseDelay;

protected:
	void SciSpawnHelper(const char* modelName);
};

LINK_ENTITY_TO_CLASS( monster_sitting_scientist, CSittingScientist )

TYPEDESCRIPTION	CSittingScientist::m_SaveData[] =
{
	// Don't need to save/restore m_baseSequence (recalced)
	DEFINE_FIELD( CSittingScientist, m_headTurn, FIELD_INTEGER ),
	DEFINE_FIELD( CSittingScientist, m_flResponseDelay, FIELD_FLOAT ),
};

IMPLEMENT_SAVERESTORE( CSittingScientist, CScientist )

// animation sequence aliases 
typedef enum
{
SITTING_ANIM_sitlookleft,
SITTING_ANIM_sitlookright,
SITTING_ANIM_sitscared,
SITTING_ANIM_sitting2,
SITTING_ANIM_sitting3
} SITTING_ANIM;

//
// ********** Scientist SPAWN **********
//
void CSittingScientist::SciSpawnHelper(const char* modelName)
{
	PrecacheMyModel( modelName );
	SetMyModel( modelName );
	Precache();
	InitBoneControllers();

	SetMySize( DefaultMinHullSize(), DefaultMaxHullSize() );

	pev->solid = SOLID_SLIDEBOX;
	if (FBitSet(pev->spawnflags, SF_SCI_SITTING_DONT_DROP))
		pev->movetype = MOVETYPE_FLY;
	else
		pev->movetype = MOVETYPE_STEP;
	pev->effects = 0;
	SetMyHealth( 50 );
	
	SetMyBloodColor( BLOOD_COLOR_RED );
	m_flFieldOfView = VIEW_FIELD_WIDE; // indicates the width of this monster's forward view cone ( as a dotproduct result )

	m_afCapability= bits_CAP_HEAR | bits_CAP_TURN_HEAD;

	SetBits( pev->spawnflags, SF_MONSTER_PREDISASTER ); // predisaster only!

	if( pev->body == -1 )
	{
		// -1 chooses a random head
		pev->body = RANDOM_LONG( 0, NUM_SCIENTIST_HEADS - 1 );// pick a head, any head
	}

	// Luther is black, make his hands black
	if( pev->body == HEAD_LUTHER )
		pev->skin = 1;

	m_baseSequence = LookupSequence( "sitlookleft" );
	pev->sequence = m_baseSequence + RANDOM_LONG( 0, 4 );
	ResetSequenceInfo();

	SetThink( &CSittingScientist::SittingThink );
	pev->nextthink = gpGlobals->time + 0.1f;

	if (!FBitSet(pev->spawnflags, SF_SCI_SITTING_DONT_DROP))
		DROP_TO_FLOOR( ENT( pev ) );
}

void CSittingScientist::Spawn( )
{
	SciSpawnHelper("models/scientist.mdl");
	// Luther is black, make his hands black
	if ( pev->body == HEAD_LUTHER )
		pev->skin = 1;
}

void CSittingScientist::Precache( void )
{
	m_baseSequence = LookupSequence( "sitlookleft" );
	TalkInit();
}

//=========================================================
// ID as a passive human
//=========================================================
int CSittingScientist::DefaultClassify( void )
{
	return CLASS_HUMAN_PASSIVE;
}

//=========================================================
// sit, do stuff
//=========================================================
void CSittingScientist::SittingThink( void )
{
	CBaseEntity *pent;

	StudioFrameAdvance();

	// try to greet player
	if( FIdleHello() )
	{
		pent = FindNearestFriend( TRUE );
		if( pent )
		{
			float yaw = VecToYaw( pent->pev->origin - pev->origin ) - pev->angles.y;

			if( yaw > 180 )
				yaw -= 360;
			if( yaw < -180 )
				yaw += 360;

			if( yaw > 0 )
				pev->sequence = m_baseSequence + SITTING_ANIM_sitlookleft;
			else
				pev->sequence = m_baseSequence + SITTING_ANIM_sitlookright;

		ResetSequenceInfo();
		pev->frame = 0;
		SetBoneController( 0, 0 );
		}
	}
	else if( m_fSequenceFinished )
	{
		int i = RANDOM_LONG( 0, 99 );
		m_headTurn = 0;
		
		if( m_flResponseDelay && gpGlobals->time > m_flResponseDelay )
		{
			// respond to question
			IdleRespond();
			pev->sequence = m_baseSequence + SITTING_ANIM_sitscared;
			m_flResponseDelay = 0;
		}
		else if( i < 30 )
		{
			pev->sequence = m_baseSequence + SITTING_ANIM_sitting3;	

			// turn towards player or nearest friend and speak

			if( !FBitSet( m_bitsSaid, bit_saidHelloPlayer ) )
				pent = FindNearestFriend( TRUE );
			else
				pent = FindNearestFriend( FALSE );

			if( !FIdleSpeak() || !pent )
			{
				m_headTurn = RANDOM_LONG( 0, 8 ) * 10 - 40;
				pev->sequence = m_baseSequence + SITTING_ANIM_sitting3;
			}
			else
			{
				// only turn head if we spoke
				float yaw = VecToYaw( pent->pev->origin - pev->origin ) - pev->angles.y;

				if( yaw > 180 )
					yaw -= 360;
				if( yaw < -180 )
					yaw += 360;

				if( yaw > 0 )
					pev->sequence = m_baseSequence + SITTING_ANIM_sitlookleft;
				else
					pev->sequence = m_baseSequence + SITTING_ANIM_sitlookright;

				//ALERT( at_console, "sitting speak\n" );
			}
		}
		else if( i < 60 )
		{
			pev->sequence = m_baseSequence + SITTING_ANIM_sitting3;	
			m_headTurn = RANDOM_LONG( 0, 8 ) * 10 - 40;
			if( RANDOM_LONG( 0, 99 ) < 5 )
			{
				//ALERT( at_console, "sitting speak2\n" );
				FIdleSpeak();
			}
		}
		else if( i < 80 )
		{
			pev->sequence = m_baseSequence + SITTING_ANIM_sitting2;
		}
		else if( i < 100 )
		{
			pev->sequence = m_baseSequence + SITTING_ANIM_sitscared;
		}

		ResetSequenceInfo( );
		pev->frame = 0;
		SetBoneController( 0, m_headTurn );
	}
	pev->nextthink = gpGlobals->time + 0.1f;
}

// prepare sitting scientist to answer a question
bool CSittingScientist::SetAnswerQuestion( CTalkMonster *pSpeaker )
{
	m_flResponseDelay = gpGlobals->time + RANDOM_FLOAT( 3.0f, 4.0f );
	m_hTalkTarget = (CBaseMonster *)pSpeaker;
	return true;
}

//=========================================================
// FIdleSpeak
// ask question of nearby friend, or make statement
//=========================================================
int CSittingScientist::FIdleSpeak( void )
{ 
	// try to start a conversation, or make statement
	if( !FOkToSpeak() )
		return FALSE;

	// if there is a friend nearby to speak to, play sentence, set friend's response time, return

	// try to talk to any standing or sitting scientists nearby
	CBaseEntity *pFriend = FindNearestFriend( FALSE );

	if( pFriend && RANDOM_LONG( 0, 1 ) )
	{
		PlaySentence( m_szGrp[TLK_PQUESTION], RANDOM_FLOAT( 4.8, 5.2 ), VOL_NORM, ATTN_IDLE);

		CBaseMonster* pMonster = pFriend->MyMonsterPointer();
		if (pMonster)
		{
			CTalkMonster *pTalkMonster = pMonster->MyTalkMonsterPointer();
			if (pTalkMonster && pTalkMonster->SetAnswerQuestion( this ))
				pTalkMonster->m_flStopTalkTime = m_flStopTalkTime;
		}
		IdleHeadTurn( pFriend->pev->origin );
		return TRUE;
	}

	// otherwise, play an idle statement
	if( RANDOM_LONG( 0, 1 ) )
	{
		PlaySentence( m_szGrp[TLK_PIDLE], RANDOM_FLOAT( 4.8, 5.2 ), VOL_NORM, ATTN_IDLE);
		return TRUE;
	}

	// never spoke
	CTalkMonster::g_talkWaitTime = 0;
	return FALSE;
}

#if FEATURE_CLEANSUIT_SCIENTIST
class CCleansuitScientist : public CScientist
{
public:
	void Spawn();
	void Precache();
	const char* DefaultDisplayName() { return "Cleansuit Scientist"; }
	BOOL CanHeal();
	bool ReadyToHeal() {return false;}
	void ReportAIState(ALERT_TYPE level);
};

LINK_ENTITY_TO_CLASS( monster_cleansuit_scientist, CCleansuitScientist )

void CCleansuitScientist::Spawn()
{
	SciSpawnHelper("models/cleansuit_scientist.mdl", gSkillData.cleansuitScientistHealth);
	TalkMonsterInit();
}

void CCleansuitScientist::Precache()
{
	PrecacheMyModel("models/cleansuit_scientist.mdl");
	PrecacheSounds();
	TalkInit();
	CTalkMonster::Precache();
}

BOOL CCleansuitScientist::CanHeal()
{
	return FALSE;
}

void CCleansuitScientist::ReportAIState(ALERT_TYPE level)
{
	CTalkMonster::ReportAIState(level);
}

class CDeadCleansuitScientist : public CDeadMonster
{
public:
	void Spawn( void );
	int	DefaultClassify ( void ) { return	CLASS_HUMAN_PASSIVE; }

	const char* getPos(int pos) const;
	static const char *m_szPoses[9];
};
const char *CDeadCleansuitScientist::m_szPoses[] = { "lying_on_back", "lying_on_stomach", "dead_sitting", "dead_hang", "dead_table1", "dead_table2", "dead_table3", "scientist_deadpose1", "dead_against_wall" };

const char* CDeadCleansuitScientist::getPos(int pos) const
{
	return m_szPoses[pos % ARRAYSIZE(m_szPoses)];
}

LINK_ENTITY_TO_CLASS( monster_cleansuit_scientist_dead, CDeadCleansuitScientist )

void CDeadCleansuitScientist :: Spawn( )
{
	SpawnHelper("models/cleansuit_scientist.mdl");
	if ( pev->body == -1 ) {
		pev->body = RANDOM_LONG(0, NUM_SCIENTIST_HEADS-1);
	}
	MonsterInitDead();
}

class CSittingCleansuitScientist : public CSittingScientist
{
public:
	void Spawn();
};

void CSittingCleansuitScientist::Spawn()
{
	SciSpawnHelper("models/cleansuit_scientist.mdl");
}

LINK_ENTITY_TO_CLASS( monster_sitting_cleansuit_scientist, CSittingCleansuitScientist )
#endif

#if FEATURE_ROSENBERG

#define FEATURE_ROSENBERG_DECAY 0

class CRosenberg : public CScientist
{
public:
	void Spawn();
	void Precache();
	const char* DefaultDisplayName() { return "Dr. Rosenberg"; }
	void TalkInit();
	int DefaultToleranceLevel() { return TOLERANCE_ABSOLUTE; }
	const char* HealSentence() { return "RO_HEAL"; }
	const char* ScreamSentence() { return "RO_SCREAM"; }
	const char* FearSentence() { return "RO_FEAR"; }
	const char* PlayerFearSentence() { return "RO_PLFEAR"; }
	void PainSound();

#if FEATURE_ROSENBERG_DECAY
	BOOL CanHeal() { return false; }
	bool ReadyToHeal() {return false; }
#endif
};

LINK_ENTITY_TO_CLASS( monster_rosenberg, CRosenberg )

void CRosenberg::Spawn()
{
#if FEATURE_ROSENBERG_DECAY
	SciSpawnHelper("models/scientist_rosenberg.mdl", gSkillData.scientistHealth * 2);
#else
	SciSpawnHelper("models/scientist.mdl", gSkillData.scientistHealth * 2);
	pev->body = 3;
#endif
	TalkMonsterInit();
}

void CRosenberg::Precache()
{
#if FEATURE_ROSENBERG_DECAY
	PrecacheMyModel("models/scientist_rosenberg.mdl");
#else
	PrecacheMyModel("models/scientist.mdl");
#endif
	PRECACHE_SOUND( "rosenberg/ro_pain0.wav" );
	PRECACHE_SOUND( "rosenberg/ro_pain1.wav" );
	PRECACHE_SOUND( "rosenberg/ro_pain2.wav" );
	PRECACHE_SOUND( "rosenberg/ro_pain3.wav" );
	PRECACHE_SOUND( "rosenberg/ro_pain4.wav" );
	PRECACHE_SOUND( "rosenberg/ro_pain5.wav" );
	PRECACHE_SOUND( "rosenberg/ro_pain6.wav" );
	PRECACHE_SOUND( "rosenberg/ro_pain7.wav" );
	PRECACHE_SOUND( "rosenberg/ro_pain8.wav" );

	PRECACHE_SOUND( "items/medshot4.wav" );

	TalkInit();
	CTalkMonster::Precache();
}

void CRosenberg::TalkInit()
{
	CTalkMonster::TalkInit();

	m_szGrp[TLK_ANSWER] = "RO_ANSWER";
	m_szGrp[TLK_QUESTION] = "RO_QUESTION";
	m_szGrp[TLK_IDLE] = "RO_IDLE";
	m_szGrp[TLK_STARE] = "RO_STARE";
	m_szGrp[TLK_USE] = "RO_OK";
	m_szGrp[TLK_UNUSE] = "RO_WAIT";
	m_szGrp[TLK_DECLINE] = "RO_POK";
	m_szGrp[TLK_STOP] = "RO_STOP";
	m_szGrp[TLK_NOSHOOT] = "RO_SCARED";
	m_szGrp[TLK_HELLO] = "RO_HELLO";

	m_szGrp[TLK_PLHURT1] = "!RO_CUREA";
	m_szGrp[TLK_PLHURT2] = "!RO_CUREB";
	m_szGrp[TLK_PLHURT3] = "!RO_CUREC";

	m_szGrp[TLK_PHELLO] = "RO_PHELLO";
	m_szGrp[TLK_PIDLE] = "RO_PIDLE";
	m_szGrp[TLK_PQUESTION] = "RO_PQUEST";
	m_szGrp[TLK_SMELL] = "RO_SMELL";

	m_szGrp[TLK_WOUND] = "RO_WOUND";
	m_szGrp[TLK_MORTAL] = "RO_MORTAL";

	m_szGrp[TLK_SHOT] = NULL;
	m_szGrp[TLK_MAD] = NULL;

	m_voicePitch = 100;
}

void CRosenberg::PainSound()
{
	if( gpGlobals->time < m_painTime )
		return;

	m_painTime = gpGlobals->time + RANDOM_FLOAT( 0.5, 0.75 );

	const char* painSound = NULL;
	switch( RANDOM_LONG( 0, 8 ) )
	{
	case 0:
		painSound ="rosenberg/ro_pain0.wav";
		break;
	case 1:
		painSound ="rosenberg/ro_pain1.wav";
		break;
	case 2:
		painSound ="rosenberg/ro_pain2.wav";
		break;
	case 3:
		painSound ="rosenberg/ro_pain3.wav";
		break;
	case 4:
		painSound ="rosenberg/ro_pain4.wav";
		break;
	case 5:
		painSound ="rosenberg/ro_pain5.wav";
		break;
	case 6:
		painSound ="rosenberg/ro_pain6.wav";
		break;
	case 7:
		painSound ="rosenberg/ro_pain7.wav";
		break;
	case 8:
		painSound ="rosenberg/ro_pain8.wav";
		break;
	}
	EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, painSound, 1, ATTN_NORM, 0, GetVoicePitch() );
}

#endif

#if FEATURE_GUS
class CGus : public CScientist
{
public:
	void Spawn();
	void Precache();
	const char* DefaultDisplayName() { return "Construction Worker"; }
	BOOL CanHeal();
	bool ReadyToHeal() {return false;}
	void ReportAIState(ALERT_TYPE level);
};

LINK_ENTITY_TO_CLASS( monster_gus, CGus )

void CGus::Spawn()
{
	SciSpawnHelper("models/gus.mdl", gSkillData.scientistHealth, 2);
	TalkMonsterInit();
}

void CGus::Precache()
{
	PrecacheMyModel("models/gus.mdl");
	PrecacheSounds();
	TalkInit();
	if (pev->body)
		m_voicePitch = 95;
	else
		m_voicePitch = 100;
	CTalkMonster::Precache();
}

BOOL CGus::CanHeal()
{
	return FALSE;
}

void CGus::ReportAIState(ALERT_TYPE level)
{
	CTalkMonster::ReportAIState(level);
}

//=========================================================
// Dead Worker PROP
//=========================================================
class CDeadWorker : public CDeadMonster
{
public:
	void Spawn( void );
	int	DefaultClassify ( void ) { return	CLASS_HUMAN_PASSIVE; }

	const char* getPos(int pos) const;
	static const char *m_szPoses[6];
};
const char *CDeadWorker::m_szPoses[] = { "lying_on_back", "lying_on_stomach", "dead_sitting", "dead_table1", "dead_table2", "dead_table3" };

const char* CDeadWorker::getPos(int pos) const
{
	return m_szPoses[pos % ARRAYSIZE(m_szPoses)];
}

LINK_ENTITY_TO_CLASS( monster_worker_dead, CDeadWorker )

void CDeadWorker :: Spawn( )
{
	SpawnHelper("models/worker.mdl");
	MonsterInitDead();
}

class CDeadGus : public CDeadWorker
{
	void Spawn( void );
};

LINK_ENTITY_TO_CLASS( monster_gus_dead, CDeadGus )

void CDeadGus :: Spawn( )
{
	SpawnHelper("models/gus.mdl");
	if (pev->body == -1)
	{
		pev->body = RANDOM_LONG(0,1);
	}
	MonsterInitDead();
}
#endif

#if FEATURE_KELLER
class CKeller : public CScientist
{
public:
	void Spawn();
	void Precache();
	const char* DefaultDisplayName() { return "Richard Keller"; }
	void TalkInit();
	int DefaultToleranceLevel() { return TOLERANCE_ABSOLUTE; }
	const char* ScreamSentence() { return "DK_SCREAM"; }
	const char* FearSentence() { return "DK_FEAR"; }
	const char* PlayerFearSentence() { return "DK_PLFEAR"; }
	void PainSound();

	BOOL CanHeal() { return false; }
	bool ReadyToHeal() {return false; }
};

LINK_ENTITY_TO_CLASS( monster_wheelchair, CKeller )

void CKeller::Spawn()
{
	SciSpawnHelper("models/wheelchair_sci.mdl", gSkillData.scientistHealth * 2);
	TalkMonsterInit();
}

void CKeller::Precache()
{
	PrecacheMyModel("models/wheelchair_sci.mdl");
	PRECACHE_SOUND( "keller/dk_pain1.wav" );
	PRECACHE_SOUND( "keller/dk_pain2.wav" );
	PRECACHE_SOUND( "keller/dk_pain3.wav" );
	PRECACHE_SOUND( "keller/dk_pain4.wav" );
	PRECACHE_SOUND( "keller/dk_pain5.wav" );
	PRECACHE_SOUND( "keller/dk_pain6.wav" );
	PRECACHE_SOUND( "keller/dk_pain7.wav" );

	PRECACHE_SOUND( "wheelchair/wheelchair_jog.wav" );
	PRECACHE_SOUND( "wheelchair/wheelchair_run.wav" );
	PRECACHE_SOUND( "wheelchair/wheelchair_walk.wav" );

	TalkInit();
	CTalkMonster::Precache();
}

void CKeller::TalkInit()
{
	CTalkMonster::TalkInit();

	m_szGrp[TLK_ANSWER] = NULL;
	m_szGrp[TLK_QUESTION] = NULL;
	m_szGrp[TLK_IDLE] = "DK_IDLE";
	m_szGrp[TLK_STARE] = "DK_STARE";
	m_szGrp[TLK_USE] = "DK_OK";
	m_szGrp[TLK_UNUSE] = "DK_WAIT";
	m_szGrp[TLK_DECLINE] = "DK_POK";
	m_szGrp[TLK_STOP] = "DK_STOP";
	m_szGrp[TLK_NOSHOOT] = "DK_SCARED";
	m_szGrp[TLK_HELLO] = "DK_HELLO";

	m_szGrp[TLK_PLHURT1] = NULL;
	m_szGrp[TLK_PLHURT2] = NULL;
	m_szGrp[TLK_PLHURT3] = NULL;

	m_szGrp[TLK_PHELLO] = NULL;
	m_szGrp[TLK_PIDLE] = NULL;
	m_szGrp[TLK_PQUESTION] = NULL;
	m_szGrp[TLK_SMELL] = NULL;

	m_szGrp[TLK_WOUND] = NULL;
	m_szGrp[TLK_MORTAL] = NULL;

	m_szGrp[TLK_SHOT] = NULL;
	m_szGrp[TLK_MAD] = NULL;

	m_voicePitch = 100;
}

void CKeller::PainSound()
{
	if( gpGlobals->time < m_painTime )
		return;

	m_painTime = gpGlobals->time + RANDOM_FLOAT( 0.5, 0.75 );

	const char* painSound = NULL;
	switch( RANDOM_LONG( 1, 7 ) )
	{
	case 1:
		painSound ="keller/dk_pain1.wav";
		break;
	case 2:
		painSound ="keller/dk_pain2.wav";
		break;
	case 3:
		painSound ="keller/dk_pain3.wav";
		break;
	case 4:
		painSound ="keller/dk_pain4.wav";
		break;
	case 5:
		painSound ="keller/dk_pain5.wav";
		break;
	case 6:
		painSound ="keller/dk_pain6.wav";
		break;
	case 7:
		painSound ="keller/dk_pain7.wav";
		break;
	}
	EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, painSound, 1, ATTN_NORM, 0, GetVoicePitch() );
}
#endif
