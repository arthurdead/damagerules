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

#include <server_class.h>
#include <eiface.h>
#include <shareddefs.h>
#ifdef __HAS_WPNHACK
#include <IWpnHack.h>
#endif

#include <igameevents.h>

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
int CBaseEntityOnTakeDamage_AliveOffset = -1;
void *CBasePlayerOnTakeDamagePtr = nullptr;

int m_iTeamNumOffset = -1;
int m_iHealthOffset = -1;
int m_hActiveWeaponOffset = -1;

class CBaseEntity : public IServerEntity
{
public:
	int entindex()
	{
		return gamehelpers->EntityToBCompatRef(this);
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
};

int CBaseCombatCharacterGetBossType = -1;

using HalloweenBossType = int;

class CBaseCombatCharacter : public CBaseEntity
{
public:
	HalloweenBossType GetBossType()
	{
		return call_vfunc<HalloweenBossType, CBaseCombatCharacter>(this, CBaseCombatCharacterGetBossType);
	}
};

class CBasePlayer : public CBaseEntity
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

#if SOURCE_ENGINE == SE_TF2
class CTFGameRules : public CGameRules
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
	
	return SH_MCALL(pEntity, OnTakeDamage)(info);
}

static cell_t CallOnTakeDamageAlive(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[2], &addr);
	
	CTakeDamageInfo info{};
	::AddrToDamageInfo(info, addr);
	
	return SH_MCALL(pEntity, OnTakeDamageAlive)(info);
}

struct callback_holder_t
{
	IPluginFunction *callback = nullptr;
	cell_t data = 0;
	IPluginFunction *alive_callback = nullptr;
	cell_t alive_data = 0;
	IdentityToken_t *owner = nullptr;
	bool erase = true;
	int ref = -1;
	
	callback_holder_t(CBaseEntity *pEntity, int ref_, IdentityToken_t *owner_);
	~callback_holder_t();
	
	void dtor(CBaseEntity *pEntity)
	{
		SH_REMOVE_MANUALHOOK(GenericDtor, pEntity, SH_MEMBER(this, &callback_holder_t::HookEntityDtor), false);
		SH_REMOVE_MANUALHOOK(OnTakeDamage, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamage), false);
		SH_REMOVE_MANUALHOOK(OnTakeDamageAlive, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamageAlive), false);
	}

	void HookEntityDtor();
	
	int HookOnTakeDamage(const CTakeDamageInfo &info)
	{
		if(!callback) {
			RETURN_META_VALUE(MRES_IGNORED, 0);
		}

		cell_t addr[DAMAGEINFO_STRUCT_SIZE_IN_CELL];
		::DamageInfoToAddr(info, addr);
		
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
		
		callback->PushCell(gamehelpers->EntityToBCompatRef(pEntity));
		callback->PushArray(addr, DAMAGEINFO_STRUCT_SIZE_IN_CELL);
		callback->PushCell(data);
		cell_t res = 0;
		callback->Execute(&res);
		
		RETURN_META_VALUE(MRES_SUPERCEDE, res);
	}

	int HookOnTakeDamageAlive(const CTakeDamageInfo &info)
	{
		if(!alive_callback) {
			RETURN_META_VALUE(MRES_IGNORED, 0);
		}

		cell_t addr[DAMAGEINFO_STRUCT_SIZE_IN_CELL];
		::DamageInfoToAddr(info, addr);
		
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
		
		alive_callback->PushCell(gamehelpers->EntityToBCompatRef(pEntity));
		alive_callback->PushArray(addr, DAMAGEINFO_STRUCT_SIZE_IN_CELL);
		alive_callback->PushCell(alive_data);
		cell_t res = 0;
		alive_callback->Execute(&res);
		
		RETURN_META_VALUE(MRES_SUPERCEDE, res);
	}
};

using callback_holder_map_t = std::unordered_map<int, callback_holder_t *>;
callback_holder_map_t callbackmap{};

void callback_holder_t::HookEntityDtor()
{
	CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
	int this_ref = gamehelpers->EntityToBCompatRef(pEntity);
	dtor(pEntity);
	callbackmap.erase(this_ref);
	erase = false;
	delete this;
	RETURN_META(MRES_HANDLED);
}

