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

#define protected public

#include <ehandle.h>
#include <predictioncopy.h>
#include <takedamageinfo.h>

#undef protected

#include "extension.h"

#include <server_class.h>
#include <eiface.h>
#include <unordered_map>

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Sample g_Sample;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_Sample);

ISDKTools *g_pSDKTools = nullptr;
ISDKHooks *g_pSDKHooks = nullptr;
IServerGameEnts *gameents = nullptr;

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

#if SOURCE_ENGINE == SE_TF2
static cell_t HandleRageGain(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	(void_to_func<void(*)(CBaseEntity *, unsigned int, float, float)>(HandleRageGainPtr))(pEntity, params[1], sp_ctof(params[2]), sp_ctof(params[3]));
	return 0;
}

struct DamageModifyExtras_t
{
	bool bIgniting{false};
	bool bSelfBlastDmg{false};
	bool bSendPreFeignDamage{false};
	bool bPlayDamageReductionSound{false};
};

#define DAMAGEINFO_STRUCT_SIZE 26
#elif SOURCE_ENGINE == SE_LEFT4DEAD2
#define DAMAGEINFO_STRUCT_SIZE 20
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

#if SOURCE_ENGINE == SE_TF2
using ECritType = CTakeDamageInfo::ECritType;
#endif

void AddrToDamageInfo(CTakeDamageInfo &info, const cell_t *addr)
{
	info.m_vecDamageForce.x = sp_ctof(addr[0]);
	info.m_vecDamageForce.y = sp_ctof(addr[1]);
	info.m_vecDamageForce.z = sp_ctof(addr[2]);
	info.m_vecDamagePosition.x = sp_ctof(addr[3]);
	info.m_vecDamagePosition.y = sp_ctof(addr[4]);
	info.m_vecDamagePosition.z = sp_ctof(addr[5]);
	info.m_vecReportedPosition.x = sp_ctof(addr[6]);
	info.m_vecReportedPosition.y = sp_ctof(addr[7]);
	info.m_vecReportedPosition.z = sp_ctof(addr[8]);
	SetHandleEntity(info.m_hInflictor, gamehelpers->EdictOfIndex(addr[9]));
	SetHandleEntity(info.m_hAttacker, gamehelpers->EdictOfIndex(addr[10]));
	SetHandleEntity(info.m_hWeapon, gamehelpers->EdictOfIndex(addr[11]));
	info.m_flDamage = sp_ctof(addr[12]);
	info.m_flMaxDamage = sp_ctof(addr[13]);
	info.m_flBaseDamage = sp_ctof(addr[14]);
	info.m_bitsDamageType = addr[15];
	info.m_iDamageCustom = addr[16];
	info.m_iDamageStats = addr[17];
	info.m_iAmmoType = addr[18];
#if SOURCE_ENGINE == SE_TF2
	info.m_iDamagedOtherPlayers = addr[19];
	info.m_iPlayerPenetrationCount = addr[20];
	info.m_flDamageBonus = sp_ctof(addr[21]);
	SetHandleEntity(info.m_hDamageBonusProvider, gamehelpers->EdictOfIndex(addr[22]));
	info.m_bForceFriendlyFire = addr[23];
	info.m_flDamageForForce = sp_ctof(addr[24]);
	info.m_eCritType = (ECritType)addr[25];
#elif SOURCE_ENGINE == SE_LEFT4DEAD2
	info.m_flRadius = sp_ctof(addr[19]);
#endif
}

