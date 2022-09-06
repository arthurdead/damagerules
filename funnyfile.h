/*
if the person who is reading this is a sm dev
and you dont like this file gues what
i dont care!!!!
i will only remove this file once theres a api for
creating ArrayList and KeyValues handles from extensions
util then this file will remain here
*/

#include <core/logic/CellArray.h>
#include <core/smn_keyvalues.h>

#define private protected
#include <core/logic/HandleSys.h>
#include <core/logic/ShareSys.h>
#include <core/logic/PluginSys.h>
#include <core/logic/ExtensionSys.h>

using namespace std::literals::string_literals;

HandleType_t arraylist_handle = 0;
IdentityType_t coreidenttype = 0;

inline HandleType_t TypeParent(HandleType_t type)
{
	return (type & ~HANDLESYS_SUBTYPE_MASK);
}

class HandleSystemHack : public HandleSystem
{
public:
	Handle_t CreateCellArrayHandle(ICellArray *&arr, IdentityToken_t *owner, HandleError *err)
	{
		arr = new CellArray(1);
		HandleSecurity sec{};
		sec.pIdentity = nullptr;
		sec.pOwner = owner;
		return CreateHandleInt__(arraylist_handle, arr, &sec, nullptr, nullptr, false);
	}

	HandleError ReadCoreHandle(Handle_t handle, HandleType_t type, const HandleSecurity *pSecurity, void **object)
	{
		return ReadHandle__(handle, type, pSecurity, object);
	}
	
	static void init()
	{
		handlesys->FindHandleType("CellArray", &arraylist_handle);
		coreidenttype = sharesys->FindIdentType("CORE");
	}
	
private:
	HandleError ReadHandle__(Handle_t handle, HandleType_t type, const HandleSecurity *pSecurity, void **object)
	{
		unsigned int index;
		QHandle *pHandle;
		HandleError err;
		IdentityToken_t *ident = pSecurity ? pSecurity->pIdentity : NULL;

		if ((err=GetHandle__(handle, ident, &pHandle, &index)) != HandleError_None)
		{
			return err;
		}

		if (!CheckAccess__(pHandle, HandleAccess_Read, pSecurity))
		{
			return HandleError_Access;
		}

		/* Check the type inheritance */
		if (pHandle->type & HANDLESYS_SUBTYPE_MASK)
		{
			if (pHandle->type != type
				&& (TypeParent(pHandle->type) != TypeParent(type)))
			{
				return HandleError_Type;
			}
		} else if (type) {
			if (pHandle->type != type)
			{
				return HandleError_Type;
			}
		}

		if (object)
		{
			/* if we're a clone, the rules change - object is ONLY in our reference */
			if (pHandle->clone)
			{
				pHandle = &m_Handles[pHandle->clone];
			}
			*object = pHandle->object;
		}

		return HandleError_None;
	}

	HandleError GetHandle__(Handle_t handle,
										IdentityToken_t *ident, 
										QHandle **in_pHandle, 
										unsigned int *in_index,
										bool ignoreFree = false)
	{
		unsigned int serial = (handle >> HANDLESYS_HANDLE_BITS);
		unsigned int index = (handle & HANDLESYS_HANDLE_MASK);

		if (index == 0 || index > m_HandleTail || index > HANDLESYS_MAX_HANDLES)
		{
			return HandleError_Index;
		}

		QHandle *pHandle = &m_Handles[index];

		if (!pHandle->set
			|| (pHandle->set == HandleSet_Freed && !ignoreFree))
		{
			return HandleError_Freed;
		} else if (pHandle->set == HandleSet_Identity
			#if 0
				   && ident != GetIdentRoot()
			#endif
		)
		{
			/* Only IdentityHandle() can read this! */
			return HandleError_Identity;
		}
		if (pHandle->serial != serial)
		{
			return HandleError_Changed;
		}

		*in_pHandle = pHandle;
		*in_index = index;

		return HandleError_None;
	}

	bool CheckAccess__(QHandle *pHandle, HandleAccessRight right, const HandleSecurity *pSecurity)
	{
		QHandleType *pType = &m_Types[pHandle->type];
		unsigned int access;

		if (pHandle->access_special)
		{
			access = pHandle->sec.access[right];
		} else {
			access = pType->hndlSec.access[right];
		}

		/* Check if the type's identity matches */
	#if 0
		if (access & HANDLE_RESTRICT_IDENTITY)
		{
			IdentityToken_t *owner = pType->typeSec.ident;
			if (!owner
				|| (!pSecurity || pSecurity->pIdentity != owner))
			{
				return false;
			}
		}
	#endif

		/* Check if the owner is allowed */
		if (access & HANDLE_RESTRICT_OWNER)
		{
			IdentityToken_t *owner = pHandle->owner;
			if (owner
				&& (!pSecurity || pSecurity->pOwner != owner))
			{
				return false;
			}
		}

		return true;
	}

