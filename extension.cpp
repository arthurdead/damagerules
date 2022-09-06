/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include <unordered_map>
#include <string_view>
#include <string>
#include <stack>

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

#ifdef __HAS_NEXTBOT
#include <INextBotExt.h>
#endif

#define protected public

#include <ehandle.h>
#include <predictioncopy.h>
#include <takedamageinfo.h>

#undef protected

#undef clamp

#include "extension.h"

#include <public/damageinfo.cpp>

#include <CDetour/detours.h>

#include <server_class.h>
#include <eiface.h>
#include <shareddefs.h>
#ifdef __HAS_WPNHACK
#include <IWpnHack.h>
#endif

#include <igameevents.h>
#include <toolframework/itoolentity.h>

#ifndef FMTFUNCTION
#define FMTFUNCTION(...)
#endif

#include <util.h>
#include <ServerNetworkProperty.h>
#include <tier1/checksum_crc.h>

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Sample g_Sample;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_Sample);

ISDKTools *g_pSDKTools = nullptr;
ISDKHooks *g_pSDKHooks = nullptr;
IServerGameEnts *gameents = nullptr;
IGameEventManager2 *gameeventmanager = nullptr;
INetworkStringTableContainer *netstringtables = NULL;
CGlobalVars *gpGlobals = nullptr;
INetworkStringTable *m_pUserInfoTable = nullptr;
IServerTools *servertools = nullptr;
IEntityFactoryDictionary *dictionary = nullptr;
#ifdef __HAS_WPNHACK
IWpnHack *g_pWpnHack = nullptr;
#endif
#ifdef __HAS_WPNHACK
INextBotExt *g_pNextBot = nullptr;
#endif

#if SOURCE_ENGINE == SE_TF2
int CTFWeaponBaseApplyOnHitAttributes = -1;

void *HandleRageGainPtr = nullptr;
void *CTFPlayerOnDealtDamage = nullptr;
void *CTFGameRulesApplyOnDamageModifyRules = nullptr;
void *CTFGameRulesApplyOnDamageAliveModifyRules = nullptr;
#endif
void *CBaseEntityTakeDamage = nullptr;

template <typename T>
T void_to_func(void *ptr)
{
	union { T f; void *p; };
	p = ptr;
	return f;
}

template <typename R, typename T, typename ...Args>
R call_mfunc(T *pThisPtr, void *offset, Args ...args)
{
	class VEmptyClass {};
	
	void **this_ptr = *reinterpret_cast<void ***>(&pThisPtr);
	
	union
	{
		R (VEmptyClass::*mfpnew)(Args...);
#ifndef PLATFORM_POSIX
		void *addr;
	} u;
	u.addr = offset;
#else
		struct  
		{
			void *addr;
			intptr_t adjustor;
		} s;
	} u;
	u.s.addr = offset;
	u.s.adjustor = 0;
#endif
	
	return (R)(reinterpret_cast<VEmptyClass *>(this_ptr)->*u.mfpnew)(args...);
}

template <typename R, typename T, typename ...Args>
R call_vfunc(T *pThisPtr, size_t offset, Args ...args)
{
	void **vtable = *reinterpret_cast<void ***>(pThisPtr);
	void *vfunc = vtable[offset];
	
	return call_mfunc<R, T, Args...>(pThisPtr, vfunc, args...);
}

template <typename T>
void *func_to_void(T ptr)
{
	union { T f; void *p; };
	f = ptr;
	return p;
}

template <typename T>
int vfunc_index(T func)
{
	SourceHook::MemFuncInfo info{};
	SourceHook::GetFuncInfo<T>(func, info);
	return info.vtblindex;
}

#if SOURCE_ENGINE == SE_TF2
struct DamageModifyExtras_t
{
	bool bIgniting{false};
	bool bSelfBlastDmg{false};
	bool bSendPreFeignDamage{false};
	bool bPlayDamageReductionSound{false};
};

#define DMG_CRITICAL (DMG_ACID)
#endif

void SetHandleEntity(CBaseHandle &hndl, edict_t *pEdict)
{
	if(!pEdict) {
		hndl.Set(nullptr);
	} else {
		gamehelpers->SetHandleEntity(hndl, pEdict);
	}
}

int IndexOfEdict(edict_t *pEdict)
{
	if(!pEdict) {
		return -1;
	} else {
		return gamehelpers->IndexOfEdict(pEdict);
	}
}

edict_t *GetHandleEntity(const CBaseHandle &hndl)
{
	return gamehelpers->GetHandleEntity(const_cast<CBaseHandle &>(hndl));
}

int CBaseEntityIsNPC = -1;
int CBaseEntityMyCombatCharacterPointer = -1;

class CBasePlayer;
class CBaseCombatCharacter;

int CBaseEntityOnTakeDamageOffset = -1;
void *CBasePlayerOnTakeDamagePtr = nullptr;
int CBaseEntityEvent_KilledOtherOffset = -1;

int m_iTeamNumOffset = -1;
int m_iHealthOffset = -1;
int m_hActiveWeaponOffset = -1;
int m_iNameOffset = -1;

class CBaseEntity : public IServerEntity
{
public:
	int entindex()
	{
		return gamehelpers->EntityToBCompatRef(this);
	}

	edict_t *edict()
	{
		return GetNetworkable()->GetEdict();
	}

	const char *GetClassname()
	{
		return gamehelpers->GetEntityClassname(this);
	}

	const char *GetName()
	{
		if(m_iNameOffset == -1) {
			datamap_t *map = gamehelpers->GetDataMap(this);
			sm_datatable_info_t info{};
			gamehelpers->FindDataMapInfo(map, "m_iName", &info);
			m_iNameOffset = info.actual_offset;
		}

		return STRING( *(string_t *)(((unsigned char *)this) + m_iNameOffset) );
	}

	CBasePlayer *IsPlayer()
	{
		int idx = gamehelpers->EntityToBCompatRef(this);
		if(idx >= 1 && idx <= playerhelpers->GetNumPlayers()) {
			return (CBasePlayer *)this;
		} else {
			return nullptr;
		}
	}

	bool IsNPC()
	{
		return call_vfunc<bool>(this, CBaseEntityIsNPC);
	}

	CBaseCombatCharacter *MyCombatCharacterPointer()
	{
		return call_vfunc<CBaseCombatCharacter *>(this, CBaseEntityMyCombatCharacterPointer);
	}

	int GetTeamNumber()
	{
		return *(int *)(((unsigned char *)this) + m_iTeamNumOffset);
	}

	void SetTeamNumber_nonetwork(int team)
	{
		*(int *)(((unsigned char *)this) + m_iTeamNumOffset) = team;
	}

	int GetHealth()
	{
		if(m_iHealthOffset == -1) {
			datamap_t *map = gamehelpers->GetDataMap(this);
			sm_datatable_info_t info{};
			gamehelpers->FindDataMapInfo(map, "m_iHealth", &info);
			m_iHealthOffset = info.actual_offset;
		}

		return *(int *)(((unsigned char *)this) + m_iHealthOffset);
	}

	void Event_KilledOther(CBaseEntity *pVictim, const CTakeDamageInfo &info)
	{
		call_vfunc<void, CBaseEntity, CBaseEntity *, const CTakeDamageInfo &>(this, CBaseEntityEvent_KilledOtherOffset, pVictim, info);
	}

	int OnTakeDamage(const CTakeDamageInfo &info)
	{
		return call_vfunc<int, CBaseEntity, const CTakeDamageInfo &>(this, CBaseEntityOnTakeDamageOffset, info);
	}
};

void SetEdictStateChanged(CBaseEntity *pEntity, int offset)
{
	IServerNetworkable *pNet = pEntity->GetNetworkable();
	edict_t *edict = pNet->GetEdict();
	gamehelpers->SetEdictStateChanged(edict, offset);
}

int CBaseAnimatingIgnite = -1;

class CBaseAnimating : public CBaseEntity
{
public:
	void Ignite( float flFlameLifetime, bool bNPCOnly, float flSize, bool bCalledByLevelDesigner )
	{
		call_vfunc<void, CBaseAnimating, float, bool , float, bool>(this, CBaseAnimatingIgnite, flFlameLifetime, bNPCOnly, flSize, bCalledByLevelDesigner);
	}
};

int CBaseCombatCharacterGetBossType = -1;

using HalloweenBossType = int;

int CBaseCombatCharacterCheckTraceHullAttackRange = -1;
int CBaseCombatCharacterCheckTraceHullAttackEndPoint = -1;
void *CBaseCombatCharacterEvent_Killed = nullptr;
int CBaseCombatCharacterOnTakeDamage_AliveOffset = -1;

class CBaseCombatCharacter : public CBaseAnimating
{
public:
	HalloweenBossType GetBossType()
	{
		return call_vfunc<HalloweenBossType, CBaseCombatCharacter>(this, CBaseCombatCharacterGetBossType);
	}

	CBaseEntity *CheckTraceHullAttack( float flDist, const Vector &mins, const Vector &maxs, int iDamage, int iDmgType, float forceScale, bool bDamageAnyNPC )
	{
		return call_vfunc<CBaseEntity *, CBaseCombatCharacter, float, const Vector &, const Vector &, int, int, float, bool>(this, CBaseCombatCharacterCheckTraceHullAttackRange, flDist, mins, maxs, iDamage, iDmgType, forceScale, bDamageAnyNPC);
	}

	CBaseEntity *CheckTraceHullAttack( const Vector &vStart, const Vector &vEnd, const Vector &mins, const Vector &maxs, int iDamage, int iDmgType, float flForceScale, bool bDamageAnyNPC )
	{
		return call_vfunc<CBaseEntity *, CBaseCombatCharacter, const Vector &, const Vector &, const Vector &, const Vector &, int, int, float, bool>(this, CBaseCombatCharacterCheckTraceHullAttackEndPoint, vStart, vEnd, mins, maxs, iDamage, iDmgType, flForceScale, bDamageAnyNPC);
	}

	void Event_Killed(const CTakeDamageInfo &info)
	{
		call_mfunc<void, CBaseCombatCharacter, const CTakeDamageInfo &>(this, CBaseCombatCharacterEvent_Killed, info);
	}

	int OnTakeDamage_Alive(const CTakeDamageInfo &info)
	{
		return call_vfunc<int, CBaseCombatCharacter, const CTakeDamageInfo &>(this, CBaseCombatCharacterOnTakeDamage_AliveOffset, info);
	}
};

int NextBotCombatCharacterIgnite = -1;

class NextBotCombatCharacter : public CBaseCombatCharacter
{
public:
	void Ignite( float flFlameLifetime, CBaseEntity *pAttacker )
	{
		call_vfunc<void, NextBotCombatCharacter, float, CBaseEntity *>(this, NextBotCombatCharacterIgnite, flFlameLifetime, pAttacker);
	}
};

class CBasePlayer : public CBaseCombatCharacter
{
public:
	int OnTakeDamage( const CTakeDamageInfo &info )
	{
		return call_mfunc<int, CBasePlayer, const CTakeDamageInfo &>(this, CBasePlayerOnTakeDamagePtr, info);
	}

	int GetUserID()
	{
		return engine->GetPlayerUserId( GetNetworkable()->GetEdict() );
	}
};

#if SOURCE_ENGINE == SE_TF2
class CTFPlayer;

int CTFWeaponBaseGetWeaponID = -1;

class CTFWeaponBase : public CBaseEntity
{
public:
	void ApplyOnHitAttributes( CBaseEntity *pVictimBaseEntity, CTFPlayer *pAttacker, const CTakeDamageInfo &info )
	{
		call_vfunc<void, CTFWeaponBase, CBaseEntity *, CTFPlayer *, const CTakeDamageInfo &>(this, CTFWeaponBaseApplyOnHitAttributes, pVictimBaseEntity, pAttacker, info);
	}

	int GetWeaponID()
	{
		return call_vfunc<int, CTFWeaponBase>(this, CTFWeaponBaseGetWeaponID);
	}
};

class CTFPlayer : public CBasePlayer
{
public:
	void OnDealtDamage( CBaseCombatCharacter *pVictim, const CTakeDamageInfo &info )
	{
		call_mfunc<void, CTFPlayer, CBaseCombatCharacter *, const CTakeDamageInfo &>(this, CTFPlayerOnDealtDamage, pVictim, info);
	}

	CTFWeaponBase *GetActiveTFWeapon()
	{
		if(m_hActiveWeaponOffset == -1) {
			datamap_t *map = gamehelpers->GetDataMap(this);
			sm_datatable_info_t info{};
			gamehelpers->FindDataMapInfo(map, "m_hActiveWeapon", &info);
			m_hActiveWeaponOffset = info.actual_offset;
		}

		return (CTFWeaponBase *)(CBaseEntity *)*(EHANDLE *)(((unsigned char *)this) + m_hActiveWeaponOffset);
	}
};

enum
{
	kRageBuffFlag_None = 0x00,
	kRageBuffFlag_OnDamageDealt = 0x01,
	kRageBuffFlag_OnDamageReceived = 0x02,
	kRageBuffFlag_OnMedicHealingReceived = 0x04,
	kRageBuffFlag_OnBurnDamageDealt = 0x08,
	kRageBuffFlag_OnHeal = 0x10
};

void HandleRageGain( CTFPlayer *pPlayer, unsigned int iRequiredBuffFlags, float flDamage, float fInverseRageGainScale )
{
	(void_to_func<void(*)(CTFPlayer *, unsigned int, float, float)>(HandleRageGainPtr))(pPlayer, iRequiredBuffFlags, flDamage, fInverseRageGainScale);
}

static cell_t HandleRageGainNative(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	HandleRageGain((CTFPlayer *)pEntity, params[1], sp_ctof(params[2]), sp_ctof(params[3]));
	return 0;
}
#endif

int CGameRulesAdjustPlayerDamageTaken = -1;
int CGameRulesAdjustPlayerDamageInflicted = -1;
int CGameRulesShouldUseRobustRadiusDamage = -1;
int CGameRulesGetAmmoDamageOffset = -1;
void *CGameRulesGetAmmoDamagePtr = nullptr;
int CMultiplayRulesGetDeathScorerOffset = -1;
void *CMultiplayRulesGetDeathScorerPtr = nullptr;

int CGameRulesGetSkillLevel = -1;

class CGameRules
{
public:
	int GetSkillLevel()
	{
		return call_vfunc<int>(this, CGameRulesGetSkillLevel);
	}

	float GetAmmoDamage( CBaseEntity *pAttacker, CBaseEntity *pVictim, int nAmmoType )
	{
		return call_mfunc<float, CGameRules, CBaseEntity *, CBaseEntity *, int>(this, CGameRulesGetAmmoDamagePtr, pAttacker, pVictim, nAmmoType);
	}

	float AdjustPlayerDamageInflicted( float damage )
	{
		return call_vfunc<float, CGameRules, float>(this, CGameRulesAdjustPlayerDamageInflicted, damage);
	}

	void  AdjustPlayerDamageTaken( CTakeDamageInfo *pInfo )
	{
		call_vfunc<void, CGameRules, CTakeDamageInfo *>(this, CGameRulesAdjustPlayerDamageTaken, pInfo);
	}
};

int CMultiplayRulesDeathNoticeOffset = -1;
void *CMultiplayRulesDeathNoticePtr = nullptr;

class CMultiplayRules : public CGameRules
{
public:
	void DeathNotice(CBasePlayer *pVictim, const CTakeDamageInfo &info)
	{
		call_mfunc<void, CMultiplayRules, CBasePlayer *, const CTakeDamageInfo &>(this, CMultiplayRulesDeathNoticePtr, pVictim, info);
	}

