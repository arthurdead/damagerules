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

int CBaseEntityOnTakeDamage = -1;
int CBaseCombatCharacterOnTakeDamage_Alive = -1;

void *HandleRageGainPtr = nullptr;
void *CTFPlayerOnDealtDamage = nullptr;
void *CTFGameRulesApplyOnDamageModifyRules = nullptr;
void *CTFGameRulesApplyOnDamageAliveModifyRules = nullptr;

template <typename T>
T void_to_func(void *ptr)
{
	union { T f; void *p; };
	p = ptr;
	return f;
}

enum ECritType : int;

class CTakeDamageInfo
{
public:
	Vector			m_vecDamageForce;
	Vector			m_vecDamagePosition;
	Vector			m_vecReportedPosition;	// Position players are told damage is coming from
	CBaseHandle		m_hInflictor;
	CBaseHandle		m_hAttacker;
	CBaseHandle		m_hWeapon;
	float			m_flDamage;
	float			m_flMaxDamage;
	float			m_flBaseDamage;			// The damage amount before skill leve adjustments are made. Used to get uniform damage forces.
	int				m_bitsDamageType;
	int				m_iDamageCustom;
	int				m_iDamageStats;
	int				m_iAmmoType;			// AmmoType of the weapon used to cause this damage, if any
	int				m_iDamagedOtherPlayers;
	int				m_iPlayerPenetrationCount;
	float			m_flDamageBonus;		// Anything that increases damage (crit) - store the delta
	CBaseHandle		m_hDamageBonusProvider;	// Who gave us the ability to do extra damage?
	bool			m_bForceFriendlyFire;	// Ideally this would be a dmg type, but we can't add more
	float			m_flDamageForForce;
	ECritType		m_eCritType;
};

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
	bool bIgniting;
	bool bSelfBlastDmg;
	bool bSendPreFeignDamage;
	bool bPlayDamageReductionSound;
};

#define DAMAGEINFO_STRUCT_SIZE 26

void AddrToDamageInfo(CTakeDamageInfo &info, cell_t *addr)
{
	info.m_vecDamageForce.x = addr[0];
	info.m_vecDamageForce.y = addr[1];
	info.m_vecDamageForce.z = addr[2];
	info.m_vecDamagePosition.x = addr[3];
	info.m_vecDamagePosition.y = addr[4];
	info.m_vecDamagePosition.z = addr[5];
	info.m_vecReportedPosition.x = addr[6];
	info.m_vecReportedPosition.y = addr[7];
	info.m_vecReportedPosition.z = addr[8];
	gamehelpers->SetHandleEntity(info.m_hInflictor, gamehelpers->EdictOfIndex(addr[9]));
	gamehelpers->SetHandleEntity(info.m_hAttacker, gamehelpers->EdictOfIndex(addr[10]));
	gamehelpers->SetHandleEntity(info.m_hWeapon, gamehelpers->EdictOfIndex(addr[11]));
	info.m_flDamage = sp_ctof(addr[12]);
	info.m_flMaxDamage = sp_ctof(addr[13]);
	info.m_flBaseDamage = sp_ctof(addr[14]);
	info.m_bitsDamageType = addr[15];
	info.m_iDamageCustom = addr[16];
	info.m_iDamageStats = addr[17];
	info.m_iAmmoType = addr[18];
	info.m_iDamagedOtherPlayers = addr[19];
	info.m_iPlayerPenetrationCount = addr[20];
	info.m_flDamageBonus = sp_ctof(addr[21]);
	gamehelpers->SetHandleEntity(info.m_hDamageBonusProvider, gamehelpers->EdictOfIndex(addr[22]));
	info.m_bForceFriendlyFire = addr[23];
	info.m_flDamageForForce = sp_ctof(addr[24]);
	info.m_eCritType = (ECritType)addr[25];
}

