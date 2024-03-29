#if defined damagerules_included
	#endinput
#endif
#define damagerules_included

#if !defined GAME_TF2 && !defined GAME_L4D2
	#error please define either GAME_TF2 or GAME_L4D2
#endif

#include <sdkhooks>

#define DMG_CRITICAL DMG_ACID
#define DMG_IGNITE DMG_PLASMA

#if defined REQUIRE_EXTENSIONS
	#define __DMGRUL_EXT_WAS_REQUIRED
#endif

#undef REQUIRE_EXTENSIONS
#tryinclude <wpnhack>
#tryinclude <animhelpers>

#if defined __DMGRUL_EXT_WAS_REQUIRED
	#define REQUIRE_EXTENSIONS
#endif

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

#define BASEDAMAGE_NOT_SPECIFIED 340282346638528859811704183484516925440.000000

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

	void Init(int inflictor, int attacker, float flDamage, int bitsDamageType, int iCustomDamage = 0)
	{
		this.m_hWeapon = -1;
		this.m_hInflictor = inflictor;
		if(attacker != -1) {
			this.m_hAttacker = attacker;
		} else {
			this.m_hAttacker = inflictor;
		}
		this.m_bitsDamageType = bitsDamageType;
		this.m_iDamageCustom = iCustomDamage;
		this.m_flDamage = flDamage;
		this.m_flBaseDamage = BASEDAMAGE_NOT_SPECIFIED;
		this.m_iAmmoType = -1;
		this.m_eCritType = CRIT_NONE;
	}
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

typeset ondamage_func_t
{
	function Action (int entity, CTakeDamageInfo info, int &result);
	function void (int entity, const CTakeDamageInfo info, int result);
};

typeset killed_func_t
{
	function Action (int entity, CTakeDamageInfo info, int &result);
	function void (int entity, const CTakeDamageInfo info, int result);
};

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

#if defined animhelpers_included
native void SelectDeathPoseActivityAndFrame(int entity, const CTakeDamageInfoVar info, int hitgroup, Activity &activity, int &frame);
native Activity CombatCharacterGetDeathActivity(int entity);
#endif

native void CombatCharacterEventKilled(int entity, const CTakeDamageInfoVar info);

native int CombatCharacterHullAttackRange(int entity, float flDist, const float mins[3], const float maxs[3], int iDamage, int iDmgType, float forceScale = 1.0, bool bDamageAnyNPC = false);
native int CombatCharacterHullAttackEndPoint(int entity, const float vStart[3], const float vEnd[3], const float mins[3], const float maxs[3], int iDamage, int iDmgType, float flForceScale = 1.0, bool bDamageAnyNPC = false);

native void HookEntityOnTakeDamage(int entity, ondamage_func_t func, bool post);
native void HookEntityOnTakeDamageAlive(int entity, ondamage_func_t func, bool post);
native void HookEntityKilled(int entity, killed_func_t func, bool post);

native void PushAllPlayersAway(const float vFromThisPoint[3], float flRange, float flForce, int nTeam, ArrayList PushedPlayers = null);

native void ApplyAbsVelocityImpulse(int entity, const float vecImpulse[3]);

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