	CBasePlayer *GetDeathScorer( CBaseEntity *pKiller, CBaseEntity *pInflictor )
	{
		return call_mfunc<CBasePlayer *, CMultiplayRules, CBaseEntity *, CBaseEntity *>(this, CMultiplayRulesGetDeathScorerPtr, pKiller, pInflictor);
	}
};

#if SOURCE_ENGINE == SE_TF2
int CTFGameRulesDeathNotice = -1;
void *CTFGameRulesPushAllPlayersAway{nullptr};

class CTFGameRules : public CMultiplayRules
{
public:
	bool ApplyOnDamageModifyRules( CTakeDamageInfo &info, CBaseEntity *pVictimBaseEntity, bool bAllowDamage )
	{
		return call_mfunc<bool, CTFGameRules, CTakeDamageInfo &, CBaseEntity *, bool>(this, CTFGameRulesApplyOnDamageModifyRules, info, pVictimBaseEntity, bAllowDamage);
	}

	float ApplyOnDamageAliveModifyRules( const CTakeDamageInfo &info, CBaseEntity *pVictimBaseEntity, DamageModifyExtras_t& outParams )
	{
		return call_mfunc<float, CTFGameRules, const CTakeDamageInfo &, CBaseEntity *, DamageModifyExtras_t &>(this, CTFGameRulesApplyOnDamageAliveModifyRules, info, pVictimBaseEntity, outParams);
	}

	void DeathNotice(CBasePlayer *pVictim, const CTakeDamageInfo &info, const char *eventName)
	{
		call_vfunc<void, CTFGameRules, CBasePlayer *, const CTakeDamageInfo &, const char *>(this, CTFGameRulesDeathNotice, pVictim, info, eventName);
	}

	void PushAllPlayersAway( const Vector& vFromThisPoint, float flRange, float flForce, int nTeam, CUtlVector< CTFPlayer* > *pPushedPlayers /*= NULL*/ )
	{
		call_mfunc<void, CTFGameRules, const Vector&, float, float, int, CUtlVector< CTFPlayer* > *>(this, CTFGameRulesPushAllPlayersAway, vFromThisPoint, flRange, flForce, nTeam, pPushedPlayers);
	}
};

class CPlayerResource : public CBaseEntity
{
public:

};
#endif

#if SOURCE_ENGINE == SE_TF2
using ECritType = CTakeDamageInfo::ECritType;
#endif

Vector addr_deref_vec(const cell_t *&addr)
{
	Vector ret;

	ret.x = sp_ctof(*addr);
	++addr;

	ret.y = sp_ctof(*addr);
	++addr;

	ret.z = sp_ctof(*addr);
	++addr;

	return ret;
}

void addr_read_vec(cell_t *&addr, const Vector &vec)
{
	*addr = sp_ftoc(vec.x);
	++addr;

	*addr = sp_ftoc(vec.y);
	++addr;

	*addr = sp_ftoc(vec.z);
	++addr;
}

void AddrToDamageInfo(CTakeDamageInfo &info, const cell_t *addr)
{
	const cell_t *tmp_addr{addr};
	info.m_vecDamageForce = addr_deref_vec(tmp_addr);
	info.m_vecDamagePosition = addr_deref_vec(tmp_addr);
	info.m_vecReportedPosition = addr_deref_vec(tmp_addr);
	SetHandleEntity(info.m_hInflictor, gamehelpers->EdictOfIndex(*tmp_addr));
	++tmp_addr;
	SetHandleEntity(info.m_hAttacker, gamehelpers->EdictOfIndex(*tmp_addr));
	++tmp_addr;
	SetHandleEntity(info.m_hWeapon, gamehelpers->EdictOfIndex(*tmp_addr));
	++tmp_addr;
	info.m_flDamage = sp_ctof(*tmp_addr);
	++tmp_addr;
	info.m_flMaxDamage = sp_ctof(*tmp_addr);
	++tmp_addr;
	info.m_flBaseDamage = sp_ctof(*tmp_addr);
	++tmp_addr;
	info.m_bitsDamageType = *tmp_addr;
	++tmp_addr;
	info.m_iDamageCustom = *tmp_addr;
	++tmp_addr;
	info.m_iDamageStats = *tmp_addr;
	++tmp_addr;
	info.m_iAmmoType = *tmp_addr;
	++tmp_addr;
#if SOURCE_ENGINE == SE_TF2
	info.m_iDamagedOtherPlayers = *tmp_addr;
	++tmp_addr;
	info.m_iPlayerPenetrationCount = *tmp_addr;
	++tmp_addr;
	info.m_flDamageBonus = sp_ctof(*tmp_addr);
	++tmp_addr;
	SetHandleEntity(info.m_hDamageBonusProvider, gamehelpers->EdictOfIndex(*tmp_addr));
	++tmp_addr;
	info.m_bForceFriendlyFire = *tmp_addr;
	++tmp_addr;
	info.m_flDamageForForce = sp_ctof(*tmp_addr);
	++tmp_addr;
	info.m_eCritType = (ECritType)*tmp_addr;
#elif SOURCE_ENGINE == SE_LEFT4DEAD2
	info.m_flRadius = sp_ctof(*tmp_addr);
#endif
}

void DamageInfoToAddr(const CTakeDamageInfo &info, cell_t *addr)
{
	cell_t *tmp_addr{addr};
	addr_read_vec(tmp_addr, info.m_vecDamageForce);
	addr_read_vec(tmp_addr, info.m_vecDamagePosition);
	addr_read_vec(tmp_addr, info.m_vecReportedPosition);
	*tmp_addr = IndexOfEdict(GetHandleEntity(info.m_hInflictor));
	++tmp_addr;
	*tmp_addr = IndexOfEdict(GetHandleEntity(info.m_hAttacker));
	++tmp_addr;
	*tmp_addr = IndexOfEdict(GetHandleEntity(info.m_hWeapon));
	++tmp_addr;
	*tmp_addr = sp_ftoc(info.m_flDamage);
	++tmp_addr;
	*tmp_addr = sp_ftoc(info.m_flMaxDamage);
	++tmp_addr;
	*tmp_addr = sp_ftoc(info.m_flBaseDamage);
	++tmp_addr;
	*tmp_addr = info.m_bitsDamageType;
	++tmp_addr;
	*tmp_addr = info.m_iDamageCustom;
	++tmp_addr;
	*tmp_addr = info.m_iDamageStats;
	++tmp_addr;
	*tmp_addr = info.m_iAmmoType;
	++tmp_addr;
#if SOURCE_ENGINE == SE_TF2
	*tmp_addr = info.m_iDamagedOtherPlayers;
	++tmp_addr;
	*tmp_addr = info.m_iPlayerPenetrationCount;
	++tmp_addr;
	*tmp_addr = sp_ftoc(info.m_flDamageBonus);
	++tmp_addr;
	*tmp_addr = IndexOfEdict(GetHandleEntity(info.m_hDamageBonusProvider));
	++tmp_addr;
	*tmp_addr = info.m_bForceFriendlyFire;
	++tmp_addr;
	*tmp_addr = sp_ftoc(info.m_flDamageForForce);
	++tmp_addr;
	*tmp_addr = info.m_eCritType;
#elif SOURCE_ENGINE == SE_LEFT4DEAD2
	*tmp_addr = sp_ftoc(info.m_flRadius);
#endif
}

void Sample::AddrToDamageInfo(const cell_t *addr, CTakeDamageInfo &info)
{
	::AddrToDamageInfo(info, addr);
}

void Sample::DamageInfoToAddr(const CTakeDamageInfo &info, cell_t *addr)
{
	::DamageInfoToAddr(info, addr);
}

void Sample::ParamToDamageInfo(IPluginContext *ctx, cell_t local, CTakeDamageInfo &info)
{
	cell_t *addr = nullptr;
	ctx->LocalToPhysAddr(local, &addr);
	::AddrToDamageInfo(info, addr);
}

void Sample::DamageInfoToParam(IPluginContext *ctx, const CTakeDamageInfo &info, cell_t local)
{
	cell_t *addr = nullptr;
	ctx->LocalToPhysAddr(local, &addr);
	::DamageInfoToAddr(info, addr);
}

void Sample::PushDamageInfo(ICallable *func, cell_t *addr, const CTakeDamageInfo &info)
{
	::DamageInfoToAddr(info, addr);
	func->PushArray(addr, DAMAGEINFO_STRUCT_SIZE_IN_CELL, 0);
}

void Sample::PushDamageInfo(ICallable *func, cell_t *addr, CTakeDamageInfo &info, bool copyback)
{
	::DamageInfoToAddr(info, addr);
	func->PushArray(addr, DAMAGEINFO_STRUCT_SIZE_IN_CELL, copyback ? SM_PARAM_COPYBACK : 0);
	if(copyback) {
		::AddrToDamageInfo(info, addr);
	}
}

size_t Sample::SPDamageInfoStructSize()
{
	return DAMAGEINFO_STRUCT_SIZE;
}

size_t Sample::SPDamageInfoStructSizeInCell()
{
	return DAMAGEINFO_STRUCT_SIZE_IN_CELL;
}

#if SOURCE_ENGINE == SE_TF2
static cell_t CallOnDealtDamage(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	CBaseEntity *pVictim = gamehelpers->ReferenceToEntity(params[2]);
	if(!pVictim) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[2]);
	}
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[3], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);
	
	((CTFPlayer *)pEntity)->OnDealtDamage((CBaseCombatCharacter *)pVictim, info);
	return 0;
}
#endif

class CGameRules;

CBaseEntity *g_pGameRulesProxyEntity = nullptr;
CGameRules *g_pGameRules = nullptr;
CBaseEntityList *g_pEntityList = nullptr;

#if SOURCE_ENGINE == SE_TF2
static cell_t ApplyOnDamageModifyRules(IPluginContext *pContext, const cell_t *params)
{
	if(!g_pGameRules) {
		return pContext->ThrowNativeError("gamerules is not loaded");
	}
	
	CBaseEntity *pVictim = gamehelpers->ReferenceToEntity(params[2]);
	if(!pVictim) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[2]);
	}
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[1], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);
	
	bool ret = ((CTFGameRules *)g_pGameRules)->ApplyOnDamageModifyRules(info, pVictim, params[3]);
	
	::DamageInfoToAddr(info, addr);
	
	return ret;
}

static cell_t ApplyOnDamageAliveModifyRules(IPluginContext *pContext, const cell_t *params)
{
	if(!g_pGameRules) {
		return pContext->ThrowNativeError("gamerules is not loaded");
	}
	
	CBaseEntity *pVictim = gamehelpers->ReferenceToEntity(params[2]);
	if(!pVictim) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[2]);
	}
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[1], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);
	
	DamageModifyExtras_t extra{};
	float ret = ((CTFGameRules *)g_pGameRules)->ApplyOnDamageAliveModifyRules(info, pVictim, extra);
	
	pContext->LocalToPhysAddr(params[3], &addr);
	
	IPluginRuntime *runtime = pContext->GetRuntime();
	
	uint32_t idx = (uint32_t)-1;
	runtime->FindPubvarByName("NULL_DAMAGE_MODIFY_EXTRA", &idx);

	sp_pubvar_t *NULL_DAMAGE_MODIFY_EXTRA = nullptr;
	runtime->GetPubvarByIndex(idx, &NULL_DAMAGE_MODIFY_EXTRA);
	
	if(addr != NULL_DAMAGE_MODIFY_EXTRA->offs) {
		addr[0] = extra.bIgniting;
		addr[1] = extra.bSelfBlastDmg;
		addr[2] = extra.bSendPreFeignDamage;
		addr[3] = extra.bPlayDamageReductionSound;
	}
	
	return sp_ftoc(ret);
}
#endif

SH_DECL_MANUALHOOK0_void(GenericDtor, 1, 0, 0)

SH_DECL_MANUALHOOK1(OnTakeDamage, 0, 0, 0, int, const CTakeDamageInfo &)
SH_DECL_MANUALHOOK1(OnTakeDamageAlive, 0, 0, 0, int, const CTakeDamageInfo &)
SH_DECL_MANUALHOOK1_void(Event_Killed, 0, 0, 0, const CTakeDamageInfo &)