void DamageInfoToAddr(CTakeDamageInfo &info, cell_t *addr)
{
	addr[0] = info.m_vecDamageForce.x;
	addr[1] = info.m_vecDamageForce.y;
	addr[2] = info.m_vecDamageForce.z;
	addr[3] = info.m_vecDamagePosition.x;
	addr[4] = info.m_vecDamagePosition.y;
	addr[5] = info.m_vecDamagePosition.z;
	addr[6] = info.m_vecReportedPosition.x;
	addr[7] = info.m_vecReportedPosition.y;
	addr[8] = info.m_vecReportedPosition.z;
	addr[9] = gamehelpers->IndexOfEdict(gamehelpers->GetHandleEntity(info.m_hInflictor));
	addr[10] = gamehelpers->IndexOfEdict(gamehelpers->GetHandleEntity(info.m_hAttacker));
	addr[11] = gamehelpers->IndexOfEdict(gamehelpers->GetHandleEntity(info.m_hWeapon));
	addr[12] = sp_ftoc(info.m_flDamage);
	addr[13] = sp_ftoc(info.m_flMaxDamage);
	addr[14] = sp_ftoc(info.m_flBaseDamage);
	addr[15] = info.m_bitsDamageType;
	addr[16] = info.m_iDamageCustom;
	addr[17] = info.m_iDamageStats;
	addr[18] = info.m_iAmmoType;
	addr[19] = info.m_iDamagedOtherPlayers;
	addr[20] = info.m_iPlayerPenetrationCount;
	addr[21] = sp_ftoc(info.m_flDamageBonus);
	addr[22] = gamehelpers->IndexOfEdict(gamehelpers->GetHandleEntity(info.m_hDamageBonusProvider));
	addr[23] = info.m_bForceFriendlyFire;
	addr[24] = sp_ftoc(info.m_flDamageForForce);
	addr[25] = info.m_eCritType;
}

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
	AddrToDamageInfo(info, addr);
	
	(pEntity->*void_to_func<void(CBaseEntity::*)(CBaseEntity *, const CTakeDamageInfo &)>(CTFPlayerOnDealtDamage))(pVictim, info);
	return 0;
}

class CTFGameRules;

CBaseEntity *g_pGameRulesProxyEntity = nullptr;
CTFGameRules *g_pGameRules = nullptr;

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
	AddrToDamageInfo(info, addr);
	
	bool ret = (g_pGameRules->*void_to_func<bool(CTFGameRules::*)(CTakeDamageInfo &info, CBaseEntity *, bool)>(CTFGameRulesApplyOnDamageModifyRules))(info, pVictim, params[3]);
	
	DamageInfoToAddr(info, addr);
	
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
	AddrToDamageInfo(info, addr);
	
	DamageModifyExtras_t extra{};
	float ret = (g_pGameRules->*void_to_func<float(CTFGameRules::*)(const CTakeDamageInfo &info, CBaseEntity *, DamageModifyExtras_t &)>(CTFGameRulesApplyOnDamageAliveModifyRules))(info, pVictim, extra);
	
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

SH_DECL_MANUALHOOK0_void(GenericDtor, 0, 0, 0)

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
	AddrToDamageInfo(info, addr);
	
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
	AddrToDamageInfo(info, addr);
	
	return SH_MCALL(pEntity, OnTakeDamageAlive)(info);
}

struct callback_holder_t
{
	IPluginFunction *callback = nullptr;
	IPluginFunction *alive_callback = nullptr;
	CBaseEntity *pEntity_ = nullptr;
	IdentityToken_t *owner = nullptr;
	bool erase = true;
	
	callback_holder_t(CBaseEntity *pEntity, IdentityToken_t *owner_);
	~callback_holder_t();
	
	void add_alive_hook(CBaseEntity *pEntity, IPluginFunction *callback_)
	{
		bool had = alive_callback != nullptr;
		alive_callback = callback_;
		if(!had) {
			SH_ADD_MANUALHOOK(OnTakeDamageAlive, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamageAlive), false);
		}
	}
	
	void add_hook(CBaseEntity *pEntity, IPluginFunction *callback_)
	{
		bool had = callback != nullptr;
		callback = callback_;
		if(!had) {
			SH_ADD_MANUALHOOK(OnTakeDamage, pEntity, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamage), false);
		}
	}

	void dtor()
	{
		SH_REMOVE_MANUALHOOK(GenericDtor, this, SH_MEMBER(this, &callback_holder_t::dtor), false);
		
		if(callback) {
			SH_REMOVE_MANUALHOOK(OnTakeDamage, this, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamage), false);
		}
		if(alive_callback) {
			SH_REMOVE_MANUALHOOK(OnTakeDamageAlive, this, SH_MEMBER(this, &callback_holder_t::HookOnTakeDamageAlive), false);
		}
		
		delete this;
	}
	
	int HookOnTakeDamage(const CTakeDamageInfo &info)
	{
		CTakeDamageInfo copy = info;
		
		cell_t addr[DAMAGEINFO_STRUCT_SIZE];
		DamageInfoToAddr(copy, addr);
		
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
		
		callback->PushCell(gamehelpers->EntityToBCompatRef(pEntity));
		callback->PushArray(addr, DAMAGEINFO_STRUCT_SIZE, SM_PARAM_COPYBACK);
		cell_t res = 0;
		callback->Execute(&res);
		
		AddrToDamageInfo(copy, addr);
		
		RETURN_META_VALUE_MNEWPARAMS(MRES_SUPERCEDE, res, OnTakeDamageAlive, (copy));
	}

	int HookOnTakeDamageAlive(const CTakeDamageInfo &info)
	{
		CTakeDamageInfo copy = info;
		
		cell_t addr[DAMAGEINFO_STRUCT_SIZE];
		DamageInfoToAddr(copy, addr);
		
		CBaseEntity *pEntity = META_IFACEPTR(CBaseEntity);
		
		alive_callback->PushCell(gamehelpers->EntityToBCompatRef(pEntity));
		alive_callback->PushArray(addr, DAMAGEINFO_STRUCT_SIZE, SM_PARAM_COPYBACK);
		cell_t res = 0;
		alive_callback->Execute(&res);
		
		AddrToDamageInfo(copy, addr);
		
		RETURN_META_VALUE(MRES_SUPERCEDE, res);
	}
};

