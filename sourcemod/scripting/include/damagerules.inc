#if defined damagerules_included
	#endinput
#endif
#define damagerules_included

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

typeset damagerules_funcs
{
	//OnTakeDamage
	//OnTakeDamage_Alive
	function int (int entity, CTakeDamageInfo info, any data);
}

#if defined GAME_TF2
enum struct DamageModifyExtras_t
{
	bool bIgniting;
	bool bSelfBlastDmg;
	bool bSendPreFeignDamage;
	bool bPlayDamageReductionSound;
}

#define DamageModifyExtras_tVar any[]

public const DamageModifyExtras_t NULL_DAMAGE_MODIFY_EXTRA;

native void HandleRageGain(int client, int iRequiredBuffFlags, float flDamage, float fInverseRageGainScale);

native void CallOnDealtDamage(int client, int entity, const CTakeDamageInfoVar info);
native bool ApplyOnDamageModifyRules(CTakeDamageInfoVar info, int entity, bool bAllowDamage);
native float ApplyOnDamageAliveModifyRules(const CTakeDamageInfoVar info, int entity, DamageModifyExtras_tVar extras = NULL_DAMAGE_MODIFY_EXTRA);
native void ApplyOnHitAttributes(int weapon, int victim, int attacker, const CTakeDamageInfoVar info);
#endif

native int TakeDamage(int entity, const CTakeDamageInfoVar info);

native int CallOnTakeDamage(int entity, const CTakeDamageInfoVar info);
native int CallOnTakeDamageAlive(int entity, const CTakeDamageInfoVar info);

native void SetEntityOnTakeDamage(int entity, damagerules_funcs func, any data = 0);
native void SetEntityOnTakeDamageAlive(int entity, damagerules_funcs func, any data = 0);

stock int baseline_ontakedamage(int entity, CTakeDamageInfo info, any data)
{
#if defined GAME_TF2
	ApplyOnDamageModifyRules(info, entity, true);
	
	int attacker = info.m_hAttacker;
	float damage = info.m_flDamage;
	if(damage > 0.0 && attacker != entity) {
		HandleRageGain(attacker, kRageBuffFlag_OnDamageDealt, damage, 6.0);

		if((info.m_bitsDamageType & DMG_BURN) || (info.m_bitsDamageType & DMG_PLASMA)) {
			HandleRageGain(attacker, kRageBuffFlag_OnBurnDamageDealt, damage, 30.0);
		}

		int weapon = info.m_hWeapon;
		if(weapon != -1) {
			ApplyOnHitAttributes(weapon, entity, attacker, info);
		}
	}
#endif
	
	return CallOnTakeDamage(entity, info);
}

stock int baseline_ontakedamagealive(int entity, CTakeDamageInfo info, any data)
{
#if defined GAME_TF2
	info.m_flDamage = ApplyOnDamageAliveModifyRules(info, entity);
#endif

	int ret = CallOnTakeDamageAlive(entity, info);

#if defined GAME_TF2
	int attacker = info.m_hAttacker;
	if(attacker != -1) {
		CallOnDealtDamage(attacker, entity, info);
	}
#endif

	return ret;
}

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
