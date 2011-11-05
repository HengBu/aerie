#ifndef _HLOCK_FIXTURE_HXX_AGL189
#define _HLOCK_FIXTURE_HXX_AGL189

#include <pthread.h>
#include "tool/testfw/integrationtest.h"
#include "client/libfs.h"
#include "client/config.h"
#include "client/client_i.h"
#include "rpc.fixture.h"
#include "lock.fixture.h"

using namespace client;

struct HLockFixture: public LockRegionFixture, RPCFixture {
	static bool            initialized;
	static pthread_mutex_t mutex;
	
	struct Finalize: testfw::AbstractFunctor {
		void operator()() {
			delete global_lckmgr;
			delete global_hlckmgr;
		}
	};

	HLockFixture() 
	{
		pthread_mutex_lock(&mutex);
		if (!initialized) {
			global_lckmgr = new LockManager(client::rpc_client, client::rpc_server, client::id, 0);
			global_hlckmgr = new HLockManager(global_lckmgr);
			initialized = true;
			// register a finalize action to be called by the test-framework 
			// when all threads complete
			Finalize* functor = new Finalize();
			TESTFW->RegisterFinalize(functor);

		}
		pthread_mutex_unlock(&mutex);
	}

	~HLockFixture() 
	{ }
};


#endif /* _HLOCK_FIXTURE_HXX_AGL189 */
