#if defined damagerules_included
	#endinput
#endif
#define damagerules_included

#if !defined GAME_TF2 && !defined GAME_L4D2
	#error please define either GAME_TF2 or GAME_L4D2
#endif

#include <sdkhooks>
#tryinclude <wpnhack>

#if defined GAME_TF2
#include <tf2_stocks>
#endif

#if defined GAME_TF2
enum ECritType
{
	CRIT_NONE = 0,
	CRIT_MINI,
	CRIT_FULL,
}
#endif

enum struct CTakeDamageInfo
{
	float m_vecDamageForce[3];
	float m_vecDamagePosition[3];
	float m_vecReportedPosition[3];	// Position players are told damage is coming from
	int m_hInflictor;
	int m_hAttacker;
	int m_hWeapon;
	float m_flDamage;
	float m_flMaxDamage;
	float m_flBaseDamage;			// The damage amount before skill leve adjustments are made. Used to get uniform damage forces.
	int m_bitsDamageType;
	int m_iDamageCustom;
	int m_iDamageStats;
	int m_iAmmoType;			// AmmoType of the weapon used to cause this damage, if any
#if defined GAME_TF2
	int m_iDamagedOtherPlayers;
	int m_iPlayerPenetrationCount;
	float m_flDamageBonus;		// Anything that increases damage (crit) - store the delta
	int m_hDamageBonusProvider;	// Who gave us the ability to do extra damage?
	bool m_bForceFriendlyFire;	// Ideally this would be a dmg type, but we can't add more
	float m_flDamageForForce;
	ECritType m_eCritType;
#elseif defined GAME_L4D2
	float m_flRadius;
#endif
}

#if defined GAME_TF2
enum
{
	kRageBuffFlag_None = 0x00,
	kRageBuffFlag_OnDamageDealt = 0x01,
	kRageBuffFlag_OnDamageReceived = 0x02,
	kRageBuffFlag_OnMedicHealingReceived = 0x04,
	kRageBuffFlag_OnBurnDamageDealt = 0x08,
	kRageBuffFlag_OnHeal = 0x10
};
#endif

#define CTakeDamageInfoVar any[]

typedef ondamage_func_t = function Action (int entity, CTakeDamageInfo info, int &result);
typedef killed_func_t = function Action (int entity, CTakeDamageInfo info);

#if defined GAME_TF2
enum struct DamageModifyExtras_t
{
	bool bIgniting;
	bool bSelfBlastDmg;
	bool bSendPreFeignDamage;
	bool bPlayDamageReductionSound;
}

#define DamageModifyExtras_tVar any[]

public DamageModifyExtras_t NULL_DAMAGE_MODIFY_EXTRA;

native void HandleRageGain(int client, int iRequiredBuffFlags, float flDamage, float fInverseRageGainScale);

native void PlayerOnDealtDamage(int client, int entity, const CTakeDamageInfoVar info);
native bool ApplyOnDamageModifyRules(CTakeDamageInfoVar info, int entity, bool bAllowDamage);
native float ApplyOnDamageAliveModifyRules(const CTakeDamageInfoVar info, int entity, DamageModifyExtras_tVar extras);
native void ApplyOnHitAttributes(int weapon, int victim, int attacker, const CTakeDamageInfoVar info);
#endif

native int EntityTakeDamage(int entity, const CTakeDamageInfoVar info);

native int EntityOnTakeDamage(int entity, const CTakeDamageInfoVar info);
native int EntityOnTakeDamageAlive(int entity, const CTakeDamageInfoVar info);

native void CombatCharacterEventKilled(int entity, const CTakeDamageInfoVar info);

native int CombatCharacterHullAttackRange(int entity, float flDist, const float mins[3], const float maxs[3], int iDamage, int iDmgType, float forceScale = 1.0, bool bDamageAnyNPC = false);
native int CombatCharacterHullAttackEndPoint(int entity, const float vStart[3], const float vEnd[3], const float mins[3], const float maxs[3], int iDamage, int iDmgType, float flForceScale = 1.0, bool bDamageAnyNPC = false);

native void HookEntityOnTakeDamage(int entity, ondamage_func_t func, bool post);
native void HookEntityOnTakeDamageAlive(int entity, ondamage_func_t func, bool post);
native void HookEntityKilled(int entity, killed_func_t func, bool post);

native void PushAllPlayersAway(const float vFromThisPoint[3], float flRange, float flForce, int nTeam, ArrayList PushedPlayers = null);

native void CalculateExplosiveDamageForce(CTakeDamageInfoVar info, const float vecForceDir[3], const float vecForceOrigin[3], float flScale);
native void CalculateBulletDamageForce(CTakeDamageInfoVar info, int iBulletType, const float vecForceDir[3], const float vecForceOrigin[3], float flScale);
native void CalculateMeleeDamageForce(CTakeDamageInfoVar info, const float vecForceDir[3], const float vecForceOrigin[3], float flScale);
native void GuessDamageForce(CTakeDamageInfoVar info, const float vecForceDir[3], const float vecForceOrigin[3], float flScale);

#if !defined REQUIRE_EXTENSIONS
public void __ext_damagerules_SetNTVOptional()
{
	
}
#endif

public Extension __ext_damagerules = 
{
	name = "damagerules", 
	file = "damagerules.ext", 
	
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif

#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};