cell_t CombatCharacterHullAttackRange(IPluginContext *pContext, const cell_t *params)
{
	CBaseCombatCharacter *pSubject = (CBaseCombatCharacter *)gamehelpers->ReferenceToEntity(params[1]);
	if(!pSubject)
	{
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}

	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[3], &addr);

	Vector mins{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	pContext->LocalToPhysAddr(params[4], &addr);

	Vector maxs{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	CBaseEntity *pHit = pSubject->CheckTraceHullAttack(sp_ctof(params[2]), mins, maxs, params[5], params[6], sp_ctof(params[7]), params[8]);

	return pHit ? gamehelpers->EntityToBCompatRef(pHit) : -1;
}

cell_t CombatCharacterHullAttackEndPoint(IPluginContext *pContext, const cell_t *params)
{
	CBaseCombatCharacter *pSubject = (CBaseCombatCharacter *)gamehelpers->ReferenceToEntity(params[1]);
	if(!pSubject)
	{
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}

	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[2], &addr);

	Vector vStart{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	pContext->LocalToPhysAddr(params[3], &addr);

	Vector vEnd{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	pContext->LocalToPhysAddr(params[4], &addr);

	Vector mins{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	pContext->LocalToPhysAddr(params[5], &addr);

	Vector maxs{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	CBaseEntity *pHit = pSubject->CheckTraceHullAttack(vStart, vEnd, mins, maxs, params[6], params[7], sp_ctof(params[8]), params[9]);

	return pHit ? gamehelpers->EntityToBCompatRef(pHit) : -1;
}

enum entity_type
{
	entity_any,
	entity_npc,
	entity_player,
};

static META_RES res_to_meta_res(cell_t res)
{
	switch(res) {
		case Pl_Continue:
		return MRES_IGNORED;
		case Pl_Changed:
		return MRES_HANDLED;
		case Pl_Handled:
		return MRES_SUPERCEDE;
		case Pl_Stop:
		return MRES_SUPERCEDE;
	}

	return MRES_HANDLED;
}

#define NUM_CALLBACKS 3

struct callback_t
{
	IChangeableForward *fwd = nullptr;
	bool hooked = false;
};

struct callback_holder_t
{
	callback_t callbacks[NUM_CALLBACKS][2];
	static inline bool inside_callback[NUM_CALLBACKS]{false};

	callback_t &takedmg(bool post)
	{ return callbacks[0][post ? 1 : 0]; }

	callback_t &takedmgalive(bool post)
	{ return callbacks[1][post ? 1 : 0]; }

	callback_t &killed(bool post)
	{ return callbacks[2][post ? 1 : 0]; }

	std::vector<IdentityToken_t *> owners{};
	bool erase = true;
	int ref = -1;
	entity_type type = entity_any;
	
	callback_holder_t(CBaseEntity *pEntity, int ref_);
	~callback_holder_t();
	
	void dtor(CBaseEntity *pEntity);

	void HookEntityDtor();

	META_RES SPOnTakeDamage(CBaseEntity *pEntity, CTakeDamageInfo &info, int &result, bool post)
	{
		IChangeableForward *fwd{takedmg(post).fwd};

		if(!fwd || fwd->GetFunctionCount() == 0) {
			return MRES_IGNORED;
		}

		cell_t addr[DAMAGEINFO_STRUCT_SIZE_IN_CELL]{0};
		::DamageInfoToAddr(info, addr);
		
		fwd->PushCell(gamehelpers->EntityToBCompatRef(pEntity));
		fwd->PushArray(addr, DAMAGEINFO_STRUCT_SIZE_IN_CELL, SM_PARAM_COPYBACK);
		fwd->PushCellByRef((cell_t *)&result);
		cell_t res = 0;

		inside_callback[0] = true;
		fwd->Execute(&res);
		inside_callback[0] = false;

		if(res == Pl_Changed) {
			::AddrToDamageInfo(info, addr);
		} else if(res >= Pl_Handled) {
			result = 0;
		}
		
		return res_to_meta_res(res);
	}
	
	int HookOnTakeDamage_pre(const CTakeDamageInfo &info)
	{
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);

		CTakeDamageInfo info_copy = info;

		int result = 0;
		META_RES res = SPOnTakeDamage(pEntity, info_copy, result, false);
		
		RETURN_META_VALUE(res, result);
	}

	int HookOnTakeDamage_post(const CTakeDamageInfo &info)
	{
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);

		CTakeDamageInfo info_copy = info;

		int result = 0;
		META_RES res = SPOnTakeDamage(pEntity, info_copy, result, true);
		
		RETURN_META_VALUE(res, result);
	}

	META_RES SPOnTakeDamageAlive(CBaseEntity *pEntity, CTakeDamageInfo &info, int &result, bool post)
	{
		IChangeableForward *fwd{takedmgalive(post).fwd};

		if(!fwd || fwd->GetFunctionCount() == 0) {
			return MRES_IGNORED;
		}

		cell_t addr[DAMAGEINFO_STRUCT_SIZE_IN_CELL]{0};
		::DamageInfoToAddr(info, addr);
		
		fwd->PushCell(gamehelpers->EntityToBCompatRef(pEntity));
		fwd->PushArray(addr, DAMAGEINFO_STRUCT_SIZE_IN_CELL);
		fwd->PushCellByRef((cell_t *)&result);
		cell_t res = 0;

		inside_callback[1] = true;
		fwd->Execute(&res);
		inside_callback[1] = false;

		if(res == Pl_Changed) {
			::AddrToDamageInfo(info, addr);
		} else if(res >= Pl_Handled) {
			result = 0;
		}
		
		return res_to_meta_res(res);
	}

	int HookOnTakeDamageAlive_pre(const CTakeDamageInfo &info)
	{
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);

		CTakeDamageInfo info_copy = info;

		int result = 0;
		META_RES res = SPOnTakeDamageAlive(pEntity, info_copy, result, false);
		
		RETURN_META_VALUE(res, result);
	}

	int HookOnTakeDamageAlive_post(const CTakeDamageInfo &info)
	{
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);

		CTakeDamageInfo info_copy = info;

		int result = 0;
		META_RES res = SPOnTakeDamageAlive(pEntity, info_copy, result, true);
		
		RETURN_META_VALUE(res, result);
	}

	META_RES SPEvent_Killed(CBaseEntity *pEntity, CTakeDamageInfo &info, bool post)
	{
		IChangeableForward *fwd{killed(post).fwd};

		if(!fwd || fwd->GetFunctionCount() == 0) {
			return MRES_IGNORED;
		}

		cell_t addr[DAMAGEINFO_STRUCT_SIZE_IN_CELL]{0};
		::DamageInfoToAddr(info, addr);
		
		fwd->PushCell(gamehelpers->EntityToBCompatRef(pEntity));
		fwd->PushArray(addr, DAMAGEINFO_STRUCT_SIZE_IN_CELL);
		cell_t res = 0;

		inside_callback[2] = true;
		fwd->Execute(&res);
		inside_callback[2] = false;

		if(res == Pl_Changed) {
			::AddrToDamageInfo(info, addr);
		}
		
		return res_to_meta_res(res);
	}

	void HookEvent_Killed_pre(const CTakeDamageInfo &info)
	{
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);

		CTakeDamageInfo info_copy = info;

		META_RES res = SPEvent_Killed(pEntity, info_copy, false);
		
		RETURN_META(res);
	}

	void HookEvent_Killed_post(const CTakeDamageInfo &info)
	{
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);

		CTakeDamageInfo info_copy = info;

		META_RES res = SPEvent_Killed(pEntity, info_copy, true);
		
		RETURN_META(res);
	}

	void erase_from_map(int ref);

	void create_takedmg_fwd(CBaseEntity *pEntity, bool post)
	{
		callback_t &callback{takedmg(post)};

		callback.fwd = forwards->CreateForwardEx(nullptr, ET_Hook, 3, nullptr, Param_Cell, Param_Array, Param_CellByRef);

		if(type != entity_npc) {
			if(type != entity_player) {
				if(!post) {
					SH_ADD_MANUALHOOK(OnTakeDamage, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamage_pre), false);
				} else {
					SH_ADD_MANUALHOOK(OnTakeDamage, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamage_post), true);
				}
				callback.hooked = true;
			}
		}
	}

	void create_takedmgalive_fwd(CBaseEntity *pEntity, bool post)
	{
		callback_t &callback{takedmgalive(post)};

		callback.fwd = forwards->CreateForwardEx(nullptr, ET_Hook, 3, nullptr, Param_Cell, Param_Array, Param_CellByRef);

		if(type != entity_npc) {
			if(!post) {
				SH_ADD_MANUALHOOK(OnTakeDamageAlive, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamageAlive_pre), false);
			} else {
				SH_ADD_MANUALHOOK(OnTakeDamageAlive, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamageAlive_post), true);
			}
			callback.hooked = true;
		}
	}

	void create_killed_fwd(CBaseEntity *pEntity, bool post)
	{
		callback_t &callback{killed(post)};

		callback.fwd = forwards->CreateForwardEx(nullptr, ET_Hook, 2, nullptr, Param_Cell, Param_Array);

		if(type != entity_npc) {
			if(!post) {
				SH_ADD_MANUALHOOK(Event_Killed, pEntity, SH_MEMBER(this, &callback_holder_t::HookEvent_Killed_pre), false);
			} else {
				SH_ADD_MANUALHOOK(Event_Killed, pEntity, SH_MEMBER(this, &callback_holder_t::HookEvent_Killed_post), true);
			}
			callback.hooked = true;
		}
	}
};

static cell_t CallOnTakeDamage(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[2], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);
	
	return (callback_holder_t::inside_callback[0] ? SH_MCALL(pEntity, OnTakeDamage)(info) : pEntity->OnTakeDamage(info));
}

static cell_t CallOnTakeDamageAlive(IPluginContext *pContext, const cell_t *params)
{
	CBaseCombatCharacter *pEntity = (CBaseCombatCharacter *)gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[2], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);
	
	return (callback_holder_t::inside_callback[1] ? SH_MCALL(pEntity, OnTakeDamageAlive)(info) : pEntity->OnTakeDamage_Alive(info));
}

cell_t CombatCharacterEventKilled(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pSubject = gamehelpers->ReferenceToEntity(params[1]);
	if(!pSubject)
	{
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	CBaseCombatCharacter *pCombat = pSubject->MyCombatCharacterPointer();
	if(!pCombat)
	{
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}

	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[2], &addr);

	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);

	if(callback_holder_t::inside_callback[2]) {
		SH_MCALL(pCombat, Event_Killed)(info);
	} else {
		pCombat->Event_Killed(info);
	}

	return 0;
}

using callback_holder_map_t = std::unordered_map<int, callback_holder_t *>;
callback_holder_map_t callbackmap{};
callback_holder_map_t npc_callbackmap{};
callback_holder_map_t player_callbackmap{};

void callback_holder_t::erase_from_map(int ref)
{
	callbackmap.erase(ref);

	switch(type) {
		case entity_npc: {
			npc_callbackmap.erase(ref);
		} break;
		case entity_player: {
			player_callbackmap.erase(ref);
		} break;
	}
}

void callback_holder_t::HookEntityDtor()
{
	CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
	int this_ref = gamehelpers->EntityToReference(pEntity);
	dtor(pEntity);
	erase_from_map(this_ref);
	erase = false;
	delete this;
	RETURN_META(MRES_HANDLED);
}

void callback_holder_t::dtor(CBaseEntity *pEntity)
{
	SH_REMOVE_MANUALHOOK(GenericDtor, pEntity, SH_MEMBER(this, &callback_holder_t::HookEntityDtor), false);

	if(type != entity_npc) {
		if(type != entity_player) {
			if(takedmg(false).hooked) {
				SH_REMOVE_MANUALHOOK(OnTakeDamage, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamage_pre), false);
			}
			if(takedmg(true).hooked) {
				SH_REMOVE_MANUALHOOK(OnTakeDamage, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamage_post), true);
			}
		}
		if(takedmgalive(false).hooked) {
			SH_REMOVE_MANUALHOOK(OnTakeDamageAlive, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamageAlive_pre), false);
		}
		if(takedmgalive(true).hooked) {
			SH_REMOVE_MANUALHOOK(OnTakeDamageAlive, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamageAlive_post), true);
		}
		if(killed(false).hooked) {
			SH_REMOVE_MANUALHOOK(Event_Killed, pEntity, SH_MEMBER(this, &callback_holder_t::HookEvent_Killed_pre), false);
		}
		if(killed(true).hooked) {
			SH_REMOVE_MANUALHOOK(Event_Killed, pEntity, SH_MEMBER(this, &callback_holder_t::HookEvent_Killed_post), true);
		}
	}
}

callback_holder_t::callback_holder_t(CBaseEntity *pEntity, int ref_)
	: ref{ref_}
{
	if(pEntity->IsPlayer()) {
		type = entity_player;
	} else if(g_pNextBot->entity_to_npc_type(pEntity, pEntity->GetClassname()) == npc_custom) {
		type = entity_npc;
	}

	SH_ADD_MANUALHOOK(GenericDtor, pEntity, SH_MEMBER(this, &callback_holder_t::HookEntityDtor), false);
	
	callbackmap.emplace(ref, this);

	switch(type) {
		case entity_npc: {
			npc_callbackmap.emplace(ref, this);
		} break;
		case entity_player: {
			player_callbackmap.emplace(ref, this);
		} break;
	}
}

callback_holder_t::~callback_holder_t()
{
	for(size_t i{0}; i < NUM_CALLBACKS; ++i) {
		callback_t *callback{callbacks[i]};
		if(callback[0].fwd) {
			forwards->ReleaseForward(callback[0].fwd);
		}
		if(callback[1].fwd) {
			forwards->ReleaseForward(callback[1].fwd);
		}
	}

	if(erase) {
		erase_from_map(ref);
	}
}

#if SOURCE_ENGINE == SE_TF2
static cell_t ApplyOnHitAttributes(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	CBaseEntity *pVictim = gamehelpers->ReferenceToEntity(params[2]);

	CBaseEntity *pAttacker = gamehelpers->ReferenceToEntity(params[3]);
	if(!pAttacker) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[3]);
	}
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[4], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);
	
	((CTFWeaponBase *)pEntity)->ApplyOnHitAttributes(pVictim, (CTFPlayer *)pAttacker, info);
	
	return 0;
}
#endif

static cell_t TakeDamage(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[2], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);
	
	return call_mfunc<int, CBaseEntity, const CTakeDamageInfo &>(pEntity, CBaseEntityTakeDamage, info);
}

static cell_t SetEntityOnTakeDamage(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	callback_holder_t *holder = nullptr;

	int ref = gamehelpers->EntityToReference(pEntity);
	
	callback_holder_map_t::iterator it{callbackmap.find(ref)};
	if(it != callbackmap.end()) {
		holder = it->second;
	} else {
		holder = new callback_holder_t{pEntity, ref};
	}

	bool post = params[3];

	callback_t &callback{holder->takedmg(post)};

	if(!callback.fwd) {
		holder->create_takedmg_fwd(pEntity, post);
	}

	IPluginFunction *func = pContext->GetFunctionById(params[2]);

	callback.fwd->RemoveFunction(func);
	callback.fwd->AddFunction(func);

	IdentityToken_t *iden{pContext->GetIdentity()};
	if(std::find(holder->owners.cbegin(), holder->owners.cend(), iden) == holder->owners.cend()) {
		holder->owners.emplace_back(iden);
	}
	
	return 0;
}

static cell_t SetEntityOnTakeDamageAlive(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	callback_holder_t *holder = nullptr;

	int ref = gamehelpers->EntityToReference(pEntity);
	
	callback_holder_map_t::iterator it{callbackmap.find(ref)};
	if(it != callbackmap.end()) {
		holder = it->second;
	} else {
		holder = new callback_holder_t{pEntity, ref};
	}

	bool post = params[3];

	callback_t &callback{holder->takedmgalive(post)};

	if(!callback.fwd) {
		holder->create_takedmgalive_fwd(pEntity, post);
	}

	IPluginFunction *func = pContext->GetFunctionById(params[2]);

	callback.fwd->RemoveFunction(func);
	callback.fwd->AddFunction(func);

	IdentityToken_t *iden{pContext->GetIdentity()};
	if(std::find(holder->owners.cbegin(), holder->owners.cend(), iden) == holder->owners.cend()) {
		holder->owners.emplace_back(iden);
	}
	
	return 0;
}

static cell_t HookEntityKilled(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	callback_holder_t *holder = nullptr;

	int ref = gamehelpers->EntityToReference(pEntity);
	
	callback_holder_map_t::iterator it{callbackmap.find(ref)};
	if(it != callbackmap.end()) {
		holder = it->second;
	} else {
		holder = new callback_holder_t{pEntity, ref};
	}

	bool post = params[3];

	callback_t &callback{holder->killed(post)};

	if(!callback.fwd) {
		holder->create_killed_fwd(pEntity, post);
	}

	IPluginFunction *func = pContext->GetFunctionById(params[2]);

	callback.fwd->RemoveFunction(func);
	callback.fwd->AddFunction(func);

	IdentityToken_t *iden{pContext->GetIdentity()};
	if(std::find(holder->owners.cbegin(), holder->owners.cend(), iden) == holder->owners.cend()) {
		holder->owners.emplace_back(iden);
	}
	
	return 0;
}

class CAmmoDef
#ifdef __HAS_WPNHACK
	: public IAmmoDef
#endif
{
#ifndef __HAS_WPNHACK
	static int PlrDamage(int nAmmoIndex)
	{
		return 0;
	}

	static int NPCDamage(int nAmmoIndex)
	{
		return 0;
	}

	static int DamageType(int nAmmoIndex)
	{
		return 0;
	}

	static float DamageForce(int nAmmoIndex)
	{
		return 0.0f;
	}

	static int Index(const char *name)
	{
		return -1;
	}
#endif
};

CAmmoDef *GetAmmoDef()
{
#ifdef __HAS_WPNHACK
	return (CAmmoDef *)g_pWpnHack->AmmoDef();
#else
	return nullptr;
#endif
}

float ImpulseScale( float flTargetMass, float flDesiredSpeed )
{
	return (flTargetMass * flDesiredSpeed);
}

static ConVar *phys_pushscale{nullptr};

void CalculateExplosiveDamageForce( CTakeDamageInfo *info, const Vector &vecDir, const Vector &vecForceOrigin, float flScale )
{
	info->SetDamagePosition( vecForceOrigin );

	float flClampForce = ImpulseScale( 75, 400 );

	// Calculate an impulse large enough to push a 75kg man 4 in/sec per point of damage
	float flForceScale = info->GetBaseDamage() * ImpulseScale( 75, 4 );

	if( flForceScale > flClampForce )
		flForceScale = flClampForce;

	// Fudge blast forces a little bit, so that each
	// victim gets a slightly different trajectory. 
	// This simulates features that usually vary from
	// person-to-person variables such as bodyweight,
	// which are all indentical for characters using the same model.
	flForceScale *= random->RandomFloat( 0.85, 1.15 );

	// Calculate the vector and stuff it into the takedamageinfo
	Vector vecForce = vecDir;
	VectorNormalize( vecForce );
	vecForce *= flForceScale;
	vecForce *= phys_pushscale->GetFloat();
	vecForce *= flScale;
	info->SetDamageForce( vecForce );
}