void DamageInfoToAddr(const CTakeDamageInfo &info, cell_t *addr)
{
	addr[0] = sp_ftoc(info.m_vecDamageForce.x);
	addr[1] = sp_ftoc(info.m_vecDamageForce.y);
	addr[2] = sp_ftoc(info.m_vecDamageForce.z);
	addr[3] = sp_ftoc(info.m_vecDamagePosition.x);
	addr[4] = sp_ftoc(info.m_vecDamagePosition.y);
	addr[5] = sp_ftoc(info.m_vecDamagePosition.z);
	addr[6] = sp_ftoc(info.m_vecReportedPosition.x);
	addr[7] = sp_ftoc(info.m_vecReportedPosition.y);
	addr[8] = sp_ftoc(info.m_vecReportedPosition.z);
	addr[9] = IndexOfEdict(GetHandleEntity(info.m_hInflictor));
	addr[10] = IndexOfEdict(GetHandleEntity(info.m_hAttacker));
	addr[11] = IndexOfEdict(GetHandleEntity(info.m_hWeapon));
	addr[12] = sp_ftoc(info.m_flDamage);
	addr[13] = sp_ftoc(info.m_flMaxDamage);
	addr[14] = sp_ftoc(info.m_flBaseDamage);
	addr[15] = info.m_bitsDamageType;
	addr[16] = info.m_iDamageCustom;
	addr[17] = info.m_iDamageStats;
	addr[18] = info.m_iAmmoType;
#if SOURCE_ENGINE == SE_TF2
	addr[19] = info.m_iDamagedOtherPlayers;
	addr[20] = info.m_iPlayerPenetrationCount;
	addr[21] = sp_ftoc(info.m_flDamageBonus);
	addr[22] = IndexOfEdict(GetHandleEntity(info.m_hDamageBonusProvider));
	addr[23] = info.m_bForceFriendlyFire;
	addr[24] = sp_ftoc(info.m_flDamageForForce);
	addr[25] = info.m_eCritType;
#elif SOURCE_ENGINE == SE_LEFT4DEAD2
	addr[19] = sp_ftoc(info.m_flRadius);
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

size_t Sample::SPDamageInfoStructSize()
{
	return DAMAGEINFO_STRUCT_SIZE;
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
	
	call_mfunc<void, CBaseEntity, CBaseEntity *, const CTakeDamageInfo &>(pEntity, CTFPlayerOnDealtDamage, pVictim, info);
	return 0;
}
#endif

class CGameRules;

CBaseEntity *g_pGameRulesProxyEntity = nullptr;
CGameRules *g_pGameRules = nullptr;

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
	
	bool ret = call_mfunc<bool, CGameRules, CTakeDamageInfo &, CBaseEntity *, bool>(g_pGameRules, CTFGameRulesApplyOnDamageModifyRules, info, pVictim, params[3]);
	
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
	float ret = call_mfunc<float, CGameRules, const CTakeDamageInfo &, CBaseEntity *, DamageModifyExtras_t &>(g_pGameRules, CTFGameRulesApplyOnDamageAliveModifyRules, info, pVictim, extra);
	
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

		cell_t addr[DAMAGEINFO_STRUCT_SIZE];
		::DamageInfoToAddr(info, addr);
		
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
		
		callback->PushCell(gamehelpers->EntityToBCompatRef(pEntity));
		callback->PushArray(addr, DAMAGEINFO_STRUCT_SIZE);
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

		cell_t addr[DAMAGEINFO_STRUCT_SIZE];
		::DamageInfoToAddr(info, addr);
		
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
		
		alive_callback->PushCell(gamehelpers->EntityToBCompatRef(pEntity));
		alive_callback->PushArray(addr, DAMAGEINFO_STRUCT_SIZE);
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
	RETURN_META(MRES_IGNORED);
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
	
	call_vfunc<void, CBaseEntity, CBaseEntity *, CBaseEntity *, const CTakeDamageInfo &>(pEntity, CTFWeaponBaseApplyOnHitAttributes, pVictim, pAttacker, info);
	
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
	{"HandleRageGain", HandleRageGain},
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

bool Sample::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
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

void Sample::OnCoreMapStart(edict_t * pEdictList, int edictCount, int clientMax)
{
	g_pGameRulesProxyEntity = FindEntityByServerClassname(0, g_szGameRulesProxy);
	g_pGameRules = (CGameRules *)g_pSDKTools->GetGameRules();
}

void Sample::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
	SM_GET_LATE_IFACE(SDKHOOKS, g_pSDKHooks);
	
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

	return IExtensionInterface::QueryInterfaceDrop(pInterface);
}

void Sample::NotifyInterfaceDrop(SMInterface *pInterface)
{
	if(strcmp(pInterface->GetInterfaceName(), SMINTERFACE_SDKHOOKS_NAME) == 0)
	{
		g_pSDKHooks->RemoveEntityListener(this);
		g_pSDKHooks = NULL;
	}
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
	
#if SOURCE_ENGINE == SE_TF2
	g_pGameConf->GetOffset("CTFWeaponBase::ApplyOnHitAttributes", &CTFWeaponBaseApplyOnHitAttributes);
#endif
	
	int offset = -1;
	g_pGameConf->GetOffset("CBaseEntity::OnTakeDamage", &offset);
	SH_MANUALHOOK_RECONFIGURE(OnTakeDamage, offset, 0, 0);
	
	g_pGameConf->GetOffset("CBaseCombatCharacter::OnTakeDamage_Alive", &offset);
	SH_MANUALHOOK_RECONFIGURE(OnTakeDamageAlive, offset, 0, 0);
	
#if SOURCE_ENGINE == SE_TF2
	g_pGameConf->GetMemSig("HandleRageGain", &HandleRageGainPtr);
	g_pGameConf->GetMemSig("CTFPlayer::OnDealtDamage", &CTFPlayerOnDealtDamage);
	g_pGameConf->GetMemSig("CTFGameRules::ApplyOnDamageModifyRules", &CTFGameRulesApplyOnDamageModifyRules);
	g_pGameConf->GetMemSig("CTFGameRules::ApplyOnDamageAliveModifyRules", &CTFGameRulesApplyOnDamageAliveModifyRules);
#endif
	g_pGameConf->GetMemSig("CBaseEntity::TakeDamage", &CBaseEntityTakeDamage);
	
	gameconfs->CloseGameConfigFile(g_pGameConf);

	sharesys->AddDependency(myself, "sdkhooks.ext", true, true);
	
	sharesys->AddInterface(myself, this);
	sharesys->AddNatives(myself, g_sNativesInfo);
	
	plsys->AddPluginsListener(this);
	
	sharesys->RegisterLibrary(myself, "damagerules");
	
	return true;
}
