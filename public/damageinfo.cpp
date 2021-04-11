#include "IDamageRules.h"

void CTakeDamageInfo::Init( CBaseEntity *pInflictor, CBaseEntity *pAttacker, CBaseEntity *pWeapon, const Vector &damageForce, const Vector &damagePosition, const Vector &reportedPosition, float flDamage, int bitsDamageType, int iCustomDamage )
{
	m_hInflictor = pInflictor;
	if ( pAttacker )
	{
		m_hAttacker = pAttacker;
	}
	else
	{
		m_hAttacker = pInflictor;
	}

	m_hWeapon = pWeapon;

	m_flDamage = flDamage;

	m_flBaseDamage = BASEDAMAGE_NOT_SPECIFIED;

	m_bitsDamageType = bitsDamageType;
	m_iDamageCustom = iCustomDamage;

	m_flMaxDamage = flDamage;
	m_vecDamageForce = damageForce;
	m_vecDamagePosition = damagePosition;
	m_vecReportedPosition = reportedPosition;
	m_iAmmoType = -1;
#if SOURCE_ENGINE == SE_TF2
	m_iDamagedOtherPlayers = 0;
	m_iPlayerPenetrationCount = 0;
	m_flDamageBonus = 0.f;
	m_bForceFriendlyFire = false;
	m_flDamageForForce = 0.f;
	m_eCritType = (CTakeDamageInfo::ECritType)0;
#elif SOURCE_ENGINE == SE_LEFT4DEAD2
	m_flRadius = 0.0f;
#endif
}

CTakeDamageInfo::CTakeDamageInfo()
{
	Init( NULL, NULL, NULL, vec3_origin, vec3_origin, vec3_origin, 0, 0, 0 );
}
