#include <binder/IBinder.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/LazyServiceRegistrar.h>
#include <utils/Log.h>

using android::BBinder;
using android::IBinder;
using android::IPCThreadState;
using android::OK;
using android::sp;
using android::binder::LazyServiceRegistrar;

int main() {
  sp<IBinder> binder1 = new BBinder;
  sp<IBinder> binder2 = new BBinder;

  auto lazyRegistrar = LazyServiceRegistrar::getInstance();
  LOG_ALWAYS_FATAL_IF(OK != lazyRegistrar.registerService(binder1, "aidl_lazy_test_1"), "");
  LOG_ALWAYS_FATAL_IF(OK != lazyRegistrar.registerService(binder2, "aidl_lazy_test_2"), "");

  IPCThreadState::self()->joinThreadPool();

  return 1;
}