void CalculateBulletDamageForce( CTakeDamageInfo *info, int iBulletType, const Vector &vecBulletDir, const Vector &vecForceOrigin, float flScale )
{
	info->SetDamagePosition( vecForceOrigin );
	Vector vecForce = vecBulletDir;
	VectorNormalize( vecForce );
	vecForce *= GetAmmoDef()->DamageForce( iBulletType );
	vecForce *= phys_pushscale->GetFloat();
	vecForce *= flScale;
	info->SetDamageForce( vecForce );
}

void CalculateMeleeDamageForce( CTakeDamageInfo *info, const Vector &vecMeleeDir, const Vector &vecForceOrigin, float flScale )
{
	info->SetDamagePosition( vecForceOrigin );

	// Calculate an impulse large enough to push a 75kg man 4 in/sec per point of damage
	float flForceScale = info->GetBaseDamage() * ImpulseScale( 75, 4 );
	Vector vecForce = vecMeleeDir;
	VectorNormalize( vecForce );
	vecForce *= flForceScale;
	vecForce *= phys_pushscale->GetFloat();
	vecForce *= flScale;
	info->SetDamageForce( vecForce );
}

void GuessDamageForce( CTakeDamageInfo *info, const Vector &vecForceDir, const Vector &vecForceOrigin, float flScale )
{
	if ( info->GetDamageType() & DMG_BULLET )
	{
		CalculateBulletDamageForce( info, GetAmmoDef()->Index("TF_AMMO_PRIMARY"), vecForceDir, vecForceOrigin, flScale );
	}
	else if ( info->GetDamageType() & DMG_BLAST )
	{
		CalculateExplosiveDamageForce( info, vecForceDir, vecForceOrigin, flScale );
	}
	else
	{
		CalculateMeleeDamageForce( info, vecForceDir, vecForceOrigin, flScale );
	}
}