using callback_holder_map_t = std::unordered_map<CBaseEntity *, callback_holder_t *>;
callback_holder_map_t callbackmap{};

callback_holder_t::callback_holder_t(CBaseEntity *pEntity, IdentityToken_t *owner_)
{
	SH_ADD_MANUALHOOK(GenericDtor, pEntity, SH_MEMBER(this, &callback_holder_t::dtor), false);
	
	callbackmap[pEntity] = this;
	
	owner = owner_;
	pEntity_ = pEntity;
}

callback_holder_t::~callback_holder_t()
{
	if(erase) {
		callbackmap.erase(pEntity_);
	}
}

static cell_t SetEntityOnTakeDamage(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	callback_holder_t *holder = nullptr;
	
	callback_holder_map_t::iterator it{callbackmap.find(pEntity)};
	if(it != callbackmap.end()) {
		holder = it->second;
	} else {
		holder = new callback_holder_t{pEntity, pContext->GetIdentity()};
	}
	
	IPluginFunction *callback = pContext->GetFunctionById(params[2]);
	holder->add_hook(pEntity, callback);
	
	return 0;
}

static cell_t SetEntityOnTakeDamageAlive(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	callback_holder_t *holder = nullptr;
	
	callback_holder_map_t::iterator it{callbackmap.find(pEntity)};
	if(it != callbackmap.end()) {
		holder = it->second;
	} else {
		holder = new callback_holder_t{pEntity, pContext->GetIdentity()};
	}
	
	IPluginFunction *callback = pContext->GetFunctionById(params[2]);
	holder->add_alive_hook(pEntity, callback);
	
	return 0;
}

static const sp_nativeinfo_t g_sNativesInfo[] =
{
	{"HandleRageGain", HandleRageGain},
	{"CallOnDealtDamage", CallOnDealtDamage},
	{"ApplyOnDamageModifyRules", ApplyOnDamageModifyRules},
	{"ApplyOnDamageAliveModifyRules", ApplyOnDamageAliveModifyRules},
	{"CallOnTakeDamage", CallOnTakeDamage},
	{"CallOnTakeDamageAlive", CallOnTakeDamageAlive},
	{"SetEntityOnTakeDamage", SetEntityOnTakeDamage},
	{"SetEntityOnTakeDamageAlive", SetEntityOnTakeDamageAlive},
	{nullptr, nullptr},
};

void Sample::OnEntityDestroyed(CBaseEntity *pEntity)
{
	
}

void Sample::OnPluginUnloaded(IPlugin *plugin)
{
	for(callback_holder_map_t::iterator it{callbackmap.begin()}; it != callbackmap.end(); ++it) {
		if(it->second->owner == plugin->GetIdentity()) {
			it->second->erase = false;
			it->second->dtor();
			break;
		}
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
	g_pGameRules = (CTFGameRules *)g_pSDKTools->GetGameRules();
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
	
	g_pGameConf->GetOffset("CBaseEntity::OnTakeDamage", &CBaseEntityOnTakeDamage);
	g_pGameConf->GetOffset("CBaseCombatCharacter::OnTakeDamage_Alive", &CBaseCombatCharacterOnTakeDamage_Alive);
	
	SH_MANUALHOOK_RECONFIGURE(OnTakeDamage, CBaseEntityOnTakeDamage, 0, 0);
	SH_MANUALHOOK_RECONFIGURE(OnTakeDamageAlive, CBaseCombatCharacterOnTakeDamage_Alive, 0, 0);
	
	g_pGameConf->GetMemSig("HandleRageGain", &HandleRageGainPtr);
	g_pGameConf->GetMemSig("CTFPlayer::OnDealtDamage", &CTFPlayerOnDealtDamage);
	g_pGameConf->GetMemSig("CTFGameRules::ApplyOnDamageModifyRules", &CTFGameRulesApplyOnDamageModifyRules);
	g_pGameConf->GetMemSig("CTFGameRules::ApplyOnDamageAliveModifyRules", &CTFGameRulesApplyOnDamageAliveModifyRules);
	
	gameconfs->CloseGameConfigFile(g_pGameConf);

	sharesys->AddDependency(myself, "sdkhooks.ext", true, true);
	
	sharesys->AddNatives(myself, g_sNativesInfo);
	
	plsys->AddPluginsListener(this);
	
	sharesys->RegisterLibrary(myself, "damagerules");
	
	return true;
}