callback_holder_t::callback_holder_t(CBaseEntity *pEntity, int ref_, IdentityToken_t *owner_)
	: owner{owner_}, ref{ref_}
{
	SH_ADD_MANUALHOOK(GenericDtor, pEntity, SH_MEMBER(this, &callback_holder_t::HookEntityDtor), false);
	SH_ADD_MANUALHOOK(OnTakeDamageAlive, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamageAlive), false);
	SH_ADD_MANUALHOOK(OnTakeDamage, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamage), false);
	
	callbackmap.emplace(ref, this);
}

callback_holder_t::~callback_holder_t()
{
	if(erase) {
		callbackmap.erase(ref);
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

	int ref = gamehelpers->EntityToBCompatRef(pEntity);
	
	callback_holder_map_t::iterator it{callbackmap.find(ref)};
	if(it != callbackmap.end()) {
		holder = it->second;

		if(holder->owner != pContext->GetIdentity()) {
			return pContext->ThrowNativeError("Another plugin already set this entity OnTakeDamage callback");
		}
	} else {
		holder = new callback_holder_t{pEntity, ref, pContext->GetIdentity()};
	}
	
	IPluginFunction *callback = pContext->GetFunctionById(params[2]);

	holder->callback = callback;
	holder->data = params[3];
	
	return 0;
}

static cell_t SetEntityOnTakeDamageAlive(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	callback_holder_t *holder = nullptr;

	int ref = gamehelpers->EntityToBCompatRef(pEntity);
	
	callback_holder_map_t::iterator it{callbackmap.find(ref)};
	if(it != callbackmap.end()) {
		holder = it->second;

		if(holder->owner != pContext->GetIdentity()) {
			return pContext->ThrowNativeError("Another plugin already set this entity OnTakeDamageAlive callback");
		}
	} else {
		holder = new callback_holder_t{pEntity, ref, pContext->GetIdentity()};
	}
	
	IPluginFunction *callback = pContext->GetFunctionById(params[2]);

	holder->alive_callback = callback;
	holder->alive_data = params[3];
	
	return 0;
}

static const sp_nativeinfo_t g_sNativesInfo[] =
{
#if SOURCE_ENGINE == SE_TF2
	{"HandleRageGain", HandleRageGainNative},
	{"CallOnDealtDamage", CallOnDealtDamage},
	{"ApplyOnDamageModifyRules", ApplyOnDamageModifyRules},
	{"ApplyOnDamageAliveModifyRules", ApplyOnDamageAliveModifyRules},
	{"ApplyOnHitAttributes", ApplyOnHitAttributes},
#endif
	{"CallOnTakeDamage", CallOnTakeDamage},
	{"CallOnTakeDamageAlive", CallOnTakeDamageAlive},
	{"SetEntityOnTakeDamage", SetEntityOnTakeDamage},
	{"SetEntityOnTakeDamageAlive", SetEntityOnTakeDamageAlive},
	{"TakeDamage", TakeDamage},
	{nullptr, nullptr},
};

void Sample::OnEntityDestroyed(CBaseEntity *pEntity)
{
	
}

void Sample::OnPluginUnloaded(IPlugin *plugin)
{
	callback_holder_map_t::iterator it{callbackmap.begin()};
	while(it != callbackmap.end()) {
		callback_holder_t *holder = it->second;

		if(holder->owner == plugin->GetIdentity()) {
			CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(holder->ref);
			if(pEntity) {
				holder->dtor(pEntity);
			}
			holder->erase = false;
			delete holder;
			it = callbackmap.erase(it);
			continue;
		}
		
		++it;
	}
}

const char *g_szGameRulesProxy = nullptr;

void Sample::OnCoreMapEnd()
{
	g_pGameRulesProxyEntity = nullptr;
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

bool Sample::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
	GET_V_IFACE_CURRENT(GetEngineFactory, cvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, gameeventmanager, IGameEventManager2, INTERFACEVERSION_GAMEEVENTSMANAGER2);
	g_pCVar = cvar;
	ConVar_Register(0, this);

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

class GameRulesVTableHack
{
public:
	void AdjustPlayerDamageTaken( CTakeDamageInfo *pInfo )
	{
		CGameRules *pThis = (CGameRules *)this;

		if( pInfo->GetDamageType() & (DMG_DROWN|DMG_CRUSH|DMG_FALL|DMG_POISON) )
		{
			// Skill level doesn't affect these types of damage.
			return;
		}

		switch( pThis->GetSkillLevel() )
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
	float AdjustPlayerDamageInflicted( float damage )
	{
		CGameRules *pThis = (CGameRules *)this;

		switch( pThis->GetSkillLevel() ) 
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

	bool ShouldUseRobustRadiusDamage(CBaseEntity *pEntity)
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

	float GetAmmoDamage( CBaseEntity *pAttacker, CBaseEntity *pVictim, int nAmmoType )
	{
		CGameRules *pThis = (CGameRules *)this;

		float flDamage = 0.0f;
		CAmmoDef *pAmmoDef = GetAmmoDef();

		flDamage = pThis->GetAmmoDamage( pAttacker, pVictim, nAmmoType );

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
};

#include <sourcehook/sh_memory.h>

void Sample::OnCoreMapStart(edict_t * pEdictList, int edictCount, int clientMax)
{
	g_pGameRulesProxyEntity = FindEntityByServerClassname(0, g_szGameRulesProxy);
	g_pGameRules = (CGameRules *)g_pSDKTools->GetGameRules();

	if(!gamerules_vtable_assigned) {
		if(g_pGameRules) {
			void **vtabl = *(void ***)g_pGameRules;

			SourceHook::SetMemAccess(vtabl, (CGameRulesAdjustPlayerDamageTaken * sizeof(void *)) + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);

			vtabl[CGameRulesAdjustPlayerDamageTaken] = func_to_void(&GameRulesVTableHack::AdjustPlayerDamageTaken);
			vtabl[CGameRulesAdjustPlayerDamageInflicted] = func_to_void(&GameRulesVTableHack::AdjustPlayerDamageInflicted);
			vtabl[CGameRulesShouldUseRobustRadiusDamage] = func_to_void(&GameRulesVTableHack::ShouldUseRobustRadiusDamage);
			CGameRulesGetAmmoDamagePtr = vtabl[CGameRulesGetAmmoDamageOffset];
			vtabl[CGameRulesGetAmmoDamageOffset] = func_to_void(&GameRulesVTableHack::GetAmmoDamage);

			gamerules_vtable_assigned = true;
		}
	}
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
		CBasePlayer *pThis = (CBasePlayer *)this;

		// Modify the amount of damage the player takes, based on skill.
		CTakeDamageInfo playerDamage = info;

		// Should we run this damage through the skill level adjustment?
		bool bAdjustForSkillLevel = true;

		if( info.GetDamageType() == DMG_GENERIC && info.GetAttacker() == pThis && info.GetInflictor() == pThis )
		{
			// Only do a skill level adjustment if the player isn't his own attacker AND inflictor.
			// This prevents damage from SetHealth() inputs from being adjusted for skill level.
			bAdjustForSkillLevel = false;
		}

		if( bAdjustForSkillLevel )
		{
			playerDamage.AdjustPlayerDamageTakenForSkillLevel();
		}

		return CBasePlayer::OnTakeDamage( playerDamage );
	}
};

void ModifyDamage( CTakeDamageInfo *info )
{

}

int hook_npc_takedamage( const CTakeDamageInfo &rawInfo )
{
	CBaseEntity *pThis = META_IFACEPTR(CBaseEntity);

	CTakeDamageInfo info = rawInfo;

#if SOURCE_ENGINE == SE_TF2
	if ( g_pGameRules )
	{
		((CTFGameRules *)g_pGameRules)->ApplyOnDamageModifyRules( info, pThis, true );
	}

	// On damage Rage
	// Give the soldier/pyro some rage points for dealing/taking damage.
	if ( info.GetDamage() && info.GetAttacker() != pThis )
	{
		CTFPlayer *pAttacker = (CTFPlayer *)info.GetAttacker()->IsPlayer();

		// Buff flag 1: we get rage when we deal damage. Here, that means the soldier that attacked
		// gets rage when we take damage.
		HandleRageGain( pAttacker, kRageBuffFlag_OnDamageDealt, info.GetDamage(), 6.0f );

		// Buff 5: our pyro attacker get rage when we're damaged by fire
		if ( ( info.GetDamageType() & DMG_BURN ) != 0 || ( info.GetDamageType() & DMG_PLASMA ) != 0 )
		{
			HandleRageGain( pAttacker, kRageBuffFlag_OnBurnDamageDealt, info.GetDamage(), 30.f );
		}

		if ( pAttacker && info.GetWeapon() )
		{
			CTFWeaponBase *pWeapon = (CTFWeaponBase *)info.GetWeapon();
			if ( pWeapon )
			{
				pWeapon->ApplyOnHitAttributes( pThis, pAttacker, info );
			}
		}
	}
#endif

	int result = SH_MCALL(pThis, OnTakeDamage)( info );
	RETURN_META_VALUE(MRES_SUPERCEDE, result);
}

int hook_npc_takedamagealive( const CTakeDamageInfo &rawInfo )
{
	CBaseEntity *pThis = META_IFACEPTR(CBaseEntity);

	if ( !rawInfo.GetAttacker() || rawInfo.GetAttacker()->GetTeamNumber() == pThis->GetTeamNumber() )
	{
		// no friendly fire damage
		RETURN_META_VALUE(MRES_SUPERCEDE, 0);
	}

	CTakeDamageInfo info = rawInfo;

	// weapon-specific damage modification
	ModifyDamage( &info );

#if SOURCE_ENGINE == SE_TF2
	if ( g_pGameRules )
	{
		DamageModifyExtras_t outParams;
		info.SetDamage( ((CTFGameRules *)g_pGameRules)->ApplyOnDamageAliveModifyRules( info, pThis, outParams ) );
	}
#endif

	// fire event for client combat text, beep, etc.
	IGameEvent *event = gameeventmanager->CreateEvent( "npc_hurt" );
	if ( event )
	{

		event->SetInt( "entindex", pThis->entindex() );
		event->SetInt( "health", MAX( 0, pThis->GetHealth() ) );
		event->SetInt( "damageamount", info.GetDamage() );
		event->SetBool( "crit", ( info.GetDamageType() & DMG_CRITICAL ) ? true : false );

		CTFPlayer *attackerPlayer = (CTFPlayer *)info.GetAttacker()->IsPlayer();
		if ( attackerPlayer )
		{
			event->SetInt( "attacker_player", attackerPlayer->GetUserID() );

			if ( attackerPlayer->GetActiveTFWeapon() )
			{
				event->SetInt( "weaponid", attackerPlayer->GetActiveTFWeapon()->GetWeaponID() );
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

	int result = SH_MCALL(pThis, OnTakeDamageAlive)( info );

#if SOURCE_ENGINE == SE_TF2
	// Let attacker react to the damage they dealt
	CTFPlayer *pAttacker = (CTFPlayer *)rawInfo.GetAttacker()->IsPlayer();
	if ( pAttacker )
	{
		pAttacker->OnDealtDamage( (CBaseCombatCharacter *)pThis, info );
	}
#endif

	RETURN_META_VALUE(MRES_SUPERCEDE, result);
}

void hook_npc_dtor()
{
	CBaseEntity *pThis = META_IFACEPTR(CBaseEntity);

	SH_REMOVE_MANUALHOOK(GenericDtor, pThis, SH_STATIC(hook_npc_dtor), false);
	SH_REMOVE_MANUALHOOK(OnTakeDamage, pThis, SH_STATIC(hook_npc_takedamage), false);
	SH_REMOVE_MANUALHOOK(OnTakeDamageAlive, pThis, SH_STATIC(hook_npc_takedamagealive), false);
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
		if(ntype & npc_custom) {
			SH_ADD_MANUALHOOK(GenericDtor, pEntity, SH_STATIC(hook_npc_dtor), false);
			SH_ADD_MANUALHOOK(OnTakeDamage, pEntity, SH_STATIC(hook_npc_takedamage), false);
			SH_ADD_MANUALHOOK(OnTakeDamageAlive, pEntity, SH_STATIC(hook_npc_takedamagealive), false);
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

void Sample::SDK_OnUnload()
{
	g_pSDKHooks->RemoveEntityListener(this);
	plsys->RemovePluginsListener(this);
}

bool Sample::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	IGameConfig *g_pGameConf = nullptr;
	gameconfs->LoadGameConfigFile("sdktools.games", &g_pGameConf, nullptr, 0);
	
	g_szGameRulesProxy = g_pGameConf->GetKeyValue("GameRulesProxy");
	
	gameconfs->CloseGameConfigFile(g_pGameConf);
	
	gameconfs->LoadGameConfigFile("damagerules", &g_pGameConf, nullptr, 0);

	g_pGameConf->GetOffset("CBaseEntity::IsNPC", &CBaseEntityIsNPC);
	g_pGameConf->GetOffset("CGameRules::GetSkillLevel", &CGameRulesGetSkillLevel);
	g_pGameConf->GetOffset("CGameRules::AdjustPlayerDamageTaken", &CGameRulesAdjustPlayerDamageTaken);
	g_pGameConf->GetOffset("CGameRules::AdjustPlayerDamageInflicted", &CGameRulesAdjustPlayerDamageInflicted);
	g_pGameConf->GetOffset("CGameRules::ShouldUseRobustRadiusDamage", &CGameRulesShouldUseRobustRadiusDamage);
	g_pGameConf->GetOffset("CGameRules::GetAmmoDamage", &CGameRulesGetAmmoDamageOffset);

#if SOURCE_ENGINE == SE_TF2
	g_pGameConf->GetOffset("CTFWeaponBase::ApplyOnHitAttributes", &CTFWeaponBaseApplyOnHitAttributes);

	g_pGameConf->GetOffset("CTFWeaponBase::GetWeaponID", &CTFWeaponBaseGetWeaponID);
#endif
	
	g_pGameConf->GetOffset("CBaseEntity::OnTakeDamage", &CBaseEntityOnTakeDamageOffset);
	SH_MANUALHOOK_RECONFIGURE(OnTakeDamage, CBaseEntityOnTakeDamageOffset, 0, 0);
	
	g_pGameConf->GetOffset("CBaseCombatCharacter::OnTakeDamage_Alive", &CBaseEntityOnTakeDamage_AliveOffset);
	SH_MANUALHOOK_RECONFIGURE(OnTakeDamageAlive, CBaseEntityOnTakeDamage_AliveOffset, 0, 0);
	
#if SOURCE_ENGINE == SE_TF2
	g_pGameConf->GetMemSig("HandleRageGain", &HandleRageGainPtr);
	g_pGameConf->GetMemSig("CTFPlayer::OnDealtDamage", &CTFPlayerOnDealtDamage);
	g_pGameConf->GetMemSig("CTFGameRules::ApplyOnDamageModifyRules", &CTFGameRulesApplyOnDamageModifyRules);
	g_pGameConf->GetMemSig("CTFGameRules::ApplyOnDamageAliveModifyRules", &CTFGameRulesApplyOnDamageAliveModifyRules);
#endif
	g_pGameConf->GetMemSig("CBaseEntity::TakeDamage", &CBaseEntityTakeDamage);

#if SOURCE_ENGINE == SE_TF2
	g_pGameConf->GetOffset("CBaseCombatCharacter::GetBossType", &CBaseCombatCharacterGetBossType);
#endif

	gameconfs->CloseGameConfigFile(g_pGameConf);

	sm_sendprop_info_t info{};
	gamehelpers->FindSendPropInfo("CBaseEntity", "m_iTeamNum", &info);
	m_iTeamNumOffset = info.actual_offset;

	g_pEntityList = reinterpret_cast<CBaseEntityList *>(gamehelpers->GetGlobalEntityList());

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

	return true;
}