static cell_t CalculateExplosiveDamageForceNative(IPluginContext *pContext, const cell_t *params)
{
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[1], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);

	pContext->LocalToPhysAddr(params[2], &addr);
	Vector vecDir{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	pContext->LocalToPhysAddr(params[3], &addr);
	Vector vecForceOrigin{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	CalculateExplosiveDamageForce(&info, vecDir, vecForceOrigin, sp_ctof(params[4]));

	pContext->LocalToPhysAddr(params[1], &addr);
	::DamageInfoToAddr(info, addr);

	return 0;
}

static cell_t CalculateBulletDamageForceNative(IPluginContext *pContext, const cell_t *params)
{
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[1], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);

	pContext->LocalToPhysAddr(params[3], &addr);
	Vector vecDir{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	pContext->LocalToPhysAddr(params[4], &addr);
	Vector vecForceOrigin{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	CalculateBulletDamageForce(&info, params[2], vecDir, vecForceOrigin, sp_ctof(params[5]));

	pContext->LocalToPhysAddr(params[1], &addr);
	::DamageInfoToAddr(info, addr);

	return 0;
}

static cell_t CalculateMeleeDamageForceNative(IPluginContext *pContext, const cell_t *params)
{
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[1], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);

	pContext->LocalToPhysAddr(params[2], &addr);
	Vector vecDir{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	pContext->LocalToPhysAddr(params[3], &addr);
	Vector vecForceOrigin{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	CalculateMeleeDamageForce(&info, vecDir, vecForceOrigin, sp_ctof(params[4]));

	pContext->LocalToPhysAddr(params[1], &addr);
	::DamageInfoToAddr(info, addr);

	return 0;
}

static cell_t GuessDamageForceNative(IPluginContext *pContext, const cell_t *params)
{
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[1], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);

	pContext->LocalToPhysAddr(params[2], &addr);
	Vector vecDir{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	pContext->LocalToPhysAddr(params[3], &addr);
	Vector vecForceOrigin{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	GuessDamageForce(&info, vecDir, vecForceOrigin, sp_ctof(params[4]));

	pContext->LocalToPhysAddr(params[1], &addr);
	::DamageInfoToAddr(info, addr);

	return 0;
}

#include "funnyfile.h"

static cell_t PushAllPlayersAway(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	
	ICellArray *obj = nullptr;
	if(params[5] != BAD_HANDLE) {
		HandleError err = ((HandleSystemHack *)handlesys)->ReadCoreHandle(params[5], arraylist_handle, &security, (void **)&obj);
		if(err != HandleError_None)
		{
			return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[5], err);
		}
	}

	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[1], &addr);
	Vector vFromThisPoint{sp_ctof(addr[0]), sp_ctof(addr[1]), sp_ctof(addr[2])};

	CUtlVector<CTFPlayer *> PushedPlayers{};
	((CTFGameRules *)g_pGameRules)->PushAllPlayersAway(vFromThisPoint, sp_ctof(params[2]), sp_ctof(params[3]), params[4], &PushedPlayers);

	if(obj) {
		size_t len{PushedPlayers.Count()};
		obj->resize(len);
		for(size_t i{0}; i < len; ++i) {
			*obj->at(i) = gamehelpers->EntityToBCompatRef(PushedPlayers[i]);
		}
	}

	return 0;
}

static const sp_nativeinfo_t g_sNativesInfo[] =
{
#if SOURCE_ENGINE == SE_TF2
	{"HandleRageGain", HandleRageGainNative},
	{"PlayerOnDealtDamage", CallOnDealtDamage},
	{"ApplyOnDamageModifyRules", ApplyOnDamageModifyRules},
	{"ApplyOnDamageAliveModifyRules", ApplyOnDamageAliveModifyRules},
	{"ApplyOnHitAttributes", ApplyOnHitAttributes},
#endif
	{"EntityOnTakeDamage", CallOnTakeDamage},
	{"EntityOnTakeDamageAlive", CallOnTakeDamageAlive},
	{"HookEntityOnTakeDamage", SetEntityOnTakeDamage},
	{"HookEntityOnTakeDamageAlive", SetEntityOnTakeDamageAlive},
	{"HookEntityKilled", HookEntityKilled},
	{"EntityTakeDamage", TakeDamage},
	{"CombatCharacterHullAttackRange", CombatCharacterHullAttackRange},
	{"CombatCharacterHullAttackEndPoint", CombatCharacterHullAttackEndPoint},
	{"CombatCharacterEventKilled", CombatCharacterEventKilled},
	{"CalculateExplosiveDamageForce", CalculateExplosiveDamageForceNative},
	{"CalculateBulletDamageForce", CalculateBulletDamageForceNative},
	{"CalculateMeleeDamageForce", CalculateMeleeDamageForceNative},
	{"GuessDamageForce", GuessDamageForceNative},
	{"PushAllPlayersAway", PushAllPlayersAway},
	{nullptr, nullptr},
};

void Sample::OnEntityDestroyed(CBaseEntity *pEntity)
{
	if(!pEntity) {
		return;
	}

	
}

void Sample::OnPluginUnloaded(IPlugin *plugin)
{
	callback_holder_map_t::iterator it{callbackmap.begin()};
	while(it != callbackmap.end()) {
		callback_holder_t *holder = it->second;
		std::vector<IdentityToken_t *> &owners{holder->owners};

		auto it_own{std::find(owners.begin(), owners.end(), plugin->GetIdentity())};
		if(it_own != owners.cend()) {
			owners.erase(it_own);

			size_t func_count{0};

			for(size_t i{0}; i < NUM_CALLBACKS; ++i) {
				callback_t *callback{holder->callbacks[i]};
				if(callback[0].fwd) {
					callback[0].fwd->RemoveFunctionsOfPlugin(plugin);
					func_count += callback[0].fwd->GetFunctionCount();
				}
				if(callback[1].fwd) {
					callback[1].fwd->RemoveFunctionsOfPlugin(plugin);
					func_count += callback[1].fwd->GetFunctionCount();
				}
			}

			if(func_count == 0) {
				switch(holder->type) {
					case entity_npc: {
						npc_callbackmap.erase(holder->ref);
					} break;
					case entity_player: {
						player_callbackmap.erase(holder->ref);
					} break;
				}
				CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(holder->ref);
				if(pEntity) {
					holder->dtor(pEntity);
				}
				holder->erase = false;
				delete holder;
				it = callbackmap.erase(it);
				continue;
			}
		}
		
		++it;
	}
}

ConVar *skill = nullptr;
bool ignore_skill_change = false;
#if SOURCE_ENGINE == SE_TF2
ConVar *tf_mvm_skill = nullptr;
bool ignore_mvm_change = false;
#endif

void skill_changed( IConVar *pVar, const char *pOldValue, float flOldValue )
{
	if(ignore_skill_change) {
		ignore_skill_change = false;
		return;
	}

#if SOURCE_ENGINE == SE_TF2
	ignore_mvm_change = true;

	ConVarRef cVarRef( pVar );

	switch(cVarRef.GetInt()) {
		case SKILL_EASY: tf_mvm_skill->SetValue(1); break;
		case SKILL_MEDIUM: tf_mvm_skill->SetValue(3); break;
		case SKILL_HARD: tf_mvm_skill->SetValue(5); break;
	}
#endif
}

#if SOURCE_ENGINE == SE_TF2
void mvm_skill_changed( IConVar *pVar, const char *pOldValue, float flOldValue )
{
	if(ignore_mvm_change) {
		ignore_mvm_change = false;
		return;
	}

	ignore_skill_change = true;

	ConVarRef cVarRef( pVar );

	switch(cVarRef.GetInt()) {
		case 1:
		case 2: skill->SetValue(SKILL_EASY); break;
		case 3: skill->SetValue(SKILL_MEDIUM); break;
		case 4:
		case 5: skill->SetValue(SKILL_HARD); break;
	}
}
#endif

static size_t player_size{0};

SH_DECL_HOOK1(IVEngineServer, GetPlayerUserId, SH_NOATTRIB, 0, int, const edict_t *)
SH_DECL_HOOK1(IVEngineServer, GetClientSteamID, SH_NOATTRIB, 0, const CSteamID *, edict_t *)

bool Sample::RegisterConCommandBase(ConCommandBase *pCommand)
{
	META_REGCVAR(pCommand);
	return true;
}

CBaseEntity * FindEntityByServerClassname(int iStart, const char * pServerClassName)
{
	constexpr int g_iEdictCount = 2048;
	
	if (iStart >= g_iEdictCount)
		return nullptr;
	for (int i = iStart; i < g_iEdictCount; i++)
	{
		CBaseEntity * pEnt = gamehelpers->ReferenceToEntity(i);
		if (!pEnt)
			continue;
		IServerNetworkable * pNetworkable = ((IServerUnknown *)pEnt)->GetNetworkable();
		if (!pNetworkable)
			continue;
		const char * pName = pNetworkable->GetServerClass()->GetName();
		if (pName && !strcmp(pName, pServerClassName))
			return pEnt;
	}
	return nullptr;
}

static bool gamerules_vtable_assigned{false};

// Controls the application of the robus radius damage model.
ConVar	sv_robust_explosions( "sv_robust_explosions","1", FCVAR_REPLICATED );

// Damage scale for damage inflicted by the player on each skill level.
ConVar	sk_dmg_inflict_scale1( "sk_dmg_inflict_scale1", "1.50", FCVAR_REPLICATED );
ConVar	sk_dmg_inflict_scale2( "sk_dmg_inflict_scale2", "1.00", FCVAR_REPLICATED );
ConVar	sk_dmg_inflict_scale3( "sk_dmg_inflict_scale3", "0.75", FCVAR_REPLICATED );

// Damage scale for damage taken by the player on each skill level.
ConVar	sk_dmg_take_scale1( "sk_dmg_take_scale1", "0.50", FCVAR_REPLICATED );
ConVar	sk_dmg_take_scale2( "sk_dmg_take_scale2", "1.00", FCVAR_REPLICATED );
ConVar	sk_dmg_take_scale3( "sk_dmg_take_scale3", "2.0", FCVAR_REPLICATED );

static void *player_block{nullptr};

class GameRulesVTableHack : public CTFGameRules
{
public:
	void DetourAdjustPlayerDamageTaken( CTakeDamageInfo *pInfo )
	{
		if( pInfo->GetDamageType() & (DMG_DROWN|DMG_CRUSH|DMG_FALL|DMG_POISON) )
		{
			// Skill level doesn't affect these types of damage.
			return;
		}

		switch( GetSkillLevel() )
		{
		case SKILL_EASY:
			pInfo->ScaleDamage( sk_dmg_take_scale1.GetFloat() );
			break;

		case SKILL_MEDIUM:
			pInfo->ScaleDamage( sk_dmg_take_scale2.GetFloat() );
			break;

		case SKILL_HARD:
			pInfo->ScaleDamage( sk_dmg_take_scale3.GetFloat() );
			break;
		}
	}

	//TODO!!!!! call this only on melee weapons
	float DetourAdjustPlayerDamageInflicted( float damage )
	{
		switch( GetSkillLevel() ) 
		{
		case SKILL_EASY:
			return damage * sk_dmg_inflict_scale1.GetFloat();
			break;

		case SKILL_MEDIUM:
			return damage * sk_dmg_inflict_scale2.GetFloat();
			break;

		case SKILL_HARD:
			return damage * sk_dmg_inflict_scale3.GetFloat();
			break;

		default:
			return damage;
			break;
		}
	}

	bool DetourShouldUseRobustRadiusDamage(CBaseEntity *pEntity)
	{
		if( !sv_robust_explosions.GetBool() )
			return false;

		if( !pEntity->IsNPC() )
		{
			// Only NPC's
			return false;
		}

		return true;
	}

	float DetourGetAmmoDamage( CBaseEntity *pAttacker, CBaseEntity *pVictim, int nAmmoType )
	{
		float flDamage = 0.0f;
		CAmmoDef *pAmmoDef = GetAmmoDef();

		flDamage = CTFGameRules::GetAmmoDamage( pAttacker, pVictim, nAmmoType );

		if( pAttacker->IsPlayer() && pVictim->IsNPC() )
		{
			if( pVictim->MyCombatCharacterPointer() )
			{
				// Player is shooting an NPC. Adjust the damage! This protects breakables
				// and other 'non-living' entities from being easier/harder to break
				// in different skill levels.
				flDamage = pAmmoDef->PlrDamage( nAmmoType );
				flDamage = AdjustPlayerDamageInflicted( flDamage );
			}
		}

		return flDamage;
	}

	CBasePlayer *DetourGetDeathScorer( CBaseEntity *pKiller, CBaseEntity *pInflictor );

	void DetourDeathNotice(CBasePlayer *pVictim, const CTakeDamageInfo &info);
};

#include <sourcehook/sh_memory.h>

static int player_manager_ref = INVALID_EHANDLE_INDEX;

#define PLAYER_BLOCK_USERID (SHRT_MAX-1)
#define PLAYER_BLOCK_CLIENT_IDX (playerhelpers->GetMaxClients()-1)
#define PLAYER_BLOCK_ENTITY_IDX (PLAYER_BLOCK_CLIENT_IDX+1)

static void **player_block_vtable{nullptr};

const char *g_szGameRulesProxy = nullptr;

int CBaseEntityIsPlayer = -1;
int CBaseEntityClassify = -1;
int CBasePlayerIsBot = -1;
void *CBaseEntityEvent_KilledOtherPtr = nullptr;
int CBaseEntityGetNetworkableOffset = -1;
void *CBaseEntityGetNetworkable = nullptr;

CBaseEntity *npc_ent{nullptr};

class PlayerBlockVTable
{
public:
	bool IsPlayer()
	{
		return true;
	}

	Class_T Classify()
	{
		return CLASS_PLAYER;
	}

	bool IsBot()
	{
		return true;
	}

	void Event_KilledOther(CBaseEntity *pVictim, const CTakeDamageInfo &info)
	{
		if(npc_ent) {
			npc_ent->Event_KilledOther(pVictim, info);
		}
	}
};

typedef struct player_info_s
{
	DECLARE_BYTESWAP_DATADESC();
	// scoreboard information
	char			name[MAX_PLAYER_NAME_LENGTH];
	// local server user ID, unique while server is running
	int				userID;
	// global unique player identifer
	char			guid[SIGNED_GUID_LEN + 1];
	// friends identification number
	uint32			friendsID;
	// friends name
	char			friendsName[MAX_PLAYER_NAME_LENGTH];
	// true, if player is a bot controlled by game.dll
	bool			fakeplayer;
	// true if player is the HLTV proxy
	bool			ishltv;
#if SOURCE_ENGINE == SE_TF2
	// true if player is the Replay proxy
	bool			isreplay;
#endif
	// custom files CRC for this player
	CRC32_t			customFiles[MAX_CUSTOM_FILES];
	// this counter increases each time the server downloaded a new file
	unsigned char	filesDownloaded;
} player_info_t;

BEGIN_BYTESWAP_DATADESC( player_info_s )
	DEFINE_ARRAY( name, FIELD_CHARACTER, MAX_PLAYER_NAME_LENGTH ),
	DEFINE_FIELD( userID, FIELD_INTEGER ),
	DEFINE_ARRAY( guid, FIELD_CHARACTER, SIGNED_GUID_LEN + 1 ),
	DEFINE_FIELD( friendsID, FIELD_INTEGER ),
	DEFINE_ARRAY( friendsName, FIELD_CHARACTER, MAX_PLAYER_NAME_LENGTH ),
	DEFINE_FIELD( fakeplayer, FIELD_BOOLEAN ),
	DEFINE_FIELD( ishltv, FIELD_BOOLEAN ),
#if SOURCE_ENGINE == SE_TF2
	DEFINE_FIELD( isreplay, FIELD_BOOLEAN ),
#endif
	DEFINE_ARRAY( customFiles, FIELD_INTEGER, MAX_CUSTOM_FILES ),
	DEFINE_FIELD( filesDownloaded, FIELD_INTEGER ),
END_BYTESWAP_DATADESC()

struct playerblock_info_t : public player_info_t
{
	playerblock_info_t()
	{
		strncpy(name, "ERRORNAME", MAX_PLAYER_NAME_LENGTH);
		guid[0] = '\0';
		friendsID = 0;
		friendsName[0] = '\0';
		fakeplayer = true;
		ishltv = false;
		isreplay = false;
		for(int i = 0; i < MAX_CUSTOM_FILES; ++i) {
			customFiles[i] = 0;
		}
		filesDownloaded = 0;
		userID = PLAYER_BLOCK_USERID;
	}
};

static playerblock_info_t playerblock_info{};

void init_playerblock_userinfo()
{
	CByteswap byteswap;
	byteswap.SetTargetBigEndian( false );
	byteswap.SwapFieldsToTargetEndian( &playerblock_info );

	m_pUserInfoTable->SetStringUserData( PLAYER_BLOCK_CLIENT_IDX, sizeof(player_info_t), &playerblock_info );
}

void update_playerblock_userinfo(const char *name)
{
	strncpy(playerblock_info.name, name, MAX_PLAYER_NAME_LENGTH);

	CByteswap byteswap;
	byteswap.SetTargetBigEndian( false );
	byteswap.SwapFieldsToTargetEndian( &playerblock_info );

	m_pUserInfoTable->SetStringUserData( PLAYER_BLOCK_CLIENT_IDX, sizeof(player_info_t), &playerblock_info );
}

enum deathnotice_type_t
{
	deathnotice_none,
	deathnotice_npc_death,
	deathnotice_player_death,
	deathnotice_object_death
};

deathnotice_type_t deathnotice_type{deathnotice_none};

static void setup_playerblock_vars(CBaseEntity *pEntity);
static void clear_playerblock_vars();

CDetour *pObjectKilledDetour = nullptr;
int CBaseObjectKilled = -1;

DETOUR_DECL_MEMBER1(ObjectKilled, void, const CTakeDamageInfo &, info)
{
	CBaseEntity *pKiller{info.GetAttacker()};

	npc_type ntype{pKiller ? g_pNextBot->entity_to_npc_type(pKiller, pKiller->GetClassname()) : npc_none};
	if(ntype != npc_custom) {
		DETOUR_MEMBER_CALL(ObjectKilled)(info);
		return;
	}

	deathnotice_type = deathnotice_object_death;
	setup_playerblock_vars(pKiller);
	DETOUR_MEMBER_CALL(ObjectKilled)(info);
	clear_playerblock_vars();
	deathnotice_type = deathnotice_none;
}

void RemoveEntity(CBaseEntity *pEntity)
{
	servertools->RemoveEntity(pEntity);
}

void Sample::OnCoreMapStart(edict_t * pEdictList, int edictCount, int clientMax)
{
	CBaseEntity *pEntity = servertools->FindEntityByClassname(nullptr, "player_manager");
	if(pEntity) {
		player_manager_ref = gamehelpers->EntityToReference(pEntity);
	}

	g_pGameRulesProxyEntity = FindEntityByServerClassname(0, g_szGameRulesProxy);
	g_pGameRules = (CGameRules *)g_pSDKTools->GetGameRules();

	if(!gamerules_vtable_assigned) {
		if(g_pGameRules) {
			void **vtabl = *(void ***)g_pGameRules;

			SourceHook::SetMemAccess(vtabl, (CMultiplayRulesDeathNoticeOffset * sizeof(void *)) + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);

			vtabl[CGameRulesAdjustPlayerDamageTaken] = func_to_void(&GameRulesVTableHack::DetourAdjustPlayerDamageTaken);
			vtabl[CGameRulesAdjustPlayerDamageInflicted] = func_to_void(&GameRulesVTableHack::DetourAdjustPlayerDamageInflicted);
			vtabl[CGameRulesShouldUseRobustRadiusDamage] = func_to_void(&GameRulesVTableHack::DetourShouldUseRobustRadiusDamage);
			CGameRulesGetAmmoDamagePtr = vtabl[CGameRulesGetAmmoDamageOffset];
			vtabl[CGameRulesGetAmmoDamageOffset] = func_to_void(&GameRulesVTableHack::DetourGetAmmoDamage);
			CMultiplayRulesGetDeathScorerPtr = vtabl[CMultiplayRulesGetDeathScorerOffset];
			vtabl[CMultiplayRulesGetDeathScorerOffset] = func_to_void(&GameRulesVTableHack::DetourGetDeathScorer);
			CMultiplayRulesDeathNoticePtr = vtabl[CMultiplayRulesDeathNoticeOffset];
			vtabl[CMultiplayRulesDeathNoticeOffset] = func_to_void(&GameRulesVTableHack::DetourDeathNotice);

			gamerules_vtable_assigned = true;
		}
	}

	if(!pObjectKilledDetour) {
		CBaseEntity *pEntity = dictionary->FindFactory("obj_dispenser")->Create("obj_dispenser")->GetBaseEntity();

		void **vtabl = *(void ***)pEntity;

		RemoveEntity(pEntity);

		void *KilledPtr = vtabl[CBaseObjectKilled];

		pObjectKilledDetour = DETOUR_CREATE_MEMBER(ObjectKilled, KilledPtr)
		pObjectKilledDetour->EnableDetour();
	}

	{
		if(!player_block_vtable) {
			void **base_vtabl = *(void ***)g_pGameRulesProxyEntity;

			CBaseEntityGetNetworkable = base_vtabl[CBaseEntityGetNetworkableOffset];
			CBaseEntityEvent_KilledOtherPtr = base_vtabl[CBaseEntityEvent_KilledOtherOffset];

			player_block_vtable = new void *[CBasePlayerIsBot];

			player_block_vtable[CBaseEntityGetNetworkableOffset] = CBaseEntityGetNetworkable;
			player_block_vtable[CBaseEntityIsPlayer] = func_to_void(&PlayerBlockVTable::IsPlayer);
			player_block_vtable[CBaseEntityClassify] = func_to_void(&PlayerBlockVTable::Classify);
			player_block_vtable[CBasePlayerIsBot] = func_to_void(&PlayerBlockVTable::IsBot);
			player_block_vtable[CBaseEntityEvent_KilledOtherOffset] = func_to_void(&PlayerBlockVTable::Event_KilledOther);
		}

		if(!player_block) {
			player_block = engine->PvAllocEntPrivateData(player_size);
			*(void ***)player_block = player_block_vtable;
		}
	}

	m_pUserInfoTable = netstringtables->FindTable("userinfo");
	init_playerblock_userinfo();
}

edict_t *npc_edict{nullptr};

int hook_getuserid(const edict_t *e)
{
	if(!e) {
		RETURN_META_VALUE(MRES_IGNORED, 0);
	}

	if(npc_edict != nullptr && e == npc_edict) {
		RETURN_META_VALUE(MRES_SUPERCEDE, PLAYER_BLOCK_USERID);
	}

	int idx = gamehelpers->IndexOfEdict(const_cast<edict_t *>(e));
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(idx);

	npc_type ntype{pEntity ? g_pNextBot->entity_to_npc_type(pEntity, pEntity->GetClassname()) : npc_none};
	if(ntype == npc_custom) {
		RETURN_META_VALUE(MRES_SUPERCEDE, PLAYER_BLOCK_USERID);
	}

	RETURN_META_VALUE(MRES_IGNORED, 0);
}

const CSteamID *hook_getsteamid(edict_t *e)
{
	if(!e) {
		RETURN_META_VALUE(MRES_SUPERCEDE, nullptr);
	}

	if(npc_edict != nullptr && e == npc_edict) {
		RETURN_META_VALUE(MRES_SUPERCEDE, nullptr);
	}

	int idx = gamehelpers->IndexOfEdict(const_cast<edict_t *>(e));
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(idx);

	npc_type ntype{pEntity ? g_pNextBot->entity_to_npc_type(pEntity, pEntity->GetClassname()) : npc_none};
	if(ntype == npc_custom) {
		RETURN_META_VALUE(MRES_SUPERCEDE, nullptr);
	}

	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

void Sample::OnCoreMapEnd()
{
	g_pGameRulesProxyEntity = nullptr;
	player_manager_ref = INVALID_EHANDLE_INDEX;
}

static bool player_vtable_assgined{false};

void CTakeDamageInfo::AdjustPlayerDamageInflictedForSkillLevel()
{
	CopyDamageToBaseDamage();
	SetDamage( g_pGameRules->AdjustPlayerDamageInflicted(GetDamage()) );
}

void CTakeDamageInfo::AdjustPlayerDamageTakenForSkillLevel()
{
	CopyDamageToBaseDamage();
	g_pGameRules->AdjustPlayerDamageTaken(this);
}

class PlayerVTableHack : public CBasePlayer
{
public:
	int DetourOnTakeDamage( const CTakeDamageInfo &info )
	{
		// Modify the amount of damage the player takes, based on skill.
		CTakeDamageInfo playerDamage = info;

		bool override_result = false;
		int result_override = 0;

		auto it{player_callbackmap.find(gamehelpers->EntityToReference(this))};
		if(it != player_callbackmap.cend()) {
			META_RES res = it->second->SPOnTakeDamage(this, playerDamage, result_override, false);
			switch(res) {
				case MRES_IGNORED: break;
				case MRES_HANDLED: break;
				case MRES_OVERRIDE: override_result = true; break;
				case MRES_SUPERCEDE: return result_override;
			}
		}

		// Should we run this damage through the skill level adjustment?
		bool bAdjustForSkillLevel = true;

		if( info.GetDamageType() == DMG_GENERIC && info.GetAttacker() == this && info.GetInflictor() == this )
		{
			// Only do a skill level adjustment if the player isn't his own attacker AND inflictor.
			// This prevents damage from SetHealth() inputs from being adjusted for skill level.
			bAdjustForSkillLevel = false;
		}

		if( bAdjustForSkillLevel )
		{
			playerDamage.AdjustPlayerDamageTakenForSkillLevel();
		}

		int result = CBasePlayer::OnTakeDamage( playerDamage );

		if(it != player_callbackmap.cend()) {
			META_RES res = it->second->SPOnTakeDamage(this, playerDamage, result_override, true);
			switch(res) {
				case MRES_IGNORED: break;
				case MRES_HANDLED: break;
				case MRES_OVERRIDE: override_result = true; break;
				case MRES_SUPERCEDE: return result_override;
			}
		}

		if(override_result) {
			result = result_override;
		}

		return result;
	}
};

#define TF_BURNING_FLAME_LIFE		10.0
#define TF_BURNING_FLAME_LIFE_PYRO	0.25		// pyro only displays burning effect momentarily
#define TF_BURNING_FLAME_LIFE_FLARE 10.0
#define TF_BURNING_FLAME_LIFE_PLASMA 6.0

#define TF_DAMAGE_CRIT_MULTIPLIER			3.0f
#define TF_DAMAGE_MINICRIT_MULTIPLIER		1.35f

enum ETFWeaponType
{
	TF_WEAPON_NONE = 0,
	TF_WEAPON_BAT,
	TF_WEAPON_BAT_WOOD,
	TF_WEAPON_BOTTLE, 
	TF_WEAPON_FIREAXE,
	TF_WEAPON_CLUB,
	TF_WEAPON_CROWBAR,
	TF_WEAPON_KNIFE,
	TF_WEAPON_FISTS,
	TF_WEAPON_SHOVEL,
	TF_WEAPON_WRENCH,
	TF_WEAPON_BONESAW,
	TF_WEAPON_SHOTGUN_PRIMARY,
	TF_WEAPON_SHOTGUN_SOLDIER,
	TF_WEAPON_SHOTGUN_HWG,
	TF_WEAPON_SHOTGUN_PYRO,
	TF_WEAPON_SCATTERGUN,
	TF_WEAPON_SNIPERRIFLE,
	TF_WEAPON_MINIGUN,
	TF_WEAPON_SMG,
	TF_WEAPON_SYRINGEGUN_MEDIC,
	TF_WEAPON_TRANQ,
	TF_WEAPON_ROCKETLAUNCHER,
	TF_WEAPON_GRENADELAUNCHER,
	TF_WEAPON_PIPEBOMBLAUNCHER,
	TF_WEAPON_FLAMETHROWER,
	TF_WEAPON_GRENADE_NORMAL,
	TF_WEAPON_GRENADE_CONCUSSION,
	TF_WEAPON_GRENADE_NAIL,
	TF_WEAPON_GRENADE_MIRV,
	TF_WEAPON_GRENADE_MIRV_DEMOMAN,
	TF_WEAPON_GRENADE_NAPALM,
	TF_WEAPON_GRENADE_GAS,
	TF_WEAPON_GRENADE_EMP,
	TF_WEAPON_GRENADE_CALTROP,
	TF_WEAPON_GRENADE_PIPEBOMB,
	TF_WEAPON_GRENADE_SMOKE_BOMB,
	TF_WEAPON_GRENADE_HEAL,
	TF_WEAPON_GRENADE_STUNBALL,
	TF_WEAPON_GRENADE_JAR,
	TF_WEAPON_GRENADE_JAR_MILK,
	TF_WEAPON_PISTOL,
	TF_WEAPON_PISTOL_SCOUT,
	TF_WEAPON_REVOLVER,
	TF_WEAPON_NAILGUN,
	TF_WEAPON_PDA,
	TF_WEAPON_PDA_ENGINEER_BUILD,
	TF_WEAPON_PDA_ENGINEER_DESTROY,
	TF_WEAPON_PDA_SPY,
	TF_WEAPON_BUILDER,
	TF_WEAPON_MEDIGUN,
	TF_WEAPON_GRENADE_MIRVBOMB,
	TF_WEAPON_FLAMETHROWER_ROCKET,
	TF_WEAPON_GRENADE_DEMOMAN,
	TF_WEAPON_SENTRY_BULLET,
	TF_WEAPON_SENTRY_ROCKET,
	TF_WEAPON_DISPENSER,
	TF_WEAPON_INVIS,
	TF_WEAPON_FLAREGUN,
	TF_WEAPON_LUNCHBOX,
	TF_WEAPON_JAR,
	TF_WEAPON_COMPOUND_BOW,
	TF_WEAPON_BUFF_ITEM,
	TF_WEAPON_PUMPKIN_BOMB,
	TF_WEAPON_SWORD, 
	TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT,
	TF_WEAPON_LIFELINE,
	TF_WEAPON_LASER_POINTER,
	TF_WEAPON_DISPENSER_GUN,
	TF_WEAPON_SENTRY_REVENGE,
	TF_WEAPON_JAR_MILK,
	TF_WEAPON_HANDGUN_SCOUT_PRIMARY,
	TF_WEAPON_BAT_FISH,
	TF_WEAPON_CROSSBOW,
	TF_WEAPON_STICKBOMB,
	TF_WEAPON_HANDGUN_SCOUT_SECONDARY,
	TF_WEAPON_SODA_POPPER,
	TF_WEAPON_SNIPERRIFLE_DECAP,
	TF_WEAPON_RAYGUN,
	TF_WEAPON_PARTICLE_CANNON,
	TF_WEAPON_MECHANICAL_ARM,
	TF_WEAPON_DRG_POMSON,
	TF_WEAPON_BAT_GIFTWRAP,
	TF_WEAPON_GRENADE_ORNAMENT_BALL,
	TF_WEAPON_FLAREGUN_REVENGE,
	TF_WEAPON_PEP_BRAWLER_BLASTER,
	TF_WEAPON_CLEAVER,
	TF_WEAPON_GRENADE_CLEAVER,
	TF_WEAPON_STICKY_BALL_LAUNCHER,
	TF_WEAPON_GRENADE_STICKY_BALL,
	TF_WEAPON_SHOTGUN_BUILDING_RESCUE,
	TF_WEAPON_CANNON,
	TF_WEAPON_THROWABLE,
	TF_WEAPON_GRENADE_THROWABLE,
	TF_WEAPON_PDA_SPY_BUILD,
	TF_WEAPON_GRENADE_WATERBALLOON,
	TF_WEAPON_HARVESTER_SAW,
	TF_WEAPON_SPELLBOOK,
	TF_WEAPON_SPELLBOOK_PROJECTILE,
	TF_WEAPON_SNIPERRIFLE_CLASSIC,
	TF_WEAPON_PARACHUTE,
	TF_WEAPON_GRAPPLINGHOOK,
	TF_WEAPON_PASSTIME_GUN,
	TF_WEAPON_CHARGED_SMG,
	TF_WEAPON_COUNT
};

enum ETFDmgCustom
{
	TF_DMG_CUSTOM_NONE = 0,
	TF_DMG_CUSTOM_HEADSHOT,
	TF_DMG_CUSTOM_BACKSTAB,
	TF_DMG_CUSTOM_BURNING,
	TF_DMG_WRENCH_FIX,
	TF_DMG_CUSTOM_MINIGUN,
	TF_DMG_CUSTOM_SUICIDE,
	TF_DMG_CUSTOM_TAUNTATK_HADOUKEN,
	TF_DMG_CUSTOM_BURNING_FLARE,
	TF_DMG_CUSTOM_TAUNTATK_HIGH_NOON,
	TF_DMG_CUSTOM_TAUNTATK_GRAND_SLAM,
	TF_DMG_CUSTOM_PENETRATE_MY_TEAM,
	TF_DMG_CUSTOM_PENETRATE_ALL_PLAYERS,
	TF_DMG_CUSTOM_TAUNTATK_FENCING,
	TF_DMG_CUSTOM_PENETRATE_NONBURNING_TEAMMATE,
	TF_DMG_CUSTOM_TAUNTATK_ARROW_STAB,
	TF_DMG_CUSTOM_TELEFRAG,
	TF_DMG_CUSTOM_BURNING_ARROW,
	TF_DMG_CUSTOM_FLYINGBURN,
	TF_DMG_CUSTOM_PUMPKIN_BOMB,
	TF_DMG_CUSTOM_DECAPITATION,
	TF_DMG_CUSTOM_TAUNTATK_GRENADE,
	TF_DMG_CUSTOM_BASEBALL,
	TF_DMG_CUSTOM_CHARGE_IMPACT,
	TF_DMG_CUSTOM_TAUNTATK_BARBARIAN_SWING,
	TF_DMG_CUSTOM_AIR_STICKY_BURST,
	TF_DMG_CUSTOM_DEFENSIVE_STICKY,
	TF_DMG_CUSTOM_PICKAXE,
	TF_DMG_CUSTOM_ROCKET_DIRECTHIT,
	TF_DMG_CUSTOM_TAUNTATK_UBERSLICE,
	TF_DMG_CUSTOM_PLAYER_SENTRY,
	TF_DMG_CUSTOM_STANDARD_STICKY,
	TF_DMG_CUSTOM_SHOTGUN_REVENGE_CRIT,
	TF_DMG_CUSTOM_TAUNTATK_ENGINEER_GUITAR_SMASH,
	TF_DMG_CUSTOM_BLEEDING,
	TF_DMG_CUSTOM_GOLD_WRENCH,
	TF_DMG_CUSTOM_CARRIED_BUILDING,
	TF_DMG_CUSTOM_COMBO_PUNCH,
	TF_DMG_CUSTOM_TAUNTATK_ENGINEER_ARM_KILL,
	TF_DMG_CUSTOM_FISH_KILL,
	TF_DMG_CUSTOM_TRIGGER_HURT,
	TF_DMG_CUSTOM_DECAPITATION_BOSS,
	TF_DMG_CUSTOM_STICKBOMB_EXPLOSION,
	TF_DMG_CUSTOM_AEGIS_ROUND,
	TF_DMG_CUSTOM_FLARE_EXPLOSION,
	TF_DMG_CUSTOM_BOOTS_STOMP,
	TF_DMG_CUSTOM_PLASMA,
	TF_DMG_CUSTOM_PLASMA_CHARGED,
	TF_DMG_CUSTOM_PLASMA_GIB,
	TF_DMG_CUSTOM_PRACTICE_STICKY,
	TF_DMG_CUSTOM_EYEBALL_ROCKET,
	TF_DMG_CUSTOM_HEADSHOT_DECAPITATION,
	TF_DMG_CUSTOM_TAUNTATK_ARMAGEDDON,
	TF_DMG_CUSTOM_FLARE_PELLET,
	TF_DMG_CUSTOM_CLEAVER,
	TF_DMG_CUSTOM_CLEAVER_CRIT,
	TF_DMG_CUSTOM_SAPPER_RECORDER_DEATH,
	TF_DMG_CUSTOM_MERASMUS_PLAYER_BOMB,
	TF_DMG_CUSTOM_MERASMUS_GRENADE,
	TF_DMG_CUSTOM_MERASMUS_ZAP,
	TF_DMG_CUSTOM_MERASMUS_DECAPITATION,
	TF_DMG_CUSTOM_CANNONBALL_PUSH,
	TF_DMG_CUSTOM_TAUNTATK_ALLCLASS_GUITAR_RIFF,
	TF_DMG_CUSTOM_THROWABLE,
	TF_DMG_CUSTOM_THROWABLE_KILL,
	TF_DMG_CUSTOM_SPELL_TELEPORT,
	TF_DMG_CUSTOM_SPELL_SKELETON,
	TF_DMG_CUSTOM_SPELL_MIRV,
	TF_DMG_CUSTOM_SPELL_METEOR,
	TF_DMG_CUSTOM_SPELL_LIGHTNING,
	TF_DMG_CUSTOM_SPELL_FIREBALL,
	TF_DMG_CUSTOM_SPELL_MONOCULUS,
	TF_DMG_CUSTOM_SPELL_BLASTJUMP,
	TF_DMG_CUSTOM_SPELL_BATS,
	TF_DMG_CUSTOM_SPELL_TINY,
	TF_DMG_CUSTOM_KART,
	TF_DMG_CUSTOM_GIANT_HAMMER,
	TF_DMG_CUSTOM_RUNE_REFLECT,
	TF_DMG_CUSTOM_END // END
};

ConVar *tf_flamethrower_boxsize = nullptr;

void NPCDeathNotice(CBaseEntity *pVictim, const CTakeDamageInfo &info, const char *eventName);

ConVar tf_npc_dmg_mult_sentry( "tf_npc_dmg_mult_sentry", "0.5" );
ConVar tf_npc_dmg_mult_sniper( "tf_npc_dmg_mult_sniper", "2.0" );
ConVar tf_npc_dmg_mult_arrow( "tf_npc_dmg_mult_arrow", "3.0" );
ConVar tf_npc_dmg_mult_minigun( "tf_npc_dmg_mult_minigun", "0.25" );
ConVar tf_npc_dmg_mult_flamethrower( "tf_npc_dmg_mult_flamethrower", "0.5" );
ConVar tf_npc_dmg_mult_scattergun( "tf_npc_dmg_mult_scattergun", "2.0" );
ConVar tf_npc_dmg_mult_knife( "tf_npc_dmg_mult_knife", "3.0" );
ConVar tf_npc_dmg_mult_revolver( "tf_npc_dmg_mult_revolver", "2.0" );
ConVar tf_npc_dmg_mult_handgun( "tf_npc_dmg_mult_handgun", "1.75" );
ConVar tf_npc_dmg_mult_sodapopper( "tf_npc_dmg_mult_sodapopper", "1.5" );
ConVar tf_npc_dmg_mult_brawlerblaster( "tf_npc_dmg_mult_brawlerblaster", "3.0" );
ConVar tf_npc_dmg_mult_grenade( "tf_bot_npc_dmg_mult_grenade", "2.0" );

void ModifyDamage( CTakeDamageInfo *info )
{
	CBaseEntity *pInflictor = info->GetInflictor();
	CTFWeaponBase *pWeapon = (CTFWeaponBase *)info->GetWeapon();

	const char *inflictor_classname = pInflictor->GetClassname();
	//TODO!!!! better way to do this
	if(pInflictor && (strcmp(inflictor_classname, "obj_sentrygun") == 0 || strcmp(inflictor_classname, "tf_projectile_sentryrocket") == 0)) {
		info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_sentry.GetFloat() );
	} else if(pWeapon) {
		switch( pWeapon->GetWeaponID() )
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_sniper.GetFloat() );
			break;
		case TF_WEAPON_COMPOUND_BOW:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_arrow.GetFloat() );
			break;
		case TF_WEAPON_MINIGUN:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_minigun.GetFloat() );
			break;
		case TF_WEAPON_FLAMETHROWER:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_flamethrower.GetFloat() );
			break;
		case TF_WEAPON_KNIFE:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_knife.GetFloat() );
			break;
		case TF_WEAPON_SODA_POPPER:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_sodapopper.GetFloat() );
			break;
		case TF_WEAPON_PEP_BRAWLER_BLASTER:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_brawlerblaster.GetFloat() );
			break;
		case TF_WEAPON_SCATTERGUN:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_scattergun.GetFloat() );
			break;
		case TF_WEAPON_REVOLVER:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_revolver.GetFloat() );
			break;
		case TF_WEAPON_HANDGUN_SCOUT_PRIMARY:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_handgun.GetFloat() );
			break;
		case TF_WEAPON_SENTRY_BULLET:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_sentry.GetFloat() );
			break;
		case TF_WEAPON_GRENADE_DEMOMAN:
			info->SetDamage( info->GetDamage() * tf_npc_dmg_mult_grenade.GetFloat() );
			break;
		}
	}
}