	Handle_t CreateHandleInt__(HandleType_t type, 
									   void *object, 
									   const HandleSecurity *pSec,
									   HandleError *err, 
									   const HandleAccess *pAccess,
									   bool identity)
	{
		IdentityToken_t *ident;
		IdentityToken_t *owner;

		if (pSec)
		{
			ident = pSec->pIdentity;
			owner = pSec->pOwner;
		} else {
			ident = NULL;
			owner = NULL;
		}

		if (!type 
			|| type >= HANDLESYS_TYPEARRAY_SIZE
			|| m_Types[type].dispatch == NULL)
		{
			if (err)
			{
				*err = HandleError_Parameter;
			}
			return 0;
		}

		/* Check to see if we're allowed to create this handle type */
	#if 0
		QHandleType *pType = &m_Types[type];
		if (!pType->typeSec.access[HTypeAccess_Create]
		&& (!pType->typeSec.ident
			|| pType->typeSec.ident != ident))
		{
			if (err)
			{
				*err = HandleError_Access;
			}
			return 0;
		}
	#endif

		unsigned int index;
		Handle_t handle;
		QHandle *pHandle;
		HandleError _err;

		if ((_err=MakePrimHandle__(type, &pHandle, &index, &handle, owner, identity)) != HandleError_None)
		{
			if (err)
			{
				*err = _err;
			}
			return 0;
		}

		if (pAccess)
		{
			pHandle->access_special = true;
			pHandle->sec = *pAccess;
		}

		pHandle->object = object;
		pHandle->clone = 0;
		pHandle->timestamp = g_pSM->GetAdjustedTime();
		return handle;
	}
	
	HandleError MakePrimHandle__(HandleType_t type, 
						   QHandle **in_pHandle, 
						   unsigned int *in_index, 
						   Handle_t *in_handle,
						   IdentityToken_t *owner,
						   bool identity)
	{
		HandleError err;
		unsigned int owner_index = 0;

	#if 0
		if (owner && (IdentityHandle(owner, &owner_index) != HandleError_None))
		{
			return HandleError_Identity;
		}
	#endif

		unsigned int handle;
		if ((err = TryAllocHandle(&handle)) != HandleError_None)
		{
			if (!TryAndFreeSomeHandles()
				|| (err = TryAllocHandle(&handle)) != HandleError_None)
			{
				return err;
			}
		}

		if (owner)
		{
			owner->num_handles++;
		#if 0
			if (!owner->warned_handle_usage && owner->num_handles >= HANDLESYS_WARN_USAGE)
			{
				owner->warned_handle_usage = true;

				std::string path = "<unknown>";
				if (auto plugin = scripts->FindPluginByIdentity(owner))
				{
					path = "plugin "s + plugin->GetFilename();
				}
				else if (auto ext = g_Extensions.GetExtensionFromIdent(owner))
				{
					path = "extension "s + ext->GetFilename();
				}

				logger->LogError("[SM] Warning: %s is using more than %d handles!",
					path.c_str(), HANDLESYS_WARN_USAGE);
			}
		#endif
		}

		QHandle *pHandle = &m_Handles[handle];
		
		assert(pHandle->set == false);

		if (++m_HSerial >= HANDLESYS_MAX_SERIALS)
		{
			m_HSerial = 1;
		}

		/* Set essential information */
		pHandle->set = identity ? HandleSet_Identity : HandleSet_Used;
		pHandle->refcount = 1;
		pHandle->type = type;
		pHandle->serial = m_HSerial;
		pHandle->owner = owner;
		pHandle->ch_next = 0;
		pHandle->access_special = false;
		pHandle->is_destroying = false;

		/* Create the hash value */
		Handle_t hash = pHandle->serial;
		hash <<= HANDLESYS_HANDLE_BITS;
		hash |= handle;

		/* Add a reference count to the type */
		m_Types[type].opened++;

		/* Output */
		*in_pHandle = pHandle;
		*in_index = handle;
		*in_handle = hash;

		/* Decode the identity token 
		 * For now, we don't allow nested ownership
		 */
		if (owner && !identity)
		{
			QHandle *pIdentity = &m_Handles[owner_index];
			if (pIdentity->ch_prev == 0)
			{
				pIdentity->ch_prev = handle;
				pIdentity->ch_next = handle;
				pHandle->ch_prev = 0;
			}
			else
			{
				/* Link previous node to us (forward) */
				m_Handles[pIdentity->ch_next].ch_next = handle;
				/* Link us to previous node (backwards) */
				pHandle->ch_prev = pIdentity->ch_next;
				/* Set new tail */
				pIdentity->ch_next = handle;
			}
			pIdentity->refcount++;
		}
		else
		{
			pHandle->ch_prev = 0;
		}

		return HandleError_None;
	}
};

HandleError HandleSystem::TryAllocHandle(unsigned int *handle)
{
	if (m_FreeHandles == 0)
	{
		if (m_HandleTail >= HANDLESYS_MAX_HANDLES)
		{
			return HandleError_Limit;;
		}
		*handle = ++m_HandleTail;
	}
	else
	{
		*handle = m_Handles[m_FreeHandles--].freeID;
	}

	return HandleError_None;
}

bool HandleSystem::TryAndFreeSomeHandles()
{
	return true;
}
