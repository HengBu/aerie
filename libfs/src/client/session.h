#ifndef __STAMNOS_FS_CLIENT_SESSION_H
#define __STAMNOS_FS_CLIENT_SESSION_H

#include "ssa/main/client/ssa.h"
#include "ssa/main/client/ssa-opaque.h"
#include "ssa/main/client/stm.h"


class StorageAllocator;

namespace client {

class Session {
public:
	Session(ssa::client::Dpo* ssa)
		: lckmgr_(ssa->lckmgr()),
		  hlckmgr_(ssa->hlckmgr()),
		  salloc_(ssa->salloc()),
		  omgr_(ssa->omgr()),
		  ssa_(ssa)
	{ }
	
	Session(ssa::cc::client::LockManager* lckmgr, 
	        ssa::cc::client::HLockManager* hlckmgr)
		: lckmgr_(lckmgr),
		  hlckmgr_(hlckmgr),
		  salloc_(NULL),
		  omgr_(NULL)
	{ }

	Session(ssa::cc::client::LockManager* lckmgr, 
	        ssa::cc::client::HLockManager* hlckmgr,
	        ssa::client::StorageAllocator* salloc,
	        ssa::client::ObjectManager* omgr)
		: lckmgr_(lckmgr),
		  hlckmgr_(hlckmgr),
		  salloc_(salloc),
		  omgr_(omgr)
	{ }


	ssa::client::StorageAllocator* salloc() { return ssa_->salloc(); }

	ssa::client::Dpo*                ssa_;
	ssa::cc::client::LockManager*    lckmgr_;
	ssa::cc::client::HLockManager*   hlckmgr_;
	ssa::client::StorageAllocator*     salloc_;
	ssa::client::ObjectManager*      omgr_;
	ssa::stm::client::Transaction*   tx_;
};


} // namespace client

#endif // __STAMNOS_FS_CLIENT_SESSION_H