int hook_npc_takedamage( const CTakeDamageInfo &rawInfo )
{
	CBaseEntity *pThis = META_IFACEPTR(CBaseEntity);

	CTakeDamageInfo info = rawInfo;

	bool override_result = false;
	int result_override = 0;

	auto it{npc_callbackmap.find(gamehelpers->EntityToReference(pThis))};
	if(it != npc_callbackmap.cend()) {
		META_RES res = it->second->SPOnTakeDamage(pThis, info, result_override, false);
		switch(res) {
			case MRES_IGNORED: break;
			case MRES_HANDLED: break;
			case MRES_OVERRIDE: override_result = true; break;
			case MRES_SUPERCEDE: return result_override;
		}
	}

#if SOURCE_ENGINE == SE_TF2
	if ( g_pGameRules )
	{
		if(!((CTFGameRules *)g_pGameRules)->ApplyOnDamageModifyRules( info, pThis, true )) {
			RETURN_META_VALUE(MRES_SUPERCEDE, 0);
		}
	}

	CTFPlayer *attackerPlayer = (CTFPlayer *)info.GetAttacker()->IsPlayer();
	CTFWeaponBase *attackerWeapon = attackerPlayer ? attackerPlayer->GetActiveTFWeapon() : nullptr;

	// On damage Rage
	// Give the soldier/pyro some rage points for dealing/taking damage.
	if ( info.GetDamage() && info.GetAttacker() != pThis )
	{
		// Buff flag 1: we get rage when we deal damage. Here, that means the soldier that attacked
		// gets rage when we take damage.
		HandleRageGain( attackerPlayer, kRageBuffFlag_OnDamageDealt, info.GetDamage(), 6.0f );

		// Buff 5: our pyro attacker get rage when we're damaged by fire
		if ( ( info.GetDamageType() & DMG_BURN ) != 0 || ( info.GetDamageType() & DMG_PLASMA ) != 0 )
		{
			HandleRageGain( attackerPlayer, kRageBuffFlag_OnBurnDamageDealt, info.GetDamage(), 30.f );
		}

		if ( attackerPlayer && info.GetWeapon() )
		{
			CTFWeaponBase *pWeapon = (CTFWeaponBase *)info.GetWeapon();
			if ( pWeapon )
			{
				pWeapon->ApplyOnHitAttributes( pThis, attackerPlayer, info );
			}
		}
	}
#endif

	int result = SH_MCALL(pThis, OnTakeDamage)( info );

	if(it != npc_callbackmap.cend()) {
		META_RES res = it->second->SPOnTakeDamage(pThis, info, result_override, false);
		switch(res) {
			case MRES_IGNORED: break;
			case MRES_HANDLED: break;
			case MRES_OVERRIDE: override_result = true; break;
			case MRES_SUPERCEDE: return result_override;
		}
	}

	if(override_result) {
		result = result_override;
	}

#if SOURCE_ENGINE == SE_TF2
	if(info.GetDamageType() & DMG_BURN) {
		float flFlameLifetime = TF_BURNING_FLAME_LIFE;

		if(attackerWeapon) {
			switch(attackerWeapon->GetWeaponID()) {
				case TF_WEAPON_FLAREGUN:
				flFlameLifetime = TF_BURNING_FLAME_LIFE_FLARE; break;
				case TF_WEAPON_PARTICLE_CANNON:
				flFlameLifetime = TF_BURNING_FLAME_LIFE_PLASMA; break;
			}
		}

		((NextBotCombatCharacter *)pThis)->Ignite(flFlameLifetime, info.GetAttacker());
	}
#endif

	RETURN_META_VALUE(MRES_SUPERCEDE, result);
}

float GetCritInjuryMultiplier()
{
	return TF_DAMAGE_CRIT_MULTIPLIER;
}

int hook_npc_takedamagealive( const CTakeDamageInfo &rawInfo )
{
	CBaseEntity *pThis = META_IFACEPTR(CBaseEntity);

	CTakeDamageInfo info = rawInfo;

	bool override_result = false;
	int result_override = 0;

	auto it{npc_callbackmap.find(gamehelpers->EntityToReference(pThis))};
	if(it != npc_callbackmap.cend()) {
		META_RES res = it->second->SPOnTakeDamageAlive(pThis, info, result_override, false);
		switch(res) {
			case MRES_IGNORED: break;
			case MRES_HANDLED: break;
			case MRES_OVERRIDE: override_result = true; break;
			case MRES_SUPERCEDE: return result_override;
		}
	}

	if ( !rawInfo.GetAttacker() || rawInfo.GetAttacker()->GetTeamNumber() == pThis->GetTeamNumber() )
	{
		// no friendly fire damage
		RETURN_META_VALUE(MRES_SUPERCEDE, override_result ? result_override : 0);
	}

	// weapon-specific damage modification
	ModifyDamage( &info );

	if ( info.GetDamageType() & DMG_CRITICAL )
	{
		// do the critical damage increase
		info.SetDamage( info.GetDamage() * GetCritInjuryMultiplier() );
	}

	CTFPlayer *attackerPlayer = (CTFPlayer *)info.GetAttacker()->IsPlayer();
	CTFWeaponBase *attackerWeapon = attackerPlayer ? attackerPlayer->GetActiveTFWeapon() : nullptr;

#if SOURCE_ENGINE == SE_TF2
	DamageModifyExtras_t outParams{};

	if ( g_pGameRules )
	{
		float realDamage = ((CTFGameRules *)g_pGameRules)->ApplyOnDamageAliveModifyRules( info, pThis, outParams );
		if(realDamage == -1.0f) {
			RETURN_META_VALUE(MRES_SUPERCEDE, override_result ? result_override : 0);
		}

		info.SetDamage( realDamage );
	}
#endif

	int result = SH_MCALL(pThis, OnTakeDamageAlive)( info );

	if(it != npc_callbackmap.cend()) {
		META_RES res = it->second->SPOnTakeDamageAlive(pThis, info, result_override, true);
		switch(res) {
			case MRES_IGNORED: break;
			case MRES_HANDLED: break;
			case MRES_OVERRIDE: override_result = true; break;
			case MRES_SUPERCEDE: return result_override;
		}
	}

	if(override_result) {
		result = result_override;
	}

	// fire event for client combat text, beep, etc.
	IGameEvent *event = gameeventmanager->CreateEvent( "npc_hurt" );
	if ( event )
	{
		event->SetInt( "entindex", pThis->entindex() );
		event->SetInt( "health", MAX( 0, pThis->GetHealth() ) );
		event->SetInt( "damageamount", info.GetDamage() );
		event->SetBool( "crit", ( info.GetDamageType() & DMG_CRITICAL ) ? true : false );

		if ( attackerPlayer )
		{
			event->SetInt( "attacker_player", attackerPlayer->GetUserID() );

			if ( attackerWeapon )
			{
				event->SetInt( "weaponid", attackerWeapon->GetWeaponID() );
			}
			else
			{
				event->SetInt( "weaponid", 0 );
			}
		}
		else
		{
			// hurt by world
			event->SetInt( "attacker_player", 0 );
			event->SetInt( "weaponid", 0 );
		}

		event->SetInt( "boss", ((CBaseCombatCharacter *)pThis)->GetBossType() );

		gameeventmanager->FireEvent( event );
	}

#if SOURCE_ENGINE == SE_TF2
	if(outParams.bIgniting || (info.GetDamageType() & DMG_BURN)) {
		float flFlameLifetime = TF_BURNING_FLAME_LIFE;

		if(attackerWeapon) {
			switch(attackerWeapon->GetWeaponID()) {
				case TF_WEAPON_FLAREGUN:
				flFlameLifetime = TF_BURNING_FLAME_LIFE_FLARE; break;
				case TF_WEAPON_PARTICLE_CANNON:
				flFlameLifetime = TF_BURNING_FLAME_LIFE_PLASMA; break;
			}
		}

		((NextBotCombatCharacter *)pThis)->Ignite(flFlameLifetime, info.GetAttacker());
	}
#endif

#if SOURCE_ENGINE == SE_TF2
	// Let attacker react to the damage they dealt
	CTFPlayer *pAttacker = (CTFPlayer *)rawInfo.GetAttacker()->IsPlayer();
	if ( pAttacker )
	{
		pAttacker->OnDealtDamage( (CBaseCombatCharacter *)pThis, info );
	}
#endif

	if(attackerWeapon && attackerWeapon->GetWeaponID() == TF_WEAPON_BAT_FISH)
	{
		if(pThis->GetHealth() > 0) {
			NPCDeathNotice(pThis, info, "fish_notice");
		}
	}

	RETURN_META_VALUE(MRES_SUPERCEDE, result);
}

struct npc_death_notice_info_t
{
	bool sending_connected{false};
	float connected_time{0.0f};

	int m_bConnected_offset{-1};
	int m_bValid_offset{-1};

	bool sending_team{false};
	int team{0};
	float team_time{0.0f};

	int m_iTeam_offset{-1};

	bool sending_name{false};
	float name_time{0.0f};

	std::string name{"ERRORNAME"s};

	float event_time{0.0f};
	bool sending_event{false};
	bool in_fire_event{false};
};

npc_death_notice_info_t npc_death_notice_info{};

SendVarProxyFn real_proxy_connected{nullptr};
static void fake_proxy_connected(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID)
{
	if(npc_death_notice_info.sending_connected) {
		int connected{1};
		real_proxy_connected(pProp, pStructBase, &connected, pOut, iElement, objectID);
	} else {
		real_proxy_connected(pProp, pStructBase, pData, pOut, iElement, objectID);
	}
}

SendVarProxyFn real_proxy_valid{nullptr};
static void fake_proxy_valid(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID)
{
	if(npc_death_notice_info.sending_connected) {
		int valid{1};
		real_proxy_valid(pProp, pStructBase, &valid, pOut, iElement, objectID);
	} else {
		real_proxy_valid(pProp, pStructBase, pData, pOut, iElement, objectID);
	}
}

SendVarProxyFn real_proxy_team{nullptr};
static void fake_proxy_team(const SendProp *pProp, const void *pStructBase, const void *pData, DVariant *pOut, int iElement, int objectID)
{
	if(npc_death_notice_info.sending_team) {
		real_proxy_team(pProp, pStructBase, &npc_death_notice_info.team, pOut, iElement, objectID);
	} else {
		real_proxy_team(pProp, pStructBase, pData, pOut, iElement, objectID);
	}
}

struct playerdeath_event_t
{
	char data[MAX_EVENT_BYTES]{0};
	bool dont_broadcast{false};
};

static std::stack<playerdeath_event_t> npc_death_events{};

void send_npc_death_events()
{
	while(!npc_death_events.empty()) {
		playerdeath_event_t &npc_event{npc_death_events.top()};

		bf_read read{};
		read.StartReading(npc_event.data, MAX_EVENT_BYTES);

		IGameEvent *event{gameeventmanager->UnserializeEvent(&read)};

		npc_death_notice_info.in_fire_event = true;
		gameeventmanager->FireEvent(event, npc_event.dont_broadcast);
		npc_death_notice_info.in_fire_event = false;

		npc_death_events.pop();
	}
}

void game_frame(bool simulating)
{
	if(!simulating) {
		return;
	}

	if(npc_death_notice_info.sending_name) {
		if(npc_death_notice_info.name_time < gpGlobals->curtime) {
			npc_death_notice_info.sending_name = false;
			npc_death_notice_info.name_time = 0.0f;
			npc_death_notice_info.name = "ERRORNAME"s;
		}

		update_playerblock_userinfo(npc_death_notice_info.name.c_str());
	}

	if(npc_death_notice_info.sending_team) {
		if(npc_death_notice_info.team_time < gpGlobals->curtime) {
			npc_death_notice_info.sending_team = false;
			npc_death_notice_info.team_time = 0.0f;
			npc_death_notice_info.team = 0;
		}

		CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(player_manager_ref);
		if(pEntity) {
			SetEdictStateChanged(pEntity, npc_death_notice_info.m_iTeam_offset);
		}
	}

	if(npc_death_notice_info.sending_connected) {
		if(npc_death_notice_info.connected_time < gpGlobals->curtime) {
			npc_death_notice_info.sending_connected = false;
			npc_death_notice_info.connected_time = 0.0f;
		}

		CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(player_manager_ref);
		if(pEntity) {
			SetEdictStateChanged(pEntity, npc_death_notice_info.m_bConnected_offset);
			SetEdictStateChanged(pEntity, npc_death_notice_info.m_bValid_offset);
		}
	}

	if(npc_death_notice_info.sending_event) {
		if(npc_death_notice_info.event_time < gpGlobals->curtime) {
			npc_death_notice_info.sending_event = false;
			npc_death_notice_info.event_time = 0.0f;
			send_npc_death_events();
		}
	}
}

ConVar npc_deathnotice_eventtime( "npc_deathnotice_eventtime", "0.3" );
ConVar npc_deathnotice_connecttime( "npc_deathnotice_connecttime", "0.1" );

static void setup_playerblock_vars(CBaseEntity *pEntity)
{
	const char *name{pEntity->GetName()};
	if(!name || name[0] == '\0') {
		name = pEntity->GetClassname();
	}

	update_playerblock_userinfo(name);

	npc_death_notice_info.sending_event = true;
	npc_death_notice_info.event_time = gpGlobals->curtime + npc_deathnotice_eventtime.GetFloat();

	npc_death_notice_info.sending_connected = true;
	npc_death_notice_info.connected_time = npc_death_notice_info.event_time + npc_deathnotice_connecttime.GetFloat();

	int team = pEntity->GetTeamNumber();

	npc_death_notice_info.sending_team = true;
	npc_death_notice_info.team_time = npc_death_notice_info.connected_time;
	npc_death_notice_info.team = team;

	npc_death_notice_info.sending_name = true;
	npc_death_notice_info.name_time = npc_death_notice_info.connected_time;
	npc_death_notice_info.name = name;

	CBaseEntity *pPlayerManager = gamehelpers->ReferenceToEntity(player_manager_ref);
	if(pPlayerManager) {
		SetEdictStateChanged(pPlayerManager, npc_death_notice_info.m_bConnected_offset);
		SetEdictStateChanged(pPlayerManager, npc_death_notice_info.m_bValid_offset);
		SetEdictStateChanged(pPlayerManager, npc_death_notice_info.m_iTeam_offset);
	}

	CServerNetworkProperty *m_Network = (CServerNetworkProperty *)((CBaseEntity *)player_block)->GetNetworkable();

	((CBaseEntity *)player_block)->SetTeamNumber_nonetwork(team);
	npc_ent = pEntity;
	npc_edict = pEntity->edict();
	m_Network->SetEdict(npc_edict);
}

static void clear_playerblock_vars()
{
	CServerNetworkProperty *m_Network = (CServerNetworkProperty *)((CBaseEntity *)player_block)->GetNetworkable();

	m_Network->SetEdict(nullptr);
	npc_edict = nullptr;
	npc_ent = nullptr;
	((CBaseEntity *)player_block)->SetTeamNumber_nonetwork(0);
}

CBasePlayer *GameRulesVTableHack::DetourGetDeathScorer( CBaseEntity *pKiller, CBaseEntity *pInflictor )
{
	CBasePlayer *pPlayer{CTFGameRules::GetDeathScorer(pKiller, pInflictor)};
	if(pPlayer) {
		return pPlayer;
	}

	if(pKiller && deathnotice_type != deathnotice_none) {
		if(npc_ent != nullptr && npc_ent == pKiller) {
			return ((CBasePlayer *)player_block);
		}

		npc_type ntype{g_pNextBot->entity_to_npc_type(pKiller, pKiller->GetClassname())};
		if(ntype == npc_custom) {
			return ((CBasePlayer *)player_block);
		}
	}

	return nullptr;
}

bool hook_fireevent(IGameEvent *event, bool bDontBroadcast)
{
	if(npc_death_notice_info.in_fire_event ||
		(deathnotice_type == deathnotice_none)) {
		RETURN_META_VALUE(MRES_IGNORED, false);
	}

	IGameEventManager2 *pThis = META_IFACEPTR(IGameEventManager2);

	playerdeath_event_t &npc_event{npc_death_events.emplace()};
	npc_event.dont_broadcast = bDontBroadcast;

	bf_write write{};
	write.StartWriting(npc_event.data, MAX_EVENT_BYTES);

	if(pThis->SerializeEvent(event, &write)) {
		pThis->FreeEvent(event);
		RETURN_META_VALUE(MRES_SUPERCEDE, true);
	} else {
		npc_death_events.pop();
		RETURN_META_VALUE(MRES_HANDLED, false);
	}
}

void GameRulesVTableHack::DetourDeathNotice(CBasePlayer *pVictim, const CTakeDamageInfo &info)
{
	CBaseEntity *pKiller{info.GetAttacker()};

	npc_type ntype{pKiller ? g_pNextBot->entity_to_npc_type(pKiller, pKiller->GetClassname()) : npc_none};
	if(ntype != npc_custom) {
		CMultiplayRules::DeathNotice(pVictim, info);
		return;
	}

	deathnotice_type = deathnotice_player_death;
	setup_playerblock_vars(pKiller);
	CMultiplayRules::DeathNotice(pVictim, info);
	clear_playerblock_vars();
	deathnotice_type = deathnotice_none;
}

void NPCDeathNotice(CBaseEntity *pVictim, const CTakeDamageInfo &info, const char *eventName)
{
	deathnotice_type = deathnotice_npc_death;
	setup_playerblock_vars(pVictim);
	if(eventName) {
		((CTFGameRules *)g_pGameRules)->DeathNotice((CBasePlayer *)player_block, info, eventName);
	} else {
		((CMultiplayRules *)g_pGameRules)->DeathNotice((CBasePlayer *)player_block, info);
	}
	clear_playerblock_vars();
	deathnotice_type = deathnotice_none;
}

void hook_npc_killed(const CTakeDamageInfo &info)
{
	CBaseEntity *pThis = META_IFACEPTR(CBaseEntity);

	CTakeDamageInfo info_copy = info;

	CTFPlayer *attackerPlayer = (CTFPlayer *)info.GetAttacker()->IsPlayer();
	CTFWeaponBase *attackerWeapon = attackerPlayer ? attackerPlayer->GetActiveTFWeapon() : nullptr;

	const char *eventName{nullptr};

	if(attackerWeapon && attackerWeapon->GetWeaponID() == TF_WEAPON_BAT_FISH)
	{
		info_copy.SetDamageCustom( TF_DMG_CUSTOM_FISH_KILL );

		eventName = "fish_notice";
	}

	auto it{npc_callbackmap.find(gamehelpers->EntityToReference(pThis))};
	if(it != npc_callbackmap.cend()) {
		META_RES res = it->second->SPEvent_Killed(pThis, info_copy, false);
		switch(res) {
			case MRES_IGNORED: break;
			case MRES_HANDLED: break;
			case MRES_OVERRIDE: break;
			case MRES_SUPERCEDE: return;
		}
	}

	NPCDeathNotice(pThis, info_copy, eventName);

	SH_MCALL(pThis, Event_Killed)(info_copy);

	if(it != npc_callbackmap.cend()) {
		META_RES res = it->second->SPEvent_Killed(pThis, info_copy, true);
		switch(res) {
			case MRES_IGNORED: break;
			case MRES_HANDLED: break;
			case MRES_OVERRIDE: break;
			case MRES_SUPERCEDE: return;
		}
	}

	RETURN_META(MRES_SUPERCEDE);
}

void hook_npc_dtor()
{
	CBaseEntity *pThis = META_IFACEPTR(CBaseEntity);

	SH_REMOVE_MANUALHOOK(GenericDtor, pThis, SH_STATIC(hook_npc_dtor), false);
	SH_REMOVE_MANUALHOOK(OnTakeDamage, pThis, SH_STATIC(hook_npc_takedamage), false);
	SH_REMOVE_MANUALHOOK(OnTakeDamageAlive, pThis, SH_STATIC(hook_npc_takedamagealive), false);
	SH_REMOVE_MANUALHOOK(Event_Killed, pThis, SH_STATIC(hook_npc_killed), false);
}

void Sample::OnEntityCreated(CBaseEntity *pEntity, const char *classname_ptr)
{
	std::string classname{classname_ptr};

	if(classname == "player"sv) {
		if(!player_vtable_assgined) {
			void **vtabl = *(void ***)pEntity;

			SourceHook::SetMemAccess(vtabl, (CGameRulesAdjustPlayerDamageTaken * sizeof(void *)) + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);

			CBasePlayerOnTakeDamagePtr = vtabl[CBaseEntityOnTakeDamageOffset];
			vtabl[CBaseEntityOnTakeDamageOffset] = func_to_void(&PlayerVTableHack::DetourOnTakeDamage);

			player_vtable_assgined = true;
		}
	} else {
		npc_type ntype{g_pNextBot->entity_to_npc_type(pEntity, classname)};
		if(ntype == npc_custom) {
			SH_ADD_MANUALHOOK(GenericDtor, pEntity, SH_STATIC(hook_npc_dtor), false);
			SH_ADD_MANUALHOOK(OnTakeDamage, pEntity, SH_STATIC(hook_npc_takedamage), false);
			SH_ADD_MANUALHOOK(OnTakeDamageAlive, pEntity, SH_STATIC(hook_npc_takedamagealive), false);
			SH_ADD_MANUALHOOK(Event_Killed, pEntity, SH_STATIC(hook_npc_killed), false);
		}
	}
}

void Sample::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
	SM_GET_LATE_IFACE(SDKHOOKS, g_pSDKHooks);
#ifdef __HAS_WPNHACK
	SM_GET_LATE_IFACE(WPNHACK, g_pWpnHack);
#endif
#ifdef __HAS_WPNHACK
	SM_GET_LATE_IFACE(NEXTBOT, g_pNextBot);
#endif

	g_pSDKHooks->AddEntityListener(this);
}

bool Sample::QueryRunning(char *error, size_t maxlength)
{
	SM_CHECK_IFACE(SDKHOOKS, g_pSDKHooks);
	return true;
}

bool Sample::QueryInterfaceDrop(SMInterface *pInterface)
{
	if(pInterface == g_pSDKHooks)
		return false;
#ifdef __HAS_WPNHACK
	else if(pInterface == g_pWpnHack)
		return false;
#endif
#ifdef __HAS_NEXTBOT
	else if(pInterface == g_pNextBot)
		return false;
#endif
	return IExtensionInterface::QueryInterfaceDrop(pInterface);
}

void Sample::NotifyInterfaceDrop(SMInterface *pInterface)
{
	if(strcmp(pInterface->GetInterfaceName(), SMINTERFACE_SDKHOOKS_NAME) == 0)
	{
		g_pSDKHooks->RemoveEntityListener(this);
		g_pSDKHooks = NULL;
	}
#ifdef __HAS_WPNHACK
	else if(strcmp(pInterface->GetInterfaceName(), SMINTERFACE_WPNHACK_NAME) == 0)
	{
		g_pWpnHack = NULL;
	}
#endif
#ifdef __HAS_NEXTBOT
	else if(strcmp(pInterface->GetInterfaceName(), SMINTERFACE_NEXTBOT_NAME) == 0)
	{
		g_pNextBot = NULL;
	}
#endif
}

IGameConfig *g_pGameConf = nullptr;

CDetour *pSW_GameStats_WriteKill = nullptr;

DETOUR_DECL_MEMBER5(SW_GameStats_WriteKill, void, CTFPlayer*, pKiller, CTFPlayer*, pVictim, CTFPlayer*, pAssister, IGameEvent*, event, const CTakeDamageInfo &,info )
{
	if(deathnotice_type != deathnotice_none) {
		return;
	}

	if(pKiller) {
		if(npc_ent != nullptr && npc_ent == pKiller) {
			return;
		}

		npc_type ntype{g_pNextBot->entity_to_npc_type(pKiller, pKiller->GetClassname())};
		if(ntype == npc_custom) {
			return;
		}
	}

	DETOUR_MEMBER_CALL(SW_GameStats_WriteKill)(pKiller, pVictim, pAssister, event, info);
}

SH_DECL_HOOK2(IGameEventManager2, FireEvent, SH_NOATTRIB, 0, bool, IGameEvent *, bool)

IUniformRandomStream *random{nullptr};

bool Sample::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	gpGlobals = ismm->GetCGlobals();
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
	GET_V_IFACE_CURRENT(GetEngineFactory, cvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, gameeventmanager, IGameEventManager2, INTERFACEVERSION_GAMEEVENTSMANAGER2);
	GET_V_IFACE_CURRENT(GetServerFactory, servertools, IServerTools, VSERVERTOOLS_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, netstringtables, INetworkStringTableContainer, INTERFACENAME_NETWORKSTRINGTABLESERVER)
	GET_V_IFACE_ANY(GetEngineFactory, random, IUniformRandomStream, VENGINE_SERVER_RANDOM_INTERFACE_VERSION)
	g_pCVar = cvar;
	ConVar_Register(0, this);

	tf_flamethrower_boxsize = g_pCVar->FindVar("tf_flamethrower_boxsize");

	phys_pushscale = g_pCVar->FindVar("phys_pushscale");

	SH_ADD_HOOK(IVEngineServer, GetPlayerUserId, engine, SH_STATIC(hook_getuserid), false);
	SH_ADD_HOOK(IVEngineServer, GetClientSteamID, engine, SH_STATIC(hook_getsteamid), false);
	SH_ADD_HOOK(IGameEventManager2, FireEvent, gameeventmanager, SH_STATIC(hook_fireevent), false);

	dictionary = servertools->GetEntityFactoryDictionary();
	player_size = dictionary->FindFactory("player")->GetEntitySize();

	m_pUserInfoTable = netstringtables->FindTable("userinfo");

	skill = g_pCVar->FindVar("skill");
	skill->SetValue(SKILL_MEDIUM);

#if SOURCE_ENGINE == SE_TF2
	tf_mvm_skill = g_pCVar->FindVar("tf_mvm_skill");
	tf_mvm_skill->SetValue(3);
#endif

	skill->InstallChangeCallback(skill_changed);
#if SOURCE_ENGINE == SE_TF2
	tf_mvm_skill->InstallChangeCallback(mvm_skill_changed);
#endif

	return true;
}

bool Sample::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	gameconfs->LoadGameConfigFile("sdktools.games", &g_pGameConf, nullptr, 0);
	
	g_szGameRulesProxy = g_pGameConf->GetKeyValue("GameRulesProxy");
	
	gameconfs->CloseGameConfigFile(g_pGameConf);
	
	gameconfs->LoadGameConfigFile("damagerules", &g_pGameConf, nullptr, 0);

	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);

	pSW_GameStats_WriteKill = DETOUR_CREATE_MEMBER(SW_GameStats_WriteKill, "CTFGameStats::SW_GameStats_WriteKill")
	pSW_GameStats_WriteKill->EnableDetour();

	g_pGameConf->GetOffset("CBaseEntity::IsNPC", &CBaseEntityIsNPC);
	g_pGameConf->GetOffset("CGameRules::GetSkillLevel", &CGameRulesGetSkillLevel);
	g_pGameConf->GetOffset("CGameRules::AdjustPlayerDamageTaken", &CGameRulesAdjustPlayerDamageTaken);
	g_pGameConf->GetOffset("CGameRules::AdjustPlayerDamageInflicted", &CGameRulesAdjustPlayerDamageInflicted);
	g_pGameConf->GetOffset("CGameRules::ShouldUseRobustRadiusDamage", &CGameRulesShouldUseRobustRadiusDamage);
	g_pGameConf->GetOffset("CGameRules::GetAmmoDamage", &CGameRulesGetAmmoDamageOffset);

	g_pGameConf->GetOffset("CMultiplayRules::DeathNotice", &CMultiplayRulesDeathNoticeOffset);
	g_pGameConf->GetOffset("CMultiplayRules::GetDeathScorer", &CMultiplayRulesGetDeathScorerOffset);

	g_pGameConf->GetOffset("CTFGameRules::DeathNotice", &CTFGameRulesDeathNotice);

#if SOURCE_ENGINE == SE_TF2
	g_pGameConf->GetOffset("CTFWeaponBase::ApplyOnHitAttributes", &CTFWeaponBaseApplyOnHitAttributes);

	g_pGameConf->GetOffset("CTFWeaponBase::GetWeaponID", &CTFWeaponBaseGetWeaponID);

	g_pGameConf->GetOffset("CBaseObject::Killed", &CBaseObjectKilled);
#endif
	
	g_pGameConf->GetOffset("CBaseEntity::OnTakeDamage", &CBaseEntityOnTakeDamageOffset);
	SH_MANUALHOOK_RECONFIGURE(OnTakeDamage, CBaseEntityOnTakeDamageOffset, 0, 0);
	
	g_pGameConf->GetOffset("CBaseCombatCharacter::OnTakeDamage_Alive", &CBaseCombatCharacterOnTakeDamage_AliveOffset);
	SH_MANUALHOOK_RECONFIGURE(OnTakeDamageAlive, CBaseCombatCharacterOnTakeDamage_AliveOffset, 0, 0);
	
#if SOURCE_ENGINE == SE_TF2
	g_pGameConf->GetMemSig("HandleRageGain", &HandleRageGainPtr);
	g_pGameConf->GetMemSig("CTFPlayer::OnDealtDamage", &CTFPlayerOnDealtDamage);
	g_pGameConf->GetMemSig("CTFGameRules::ApplyOnDamageModifyRules", &CTFGameRulesApplyOnDamageModifyRules);
	g_pGameConf->GetMemSig("CTFGameRules::ApplyOnDamageAliveModifyRules", &CTFGameRulesApplyOnDamageAliveModifyRules);
	g_pGameConf->GetMemSig("CTFGameRules::PushAllPlayersAway", &CTFGameRulesPushAllPlayersAway);
#endif
	g_pGameConf->GetMemSig("CBaseEntity::TakeDamage", &CBaseEntityTakeDamage);

	g_pGameConf->GetOffset("CBaseAnimating::Ignite", &CBaseAnimatingIgnite);
	g_pGameConf->GetOffset("NextBotCombatCharacter::Ignite", &NextBotCombatCharacterIgnite);

#if SOURCE_ENGINE == SE_TF2
	g_pGameConf->GetOffset("CBaseCombatCharacter::GetBossType", &CBaseCombatCharacterGetBossType);
#endif

	g_pGameConf->GetOffset("CBaseEntity::MyCombatCharacterPointer", &CBaseEntityMyCombatCharacterPointer);

	int offset = -1;
	g_pGameConf->GetOffset("CBaseCombatCharacter::Event_Killed", &offset);
	SH_MANUALHOOK_RECONFIGURE(Event_Killed, offset, 0, 0);

	g_pGameConf->GetOffset("CBaseCombatCharacter::CheckTraceHullAttack(float)", &CBaseCombatCharacterCheckTraceHullAttackRange);
	g_pGameConf->GetOffset("CBaseCombatCharacter::CheckTraceHullAttack(Vector)", &CBaseCombatCharacterCheckTraceHullAttackEndPoint);

	g_pGameConf->GetMemSig("CBaseCombatCharacter::Event_Killed", &CBaseCombatCharacterEvent_Killed);

	g_pGameConf->GetOffset("CBaseEntity::IsPlayer", &CBaseEntityIsPlayer);
	g_pGameConf->GetOffset("CBaseEntity::Classify", &CBaseEntityClassify);
	g_pGameConf->GetOffset("CBasePlayer::IsBot", &CBasePlayerIsBot);
	g_pGameConf->GetOffset("CBaseEntity::Event_KilledOther", &CBaseEntityEvent_KilledOtherOffset);

	CBaseEntityGetNetworkableOffset = vfunc_index(&CBaseEntity::GetNetworkable);

	sm_sendprop_info_t info{};
	gamehelpers->FindSendPropInfo("CBaseEntity", "m_iTeamNum", &info);
	m_iTeamNumOffset = info.actual_offset;

	gamehelpers->FindSendPropInfo("CPlayerResource", "m_bConnected", &info);
	npc_death_notice_info.m_bConnected_offset = info.actual_offset;
	SendTable *pPropTable{info.prop->GetDataTable()};
	SendProp *pChildProp{pPropTable->GetProp(PLAYER_BLOCK_ENTITY_IDX)};
	real_proxy_connected = pChildProp->GetProxyFn();
	pChildProp->SetProxyFn(fake_proxy_connected);

	gamehelpers->FindSendPropInfo("CPlayerResource", "m_bValid", &info);
	npc_death_notice_info.m_bValid_offset = info.actual_offset;
	pPropTable = info.prop->GetDataTable();
	pChildProp = pPropTable->GetProp(PLAYER_BLOCK_ENTITY_IDX);
	real_proxy_valid = pChildProp->GetProxyFn();
	pChildProp->SetProxyFn(fake_proxy_valid);

	gamehelpers->FindSendPropInfo("CPlayerResource", "m_iTeam", &info);
	npc_death_notice_info.m_iTeam_offset = info.actual_offset;
	pPropTable = info.prop->GetDataTable();
	pChildProp = pPropTable->GetProp(PLAYER_BLOCK_ENTITY_IDX);
	real_proxy_team = pChildProp->GetProxyFn();
	pChildProp->SetProxyFn(fake_proxy_team);

	g_pEntityList = reinterpret_cast<CBaseEntityList *>(gamehelpers->GetGlobalEntityList());

	smutils->AddGameFrameHook(game_frame);

	sharesys->AddDependency(myself, "sdkhooks.ext", true, true);
	
	sharesys->AddInterface(myself, this);
	sharesys->AddNatives(myself, g_sNativesInfo);
	
	plsys->AddPluginsListener(this);

#ifdef __HAS_NEXTBOT
	sharesys->AddDependency(myself, "nextbot.ext", false, true);
#endif
#ifdef __HAS_WPNHACK
	sharesys->AddDependency(myself, "wpnhack.ext", false, true);
#endif
	
	sharesys->RegisterLibrary(myself, "damagerules");

	HandleSystemHack::init();

	return true;
}

void Sample::SDK_OnUnload()
{
	g_pSDKHooks->RemoveEntityListener(this);
	plsys->RemovePluginsListener(this);

	if(pObjectKilledDetour) {
		pObjectKilledDetour->Destroy();
	}

	smutils->RemoveGameFrameHook(game_frame);

	pSW_GameStats_WriteKill->Destroy();

	gameconfs->CloseGameConfigFile(g_pGameConf);
}